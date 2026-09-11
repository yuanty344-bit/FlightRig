#include "sessions/replaycontroller.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix3x3>
#include <QStringConverter>
#include <QTextStream>
#include <QUrl>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {
constexpr int TickIntervalMs = 33;
constexpr int PathRebuildIntervalMs = 160;
constexpr int MaximumChartSamples = 240;
constexpr qint64 MinimumReplayFrameFreshnessMs = 1500;

double interpolateAngle(double from, double to, double fraction)
{
    return from + std::remainder(to - from, 360.0) * fraction;
}


}

ReplayController::ReplayController(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(TickIntervalMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ReplayController::onTick);
}

bool ReplayController::start(const QString &csvPath, const QString &eventsPath,
                             const QString &startedAtIso, const QString &sessionFolder)
{
    stop();
    m_errorMessage.clear();
    m_samples = loadSamples(csvPath, &m_errorMessage);
    if (m_samples.size() < 2) return false;
    m_durationMs = m_samples.constLast().elapsedMs;
    m_timeMs = 0;
    m_lastPathBuildMs = -1;
    m_speedMultiplier = 1.0;
    loadEvents(eventsPath, startedAtIso);
    loadSensorTimeline(sessionFolder);
    buildTelemetrySeries();
    m_active = true;
    updateInterpolatedState();
    rebuildPlayedPath();
    m_playing = true;
    m_clockAnchorMs = 0;
    m_playClock.restart();
    m_timer.start();
    emit activeChanged();
    emit playingChanged();
    emit timeChanged();
    emit dataChanged();
    return true;
}

void ReplayController::stop()
{
    const bool wasActive = m_active;
    const bool wasPlaying = m_playing;
    m_timer.stop();
    m_active = false;
    m_playing = false;
    m_samples.clear();
    m_events.clear();
    m_telemetrySeries.clear();
    m_sensorTimeline.rgbSamples.clear();
    m_sensorTimeline.depthSamples.clear();
    m_sensorTimeline.lidarSamples.clear();
    m_sensorTimeline.gimbalTargetSamples.clear();
    m_sensorTimeline.gimbalFeedbackSamples.clear();
    m_rgbSampleIndex = m_depthSampleIndex = m_lidarSampleIndex = -1;
    m_gimbalTargetSampleIndex = m_gimbalFeedbackSampleIndex = -1;
    m_sensorTimeline.available = false;
    m_sensorTimeline.kind.clear();
    m_rgbFrameDataUrl.clear();
    m_depthFrameDataUrl.clear();
    m_rgbFramePath.clear();
    m_depthFramePath.clear();
    m_sensorTimeline.path.clear();
    m_rgbFrameHz = m_depthFrameHz = 0.0;
    m_rgbLatencyMilliseconds = m_depthLatencyMilliseconds = -1;
    m_rgbStreamInterrupted = m_depthStreamInterrupted = m_lidarStreamInterrupted = false;
    m_depthCenterDistanceMeters = m_depthNearestDistanceMeters = m_depthValidRatio = 0.0;
    m_depthMillimetres.clear();
    m_depthGridWidth = m_depthGridHeight = 0;
    m_lidarPoints.clear();
    m_lidarNearestDistanceMeters = m_lidarNearestAngleDegrees = 0.0;
    m_lidarScanHz = m_lidarFieldOfViewDegrees = 0.0;
    m_gimbalYawDegrees = m_gimbalPitchDegrees = m_gimbalRollDegrees = 0.0;
    m_gimbalTargetYawDegrees = m_gimbalTargetPitchDegrees = 0.0;
    m_gimbalFeedbackAvailable = false;
    m_timeMs = 0;
    m_durationMs = 0;
    m_currentEventIndex = -1;
    m_lastPathBuildMs = -1;
    m_pathGeometry.clearPath();
    if (wasActive) emit activeChanged();
    if (wasPlaying) emit playingChanged();
    if (wasActive) { emit timeChanged(); emit dataChanged(); emit sensorDataChanged(); }
}

void ReplayController::synchronizeClock()
{
    if (!m_playing || !m_playClock.isValid()) return;
    m_timeMs = std::clamp<qint64>(m_clockAnchorMs
                                     + qRound64(m_playClock.elapsed() * m_speedMultiplier),
                                 0, m_durationMs);
}

void ReplayController::pause()
{
    if (!m_active || !m_playing) return;
    synchronizeClock();
    m_playing = false;
    m_timer.stop();
    updateInterpolatedState();
    rebuildPlayedPath();
    emit playingChanged();
    emit timeChanged();
}

void ReplayController::resume()
{
    if (!m_active || m_playing) return;
    if (m_timeMs >= m_durationMs) seek(0);
    m_clockAnchorMs = m_timeMs;
    m_playClock.restart();
    m_playing = true;
    m_timer.start();
    emit playingChanged();
}

void ReplayController::seek(qint64 timeMs)
{
    if (!m_active) return;
    m_timeMs = std::clamp<qint64>(timeMs, 0, m_durationMs);
    m_clockAnchorMs = m_timeMs;
    if (m_playing) m_playClock.restart();
    updateInterpolatedState();
    rebuildPlayedPath();
    emit timeChanged();
}

void ReplayController::setSpeedMultiplier(double multiplier)
{
    const double bounded = std::clamp(multiplier, 0.25, 8.0);
    if (qFuzzyCompare(m_speedMultiplier, bounded)) return;
    synchronizeClock();
    m_speedMultiplier = bounded;
    m_clockAnchorMs = m_timeMs;
    if (m_playing) m_playClock.restart();
    emit dataChanged();
}

void ReplayController::onTick()
{
    if (!m_playing) return;
    synchronizeClock();
    updateInterpolatedState();
    if (m_lastPathBuildMs < 0 || std::abs(m_timeMs - m_lastPathBuildMs) >= PathRebuildIntervalMs
        || m_timeMs >= m_durationMs) rebuildPlayedPath();
    emit timeChanged();
    if (m_timeMs >= m_durationMs) pause();
}

void ReplayController::updateInterpolatedState()
{
    if (m_samples.isEmpty()) return;
    const auto upper = std::upper_bound(m_samples.cbegin(), m_samples.cend(), m_timeMs,
        [](qint64 value, const Sample &sample) { return value < sample.elapsedMs; });
    qsizetype endIndex = std::distance(m_samples.cbegin(), upper);
    endIndex = std::clamp<qsizetype>(endIndex, 0, m_samples.size() - 1);
    const qsizetype startIndex = std::max<qsizetype>(0, endIndex - 1);
    const Sample &start = m_samples.at(startIndex);
    const Sample &end = m_samples.at(endIndex);
    const qint64 spanMs = std::max<qint64>(1, end.elapsedMs - start.elapsedMs);
    const double fraction = startIndex == endIndex ? 0.0
                            : static_cast<double>(m_timeMs - start.elapsedMs) / spanMs;
    const auto lerp = [fraction](double from, double to) { return from + (to - from) * fraction; };
    m_position = start.position + (end.position - start.position) * static_cast<float>(fraction);
    m_rotation = QQuaternion::slerp(start.rotation, end.rotation, static_cast<float>(fraction));
    m_headingDegrees = interpolateAngle(start.headingDegrees, end.headingDegrees, fraction);
    m_rollDegrees = lerp(start.rollDegrees, end.rollDegrees);
    m_pitchDegrees = lerp(start.pitchDegrees, end.pitchDegrees);
    m_yawDegrees = interpolateAngle(start.yawDegrees, end.yawDegrees, fraction);
    m_altitudeMeters = lerp(start.altitudeMeters, end.altitudeMeters);
    m_horizontalSpeedMetersPerSecond = lerp(start.horizontalSpeedMetersPerSecond,
                                            end.horizontalSpeedMetersPerSecond);
    m_verticalSpeedMetersPerSecond = lerp(start.verticalSpeedMetersPerSecond,
                                          end.verticalSpeedMetersPerSecond);
    updateCurrentEventIndex();
    updateSensorState();
    emit positionChanged();
}

void ReplayController::loadSensorTimeline(const QString &sessionFolder)
{
    m_sensorTimeline.load(sessionFolder);
}

QVariantMap ReplayController::loadSensorSample(const SensorSample &sample) const
{
    return m_sensorTimeline.read(sample);
}

void ReplayController::updateSensorState()
{
    if (m_sensorTimeline.rgbSamples.isEmpty() && m_sensorTimeline.depthSamples.isEmpty() && m_sensorTimeline.lidarSamples.isEmpty()
        && m_sensorTimeline.gimbalTargetSamples.isEmpty() && m_sensorTimeline.gimbalFeedbackSamples.isEmpty()) return;
    auto sampleIndex = [this](const QVector<SensorSample> &samples) {
        const auto upper = std::upper_bound(samples.cbegin(), samples.cend(), m_timeMs,
            [](qint64 value, const SensorSample &sample) { return value < sample.elapsedMs; });
        return upper == samples.cbegin() ? -1
                                        : static_cast<int>(std::distance(samples.cbegin(), upper) - 1);
    };
    bool changed = false;
    const auto updateCamera = [this, &sampleIndex, &changed](
                                  const QVector<SensorSample> &samples, int &currentIndex,
                                   QString &dataUrl, QString &framePath,
                                   double &frameHz, int &latencyMs,
                                   bool &interrupted, bool depth) {
        const int nextIndex = sampleIndex(samples);
        const bool nextInterrupted = nextIndex >= 0
            && m_timeMs - samples.at(nextIndex).elapsedMs
                   > std::max<qint64>(MinimumReplayFrameFreshnessMs,
                       qRound64(3000.0 / std::max(1.0,
                           samples.at(nextIndex).metadata.value(QStringLiteral("hz"), 1.0).toDouble())));
        if (nextIndex == currentIndex && nextInterrupted == interrupted) return;
        currentIndex = nextIndex;
        interrupted = nextInterrupted;
        dataUrl.clear();
        framePath.clear();
        if (nextIndex >= 0) {
            const QVariantMap data = loadSensorSample(samples.at(nextIndex));
            framePath = samples.at(nextIndex).metadata.value(QStringLiteral("file")).toString();
            if (!interrupted && QFileInfo::exists(framePath))
                dataUrl = QUrl::fromLocalFile(framePath).toString();
            frameHz = data.value(QStringLiteral("hz")).toDouble();
            latencyMs = data.value(QStringLiteral("latencyMethod")).toString()
                            == QStringLiteral("monotonic-sync-v1")
                ? data.value(QStringLiteral("latencyMilliseconds"), -1).toInt() : -1;
            if (depth && !interrupted) {
                m_depthCenterDistanceMeters = data.value(QStringLiteral("centerDistance")).toDouble();
                m_depthNearestDistanceMeters = data.value(QStringLiteral("nearestDistance")).toDouble();
                m_depthValidRatio = data.value(QStringLiteral("validRatio")).toDouble();
                m_depthGridWidth = data.value(QStringLiteral("gridWidth")).toInt();
                m_depthGridHeight = data.value(QStringLiteral("gridHeight")).toInt();
                m_depthMillimetres = QByteArray::fromBase64(
                    data.value(QStringLiteral("depthMillimetresBase64")).toString().toLatin1());
            } else if (depth) {
                m_depthCenterDistanceMeters = 0.0;
                m_depthNearestDistanceMeters = 0.0;
                m_depthValidRatio = 0.0;
                m_depthGridWidth = m_depthGridHeight = 0;
                m_depthMillimetres.clear();
            }
        } else {
            frameHz = 0.0;
            latencyMs = -1;
            if (depth) {
                m_depthCenterDistanceMeters = 0.0;
                m_depthNearestDistanceMeters = 0.0;
                m_depthValidRatio = 0.0;
                m_depthGridWidth = m_depthGridHeight = 0;
                m_depthMillimetres.clear();
            }
        }
        changed = true;
    };
    updateCamera(m_sensorTimeline.rgbSamples, m_rgbSampleIndex, m_rgbFrameDataUrl, m_rgbFramePath,
                 m_rgbFrameHz, m_rgbLatencyMilliseconds, m_rgbStreamInterrupted, false);
    updateCamera(m_sensorTimeline.depthSamples, m_depthSampleIndex, m_depthFrameDataUrl, m_depthFramePath,
                 m_depthFrameHz, m_depthLatencyMilliseconds, m_depthStreamInterrupted, true);

    const int lidarIndex = sampleIndex(m_sensorTimeline.lidarSamples);
    const bool lidarInterrupted = lidarIndex >= 0
        && m_timeMs - m_sensorTimeline.lidarSamples.at(lidarIndex).elapsedMs
               > std::max<qint64>(MinimumReplayFrameFreshnessMs,
                   qRound64(3000.0 / std::max(1.0,
                       m_sensorTimeline.lidarSamples.at(lidarIndex).metadata
                           .value(QStringLiteral("hz"), 1.0).toDouble())));
    if (lidarIndex != m_lidarSampleIndex || lidarInterrupted != m_lidarStreamInterrupted) {
        m_lidarSampleIndex = lidarIndex;
        m_lidarStreamInterrupted = lidarInterrupted;
        m_lidarPoints.clear();
        m_lidarNearestDistanceMeters = 0.0;
        m_lidarNearestAngleDegrees = 0.0;
        m_lidarScanHz = 0.0;
        m_lidarFieldOfViewDegrees = 0.0;
        if (lidarIndex >= 0 && !lidarInterrupted) {
            const QVariantMap data = loadSensorSample(m_sensorTimeline.lidarSamples.at(lidarIndex));
            m_lidarPoints = data.value(QStringLiteral("points")).toList();
            m_lidarNearestDistanceMeters = data.value(QStringLiteral("nearestDistance")).toDouble();
            m_lidarNearestAngleDegrees = data.value(QStringLiteral("nearestAngle")).toDouble();
            m_lidarScanHz = data.value(QStringLiteral("hz")).toDouble();
            m_lidarFieldOfViewDegrees = data.value(QStringLiteral("fieldOfView")).toDouble();
        }
        changed = true;
    }
    const int targetIndex = sampleIndex(m_sensorTimeline.gimbalTargetSamples);
    if (targetIndex != m_gimbalTargetSampleIndex) {
        m_gimbalTargetSampleIndex = targetIndex;
        if (targetIndex >= 0) {
            const QVariantMap data = loadSensorSample(m_sensorTimeline.gimbalTargetSamples.at(targetIndex));
            m_gimbalTargetYawDegrees = data.value(QStringLiteral("targetYaw")).toDouble();
            m_gimbalTargetPitchDegrees = data.value(QStringLiteral("targetPitch")).toDouble();
        } else {
            m_gimbalTargetYawDegrees = m_gimbalTargetPitchDegrees = 0.0;
        }
        changed = true;
    }
    const int feedbackIndex = sampleIndex(m_sensorTimeline.gimbalFeedbackSamples);
    if (feedbackIndex != m_gimbalFeedbackSampleIndex) {
        m_gimbalFeedbackSampleIndex = feedbackIndex;
        m_gimbalFeedbackAvailable = feedbackIndex >= 0;
        if (feedbackIndex >= 0) {
            const QVariantMap data = loadSensorSample(m_sensorTimeline.gimbalFeedbackSamples.at(feedbackIndex));
            m_gimbalYawDegrees = data.value(QStringLiteral("yaw")).toDouble();
            m_gimbalPitchDegrees = data.value(QStringLiteral("pitch")).toDouble();
            m_gimbalRollDegrees = data.value(QStringLiteral("roll")).toDouble();
            if (m_sensorTimeline.gimbalTargetSamples.isEmpty()) {
                m_gimbalTargetYawDegrees = data.value(
                    QStringLiteral("targetYaw"), m_gimbalYawDegrees).toDouble();
                m_gimbalTargetPitchDegrees = data.value(
                    QStringLiteral("targetPitch"), m_gimbalPitchDegrees).toDouble();
            }
        } else {
            m_gimbalYawDegrees = m_gimbalPitchDegrees = m_gimbalRollDegrees = 0.0;
        }
        changed = true;
    }
    if (changed) emit sensorDataChanged();
}

bool ReplayController::gimbalMoving() const
{
    if (!m_gimbalFeedbackAvailable) return false;
    return std::abs(std::remainder(m_gimbalTargetYawDegrees - m_gimbalYawDegrees, 360.0)) > 1.5
           || std::abs(m_gimbalTargetPitchDegrees - m_gimbalPitchDegrees) > 1.5;
}

bool ReplayController::saveCurrentCameraFrame(const QString &stream,
                                              const QString &directory,
                                              QString *savedPath) const
{
    const QString source = stream == QStringLiteral("depth")
        ? m_depthFramePath : m_rgbFramePath;
    if (source.isEmpty() || !QFileInfo(source).isFile()) return false;
    QDir target(directory);
    if (!target.mkpath(QStringLiteral("."))) return false;
    const QString destination = target.filePath(
        QStringLiteral("%1-replay-%2.jpg")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
                 stream == QStringLiteral("depth") ? QStringLiteral("depth")
                                                    : QStringLiteral("rgb")));
    if (!QFile::copy(source, destination)) return false;
    if (savedPath) *savedPath = QDir::toNativeSeparators(destination);
    return true;
}

double ReplayController::depthDistanceAt(double normalizedX, double normalizedY) const
{
    if (m_depthGridWidth <= 0 || m_depthGridHeight <= 0) return 0.0;
    const int x = std::clamp(static_cast<int>(normalizedX * m_depthGridWidth),
                             0, m_depthGridWidth - 1);
    const int y = std::clamp(static_cast<int>(normalizedY * m_depthGridHeight),
                             0, m_depthGridHeight - 1);
    const qsizetype offset = (static_cast<qsizetype>(y) * m_depthGridWidth + x) * 2;
    if (offset + 1 >= m_depthMillimetres.size()) return 0.0;
    const auto low = static_cast<quint8>(m_depthMillimetres.at(offset));
    const auto high = static_cast<quint8>(m_depthMillimetres.at(offset + 1));
    return static_cast<double>(low | (high << 8)) / 1000.0;
}

void ReplayController::rebuildPlayedPath()
{
    QVector<QVector3D> points;
    for (const Sample &sample : std::as_const(m_samples)) {
        if (sample.elapsedMs > m_timeMs) break;
        points.append(sample.position);
    }
    if (!points.isEmpty() && points.constLast().distanceToPoint(m_position) > 0.001f)
        points.append(m_position);
    m_pathGeometry.setPoints(points);
    m_lastPathBuildMs = m_timeMs;
}

QVector<ReplayController::Sample> ReplayController::loadSamples(const QString &csvPath, QString *errorMessage)
{
    return ReplayData::loadSamples(csvPath, errorMessage);
}

bool ReplayController::containsPositionSamples(const QString &csvPath)
{
    return loadSamples(csvPath).size() >= 2;
}

void ReplayController::loadEvents(const QString &eventsPath, const QString &startedAtIso)
{
    m_events.clear();
    QFile file(eventsPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QDateTime startedAt = QDateTime::fromString(startedAtIso, Qt::ISODate);
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;
        qint64 elapsedMs = 0;
        if (startedAt.isValid() && line.size() >= 10 && line.startsWith(QLatin1Char('['))) {
            const QTime time = QTime::fromString(line.mid(1, 8), QStringLiteral("HH:mm:ss"));
            if (time.isValid()) {
                int seconds = startedAt.time().secsTo(time);
                if (seconds < 0) seconds += 24 * 60 * 60;
                elapsedMs = std::clamp<qint64>(seconds * 1000LL, 0, m_durationMs);
            }
        }
        m_events.append(QVariantMap{{QStringLiteral("timeMs"), elapsedMs},
                                    {QStringLiteral("label"), line}});
    }
}

void ReplayController::buildTelemetrySeries()
{
    m_telemetrySeries.clear();
    if (m_samples.isEmpty()) return;
    const qsizetype step = std::max<qsizetype>(1, static_cast<qsizetype>(
        std::ceil(static_cast<double>(m_samples.size()) / MaximumChartSamples)));
    for (qsizetype index = 0; index < m_samples.size(); index += step) {
        const Sample &sample = m_samples.at(index);
        m_telemetrySeries.append(QVariantMap{{QStringLiteral("timeMs"), sample.elapsedMs},
            {QStringLiteral("altitude"), sample.altitudeMeters},
            {QStringLiteral("speed"), sample.horizontalSpeedMetersPerSecond},
            {QStringLiteral("verticalSpeed"), sample.verticalSpeedMetersPerSecond},
            {QStringLiteral("roll"), sample.rollDegrees},
            {QStringLiteral("pitch"), sample.pitchDegrees},
            {QStringLiteral("yaw"), sample.yawDegrees}});
    }
}

void ReplayController::updateCurrentEventIndex()
{
    int index = -1;
    for (int candidate = 0; candidate < m_events.size(); ++candidate) {
        if (m_events.at(candidate).toMap().value(QStringLiteral("timeMs")).toLongLong() > m_timeMs) break;
        index = candidate;
    }
    m_currentEventIndex = index;
}

QString ReplayController::currentEventLabel() const
{
    if (m_currentEventIndex < 0 || m_currentEventIndex >= m_events.size()) return {};
    return m_events.at(m_currentEventIndex).toMap().value(QStringLiteral("label")).toString();
}

void ReplayController::seekPreviousEvent()
{
    if (!m_active || m_events.isEmpty()) return;
    int target = m_currentEventIndex;
    if (target >= 0) {
        const qint64 current = m_events.at(target).toMap().value(QStringLiteral("timeMs")).toLongLong();
        if (m_timeMs - current < 1500) --target;
    }
    seekEvent(std::max(0, target));
}

void ReplayController::seekNextEvent()
{
    if (!m_active || m_events.isEmpty()) return;
    const int target = m_currentEventIndex < 0 ? 0 : m_currentEventIndex + 1;
    if (target < m_events.size()) seekEvent(target);
    else seek(m_durationMs);
}

void ReplayController::seekEvent(int eventIndex)
{
    if (!m_active || eventIndex < 0 || eventIndex >= m_events.size()) return;
    m_timeMs = std::clamp<qint64>(
        m_events.at(eventIndex).toMap().value(QStringLiteral("timeMs")).toLongLong(),
        0, m_durationMs);
    m_clockAnchorMs = m_timeMs;
    if (m_playing) m_playClock.restart();
    updateInterpolatedState();
    // Several log entries can share the same HH:mm:ss timestamp. Preserve the
    // requested row instead of letting time-based lookup select the final entry
    // from that second.
    m_currentEventIndex = eventIndex;
    rebuildPlayedPath();
    emit timeChanged();
}
