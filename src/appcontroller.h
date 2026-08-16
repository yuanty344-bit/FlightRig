#pragma once

#include "dockercontroller.h"
#include "flightpathgeometry.h"
#include "flightsessionrecorder.h"
#include "routelibrary.h"
#include "vehicleconnection.h"

#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dockerAvailable READ dockerAvailable NOTIFY dockerAvailableChanged)
    Q_PROPERTY(bool imageAvailable READ imageAvailable NOTIFY imageAvailableChanged)
    Q_PROPERTY(bool simulationRunning READ simulationRunning NOTIFY simulationRunningChanged)
    Q_PROPERTY(bool vehicleConnected READ vehicleConnected NOTIFY vehicleConnectedChanged)
    Q_PROPERTY(bool vehicleReady READ vehicleReady NOTIFY vehicleReadyChanged)
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)
    Q_PROPERTY(bool inAir READ inAir NOTIFY inAirChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool gyrometerOk READ gyrometerOk NOTIFY healthChanged)
    Q_PROPERTY(bool accelerometerOk READ accelerometerOk NOTIFY healthChanged)
    Q_PROPERTY(bool magnetometerOk READ magnetometerOk NOTIFY healthChanged)
    Q_PROPERTY(bool localPositionOk READ localPositionOk NOTIFY healthChanged)
    Q_PROPERTY(bool globalPositionOk READ globalPositionOk NOTIFY healthChanged)
    Q_PROPERTY(bool homePositionOk READ homePositionOk NOTIFY healthChanged)
    Q_PROPERTY(bool armable READ armable NOTIFY healthChanged)
    Q_PROPERTY(double altitudeMeters READ altitudeMeters NOTIFY altitudeMetersChanged)
    Q_PROPERTY(double speedMetersPerSecond READ speedMetersPerSecond NOTIFY speedMetersPerSecondChanged)
    Q_PROPERTY(double verticalSpeedMetersPerSecond READ verticalSpeedMetersPerSecond NOTIFY velocityChanged)
    Q_PROPERTY(bool localPositionAvailable READ localPositionAvailable NOTIFY localPositionChanged)
    Q_PROPERTY(QVector3D vehicleScenePosition READ vehicleScenePosition NOTIFY vehicleScenePositionChanged)
    Q_PROPERTY(QQuaternion vehicleSceneRotation READ vehicleSceneRotation NOTIFY vehicleSceneRotationChanged)
    Q_PROPERTY(QQuick3DGeometry *flightPathGeometry READ flightPathGeometry CONSTANT)
    Q_PROPERTY(int flightPathPointCount READ flightPathPointCount NOTIFY flightPathPointCountChanged)
    Q_PROPERTY(double flightPathLengthMeters READ flightPathLengthMeters NOTIFY flightPathPointCountChanged)
    Q_PROPERTY(bool flightPathVisible READ flightPathVisible WRITE setFlightPathVisible NOTIFY flightPathDisplaySettingsChanged)
    Q_PROPERTY(double flightPathWidthMeters READ flightPathWidthMeters WRITE setFlightPathWidthMeters NOTIFY flightPathDisplaySettingsChanged)
    Q_PROPERTY(QString flightPathColorPreset READ flightPathColorPreset WRITE setFlightPathColorPreset NOTIFY flightPathDisplaySettingsChanged)
    Q_PROPERTY(int flightPathMaxVisibleLengthMeters READ flightPathMaxVisibleLengthMeters WRITE setFlightPathMaxVisibleLengthMeters NOTIFY flightPathDisplaySettingsChanged)
    Q_PROPERTY(double headingDegrees READ headingDegrees NOTIFY headingChanged)
    Q_PROPERTY(double rollDegrees READ rollDegrees NOTIFY attitudeChanged)
    Q_PROPERTY(double pitchDegrees READ pitchDegrees NOTIFY attitudeChanged)
    Q_PROPERTY(double yawDegrees READ yawDegrees NOTIFY attitudeChanged)
    Q_PROPERTY(double gyroRollRateDegreesPerSecond READ gyroRollRateDegreesPerSecond NOTIFY angularVelocityChanged)
    Q_PROPERTY(double gyroPitchRateDegreesPerSecond READ gyroPitchRateDegreesPerSecond NOTIFY angularVelocityChanged)
    Q_PROPERTY(double gyroYawRateDegreesPerSecond READ gyroYawRateDegreesPerSecond NOTIFY angularVelocityChanged)
    Q_PROPERTY(double batteryRemainingPercent READ batteryRemainingPercent NOTIFY batteryChanged)
    Q_PROPERTY(double batteryVoltageVolts READ batteryVoltageVolts NOTIFY batteryChanged)
    Q_PROPERTY(int gpsSatellites READ gpsSatellites NOTIFY gpsChanged)
    Q_PROPERTY(QString gpsFixType READ gpsFixType NOTIFY gpsChanged)
    Q_PROPERTY(bool telemetryAvailable READ telemetryAvailable NOTIFY telemetryAvailableChanged)
    Q_PROPERTY(bool imuAvailable READ imuAvailable NOTIFY imuAvailableChanged)
    Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY batteryAvailableChanged)
    Q_PROPERTY(bool gpsAvailable READ gpsAvailable NOTIFY gpsAvailableChanged)
    Q_PROPERTY(double takeoffAltitude READ takeoffAltitude WRITE setTakeoffAltitude NOTIFY takeoffAltitudeChanged)
    Q_PROPERTY(QVariantList simulationModels READ simulationModels CONSTANT)
    Q_PROPERTY(QVariantList simulationWorlds READ simulationWorlds CONSTANT)
    Q_PROPERTY(QString selectedModelId READ selectedModelId WRITE setSelectedModelId NOTIFY selectedModelChanged)
    Q_PROPERTY(QString selectedModelName READ selectedModelName NOTIFY selectedModelChanged)
    Q_PROPERTY(QString selectedModelDescription READ selectedModelDescription NOTIFY selectedModelChanged)
    Q_PROPERTY(QString selectedWorldId READ selectedWorldId WRITE setSelectedWorldId NOTIFY selectedWorldChanged)
    Q_PROPERTY(QString selectedWorldName READ selectedWorldName NOTIFY selectedWorldChanged)
    Q_PROPERTY(QString selectedWorldDescription READ selectedWorldDescription NOTIFY selectedWorldChanged)
    Q_PROPERTY(QUrl selectedWorldPreviewDirectory READ selectedWorldPreviewDirectory NOTIFY selectedWorldChanged)
    Q_PROPERTY(QString containerName READ containerName WRITE setContainerName NOTIFY containerNameChanged)
    Q_PROPERTY(QString flightMode READ flightMode NOTIFY flightModeChanged)
    Q_PROPERTY(bool keyboardControlActive READ keyboardControlActive NOTIFY keyboardControlActiveChanged)
    Q_PROPERTY(bool keyboardControlTransitioning READ keyboardControlTransitioning NOTIFY keyboardControlTransitioningChanged)
    Q_PROPERTY(bool landingAndStopping READ landingAndStopping NOTIFY landingAndStoppingChanged)
    Q_PROPERTY(bool preciseTurnActive READ preciseTurnActive NOTIFY preciseTurnChanged)
    Q_PROPERTY(double preciseTurnTargetDegrees READ preciseTurnTargetDegrees NOTIFY preciseTurnChanged)
    Q_PROPERTY(double controlSpeedMetersPerSecond READ controlSpeedMetersPerSecond WRITE setControlSpeedMetersPerSecond NOTIFY controlSpeedChanged)
    Q_PROPERTY(QString controlCommandText READ controlCommandText NOTIFY controlCommandChanged)
    Q_PROPERTY(bool globalPositionAvailable READ globalPositionAvailable NOTIFY globalPositionChanged)
    Q_PROPERTY(bool missionBusy READ missionBusy NOTIFY missionStateChanged)
    Q_PROPERTY(bool missionUploaded READ missionUploaded NOTIFY missionStateChanged)
    Q_PROPERTY(bool missionRunning READ missionRunning NOTIFY missionStateChanged)
    Q_PROPERTY(bool missionPaused READ missionPaused NOTIFY missionStateChanged)
    Q_PROPERTY(int missionCurrentItem READ missionCurrentItem NOTIFY missionProgressChanged)
    Q_PROPERTY(int missionTotalItems READ missionTotalItems NOTIFY missionProgressChanged)
    Q_PROPERTY(QString missionStatusMessage READ missionStatusMessage NOTIFY missionStateChanged)
    Q_PROPERTY(double missionDistanceToNextMeters READ missionDistanceToNextMeters NOTIFY missionMonitorChanged)
    Q_PROPERTY(double missionCrossTrackErrorMeters READ missionCrossTrackErrorMeters NOTIFY missionMonitorChanged)
    Q_PROPERTY(double missionRemainingDistanceMeters READ missionRemainingDistanceMeters NOTIFY missionMonitorChanged)
    Q_PROPERTY(int missionEstimatedSeconds READ missionEstimatedSeconds NOTIFY missionMonitorChanged)
    Q_PROPERTY(QString missionMonitorState READ missionMonitorState NOTIFY missionMonitorChanged)
    Q_PROPERTY(QString missionMonitorMessage READ missionMonitorMessage NOTIFY missionMonitorChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool hasIssue READ hasIssue NOTIFY issueChanged)
    Q_PROPERTY(QString issueCode READ issueCode NOTIFY issueChanged)
    Q_PROPERTY(QString issueMessage READ issueMessage NOTIFY issueChanged)
    Q_PROPERTY(QString recoveryAction READ recoveryAction NOTIFY issueChanged)
    Q_PROPERTY(QString recoveryLabel READ recoveryLabel NOTIFY issueChanged)
    Q_PROPERTY(QStringList diagnosticEntries READ diagnosticEntries NOTIFY diagnosticEntriesChanged)
    Q_PROPERTY(bool flightSessionActive READ flightSessionActive NOTIFY activeFlightSessionChanged)
    Q_PROPERTY(QVariantMap activeFlightSession READ activeFlightSession NOTIFY activeFlightSessionChanged)
    Q_PROPERTY(QVariantList flightSessions READ flightSessions NOTIFY flightSessionsChanged)
    Q_PROPERTY(QString sessionsDirectory READ sessionsDirectory CONSTANT)
    Q_PROPERTY(QVariantList savedRoutes READ savedRoutes NOTIFY savedRoutesChanged)
    Q_PROPERTY(QString routesDirectory READ routesDirectory CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
    Q_PROPERTY(QString qtRuntimeVersion READ qtRuntimeVersion CONSTANT)
    Q_PROPERTY(QString mavsdkVersion READ mavsdkVersion CONSTANT)
    Q_PROPERTY(QString buildDescription READ buildDescription CONSTANT)
    Q_PROPERTY(QString platformDescription READ platformDescription CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    [[nodiscard]] bool dockerAvailable() const { return m_docker.dockerAvailable(); }
    [[nodiscard]] bool imageAvailable() const { return m_docker.imageAvailable(); }
    [[nodiscard]] bool simulationRunning() const { return m_docker.simulationRunning(); }
    [[nodiscard]] bool vehicleConnected() const { return m_vehicle.connected(); }
    [[nodiscard]] bool vehicleReady() const { return m_vehicle.ready(); }
    [[nodiscard]] bool armed() const { return m_vehicle.armed(); }
    [[nodiscard]] bool inAir() const { return m_vehicle.inAir(); }
    [[nodiscard]] bool busy() const { return m_docker.busy() || m_vehicle.busy() || m_vehicle.missionBusy() || m_landingAndStopping; }
    [[nodiscard]] bool gyrometerOk() const { return m_vehicle.gyrometerOk(); }
    [[nodiscard]] bool accelerometerOk() const { return m_vehicle.accelerometerOk(); }
    [[nodiscard]] bool magnetometerOk() const { return m_vehicle.magnetometerOk(); }
    [[nodiscard]] bool localPositionOk() const { return m_vehicle.localPositionOk(); }
    [[nodiscard]] bool globalPositionOk() const { return m_vehicle.globalPositionOk(); }
    [[nodiscard]] bool homePositionOk() const { return m_vehicle.homePositionOk(); }
    [[nodiscard]] bool armable() const { return m_vehicle.armable(); }
    [[nodiscard]] double altitudeMeters() const { return m_vehicle.altitudeMeters(); }
    [[nodiscard]] double speedMetersPerSecond() const { return m_vehicle.speedMetersPerSecond(); }
    [[nodiscard]] double verticalSpeedMetersPerSecond() const { return m_vehicle.verticalSpeedMetersPerSecond(); }
    [[nodiscard]] bool localPositionAvailable() const
    {
        return m_scenePreviewActive || m_vehicle.localPositionAvailable();
    }
    [[nodiscard]] QVector3D vehicleScenePosition() const { return m_vehicleScenePosition; }
    [[nodiscard]] QQuaternion vehicleSceneRotation() const { return m_vehicle.sceneRotation(); }
    [[nodiscard]] QQuick3DGeometry *flightPathGeometry() { return &m_flightPathGeometry; }
    [[nodiscard]] int flightPathPointCount() const { return m_flightPathGeometry.pointCount(); }
    [[nodiscard]] double flightPathLengthMeters() const { return m_flightPathGeometry.lengthMeters(); }
    [[nodiscard]] bool flightPathVisible() const { return m_flightPathVisible; }
    [[nodiscard]] double flightPathWidthMeters() const { return m_flightPathGeometry.widthMeters(); }
    [[nodiscard]] QString flightPathColorPreset() const { return m_flightPathColorPreset; }
    [[nodiscard]] int flightPathMaxVisibleLengthMeters() const
    {
        return m_flightPathGeometry.maxVisibleLengthMeters();
    }
    [[nodiscard]] double headingDegrees() const { return m_vehicle.headingDegrees(); }
    [[nodiscard]] double rollDegrees() const { return m_vehicle.rollDegrees(); }
    [[nodiscard]] double pitchDegrees() const { return m_vehicle.pitchDegrees(); }
    [[nodiscard]] double yawDegrees() const { return m_vehicle.yawDegrees(); }
    [[nodiscard]] double gyroRollRateDegreesPerSecond() const { return m_vehicle.gyroRollRateDegreesPerSecond(); }
    [[nodiscard]] double gyroPitchRateDegreesPerSecond() const { return m_vehicle.gyroPitchRateDegreesPerSecond(); }
    [[nodiscard]] double gyroYawRateDegreesPerSecond() const { return m_vehicle.gyroYawRateDegreesPerSecond(); }
    [[nodiscard]] double batteryRemainingPercent() const { return m_vehicle.batteryRemainingPercent(); }
    [[nodiscard]] double batteryVoltageVolts() const { return m_vehicle.batteryVoltageVolts(); }
    [[nodiscard]] int gpsSatellites() const { return m_vehicle.gpsSatellites(); }
    [[nodiscard]] QString gpsFixType() const { return m_vehicle.gpsFixType(); }
    [[nodiscard]] bool telemetryAvailable() const { return m_vehicle.telemetryAvailable(); }
    [[nodiscard]] bool imuAvailable() const { return m_vehicle.imuAvailable(); }
    [[nodiscard]] bool batteryAvailable() const { return m_vehicle.batteryAvailable(); }
    [[nodiscard]] bool gpsAvailable() const { return m_vehicle.gpsAvailable(); }
    [[nodiscard]] double takeoffAltitude() const { return m_vehicle.takeoffAltitude(); }
    [[nodiscard]] QVariantList simulationModels() const;
    [[nodiscard]] QVariantList simulationWorlds() const;
    [[nodiscard]] QString selectedModelId() const { return m_selectedModelId; }
    [[nodiscard]] QString selectedModelName() const;
    [[nodiscard]] QString selectedModelDescription() const;
    [[nodiscard]] QString selectedWorldId() const { return m_selectedWorldId; }
    [[nodiscard]] QString selectedWorldName() const;
    [[nodiscard]] QString selectedWorldDescription() const;
    [[nodiscard]] QUrl selectedWorldPreviewDirectory() const;
    [[nodiscard]] QString containerName() const { return m_containerName; }
    [[nodiscard]] QString flightMode() const { return m_vehicle.flightMode(); }
    [[nodiscard]] bool keyboardControlActive() const { return m_vehicle.keyboardControlActive(); }
    [[nodiscard]] bool keyboardControlTransitioning() const { return m_vehicle.keyboardControlTransitioning(); }
    [[nodiscard]] bool landingAndStopping() const { return m_landingAndStopping; }
    [[nodiscard]] bool preciseTurnActive() const { return m_vehicle.preciseTurnActive(); }
    [[nodiscard]] double preciseTurnTargetDegrees() const { return m_vehicle.preciseTurnTargetDegrees(); }
    [[nodiscard]] double controlSpeedMetersPerSecond() const
    {
        return m_vehicle.controlSpeedMetersPerSecond();
    }
    [[nodiscard]] QString controlCommandText() const { return m_vehicle.controlCommandText(); }
    [[nodiscard]] bool globalPositionAvailable() const { return m_vehicle.globalPositionAvailable(); }
    [[nodiscard]] bool missionBusy() const { return m_vehicle.missionBusy(); }
    [[nodiscard]] bool missionUploaded() const { return m_vehicle.missionUploaded(); }
    [[nodiscard]] bool missionRunning() const { return m_vehicle.missionRunning(); }
    [[nodiscard]] bool missionPaused() const { return m_vehicle.missionPaused(); }
    [[nodiscard]] int missionCurrentItem() const { return m_vehicle.missionCurrentItem(); }
    [[nodiscard]] int missionTotalItems() const { return m_vehicle.missionTotalItems(); }
    [[nodiscard]] QString missionStatusMessage() const { return m_vehicle.missionStatusMessage(); }
    [[nodiscard]] double missionDistanceToNextMeters() const { return m_missionDistanceToNextMeters; }
    [[nodiscard]] double missionCrossTrackErrorMeters() const { return m_missionCrossTrackErrorMeters; }
    [[nodiscard]] double missionRemainingDistanceMeters() const { return m_missionRemainingDistanceMeters; }
    [[nodiscard]] int missionEstimatedSeconds() const { return m_missionEstimatedSeconds; }
    [[nodiscard]] QString missionMonitorState() const { return m_missionMonitorState; }
    [[nodiscard]] QString missionMonitorMessage() const { return m_missionMonitorMessage; }
    [[nodiscard]] QString statusMessage() const { return m_statusMessage; }
    [[nodiscard]] bool hasIssue() const { return !m_issueCode.isEmpty(); }
    [[nodiscard]] QString issueCode() const { return m_issueCode; }
    [[nodiscard]] QString issueMessage() const { return m_issueMessage; }
    [[nodiscard]] QString recoveryAction() const { return m_recoveryAction; }
    [[nodiscard]] QString recoveryLabel() const { return m_recoveryLabel; }
    [[nodiscard]] QStringList diagnosticEntries() const { return m_diagnosticEntries; }
    [[nodiscard]] bool flightSessionActive() const { return m_sessionRecorder.active(); }
    [[nodiscard]] QVariantMap activeFlightSession() const { return m_sessionRecorder.activeSession(); }
    [[nodiscard]] QVariantList flightSessions() const { return m_sessionRecorder.completedSessions(); }
    [[nodiscard]] QString sessionsDirectory() const { return m_sessionRecorder.sessionsDirectory(); }
    [[nodiscard]] QVariantList savedRoutes() const { return m_routeLibrary.routes(); }
    [[nodiscard]] QString routesDirectory() const { return m_routeLibrary.directory(); }
    [[nodiscard]] QString applicationVersion() const;
    [[nodiscard]] QString qtRuntimeVersion() const;
    [[nodiscard]] QString mavsdkVersion() const;
    [[nodiscard]] QString buildDescription() const;
    [[nodiscard]] QString platformDescription() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startSimulation();
    Q_INVOKABLE void stopSimulation();
    Q_INVOKABLE void landAndStopSimulation();
    Q_INVOKABLE void takeoff();
    Q_INVOKABLE void land();
    Q_INVOKABLE void startKeyboardControl();
    Q_INVOKABLE void stopKeyboardControl();
    Q_INVOKABLE void releaseKeyboardControl();
    Q_INVOKABLE void heartbeatKeyboardControl();
    Q_INVOKABLE void turnKeyboardControl(double degrees);
    Q_INVOKABLE void setKeyboardControlAxes(double forward,
                                            double right,
                                            double up,
                                            double yaw);
    Q_INVOKABLE void uploadAndStartMission(const QVariantList &waypoints,
                                           const QString &endAction,
                                           const QString &routeId = {},
                                           const QString &routeName = {});
    Q_INVOKABLE void pauseMission();
    Q_INVOKABLE void resumeMission();
    Q_INVOKABLE void clearMission();
    Q_INVOKABLE void abortMissionToHold();
    Q_INVOKABLE void returnToLaunch();
    Q_INVOKABLE void clearFlightPath();
    Q_INVOKABLE void resetFlightPathDisplaySettings();
    Q_INVOKABLE QString exportDiagnosticReport();
    Q_INVOKABLE void clearDiagnostics();
    Q_INVOKABLE void recover();
    Q_INVOKABLE QVariantMap sessionDetails(const QString &sessionId) const;
    Q_INVOKABLE QString exportSessionCsv(const QString &sessionId);
    Q_INVOKABLE bool openSessionFolder(const QString &sessionId);
    Q_INVOKABLE bool openSessionsFolder();
    Q_INVOKABLE QVariantMap saveRoute(const QString &routeId,
                                      const QString &name,
                                      const QVariantList &waypoints,
                                      const QString &endAction,
                                      double anchorEastMeters,
                                      double anchorNorthMeters);
    Q_INVOKABLE QVariantMap loadSavedRoute(const QString &routeId,
                                           double targetEastMeters,
                                           double targetNorthMeters) const;
    Q_INVOKABLE QVariantMap duplicateSavedRoute(const QString &routeId);
    Q_INVOKABLE QVariantMap renameSavedRoute(const QString &routeId,
                                             const QString &name);
    Q_INVOKABLE QVariantMap deleteSavedRoute(const QString &routeId);
    Q_INVOKABLE bool openRoutesFolder();
    void setTakeoffAltitude(double value);
    void setControlSpeedMetersPerSecond(double value);
    void setSelectedModelId(const QString &modelId);
    void setSelectedWorldId(const QString &worldId);
    void setContainerName(const QString &name);
    void setFlightPathVisible(bool visible);
    void setFlightPathWidthMeters(double value);
    void setFlightPathColorPreset(const QString &preset);
    void setFlightPathMaxVisibleLengthMeters(int value);
    void prepareFlightScenePreview();

signals:
    void dockerAvailableChanged();
    void imageAvailableChanged();
    void simulationRunningChanged();
    void vehicleConnectedChanged();
    void vehicleReadyChanged();
    void armedChanged();
    void inAirChanged();
    void busyChanged();
    void healthChanged();
    void altitudeMetersChanged();
    void speedMetersPerSecondChanged();
    void velocityChanged();
    void localPositionChanged();
    void vehicleScenePositionChanged();
    void vehicleSceneRotationChanged();
    void flightPathPointCountChanged();
    void flightPathDisplaySettingsChanged();
    void headingChanged();
    void attitudeChanged();
    void angularVelocityChanged();
    void batteryChanged();
    void gpsChanged();
    void telemetryAvailableChanged();
    void imuAvailableChanged();
    void batteryAvailableChanged();
    void gpsAvailableChanged();
    void takeoffAltitudeChanged();
    void selectedModelChanged();
    void selectedWorldChanged();
    void containerNameChanged();
    void flightModeChanged();
    void keyboardControlActiveChanged();
    void keyboardControlTransitioningChanged();
    void landingAndStoppingChanged();
    void preciseTurnChanged();
    void controlSpeedChanged();
    void controlCommandChanged();
    void globalPositionChanged();
    void missionStateChanged();
    void missionProgressChanged();
    void missionMonitorChanged();
    void statusMessageChanged();
    void diagnosticEntriesChanged();
    void issueChanged();
    void activeFlightSessionChanged();
    void flightSessionsChanged();
    void savedRoutesChanged();

private:
    void useStatus(const QString &message);
    void appendDiagnostic(const QString &source, const QString &message);
    void updateIssueState();
    void tryCompleteLandingAndStop();
    void setLandingAndStopping(bool value);
    void resetFlightScene();
    void updateFlightScenePosition();
    void updateMissionMonitor();
    void resetMissionMonitor();
    void triggerMissionProtection(const QString &reason);

    DockerController m_docker;
    VehicleConnection m_vehicle;
    FlightPathGeometry m_flightPathGeometry;
    FlightSessionRecorder m_sessionRecorder;
    RouteLibrary m_routeLibrary;
    QTimer m_sessionSampleTimer;
    QTimer m_connectionTimeout;
    QTimer m_healthTimeout;
    QTimer m_missionMonitorTimer;
    bool m_connectionTimedOut{false};
    bool m_healthTimedOut{false};
    bool m_commandIssue{false};
    bool m_restartRequested{false};
    bool m_landingAndStopping{false};
    bool m_scenePreviewActive{false};
    bool m_sceneOriginAvailable{false};
    double m_sceneOriginNorthMeters{0.0};
    double m_sceneOriginEastMeters{0.0};
    double m_sceneOriginDownMeters{0.0};
    QVector3D m_vehicleScenePosition;
    QVector3D m_missionStartScenePosition;
    QVector<QVector3D> m_missionSceneWaypoints;
    QVector<double> m_missionWaypointSpeeds;
    QString m_missionEndAction;
    double m_missionDistanceToNextMeters{0.0};
    double m_missionCrossTrackErrorMeters{0.0};
    double m_missionRemainingDistanceMeters{0.0};
    double m_missionMaximumCrossTrackErrorMeters{0.0};
    double m_missionBestDistanceToNextMeters{0.0};
    int m_missionEstimatedSeconds{0};
    int m_missionDeviationDurationMs{0};
    int m_missionTelemetryMissingDurationMs{0};
    int m_missionUnexpectedModeDurationMs{0};
    int m_missionStalledDurationMs{0};
    int m_missionLastProgressItem{-1};
    bool m_missionProtectionTriggered{false};
    QString m_missionMonitorState{QStringLiteral("idle")};
    QString m_missionMonitorMessage;
    QString m_commandIssueMessage;
    QString m_issueCode;
    QString m_issueMessage;
    QString m_recoveryAction;
    QString m_recoveryLabel;
    QString m_statusMessage{QStringLiteral("FlightBench 正在进行环境检查…")};
    QStringList m_diagnosticEntries;
    QString m_selectedModelId{QStringLiteral("gz_x500")};
    QString m_selectedWorldId{QStringLiteral("default")};
    QString m_containerName{QStringLiteral("flightbench-px4")};
    bool m_flightPathVisible{true};
    QString m_flightPathColorPreset{QStringLiteral("orange")};
    QString m_lastRecordedIssueCode;
};
