#pragma once
#include "sensors/sensorsessionrecorder.h"
#include "sensors/mjpegaviwriter.h"

#include <QObject>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include "sensors/sensorclocksync.h"

class SensorServiceController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool imageAvailable READ imageAvailable NOTIFY imageAvailableChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString problemCode READ problemCode NOTIFY problemCodeChanged)
    Q_PROPERTY(QVariantList lidarPoints READ lidarPoints NOTIFY lidarScanChanged)
    Q_PROPERTY(double lidarNearestDistanceMeters READ lidarNearestDistanceMeters NOTIFY lidarScanChanged)
    Q_PROPERTY(double lidarNearestAngleDegrees READ lidarNearestAngleDegrees NOTIFY lidarScanChanged)
    Q_PROPERTY(double lidarScanHz READ lidarScanHz NOTIFY lidarScanChanged)
    Q_PROPERTY(double lidarFieldOfViewDegrees READ lidarFieldOfViewDegrees NOTIFY lidarScanChanged)
    Q_PROPERTY(QString sensorKind READ sensorKind NOTIFY sensorKindChanged)
    Q_PROPERTY(QString rgbFrameDataUrl READ rgbFrameDataUrl NOTIFY cameraFrameChanged)
    Q_PROPERTY(QString depthFrameDataUrl READ depthFrameDataUrl NOTIFY depthFrameChanged)
    Q_PROPERTY(bool rgbFrameReady READ rgbFrameReady NOTIFY cameraFrameChanged)
    Q_PROPERTY(bool depthFrameReady READ depthFrameReady NOTIFY depthFrameChanged)
    Q_PROPERTY(double rgbFrameHz READ rgbFrameHz NOTIFY cameraFrameChanged)
    Q_PROPERTY(double depthFrameHz READ depthFrameHz NOTIFY depthFrameChanged)
    Q_PROPERTY(int rgbLatencyMilliseconds READ rgbLatencyMilliseconds NOTIFY cameraFrameChanged)
    Q_PROPERTY(int depthLatencyMilliseconds READ depthLatencyMilliseconds NOTIFY depthFrameChanged)
    Q_PROPERTY(bool cameraStreamInterrupted READ cameraStreamInterrupted NOTIFY cameraStateChanged)
    Q_PROPERTY(bool rgbStreamInterrupted READ rgbStreamInterrupted NOTIFY cameraStateChanged)
    Q_PROPERTY(bool depthStreamInterrupted READ depthStreamInterrupted NOTIFY cameraStateChanged)
    Q_PROPERTY(double depthCenterDistanceMeters READ depthCenterDistanceMeters NOTIFY depthFrameChanged)
    Q_PROPERTY(double depthNearestDistanceMeters READ depthNearestDistanceMeters NOTIFY depthFrameChanged)
    Q_PROPERTY(double depthValidRatio READ depthValidRatio NOTIFY depthFrameChanged)
    Q_PROPERTY(double gimbalYawDegrees READ gimbalYawDegrees NOTIFY gimbalStateChanged)
    Q_PROPERTY(double gimbalPitchDegrees READ gimbalPitchDegrees NOTIFY gimbalStateChanged)
    Q_PROPERTY(double gimbalRollDegrees READ gimbalRollDegrees NOTIFY gimbalStateChanged)
    Q_PROPERTY(double gimbalTargetYawDegrees READ gimbalTargetYawDegrees NOTIFY gimbalStateChanged)
    Q_PROPERTY(double gimbalTargetPitchDegrees READ gimbalTargetPitchDegrees NOTIFY gimbalStateChanged)
    Q_PROPERTY(bool gimbalFeedbackAvailable READ gimbalFeedbackAvailable NOTIFY gimbalStateChanged)
    Q_PROPERTY(bool gimbalMoving READ gimbalMoving NOTIFY gimbalStateChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QString capturesDirectory READ capturesDirectory CONSTANT)
    Q_PROPERTY(QString lastCapturePath READ lastCapturePath NOTIFY captureSaved)
    Q_PROPERTY(QString performanceMode READ performanceMode NOTIFY performanceModeChanged)
    Q_PROPERTY(QString effectivePerformanceMode READ effectivePerformanceMode NOTIFY performanceModeChanged)
    Q_PROPERTY(int targetFramesPerSecond READ targetFramesPerSecond NOTIFY performanceModeChanged)
    Q_PROPERTY(bool sessionRecording READ sessionRecording NOTIFY sessionRecordingChanged)

public:
    explicit SensorServiceController(QObject *parent = nullptr);
    ~SensorServiceController() override;

    [[nodiscard]] bool imageAvailable() const { return m_imageAvailable; }
    [[nodiscard]] bool running() const { return m_running; }
    [[nodiscard]] bool connected() const { return m_connected; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString statusMessage() const { return m_statusMessage; }
    [[nodiscard]] QString problemCode() const { return m_problemCode; }
    [[nodiscard]] QVariantList lidarPoints() const { return m_lidarPoints; }
    // Internal consumers can inspect the current scan without increasing the
    // implicit-sharing reference count on every ROS message.
    [[nodiscard]] const QVariantList &lidarPointsForMapping() const { return m_lidarPoints; }
    [[nodiscard]] qint64 lidarTimestampEpochMs() const { return m_lidarTimestampEpochMs; }
    [[nodiscard]] double lidarNearestDistanceMeters() const
    {
        return m_lidarNearestDistanceMeters;
    }
    [[nodiscard]] double lidarNearestAngleDegrees() const
    {
        return m_lidarNearestAngleDegrees;
    }
    [[nodiscard]] double lidarScanHz() const { return m_lidarScanHz; }
    [[nodiscard]] double lidarFieldOfViewDegrees() const { return m_lidarFieldOfViewDegrees; }
    [[nodiscard]] QString sensorKind() const { return m_sensorKind; }
    [[nodiscard]] QString rgbFrameDataUrl() const;
    [[nodiscard]] QString depthFrameDataUrl() const;
    [[nodiscard]] bool rgbFrameReady() const { return !m_rgbJpeg.isEmpty(); }
    [[nodiscard]] bool depthFrameReady() const { return !m_depthJpeg.isEmpty(); }
    [[nodiscard]] double rgbFrameHz() const { return m_rgbFrameHz; }
    [[nodiscard]] double depthFrameHz() const { return m_depthFrameHz; }
    [[nodiscard]] int rgbLatencyMilliseconds() const { return m_rgbLatencyMilliseconds; }
    [[nodiscard]] int depthLatencyMilliseconds() const { return m_depthLatencyMilliseconds; }
    [[nodiscard]] bool cameraStreamInterrupted() const { return m_cameraStreamInterrupted; }
    [[nodiscard]] bool rgbStreamInterrupted() const { return m_rgbStreamInterrupted; }
    [[nodiscard]] bool depthStreamInterrupted() const { return m_depthStreamInterrupted; }
    [[nodiscard]] double depthCenterDistanceMeters() const { return m_depthCenterDistanceMeters; }
    [[nodiscard]] double depthNearestDistanceMeters() const { return m_depthNearestDistanceMeters; }
    [[nodiscard]] double depthValidRatio() const { return m_depthValidRatio; }
    [[nodiscard]] const QByteArray &depthMillimetresForMapping() const
    {
        return m_depthMillimetres;
    }
    [[nodiscard]] int depthGridWidthForMapping() const { return m_depthGridWidth; }
    [[nodiscard]] int depthGridHeightForMapping() const { return m_depthGridHeight; }
    [[nodiscard]] qint64 depthTimestampEpochMs() const { return m_depthTimestampEpochMs; }
    [[nodiscard]] double gimbalYawDegrees() const { return m_gimbalYawDegrees; }
    [[nodiscard]] double gimbalPitchDegrees() const { return m_gimbalPitchDegrees; }
    [[nodiscard]] double gimbalRollDegrees() const { return m_gimbalRollDegrees; }
    [[nodiscard]] double gimbalTargetYawDegrees() const { return m_gimbalTargetYawDegrees; }
    [[nodiscard]] double gimbalTargetPitchDegrees() const { return m_gimbalTargetPitchDegrees; }
    [[nodiscard]] bool gimbalFeedbackAvailable() const { return m_gimbalFeedbackAvailable; }
    [[nodiscard]] bool gimbalFeedbackFresh() const {
        return m_gimbalFeedbackAvailable && m_gimbalFeedbackClock.isValid()
               && m_gimbalFeedbackClock.elapsed() <= 500;
    }
    [[nodiscard]] bool gimbalMoving() const;
    [[nodiscard]] bool recording() const { return m_recording; }
    [[nodiscard]] QString capturesDirectory() const;
    [[nodiscard]] QString lastCapturePath() const { return m_lastCapturePath; }
    [[nodiscard]] QString performanceMode() const { return m_performanceMode; }
    [[nodiscard]] QString effectivePerformanceMode() const { return m_effectivePerformanceMode; }
    [[nodiscard]] int targetFramesPerSecond() const;
    [[nodiscard]] bool sessionRecording() const { return m_sessionRecorder.recording(); }
    [[nodiscard]] quint64 rgbFrameSequence() const { return m_rgbFrameSequence; }
    [[nodiscard]] const QByteArray &rgbJpegForSurvey() const { return m_rgbJpeg; }
    [[nodiscard]] int rgbWidthForSurvey() const { return m_rgbWidth; }
    [[nodiscard]] int rgbHeightForSurvey() const { return m_rgbHeight; }
    [[nodiscard]] qint64 rgbTimestampEpochMs() const { return m_rgbTimestampEpochMs; }

    Q_INVOKABLE void probe();
    void start(const QString &modelId, const QString &worldId,
               const QString &performanceMode = QStringLiteral("standard"));
    bool beginSessionRecording(const QString &sessionFolder,
                               const QString &usage,
                               const QString &performanceMode);
    QVariantMap finishSessionRecording();
    Q_INVOKABLE void stop();
    Q_INVOKABLE double depthDistanceAt(double normalizedX, double normalizedY) const;
    Q_INVOKABLE bool saveSnapshot(const QString &stream);
    Q_INVOKABLE bool startRecording(const QString &stream);
    Q_INVOKABLE bool stopRecording();
    Q_INVOKABLE bool setGimbalAngles(double yawDegrees,
                                     double pitchDegrees,
                                     double rollDegrees = 0.0);
#ifdef FLIGHTBENCH_TESTING
    void setSensorKindForTesting(const QString &kind);
    void injectGatewayMessageForTesting(const QJsonObject &message);
    qint64 synchronizeClockForTesting(qint64 remoteOffsetMs);
    void setMaximumRecordedBytesForTesting(qint64 bytes);
#endif

signals:
    void imageAvailableChanged();
    void runningChanged();
    void connectedChanged();
    void busyChanged();
    void statusMessageChanged();
    void problemCodeChanged();
    void lidarScanChanged();
    void sensorKindChanged();
    void cameraFrameChanged();
    void depthFrameChanged();
    void cameraStateChanged();
    void gimbalStateChanged();
    void recordingChanged();
    void captureSaved();
    void performanceModeChanged();
    void sessionRecordingChanged();

private:
    enum class Operation { None, ProbeImage, Start, Stop, Inspect };

    void run(Operation operation, const QStringList &arguments);
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleError(QProcess::ProcessError error);
    void connectGateway();
    void requestClockSync();
    void readGatewayData();
    void processGatewayMessage(const QByteArray &line);
    void applyPerformanceMode(const QString &mode, bool notifyGateway);
    void setEffectivePerformanceMode(const QString &mode, bool notifyGateway);
    void updateAutomaticPerformanceMode(bool streamUnstable);
    void recordSensorMessage(const QJsonObject &message, const QByteArray &jpeg = {});
    void resetScan();
    void resetCamera();
    void resetGimbal();
    void appendRecordingFrame(const QByteArray &jpeg, int width, int height);
    bool finalizeRecording();
    void shutdownSynchronously();
    void setImageAvailable(bool value);
    void setRunning(bool value);
    void setConnected(bool value);
    void setBusy(bool value);
    void setStatusMessage(const QString &message);
    void setProblemCode(const QString &code);

    static constexpr auto ImageName = "flightbench/ros-sensor-bridge:0.12.0";
    static constexpr auto ContainerName = "flightbench-ros";
    static constexpr auto NetworkName = "flightbench-net";
    static constexpr quint16 GatewayPort = 8765;

    QProcess m_process;
    QTcpSocket m_gateway;
    QElapsedTimer m_gatewayClock;
    QTimer m_clockSyncTimer;
    SensorClockSync m_clockSync;
    qint64 m_clockRequestId{0};
    qint64 m_clockRequestSentMs{-1};
    QTimer m_monitorTimer;
    QTimer m_reconnectTimer;
    QElapsedTimer m_lastScanTimer;
    QElapsedTimer m_lastCameraFrameTimer;
    QElapsedTimer m_lastRgbFrameTimer;
    QElapsedTimer m_lastDepthFrameTimer;
    QString m_dockerPath;
    Operation m_operation{Operation::None};
    QByteArray m_gatewayBuffer;
    QVariantList m_lidarPoints;
    qint64 m_lidarTimestampEpochMs{0};
    double m_lidarNearestDistanceMeters{0.0};
    double m_lidarNearestAngleDegrees{0.0};
    double m_lidarScanHz{0.0};
    double m_lidarFieldOfViewDegrees{0.0};
    QString m_sensorKind;
    QByteArray m_rgbJpeg;
    QByteArray m_depthJpeg;
    QString m_rgbFrameDataUrlCache;
    QString m_depthFrameDataUrlCache;
    QByteArray m_depthMillimetres;
    qint64 m_depthTimestampEpochMs{0};
    int m_depthGridWidth{0};
    int m_depthGridHeight{0};
    int m_rgbWidth{0};
    int m_rgbHeight{0};
    int m_depthWidth{0};
    int m_depthHeight{0};
    double m_rgbFrameHz{0.0};
    double m_depthFrameHz{0.0};
    int m_rgbLatencyMilliseconds{-1};
    int m_depthLatencyMilliseconds{-1};
    double m_depthCenterDistanceMeters{0.0};
    double m_depthNearestDistanceMeters{0.0};
    double m_depthValidRatio{0.0};
    double m_gimbalYawDegrees{0.0};
    double m_gimbalPitchDegrees{0.0};
    double m_gimbalRollDegrees{0.0};
    double m_gimbalTargetYawDegrees{0.0};
    double m_gimbalTargetPitchDegrees{0.0};
    double m_gimbalTargetRollDegrees{0.0};
    bool m_gimbalFeedbackAvailable{false};
    QElapsedTimer m_gimbalFeedbackClock;
    bool m_cameraStreamInterrupted{false};
    bool m_rgbStreamInterrupted{false};
    bool m_depthStreamInterrupted{false};
    bool m_recording{false};
    QString m_recordingStream;
    QString m_lastCapturePath;
    QString m_recordingPath;
    MjpegAviWriter m_aviWriter;
    bool m_imageAvailable{false};
    bool m_running{false};
    bool m_connected{false};
    bool m_busy{false};
    bool m_startedByUs{false};
    QString m_statusMessage{QStringLiteral("ROS 2 数据服务尚未检查")};
    QString m_problemCode;
    QString m_performanceMode{QStringLiteral("standard")};
    QString m_effectivePerformanceMode{QStringLiteral("standard")};
    int m_autoStableIntervals{0};
    SensorSessionRecorder m_sessionRecorder;
    quint64 m_rgbFrameSequence{0};
    qint64 m_rgbTimestampEpochMs{0};
};
