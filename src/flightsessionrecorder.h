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
    explicit FlightSessionRecorder(QObject *parent = nullptr);
    ~FlightSessionRecorder() override;

    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] QString activeSessionId() const;
    [[nodiscard]] QVariantMap activeSession() const;
    [[nodiscard]] QVariantList completedSessions() const { return m_completedSessions; }
    [[nodiscard]] QString sessionsDirectory() const { return m_sessionsDirectory; }

    bool startSession(const QString &modelId,
                      const QString &modelName,
                      double takeoffAltitudeMeters);
    void finishSession(const QString &reason);
    void appendEvent(const QString &entry);
    void markHealthReady();
    void markIssue(const QString &message);
    void markTakeoffRequested();
    void markLandingRequested();
    void setMissionPlan(const QString &routeId,
                        const QString &routeName,
                        const QVariantList &relativeWaypoints,
                        const QString &endAction);
    void updateMissionMetrics(double crossTrackErrorMeters,
                              int completedItems,
                              int totalItems,
                              const QString &interruptionReason = {});
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

signals:
    void activeSessionChanged();
    void completedSessionsChanged();
    void recorderError(const QString &message);

private:
    void loadCompletedSessions();
    void writeMetadata();
    void refreshActiveSummary();
    [[nodiscard]] QVariantMap metadataForSession(const QString &sessionId) const;
    [[nodiscard]] static QString formatDuration(int seconds);
    [[nodiscard]] static QString csvField(const QString &value);
    [[nodiscard]] static QString safeFolderPart(const QString &value);

    QString m_sessionsDirectory;
    QVariantMap m_metadata;
    QVariantList m_completedSessions;
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
    int m_missionCompletedItems{0};
    int m_missionTotalItems{0};
    QString m_missionInterruptionReason;
};
