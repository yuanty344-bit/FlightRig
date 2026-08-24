#pragma once

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class FlightSessionRecorder final : public QObject
{
    Q_OBJECT

public:
    explicit FlightSessionRecorder(QObject *parent = nullptr,
                                   const QString &sessionsDirectory = {});
    ~FlightSessionRecorder() override;

    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] QString activeSessionId() const;
    [[nodiscard]] QVariantMap activeSession() const;
    [[nodiscard]] QVariantList completedSessions() const { return m_completedSessions; }
    [[nodiscard]] QVariantList trashedSessions() const { return m_trashedSessions; }
    [[nodiscard]] QString sessionsDirectory() const { return m_sessionsDirectory; }
    [[nodiscard]] QString trashDirectory() const { return m_trashDirectory; }

    bool startSession(const QString &modelId,
                      const QString &modelName,
                      double takeoffAltitudeMeters,
                      const QString &worldId,
                      const QString &worldName,
                      const QString &applicationVersion,
                      const QString &simulationImage,
                      const QString &qtVersion = {},
                      const QString &mavsdkVersion = {});
    void finishSession(const QString &reason);
    void appendEvent(const QString &entry);
    void markHealthReady();
    void markIssue(const QString &message);
    void markTakeoffRequested();
    void markLandingRequested();
    void setTestPlan(const QVariantMap &plan);
    void setMissionPlan(const QString &routeId,
                        const QString &routeName,
                        const QVariantList &relativeWaypoints,
                        const QString &endAction,
                        double startEastMeters,
                        double startUpMeters,
                        double startSouthMeters);
    void updateMissionMetrics(double crossTrackErrorMeters,
                              int completedItems,
                              int totalItems,
                              const QString &interruptionReason = {});
    void updateObstacleClearance(bool available,
                                 double clearanceMeters,
                                 bool infringed);
    void updateFlightState(bool armed, bool inAir);
    void recordSample(bool connected,
                      bool ready,
                      bool armed,
                      bool inAir,
                      bool telemetryAvailable,
                      double altitudeMeters,
                      double horizontalSpeedMetersPerSecond,
                      double verticalSpeedMetersPerSecond,
                      double headingDegrees,
                      double rollDegrees,
                      double pitchDegrees,
                      double yawDegrees,
                      bool imuAvailable,
                      double gyroRollDegreesPerSecond,
                      double gyroPitchDegreesPerSecond,
                      double gyroYawDegreesPerSecond,
                      bool gpsAvailable,
                      const QString &gpsFixType,
                      int gpsSatellites,
                      bool batteryAvailable,
                      double batteryRemainingPercent,
                      double batteryVoltageVolts,
                      bool keyboardControlActive,
                      double commandForwardMetersPerSecond,
                      double commandRightMetersPerSecond,
                      double commandUpMetersPerSecond,
                      double commandYawDegreesPerSecond,
                      bool preciseTurnActive,
                      double preciseTurnTargetDegrees,
                      bool localPositionAvailable,
                      double sceneEastMeters,
                      double sceneUpMeters,
                      double sceneSouthMeters);

    [[nodiscard]] QVariantMap sessionDetails(const QString &sessionId) const;
    [[nodiscard]] QString csvPathForSession(const QString &sessionId) const;
    [[nodiscard]] QString folderPathForSession(const QString &sessionId) const;
    bool updateSessionDetails(const QString &sessionId,
                              const QString &displayName,
                              const QString &notes);
    bool setSessionReport(const QString &sessionId, const QString &reportPath);
    bool deleteSession(const QString &sessionId);
    bool restoreSession(const QString &sessionId);
    bool permanentlyDeleteSession(const QString &sessionId);
    int purgeTrashOlderThan(int retentionDays);
    int clearTrash();

signals:
    void activeSessionChanged();
    void completedSessionsChanged();
    void trashedSessionsChanged();
    void recorderError(const QString &message);

private:
    void loadCompletedSessions();
    void loadTrashedSessions();
    [[nodiscard]] bool saveStandaloneMetadata(QVariantMap &metadata,
                                              const QString &folderPath) const;
    void writeMetadata();
    void refreshActiveSummary();
    [[nodiscard]] QVariantMap metadataForSession(const QString &sessionId) const;
    [[nodiscard]] static QString formatDuration(int seconds);
    [[nodiscard]] static QString csvField(const QString &value);
    [[nodiscard]] static QString safeFolderPart(const QString &value);

    QString m_sessionsDirectory;
    QString m_trashDirectory;
    QVariantMap m_metadata;
    QVariantList m_completedSessions;
    QVariantList m_trashedSessions;
    QFile m_csvFile;
    QFile m_eventsFile;
    QTimer m_summaryTimer;
    QDateTime m_startedAt;
    bool m_active{false};
    bool m_healthPassed{false};
    bool m_takeoffRequested{false};
    bool m_landingRequested{false};
    bool m_sawInAir{false};
    bool m_armed{false};
    bool m_inAir{false};
    bool m_metadataErrorReported{false};
    int m_issueCount{0};
    qint64 m_sampleCount{0};
    double m_maxAltitude{0.0};
    double m_maxHorizontalSpeed{0.0};
    double m_maxVerticalSpeed{0.0};
    double m_maxMissionCrossTrackError{0.0};
    qint64 m_lastSampleElapsedMs{-1};
    bool m_sawTelemetry{false};
    qint64 m_telemetryGapStartedMs{-1};
    qint64 m_maxTelemetryGapMs{0};
    bool m_obstacleClearanceAvailable{false};
    double m_minimumObstacleClearanceMeters{0.0};
    int m_obstacleInfringementCount{0};
    bool m_lastObstacleInfringed{false};
    int m_missionCompletedItems{0};
    int m_missionTotalItems{0};
    QString m_missionInterruptionReason;
};
