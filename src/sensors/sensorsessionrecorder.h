#pragma once
#include "sensors/sensorsessionstorage.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <functional>
#include <utility>

// Owns one sensor recording session, its statistics and persistence.
// Callbacks are synchronous and must outlive this object.
class SensorSessionRecorder {
public:
    struct Callbacks {
        std::function<QString()> kind, mode, effectiveMode;
        std::function<int()> targetFps;
        std::function<void(const QString &)> status, problem;
        std::function<void()> changed;
    };
    void setCallbacks(Callbacks callbacks) { m_callbacks = std::move(callbacks); }
    bool beginSessionRecording(const QString &folder, const QString &usage, const QString &performance);
    QVariantMap finishSessionRecording();
    void recordSensorMessage(const QJsonObject &message, const QByteArray &jpeg = {});
    void updateExpectedSessionSamples(qint64 elapsedMs);
    bool recording() const { return m_sessionRecording; }
    bool tracking() const { return m_sessionTracking; }
    bool clockValid() const { return m_sessionRecordingTimer.isValid(); }
    qint64 elapsed() const { return m_sessionRecordingTimer.elapsed(); }
    void noteDropout() { ++m_sessionDropoutCount; }
    void noteStaleFrames(int count) { m_sessionStaleFrameDiscardCount += count; }
    void setMaximumBytesForTesting(qint64 bytes) { m_sessionMaximumRecordedBytes = bytes; }
private:
    void stopSessionFileRecording(const QString &message, bool storageLimit);
    bool writeSessionManifest(const QVariantMap &summary);
    Callbacks m_callbacks;
    QElapsedTimer m_sessionRecordingTimer;
    bool m_sessionRecording{false};
    bool m_sessionRecordingRequested{false};
    bool m_sessionWriteFailed{false};
    bool m_sessionTracking{false};
    QString m_sessionUsage{QStringLiteral("off")};
    SensorSessionStorage m_sessionStorage;
    qint64 m_sessionBytesWritten{0};
    qint64 m_sessionFirstDataMs{-1};
    qint64 m_sessionLastDataMs{-1};
    qint64 m_sessionPrimaryFirstDataMs{-1};
    qint64 m_lastRgbRecordMs{-1};
    qint64 m_lastDepthRecordMs{-1};
    qint64 m_lastScanRecordMs{-1};
    qint64 m_lastPrimarySampleMs{-1};
    qint64 m_lastRgbPrimarySampleMs{-1};
    qint64 m_lastDepthPrimarySampleMs{-1};
    qint64 m_lastLidarPrimarySampleMs{-1};
    qint64 m_sessionMaximumRecordedBytes{2LL * 1024 * 1024 * 1024};
    qint64 m_sessionNextStorageCheckBytes{0};
    qint64 m_sessionExpectedUpdatedAtMs{-1};
    double m_sessionExpectedSamples{0.0};
    bool m_sessionPrimaryDataStarted{false};
    qint64 m_sessionMaximumDataGapMs{0};
    int m_sessionRgbFrames{0};
    int m_sessionDepthFrames{0};
    int m_sessionScanCount{0};
    int m_sessionGimbalSamples{0};
    int m_sessionDropoutCount{0};
    double m_sessionLatencyTotalMs{0.0};
    int m_sessionLatencySamples{0};
    double m_sessionMinimumFrameRateHz{0.0};
    double m_sessionRgbMinimumFrameRateHz{0.0};
    double m_sessionDepthMinimumFrameRateHz{0.0};
    double m_sessionLidarMinimumFrameRateHz{0.0};
    qint64 m_sessionRgbMaximumDataGapMs{0};
    qint64 m_sessionDepthMaximumDataGapMs{0};
    qint64 m_sessionLidarMaximumDataGapMs{0};
    double m_sessionDepthValidRatioTotal{0.0};
    int m_sessionDepthValidRatioSamples{0};
    int m_sessionRecordedSampleCount{0};
    int m_sessionWriteFailureCount{0};
    int m_sessionStaleFrameDiscardCount{0};
    double m_sessionNearestObstacleMeters{0.0};
    bool m_sessionStorageLimitReached{false};
};
