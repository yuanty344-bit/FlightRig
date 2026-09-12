#include "sensors/sensorsessionrecorder.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStorageInfo>
#include <algorithm>
bool SensorSessionRecorder::beginSessionRecording(const QString &sessionFolder,
                                                    const QString &usage,
                                                    const QString & /*performanceMode*/)
{
    if (m_sessionTracking) finishSessionRecording();
    if (usage != QStringLiteral("preview") && usage != QStringLiteral("record")) return true;
    const QString cleanFolder = QDir::cleanPath(sessionFolder);
    if (cleanFolder.isEmpty() || !QFileInfo(cleanFolder).isDir()) return false;
    m_sessionUsage = usage;
    m_sessionRecordingRequested = usage == QStringLiteral("record");
    m_sessionWriteFailed = false;
    m_sessionMaximumRecordedBytes = 2LL * 1024 * 1024 * 1024;
    if (usage == QStringLiteral("record")) {
        QStorageInfo storage(cleanFolder);
        constexpr qint64 ReservedFreeBytes = 256LL * 1024 * 1024;
        constexpr qint64 MinimumUsefulRecordingBytes = 128LL * 1024 * 1024;
        if (storage.isValid() && storage.isReady()) {
            m_sessionMaximumRecordedBytes = std::min<qint64>(
                m_sessionMaximumRecordedBytes,
                std::max<qint64>(0, storage.bytesAvailable() - ReservedFreeBytes));
        }
        if (m_sessionMaximumRecordedBytes < MinimumUsefulRecordingBytes) {
            m_callbacks.problem(QStringLiteral("sensor-storage-low"));
            m_callbacks.status(QStringLiteral("磁盘空间不足，已保留 256 MB 安全余量并改为仅预览"));
            m_sessionUsage = QStringLiteral("preview");
        }
    }
    if (m_sessionUsage == QStringLiteral("record")) {
        const auto opened = m_sessionStorage.open(cleanFolder);
        if (opened != SensorSessionStorage::OpenResult::Ready) {
            m_callbacks.status(opened == SensorSessionStorage::OpenResult::DirectoryFailed
                ? QStringLiteral("无法创建飞行记录的传感器目录，已改为仅预览")
                : QStringLiteral("无法创建传感器时间索引，已改为仅预览"));
            m_sessionUsage = QStringLiteral("preview");
        }
    }
    m_sessionBytesWritten = 0;
    m_sessionFirstDataMs = -1;
    m_sessionLastDataMs = -1;
    m_lastRgbRecordMs = m_lastDepthRecordMs = m_lastScanRecordMs = -1;
    m_lastPrimarySampleMs = -1;
    m_lastRgbPrimarySampleMs = m_lastDepthPrimarySampleMs = m_lastLidarPrimarySampleMs = -1;
    m_sessionMaximumDataGapMs = 0;
    m_sessionRgbFrames = m_sessionDepthFrames = m_sessionScanCount = 0;
    m_sessionGimbalSamples = m_sessionDropoutCount = 0;
    m_sessionLatencyTotalMs = 0.0;
    m_sessionLatencySamples = 0;
    m_sessionMinimumFrameRateHz = 0.0;
    m_sessionRgbMinimumFrameRateHz = 0.0;
    m_sessionDepthMinimumFrameRateHz = 0.0;
    m_sessionLidarMinimumFrameRateHz = 0.0;
    m_sessionRgbMaximumDataGapMs = 0;
    m_sessionDepthMaximumDataGapMs = 0;
    m_sessionLidarMaximumDataGapMs = 0;
    m_sessionDepthValidRatioTotal = 0.0;
    m_sessionDepthValidRatioSamples = 0;
    m_sessionRecordedSampleCount = 0;
    m_sessionWriteFailureCount = 0;
    m_sessionStaleFrameDiscardCount = 0;
    m_sessionExpectedSamples = 0.0;
    m_sessionExpectedUpdatedAtMs = -1;
    m_sessionPrimaryDataStarted = false;
    m_sessionPrimaryFirstDataMs = -1;
    m_sessionNextStorageCheckBytes = 0;
    m_sessionNearestObstacleMeters = 0.0;
    m_sessionStorageLimitReached = false;
    m_sessionRecordingTimer.restart();
    m_sessionTracking = true;
    const bool recording = m_sessionUsage == QStringLiteral("record");
    if (m_sessionRecording != recording) {
        m_sessionRecording = recording;
        m_callbacks.changed();
    }
    return true;
}

void SensorSessionRecorder::updateExpectedSessionSamples(qint64 elapsedMs)
{
    // Container creation, bridge discovery and the first ROS subscription take
    // several seconds. They are startup time, not a data-stream interruption.
    if (!m_sessionTracking || !m_sessionPrimaryDataStarted) return;
    if (m_sessionExpectedUpdatedAtMs < 0) {
        m_sessionExpectedUpdatedAtMs = elapsedMs;
        return;
    }
    if (elapsedMs <= m_sessionExpectedUpdatedAtMs) return;
    const qint64 deltaMs = elapsedMs - m_sessionExpectedUpdatedAtMs;
    m_sessionExpectedSamples += deltaMs * m_callbacks.targetFps() / 1000.0;
    m_sessionExpectedUpdatedAtMs = elapsedMs;
}

void SensorSessionRecorder::stopSessionFileRecording(const QString &message,
                                                       bool storageLimit)
{
    if (!m_sessionRecording) return;
    m_sessionStorageLimitReached = m_sessionStorageLimitReached || storageLimit;
    m_sessionWriteFailed = m_sessionWriteFailed || !storageLimit;
    if (!storageLimit) ++m_sessionWriteFailureCount;
    m_sessionStorage.close();
    m_sessionRecording = false;
    m_callbacks.changed();
    m_callbacks.problem(storageLimit ? QStringLiteral("sensor-storage-limit")
                                : QStringLiteral("sensor-recording-write-failed"));
    m_callbacks.status(message);
}

void SensorSessionRecorder::recordSensorMessage(const QJsonObject &source,
                                                  const QByteArray &jpeg)
{
    if (!m_sessionTracking || !m_sessionRecordingTimer.isValid()) return;
    if (m_sessionRecording && m_sessionBytesWritten >= m_sessionMaximumRecordedBytes) {
        ++m_sessionDropoutCount;
        stopSessionFileRecording(
            QStringLiteral("传感器记录已达到本次安全容量上限，实时预览继续运行"), true);
    }
    if (m_sessionRecording && m_sessionBytesWritten >= m_sessionNextStorageCheckBytes) {
        constexpr qint64 MinimumFreeBytes = 192LL * 1024 * 1024;
        constexpr qint64 StorageCheckIntervalBytes = 16LL * 1024 * 1024;
        const QStorageInfo storage(m_sessionStorage.directory());
        if (storage.isValid() && storage.isReady() && storage.bytesAvailable() < MinimumFreeBytes) {
            ++m_sessionDropoutCount;
            stopSessionFileRecording(
                QStringLiteral("磁盘剩余空间不足，已停止保存但继续实时预览"), true);
        }
        m_sessionNextStorageCheckBytes = m_sessionBytesWritten + StorageCheckIntervalBytes;
    }
    const qint64 elapsedMs = m_sessionRecordingTimer.elapsed();
    QJsonObject entry = source;
    entry.insert(QStringLiteral("elapsedMs"), elapsedMs);
    entry.remove(QStringLiteral("jpegBase64"));
    if (!m_sessionRecording)
        entry.remove(QStringLiteral("depthMillimetresBase64"));
    const QString type = entry.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("camera")) {
        const QString stream = entry.value(QStringLiteral("stream")).toString() == QStringLiteral("depth")
                                   ? QStringLiteral("depth") : QStringLiteral("rgb");
        qint64 &lastMs = stream == QStringLiteral("depth") ? m_lastDepthRecordMs : m_lastRgbRecordMs;
        const int minimumInterval = std::max(1, 1000 / m_callbacks.targetFps());
        if (lastMs >= 0 && elapsedMs - lastMs < minimumInterval - 2) return;
        lastMs = elapsedMs;
        if (!m_sessionPrimaryDataStarted) {
            m_sessionPrimaryDataStarted = true;
            m_sessionPrimaryFirstDataMs = elapsedMs;
            m_sessionExpectedUpdatedAtMs = elapsedMs;
        } else {
            updateExpectedSessionSamples(elapsedMs);
        }
        int &counter = stream == QStringLiteral("depth") ? m_sessionDepthFrames : m_sessionRgbFrames;
        const QString relativePath = QStringLiteral("%1/%2-%3.jpg")
                                         .arg(stream)
                                         .arg(counter, 7, 10, QLatin1Char('0'))
                                         .arg(elapsedMs, 9, 10, QLatin1Char('0'));
        if (m_sessionRecording) {
            if (!m_sessionStorage.writeFrame(relativePath, jpeg)) {
                ++m_sessionDropoutCount;
                stopSessionFileRecording(
                    QStringLiteral("传感器文件写入失败，已停止保存但继续实时预览"), false);
                entry.remove(QStringLiteral("file"));
            } else {
                entry.insert(QStringLiteral("file"), relativePath);
                m_sessionBytesWritten += jpeg.size();
            }
        }
        ++counter;
        const double frameRate = entry.value(QStringLiteral("hz")).toDouble();
        if (frameRate > 0.0 && (m_sessionMinimumFrameRateHz <= 0.0
                               || frameRate < m_sessionMinimumFrameRateHz))
            m_sessionMinimumFrameRateHz = frameRate;
        if (stream == QStringLiteral("depth")) {
            if (frameRate > 0.0 && (m_sessionDepthMinimumFrameRateHz <= 0.0
                                   || frameRate < m_sessionDepthMinimumFrameRateHz))
                m_sessionDepthMinimumFrameRateHz = frameRate;
            const double validRatio = std::clamp(
                entry.value(QStringLiteral("validRatio")).toDouble(), 0.0, 1.0);
            m_sessionDepthValidRatioTotal += validRatio;
            ++m_sessionDepthValidRatioSamples;
            const double nearest = entry.value(QStringLiteral("nearestDistance")).toDouble();
            if (nearest > 0.0 && (m_sessionNearestObstacleMeters <= 0.0
                                  || nearest < m_sessionNearestObstacleMeters))
                m_sessionNearestObstacleMeters = nearest;
        } else if (frameRate > 0.0 && (m_sessionRgbMinimumFrameRateHz <= 0.0
                                      || frameRate < m_sessionRgbMinimumFrameRateHz)) {
            m_sessionRgbMinimumFrameRateHz = frameRate;
        }
        const int latencyMs = entry.value(QStringLiteral("latencyMilliseconds")).toInt(-1);
        if (latencyMs >= 0) {
            m_sessionLatencyTotalMs += latencyMs;
            ++m_sessionLatencySamples;
        }
    } else if (type == QStringLiteral("scan")) {
        const int minimumInterval = std::max(1, 1000 / m_callbacks.targetFps());
        if (m_lastScanRecordMs >= 0 && elapsedMs - m_lastScanRecordMs < minimumInterval - 2) return;
        m_lastScanRecordMs = elapsedMs;
        if (!m_sessionPrimaryDataStarted) {
            m_sessionPrimaryDataStarted = true;
            m_sessionPrimaryFirstDataMs = elapsedMs;
            m_sessionExpectedUpdatedAtMs = elapsedMs;
        } else {
            updateExpectedSessionSamples(elapsedMs);
        }
        ++m_sessionScanCount;
        const double frameRate = entry.value(QStringLiteral("hz")).toDouble();
        if (frameRate > 0.0 && (m_sessionMinimumFrameRateHz <= 0.0
                               || frameRate < m_sessionMinimumFrameRateHz))
            m_sessionMinimumFrameRateHz = frameRate;
        if (frameRate > 0.0 && (m_sessionLidarMinimumFrameRateHz <= 0.0
                               || frameRate < m_sessionLidarMinimumFrameRateHz))
            m_sessionLidarMinimumFrameRateHz = frameRate;
        const double nearest = entry.value(QStringLiteral("nearestDistance")).toDouble();
        if (nearest > 0.0 && (m_sessionNearestObstacleMeters <= 0.0
                              || nearest < m_sessionNearestObstacleMeters))
            m_sessionNearestObstacleMeters = nearest;
        const int latencyMs = entry.value(QStringLiteral("latencyMilliseconds")).toInt(-1);
        if (latencyMs >= 0) {
            m_sessionLatencyTotalMs += latencyMs;
            ++m_sessionLatencySamples;
        }
    } else if (type == QStringLiteral("gimbal-state")) {
        ++m_sessionGimbalSamples;
    }
    if (type == QStringLiteral("camera") || type == QStringLiteral("scan")) {
        if (m_lastPrimarySampleMs >= 0)
            m_sessionMaximumDataGapMs = std::max(
                m_sessionMaximumDataGapMs, elapsedMs - m_lastPrimarySampleMs);
        m_lastPrimarySampleMs = elapsedMs;
        qint64 *streamLastMs = nullptr;
        qint64 *streamMaximumGapMs = nullptr;
        if (type == QStringLiteral("scan")) {
            streamLastMs = &m_lastLidarPrimarySampleMs;
            streamMaximumGapMs = &m_sessionLidarMaximumDataGapMs;
        } else if (entry.value(QStringLiteral("stream")).toString() == QStringLiteral("depth")) {
            streamLastMs = &m_lastDepthPrimarySampleMs;
            streamMaximumGapMs = &m_sessionDepthMaximumDataGapMs;
        } else {
            streamLastMs = &m_lastRgbPrimarySampleMs;
            streamMaximumGapMs = &m_sessionRgbMaximumDataGapMs;
        }
        if (*streamLastMs >= 0)
            *streamMaximumGapMs = std::max(*streamMaximumGapMs, elapsedMs - *streamLastMs);
        *streamLastMs = elapsedMs;
    }
    if (m_sessionRecording) {
        const QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
        if (!m_sessionStorage.append(line)) {
            ++m_sessionDropoutCount;
            stopSessionFileRecording(
                QStringLiteral("传感器时间索引写入失败，已停止保存但继续实时预览"), false);
        } else {
            m_sessionBytesWritten += line.size();
            ++m_sessionRecordedSampleCount;
        }
    }
    if (m_sessionFirstDataMs < 0) m_sessionFirstDataMs = elapsedMs;
    m_sessionLastDataMs = elapsedMs;
    if ((m_sessionRgbFrames + m_sessionDepthFrames + m_sessionScanCount
         + m_sessionGimbalSamples) % 10 == 0) {
        m_sessionStorage.flush();
    }
}

bool SensorSessionRecorder::writeSessionManifest(const QVariantMap &summary)
{
    return m_sessionStorage.writeManifest(summary);
}

QVariantMap SensorSessionRecorder::finishSessionRecording()
{
    if (!m_sessionTracking) return {};
    const qint64 finishedAtMs = m_sessionRecordingTimer.isValid()
        ? m_sessionRecordingTimer.elapsed() : 0;
    updateExpectedSessionSamples(finishedAtMs);
    if (m_lastPrimarySampleMs >= 0 && finishedAtMs > m_lastPrimarySampleMs) {
        m_sessionMaximumDataGapMs = std::max(
            m_sessionMaximumDataGapMs, finishedAtMs - m_lastPrimarySampleMs);
    }
    const auto includeTailGap = [finishedAtMs](qint64 lastSampleMs, qint64 &maximumGapMs) {
        if (lastSampleMs >= 0)
            maximumGapMs = std::max(maximumGapMs, finishedAtMs - lastSampleMs);
    };
    includeTailGap(m_lastRgbPrimarySampleMs, m_sessionRgbMaximumDataGapMs);
    includeTailGap(m_lastDepthPrimarySampleMs, m_sessionDepthMaximumDataGapMs);
    includeTailGap(m_lastLidarPrimarySampleMs, m_sessionLidarMaximumDataGapMs);
    m_sessionStorage.close();
    const int totalSamples = m_sessionRgbFrames + m_sessionDepthFrames
                             + m_sessionScanCount + m_sessionGimbalSamples;
    const double durationSeconds = std::max<qint64>(0, finishedAtMs) / 1000.0;
    int primarySamples = m_sessionScanCount;
    if (m_callbacks.kind() == QStringLiteral("depth-camera"))
        primarySamples = std::min(m_sessionRgbFrames, m_sessionDepthFrames);
    else if (m_callbacks.kind() != QStringLiteral("lidar-2d"))
        primarySamples = m_sessionRgbFrames;
    const double expectedSamples = std::max(0.0, m_sessionExpectedSamples);
    const double activeDurationSeconds = m_sessionPrimaryFirstDataMs >= 0
        && m_lastPrimarySampleMs > m_sessionPrimaryFirstDataMs
        ? (m_lastPrimarySampleMs - m_sessionPrimaryFirstDataMs) / 1000.0 : 0.0;
    const double averageFrameRateHz = activeDurationSeconds > 0.0 && primarySamples > 1
        ? (primarySamples - 1) / activeDurationSeconds
        : static_cast<double>(primarySamples);
    const auto availability = [expectedSamples](int count) {
        return expectedSamples > 0.0
            ? std::clamp(count / expectedSamples * 100.0, 0.0, 100.0)
            : count > 0 ? 100.0 : 0.0;
    };
    const double rgbAvailabilityPercent = availability(m_sessionRgbFrames);
    const double depthAvailabilityPercent = availability(m_sessionDepthFrames);
    const double lidarAvailabilityPercent = availability(m_sessionScanCount);
    double availabilityPercent = lidarAvailabilityPercent;
    if (m_callbacks.kind() == QStringLiteral("depth-camera"))
        availabilityPercent = std::min(rgbAvailabilityPercent, depthAvailabilityPercent);
    else if (m_callbacks.kind() != QStringLiteral("lidar-2d"))
        availabilityPercent = rgbAvailabilityPercent;

    double primaryMinimumFrameRateHz = m_sessionLidarMinimumFrameRateHz;
    qint64 primaryMaximumGapMs = m_sessionLidarMaximumDataGapMs;
    if (m_callbacks.kind() == QStringLiteral("depth-camera")) {
        primaryMinimumFrameRateHz = (m_sessionRgbMinimumFrameRateHz > 0.0
                                     && m_sessionDepthMinimumFrameRateHz > 0.0)
            ? std::min(m_sessionRgbMinimumFrameRateHz, m_sessionDepthMinimumFrameRateHz) : 0.0;
        primaryMaximumGapMs = std::max(m_sessionRgbMaximumDataGapMs,
                                       m_sessionDepthMaximumDataGapMs);
    } else if (m_callbacks.kind() != QStringLiteral("lidar-2d")) {
        primaryMinimumFrameRateHz = m_sessionRgbMinimumFrameRateHz;
        primaryMaximumGapMs = m_sessionRgbMaximumDataGapMs;
    }
    const double depthValidRatio = m_sessionDepthValidRatioSamples > 0
        ? m_sessionDepthValidRatioTotal / m_sessionDepthValidRatioSamples : 0.0;
    const QString timelinePath = QDir(m_sessionStorage.directory())
                                     .filePath(QStringLiteral("timeline.ndjson"));
    const bool replayable = m_sessionRecordedSampleCount > 0
                            && QFileInfo(timelinePath).isFile()
                            && QFileInfo(timelinePath).size() > 0;
    const bool requestedRecording = m_sessionRecordingRequested;
    bool recordingComplete = replayable && !m_sessionWriteFailed
                             && !m_sessionStorageLimitReached;
    const bool visualDataAvailable = m_callbacks.kind() == QStringLiteral("lidar-2d")
        ? m_sessionScanCount > 0
        : m_callbacks.kind() == QStringLiteral("depth-camera")
          ? m_sessionRgbFrames > 0 && m_sessionDepthFrames > 0 && depthValidRatio > 0.0
          : m_sessionRgbFrames > 0;
    QVariantMap summary{
        {QStringLiteral("format"), QStringLiteral("FlightBenchSensorSession")},
        {QStringLiteral("formatVersion"), 2},
        {QStringLiteral("available"), visualDataAvailable},
        {QStringLiteral("usage"), m_sessionUsage},
        {QStringLiteral("requestedUsage"), requestedRecording
             ? QStringLiteral("record") : m_sessionUsage},
        {QStringLiteral("recorded"), replayable},
        {QStringLiteral("replayable"), replayable},
        {QStringLiteral("recordingComplete"), recordingComplete},
        {QStringLiteral("kind"), m_callbacks.kind()},
        {QStringLiteral("performanceMode"), m_callbacks.mode()},
        {QStringLiteral("effectivePerformanceMode"), m_callbacks.effectiveMode()},
        {QStringLiteral("targetFramesPerSecond"), m_callbacks.targetFps()},
        {QStringLiteral("expectedSampleCount"), expectedSamples},
        {QStringLiteral("startedOffsetMs"), std::max<qint64>(0, m_sessionFirstDataMs)},
        {QStringLiteral("primaryStartedOffsetMs"),
         std::max<qint64>(0, m_sessionPrimaryFirstDataMs)},
        {QStringLiteral("endedOffsetMs"), std::max<qint64>(0, finishedAtMs)},
        {QStringLiteral("durationSeconds"), durationSeconds},
        {QStringLiteral("rgbFrameCount"), m_sessionRgbFrames},
        {QStringLiteral("depthFrameCount"), m_sessionDepthFrames},
        {QStringLiteral("lidarScanCount"), m_sessionScanCount},
        {QStringLiteral("gimbalSampleCount"), m_sessionGimbalSamples},
        {QStringLiteral("dropoutCount"), m_sessionDropoutCount},
        {QStringLiteral("staleFrameDiscardCount"), m_sessionStaleFrameDiscardCount},
        {QStringLiteral("minimumFrameRateHz"), primaryMinimumFrameRateHz},
        {QStringLiteral("averageFrameRateHz"), averageFrameRateHz},
        {QStringLiteral("maximumDataGapSeconds"), primaryMaximumGapMs / 1000.0},
        {QStringLiteral("dataAvailabilityPercent"), availabilityPercent},
        {QStringLiteral("rgbAvailabilityPercent"), rgbAvailabilityPercent},
        {QStringLiteral("depthAvailabilityPercent"), depthAvailabilityPercent},
        {QStringLiteral("lidarAvailabilityPercent"), lidarAvailabilityPercent},
        {QStringLiteral("rgbMinimumFrameRateHz"), m_sessionRgbMinimumFrameRateHz},
        {QStringLiteral("depthMinimumFrameRateHz"), m_sessionDepthMinimumFrameRateHz},
        {QStringLiteral("lidarMinimumFrameRateHz"), m_sessionLidarMinimumFrameRateHz},
        {QStringLiteral("rgbMaximumDataGapSeconds"), m_sessionRgbMaximumDataGapMs / 1000.0},
        {QStringLiteral("depthMaximumDataGapSeconds"), m_sessionDepthMaximumDataGapMs / 1000.0},
        {QStringLiteral("lidarMaximumDataGapSeconds"), m_sessionLidarMaximumDataGapMs / 1000.0},
        {QStringLiteral("depthValidRatio"), depthValidRatio},
        {QStringLiteral("nearestObstacleMeters"), m_sessionNearestObstacleMeters},
        {QStringLiteral("obstacleDistanceMeasured"), m_sessionNearestObstacleMeters > 0.0},
        {QStringLiteral("latencyMeasured"), m_sessionLatencySamples > 0},
        {QStringLiteral("latencyMethod"), QStringLiteral("monotonic-sync-v1")},
        {QStringLiteral("latencySampleCount"), m_sessionLatencySamples},
        {QStringLiteral("averageLatencyMilliseconds"),
         m_sessionLatencySamples > 0 ? m_sessionLatencyTotalMs / m_sessionLatencySamples : 0.0},
        {QStringLiteral("sizeBytes"), m_sessionBytesWritten},
        {QStringLiteral("relativeDirectory"), replayable
             ? QStringLiteral("sensor") : QString()},
        {QStringLiteral("timelineFile"), replayable
             ? QStringLiteral("sensor/timeline.ndjson") : QString()},
        {QStringLiteral("slamMapAvailable"), false},
        {QStringLiteral("storageLimitReached"), m_sessionStorageLimitReached},
        {QStringLiteral("writeFailed"), m_sessionWriteFailed},
        {QStringLiteral("writeFailureCount"), m_sessionWriteFailureCount},
        {QStringLiteral("recordedSampleCount"), m_sessionRecordedSampleCount},
        {QStringLiteral("totalSampleCount"), totalSamples},
        {QStringLiteral("statusLabel"), !visualDataAvailable
             ? QStringLiteral("未收到有效传感器数据")
             : requestedRecording && !replayable
               ? QStringLiteral("收到数据，但没有成功保存可回放记录")
             : m_sessionStorageLimitReached
               ? QStringLiteral("已保存部分数据；因容量保护提前停止记录")
             : m_sessionWriteFailed
               ? QStringLiteral("已保存部分数据；写入失败导致记录不完整")
             : replayable ? QStringLiteral("已记录并可同步回放")
                                  : QStringLiteral("已收到数据，但方案选择了仅预览")},
    };
    if (replayable && !writeSessionManifest(summary)) {
        recordingComplete = false;
        summary.insert(QStringLiteral("recordingComplete"), false);
        summary.insert(QStringLiteral("manifestWriteFailed"), true);
        summary.insert(QStringLiteral("statusLabel"),
                       QStringLiteral("数据已保存，但飞行记录索引写入失败"));
    }
    const bool wasRecording = m_sessionRecording;
    m_sessionRecording = false;
    m_sessionTracking = false;
    m_sessionUsage = QStringLiteral("off");
    if (wasRecording) m_callbacks.changed();
    m_sessionStorage.clearDirectory();
    return summary;
}
