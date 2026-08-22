#include "replaycontroller.h"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QMatrix3x3>
#include <QStringConverter>
#include <QTextStream>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {
constexpr int TickIntervalMs = 33;
constexpr int PathRebuildIntervalMs = 160;
constexpr int MaximumChartSamples = 240;

QStringList splitCsvLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool quoted = false;
    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (quoted) {
            if (character == QLatin1Char('"')) {
                if (index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"')) {
                    current.append(QLatin1Char('"'));
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                current.append(character);
            }
        } else if (character == QLatin1Char('"')) {
            quoted = true;
        } else if (character == QLatin1Char(',')) {
            fields.append(current);
            current.clear();
        } else {
            current.append(character);
        }
    }
    fields.append(current);
    return fields;
}

QQuaternion sceneRotation(double rollDegrees, double pitchDegrees, double yawDegrees)
{
    const double roll = qDegreesToRadians(rollDegrees);
    const double pitch = qDegreesToRadians(pitchDegrees);
    const double yaw = qDegreesToRadians(yawDegrees);
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    QMatrix3x3 bodyToNed;
    bodyToNed(0, 0) = cy * cp;
    bodyToNed(0, 1) = cy * sp * sr - sy * cr;
    bodyToNed(0, 2) = cy * sp * cr + sy * sr;
    bodyToNed(1, 0) = sy * cp;
    bodyToNed(1, 1) = sy * sp * sr + cy * cr;
    bodyToNed(1, 2) = sy * sp * cr - cy * sr;
    bodyToNed(2, 0) = -sp;
    bodyToNed(2, 1) = cp * sr;
    bodyToNed(2, 2) = cp * cr;
    QMatrix3x3 nedToScene;
    nedToScene(0, 0) = 0;  nedToScene(0, 1) = 1;  nedToScene(0, 2) = 0;
    nedToScene(1, 0) = 0;  nedToScene(1, 1) = 0;  nedToScene(1, 2) = -1;
    nedToScene(2, 0) = -1; nedToScene(2, 1) = 0;  nedToScene(2, 2) = 0;
    QMatrix3x3 modelToBody;
    modelToBody(0, 0) = 0; modelToBody(0, 1) = 0;  modelToBody(0, 2) = -1;
    modelToBody(1, 0) = 1; modelToBody(1, 1) = 0;  modelToBody(1, 2) = 0;
    modelToBody(2, 0) = 0; modelToBody(2, 1) = -1; modelToBody(2, 2) = 0;
    return QQuaternion::fromRotationMatrix(nedToScene * bodyToNed * modelToBody).normalized();
}

double interpolateAngle(double from, double to, double fraction)
{
    return from + std::remainder(to - from, 360.0) * fraction;
}

bool parseFinite(const QStringList &fields, int index, double *value)
{
    if (index < 0 || index >= fields.size()) return false;
    bool ok = false;
    const double parsed = fields.at(index).toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}
}

ReplayController::ReplayController(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(TickIntervalMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ReplayController::onTick);
}

bool ReplayController::start(const QString &csvPath, const QString &eventsPath,
                             const QString &startedAtIso)
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
    m_timeMs = 0;
    m_durationMs = 0;
    m_currentEventIndex = -1;
    m_lastPathBuildMs = -1;
    m_pathGeometry.clearPath();
    if (wasActive) emit activeChanged();
    if (wasPlaying) emit playingChanged();
    if (wasActive) { emit timeChanged(); emit dataChanged(); }
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
    emit positionChanged();
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

QVector<ReplayController::Sample> ReplayController::loadSamples(const QString &csvPath,
                                                                QString *errorMessage)
{
    QVector<Sample> samples;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法读取遥测文件");
        return samples;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd()) return samples;
    const QStringList header = splitCsvLine(stream.readLine());
    QHash<QString, int> columns;
    for (int index = 0; index < header.size(); ++index)
        columns.insert(header.at(index).trimmed(), index);
    const QStringList required{QStringLiteral("elapsed_ms"), QStringLiteral("local_position_available"),
        QStringLiteral("scene_x_east_m"), QStringLiteral("scene_y_up_m"),
        QStringLiteral("scene_z_south_m")};
    for (const QString &name : required) {
        if (!columns.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("遥测文件缺少位置列：%1").arg(name);
            return {};
        }
    }
    const auto column = [&columns](const char *name) { return columns.value(QLatin1String(name), -1); };
    const int elapsedColumn = column("elapsed_ms");
    const int availableColumn = column("local_position_available");
    const int eastColumn = column("scene_x_east_m");
    const int upColumn = column("scene_y_up_m");
    const int southColumn = column("scene_z_south_m");
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty()) continue;
        const QStringList fields = splitCsvLine(line);
        if (availableColumn >= fields.size() || fields.at(availableColumn).trimmed() != QLatin1String("1"))
            continue;
        bool elapsedOk = false;
        const qint64 elapsedMs = elapsedColumn < fields.size()
                                     ? fields.at(elapsedColumn).toLongLong(&elapsedOk) : -1;
        double east = 0, up = 0, south = 0;
        if (!elapsedOk || elapsedMs < 0 || !parseFinite(fields, eastColumn, &east)
            || !parseFinite(fields, upColumn, &up) || !parseFinite(fields, southColumn, &south))
            continue;
        Sample sample;
        sample.elapsedMs = elapsedMs;
        sample.position = QVector3D(static_cast<float>(east), static_cast<float>(up), static_cast<float>(south));
        parseFinite(fields, column("heading_deg"), &sample.headingDegrees);
        parseFinite(fields, column("roll_deg"), &sample.rollDegrees);
        parseFinite(fields, column("pitch_deg"), &sample.pitchDegrees);
        parseFinite(fields, column("yaw_deg"), &sample.yawDegrees);
        parseFinite(fields, column("altitude_m"), &sample.altitudeMeters);
        parseFinite(fields, column("horizontal_speed_m_s"), &sample.horizontalSpeedMetersPerSecond);
        parseFinite(fields, column("vertical_speed_m_s"), &sample.verticalSpeedMetersPerSecond);
        sample.rotation = sceneRotation(sample.rollDegrees, sample.pitchDegrees, sample.yawDegrees);
        if (!samples.isEmpty() && sample.elapsedMs < samples.constLast().elapsedMs) continue;
        if (!samples.isEmpty() && sample.elapsedMs == samples.constLast().elapsedMs) samples.last() = sample;
        else samples.append(sample);
    }
    if (samples.size() < 2 && errorMessage)
        *errorMessage = QStringLiteral("该会话没有足够的本地位置数据");
    return samples;
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
