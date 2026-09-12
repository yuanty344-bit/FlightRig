#include "inspection/visualsurveycontroller.h"

QVariantMap VisualSurveyController::inspectFrameQuality(const QByteArray &jpeg)
{
    const ImageQuality quality = analyze(jpeg);
    return {{QStringLiteral("label"), quality.label},
            {QStringLiteral("reason"), quality.reason},
            {QStringLiteral("score"), quality.score},
            {QStringLiteral("brightness"), quality.brightness},
            {QStringLiteral("sharpness"), quality.sharpness},
            {QStringLiteral("edgeRatio"), quality.edgeRatio},
            {QStringLiteral("accepted"), !quality.hardReject && quality.label == QStringLiteral("清晰")}};
}

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QtMath>

#include <algorithm>
#include <cmath>

VisualSurveyController::VisualSurveyController(QObject *parent) : QObject(parent) {}

double VisualSurveyController::coveragePercent() const
{
    if (m_observedCells.isEmpty()) return m_loadedCoveragePercent;
    return 100.0 * m_capturedCells.size() / m_observedCells.size();
}

double VisualSurveyController::qualityPassPercent() const
{
    if (m_keyframes.isEmpty()) return m_loadedQualityPassPercent;
    return 100.0 * m_goodCount / m_keyframes.size();
}

QVariantMap VisualSurveyController::selectedKeyframe() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_keyframes.size()
        ? m_keyframes.at(m_selectedIndex).toMap() : QVariantMap{};
}

QString VisualSurveyController::surveyDirectory() const
{
    return QDir(m_sessionFolder).filePath(QStringLiteral("sensor/visual-survey"));
}

void VisualSurveyController::prepareSession(const QString &sessionFolder, bool canRecord)
{
    clear();
    m_sessionFolder = sessionFolder;
    m_canRecord = canRecord;
    QFile metadata(QDir(sessionFolder).filePath(QStringLiteral("session.json")));
    if (metadata.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll());
        const QDateTime started = QDateTime::fromString(
            document.object().value(QStringLiteral("startedAt")).toString(), Qt::ISODate);
        if (started.isValid()) m_startedMs = started.toMSecsSinceEpoch();
    }
    setStatus(canRecord ? QStringLiteral("可以开始视觉巡检")
                        : QStringLiteral("方案需选择“预览并记录”才能建立影像地图"));
}

bool VisualSurveyController::start()
{
    if (m_historical || m_sessionFolder.isEmpty() || !m_canRecord) return false;
    if (!QDir().mkpath(QDir(surveyDirectory()).filePath(QStringLiteral("images")))) {
        setStatus(QStringLiteral("无法创建影像地图目录"));
        return false;
    }
    m_active = true;
    if (m_startedMs <= 0) m_startedMs = QDateTime::currentMSecsSinceEpoch();
    setStatus(QStringLiteral("视觉巡检进行中，正在自动选择关键帧"));
    emit stateChanged();
    return true;
}

void VisualSurveyController::stop()
{
    if (!m_active) return;
    m_active = false;
    writeManifest(false);
    setStatus(m_keyframes.isEmpty() ? QStringLiteral("未获得可用关键帧")
                                    : QStringLiteral("视觉巡检已暂停，可继续或随会话保存"));
    emit stateChanged();
}

double VisualSurveyController::normalizedAngleDifference(double a, double b)
{
    double difference = std::fmod(a - b + 540.0, 360.0) - 180.0;
    return std::abs(difference);
}

int VisualSurveyController::hammingDistance(quint64 a, quint64 b)
{
    quint64 value = a ^ b;
    int count = 0;
    while (value) { value &= value - 1; ++count; }
    return count;
}

VisualSurveyController::ImageQuality VisualSurveyController::analyze(const QByteArray &jpeg)
{
    ImageQuality result;
    QImage image = QImage::fromData(jpeg, "JPEG");
    if (image.isNull()) {
        result.hardReject = true;
        result.reason = QStringLiteral("图像解码失败");
        result.label = QStringLiteral("不可用");
        return result;
    }
    image = image.convertToFormat(QImage::Format_Grayscale8)
                 .scaled(72, 48, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    double total = 0.0;
    double gradient = 0.0;
    int edgePixels = 0;
    int gradientPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            total += row[x];
            int localGradient = 0;
            if (x > 0) localGradient += std::abs(int(row[x]) - int(row[x - 1]));
            if (y > 0) localGradient += std::abs(int(row[x]) - int(image.constScanLine(y - 1)[x]));
            if (x > 0 || y > 0) {
                gradient += localGradient;
                ++gradientPixels;
                if (localGradient >= 12) ++edgePixels;
            }
        }
    }
    result.brightness = total / (image.width() * image.height());
    result.sharpness = gradient / std::max(1, gradientPixels * 2);
    result.edgeRatio = static_cast<double>(edgePixels) / std::max(1, gradientPixels);
    QImage hashImage = image.scaled(9, 8, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    quint64 hash = 0;
    for (int y = 0; y < 8; ++y) {
        const uchar *row = hashImage.constScanLine(y);
        for (int x = 0; x < 8; ++x)
            if (row[x] > row[x + 1]) hash |= quint64(1) << (y * 8 + x);
    }
    result.hash = hash;
    // Synthetic city models intentionally contain broad, flat-colour walls and
    // sky. A global edge average therefore measures scene texture more than
    // focus. Only reject frames with almost no structure; use both gradient and
    // edge density so a crisp horizon or building outline remains acceptable.
    const bool dark = result.brightness < 28.0;
    const bool bright = result.brightness > 242.0;
    const bool soft = result.sharpness < 1.45 && result.edgeRatio < 0.018;
    result.hardReject = result.brightness < 10.0 || result.brightness > 250.0
                        || (result.sharpness < 0.35 && result.edgeRatio < 0.002);
    const double exposurePenalty = result.brightness < 48.0
        ? (48.0 - result.brightness) * 0.9
        : result.brightness > 232.0 ? (result.brightness - 232.0) * 1.2 : 0.0;
    const double detailBonus = std::min(25.0, result.sharpness * 5.0)
                               + std::min(10.0, result.edgeRatio * 180.0);
    result.score = std::clamp(qRound(65.0 + detailBonus - exposurePenalty), 0, 100);
    if (result.hardReject) {
        result.label = QStringLiteral("已筛除");
        result.reason = result.sharpness < 0.35 ? QStringLiteral("画面缺少可辨识结构")
                      : result.brightness < 10.0 ? QStringLiteral("画面过暗")
                                                 : QStringLiteral("画面过曝");
    } else if (dark || bright || soft) {
        result.label = QStringLiteral("需复核");
        result.reason = soft ? QStringLiteral("画面细节较少，建议复核")
                      : dark ? QStringLiteral("画面偏暗") : QStringLiteral("画面偏亮");
    } else {
        result.label = QStringLiteral("清晰");
        result.reason = QStringLiteral("质量正常");
    }
    return result;
}

void VisualSurveyController::ingestFrame(const QByteArray &jpeg, int width, int height,
                                         qint64 timestampMs, const QVector3D &position,
                                         double headingDegrees, double pitchDegrees,
                                         bool poseValid)
{
    if (!m_active || jpeg.isEmpty()) return;
    if (timestampMs <= 0) timestampMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastCandidateMs > 0 && timestampMs - m_lastCandidateMs < 850) return;
    if (!poseValid) { setStatus(QStringLiteral("等待飞行器位置后自动拍摄")); return; }

    const double moved = m_keyframes.isEmpty() ? 999.0
        : (position - m_lastAcceptedPosition).length();
    const double turned = m_keyframes.isEmpty() ? 999.0
        : normalizedAngleDifference(headingDegrees, m_lastAcceptedHeading);
    const qint64 waited = timestampMs - m_lastAcceptedMs;
    if (!m_keyframes.isEmpty() && moved < 1.8 && turned < 14.0 && waited < 5000) return;
    m_lastCandidateMs = timestampMs;
    ++m_candidateCount;

    const QString cell = QStringLiteral("%1:%2:%3")
        .arg(qFloor(position.x() / 4.0)).arg(qFloor((-position.z()) / 4.0))
        .arg(qFloor((headingDegrees + 22.5) / 45.0));
    m_observedCells.insert(cell);
    const ImageQuality quality = analyze(jpeg);
    const bool duplicate = !m_keyframes.isEmpty() && moved < 4.0 && turned < 28.0
                           && hammingDistance(quality.hash, m_lastAcceptedHash) <= 5;
    if (duplicate || quality.hardReject) {
        ++m_rejectedCount;
        if (duplicate) ++m_duplicateCount;
        setStatus(duplicate ? QStringLiteral("已自动跳过重复画面")
                            : QStringLiteral("已自动筛除：%1").arg(quality.reason));
        emit surveyChanged();
        return;
    }

    const int index = m_keyframes.size();
    const QString fileName = QStringLiteral("%1_%2.jpg")
        .arg(index + 1, 5, 10, QLatin1Char('0')).arg(timestampMs);
    const QString absolutePath = QDir(surveyDirectory()).filePath(
        QStringLiteral("images/%1").arg(fileName));
    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(jpeg) != jpeg.size() || !file.commit()) {
        ++m_rejectedCount;
        setStatus(QStringLiteral("关键帧写入失败，请检查磁盘空间"));
        emit surveyChanged();
        return;
    }

    QVariantMap frame;
    frame.insert(QStringLiteral("index"), index);
    frame.insert(QStringLiteral("timestampEpochMs"), timestampMs);
    frame.insert(QStringLiteral("elapsedMs"), std::max<qint64>(0, timestampMs - m_startedMs));
    frame.insert(QStringLiteral("timeLabel"), QDateTime::fromMSecsSinceEpoch(timestampMs).toString(QStringLiteral("HH:mm:ss")));
    frame.insert(QStringLiteral("fileName"), fileName);
    frame.insert(QStringLiteral("relativeFile"), QStringLiteral("images/%1").arg(fileName));
    frame.insert(QStringLiteral("source"), QUrl::fromLocalFile(absolutePath));
    frame.insert(QStringLiteral("width"), width);
    frame.insert(QStringLiteral("height"), height);
    frame.insert(QStringLiteral("eastMeters"), position.x());
    frame.insert(QStringLiteral("northMeters"), -position.z());
    frame.insert(QStringLiteral("altitudeMeters"), position.y());
    frame.insert(QStringLiteral("headingDegrees"), headingDegrees);
    frame.insert(QStringLiteral("pitchDegrees"), pitchDegrees);
    frame.insert(QStringLiteral("horizontalFovDegrees"), 80.0);
    frame.insert(QStringLiteral("qualityScore"), quality.score);
    frame.insert(QStringLiteral("qualityLabel"), quality.label);
    frame.insert(QStringLiteral("qualityReason"), quality.reason);
    frame.insert(QStringLiteral("brightness"), quality.brightness);
    frame.insert(QStringLiteral("sharpness"), quality.sharpness);
    frame.insert(QStringLiteral("anomaly"), false);
    frame.insert(QStringLiteral("notes"), QString());
    frame.insert(QStringLiteral("areaLabel"), QStringLiteral("区域 %1 / %2")
        .arg(qFloor(position.x() / 20.0)).arg(qFloor((-position.z()) / 20.0)));
    m_keyframes.append(frame);
    m_capturedCells.insert(cell);
    m_visitedAreas.insert(QStringLiteral("%1:%2")
        .arg(qFloor(position.x() / 20.0)).arg(qFloor((-position.z()) / 20.0)));
    m_lastAcceptedMs = timestampMs;
    m_lastAcceptedPosition = position;
    m_lastAcceptedHeading = headingDegrees;
    m_lastAcceptedHash = quality.hash;
    rebuildCounts();
    m_selectedIndex = index;
    setStatus(quality.label == QStringLiteral("清晰")
              ? QStringLiteral("已保存关键帧 %1").arg(index + 1)
              : QStringLiteral("已保存关键帧，%1").arg(quality.reason));
    // Persist a checkpoint periodically rather than rewriting the growing JSON
    // array for every accepted frame. The original JPEG is already committed,
    // and stop/finalize always writes the complete manifest.
    if (m_keyframes.size() == 1 || m_keyframes.size() % 5 == 0) writeManifest(false);
    emit surveyChanged();
    emit selectedIndexChanged();
}

void VisualSurveyController::rebuildCounts()
{
    m_goodCount = 0; m_warningCount = 0;
    for (const QVariant &item : m_keyframes) {
        if (item.toMap().value(QStringLiteral("qualityLabel")).toString() == QStringLiteral("清晰"))
            ++m_goodCount;
        else ++m_warningCount;
    }
}

bool VisualSurveyController::writeManifest(bool completed)
{
    if (m_sessionFolder.isEmpty()) return false;
    QJsonObject root = QJsonObject::fromVariantMap(summary());
    root.insert(QStringLiteral("completed"), completed);
    QSaveFile file(QDir(surveyDirectory()).filePath(QStringLiteral("visual-survey.json")));
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QVariantMap VisualSurveyController::summary() const
{
    QVariantList portableFrames;
    portableFrames.reserve(m_keyframes.size());
    qint64 sizeBytes = 0;
    for (const QVariant &entry : m_keyframes) {
        QVariantMap frame = entry.toMap();
        const QString source = frame.value(QStringLiteral("source")).toUrl().toLocalFile();
        if (!source.isEmpty()) sizeBytes += QFileInfo(source).size();
        frame.remove(QStringLiteral("source"));
        portableFrames.append(frame);
    }
    QVariantMap result;
    result.insert(QStringLiteral("formatVersion"), 2);
    result.insert(QStringLiteral("available"), available());
    result.insert(QStringLiteral("keyframeCount"), keyframeCount());
    result.insert(QStringLiteral("goodKeyframeCount"), m_goodCount);
    result.insert(QStringLiteral("warningKeyframeCount"), m_warningCount);
    result.insert(QStringLiteral("rejectedCount"), m_rejectedCount);
    result.insert(QStringLiteral("duplicateCount"), m_duplicateCount);
    result.insert(QStringLiteral("candidateCount"), m_candidateCount);
    result.insert(QStringLiteral("coveragePercent"), coveragePercent());
    result.insert(QStringLiteral("spatialCoveragePercent"), coveragePercent());
    result.insert(QStringLiteral("qualityPassPercent"), qualityPassPercent());
    result.insert(QStringLiteral("observedViewCount"), m_observedCells.isEmpty()
                      ? m_loadedObservedViewCount : m_observedCells.size());
    result.insert(QStringLiteral("capturedViewCount"), m_observedCells.isEmpty()
                      ? m_loadedCapturedViewCount : m_capturedCells.size());
    result.insert(QStringLiteral("visitedAreaCount"), m_observedCells.isEmpty()
                      ? m_loadedVisitedAreaCount : m_visitedAreas.size());
    result.insert(QStringLiteral("qualityMetricVersion"), 2);
    result.insert(QStringLiteral("coverageDefinition"), coverageDefinition());
    result.insert(QStringLiteral("sizeBytes"), sizeBytes);
    result.insert(QStringLiteral("keyframes"), portableFrames);
    result.insert(QStringLiteral("relativeManifest"), QStringLiteral("visual-survey/visual-survey.json"));
    return result;
}

QVariantMap VisualSurveyController::finishSession()
{
    if (m_active) { m_active = false; emit stateChanged(); }
    const QVariantMap result = summary();
    writeManifest(true);
    return result;
}

bool VisualSurveyController::loadFromSession(const QString &sessionFolder)
{
    clear();
    const QString directory = QDir(sessionFolder).filePath(QStringLiteral("sensor/visual-survey"));
    QFile file(QDir(directory).filePath(QStringLiteral("visual-survey.json")));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return false;
    const QVariantMap root = document.object().toVariantMap();
    if (root.value(QStringLiteral("formatVersion")).toInt() != 2
        || root.value(QStringLiteral("qualityMetricVersion")).toInt() != 2)
        return false;
    m_sessionFolder = sessionFolder;
    m_historical = true;
    m_keyframes = root.value(QStringLiteral("keyframes")).toList();
    for (int i = 0; i < m_keyframes.size(); ++i) {
        QVariantMap frame = m_keyframes.at(i).toMap();
        frame.insert(QStringLiteral("index"), i);
        frame.insert(QStringLiteral("source"), QUrl::fromLocalFile(
            QDir(directory).filePath(frame.value(QStringLiteral("relativeFile")).toString())));
        m_keyframes[i] = frame;
    }
    m_rejectedCount = root.value(QStringLiteral("rejectedCount")).toInt();
    m_duplicateCount = root.value(QStringLiteral("duplicateCount")).toInt();
    m_candidateCount = root.value(QStringLiteral("candidateCount")).toInt();
    m_loadedCoveragePercent = root.value(QStringLiteral("spatialCoveragePercent")).toDouble();
    m_loadedQualityPassPercent = root.value(QStringLiteral("qualityPassPercent")).toDouble();
    m_loadedObservedViewCount = root.value(QStringLiteral("observedViewCount")).toInt();
    m_loadedCapturedViewCount = root.value(QStringLiteral("capturedViewCount")).toInt();
    m_loadedVisitedAreaCount = root.value(QStringLiteral("visitedAreaCount")).toInt();
    rebuildCounts();
    m_selectedIndex = m_keyframes.isEmpty() ? -1 : 0;
    setStatus(m_keyframes.isEmpty() ? QStringLiteral("该会话没有视觉巡检关键帧")
                                    : QStringLiteral("正在查看历史视觉巡检"));
    emit surveyChanged(); emit selectedIndexChanged(); emit stateChanged();
    return !m_keyframes.isEmpty();
}

void VisualSurveyController::clear()
{
    m_active = false; m_historical = false; m_canRecord = false;
    m_sessionFolder.clear(); m_keyframes.clear(); m_selectedIndex = -1;
    m_goodCount = m_warningCount = m_rejectedCount = m_duplicateCount = m_candidateCount = 0;
    m_startedMs = m_lastCandidateMs = m_lastAcceptedMs = 0;
    m_observedCells.clear(); m_capturedCells.clear(); m_visitedAreas.clear();
    m_loadedCoveragePercent = 0.0;
    m_loadedQualityPassPercent = 0.0;
    m_loadedObservedViewCount = m_loadedCapturedViewCount = m_loadedVisitedAreaCount = 0;
    m_statusMessage = QStringLiteral("尚未开始视觉巡检");
    emit surveyChanged(); emit selectedIndexChanged(); emit stateChanged();
}

void VisualSurveyController::setSelectedIndex(int index)
{
    const int bounded = m_keyframes.isEmpty() ? -1 : std::clamp(index, 0, int(m_keyframes.size() - 1));
    if (m_selectedIndex == bounded) return;
    m_selectedIndex = bounded;
    emit selectedIndexChanged();
}

bool VisualSurveyController::updateSelected(const QString &notes, bool anomaly)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_keyframes.size()) return false;
    QVariantMap frame = m_keyframes.at(m_selectedIndex).toMap();
    frame.insert(QStringLiteral("notes"), notes.trimmed().left(1000));
    frame.insert(QStringLiteral("anomaly"), anomaly);
    m_keyframes[m_selectedIndex] = frame;
    const bool saved = writeManifest(m_historical);
    emit surveyChanged(); emit selectedIndexChanged();
    return saved;
}

void VisualSurveyController::selectNearest(double eastMeters, double northMeters)
{
    int best = -1; double bestDistance = 1e100;
    for (int i = 0; i < m_keyframes.size(); ++i) {
        const QVariantMap frame = m_keyframes.at(i).toMap();
        const double dx = frame.value(QStringLiteral("eastMeters")).toDouble() - eastMeters;
        const double dy = frame.value(QStringLiteral("northMeters")).toDouble() - northMeters;
        const double distance = dx * dx + dy * dy;
        if (distance < bestDistance) { bestDistance = distance; best = i; }
    }
    setSelectedIndex(best);
}

void VisualSurveyController::setStatus(const QString &message)
{
    if (m_statusMessage == message) return;
    m_statusMessage = message;
    emit stateChanged();
}
