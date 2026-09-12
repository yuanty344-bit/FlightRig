#pragma once
#include "app/simulationruncoordinator.h"

#include "infrastructure/applicationpreferences.h"
#include "infrastructure/dockercontroller.h"
#include "mapping/depthmappingcontroller.h"
#include "mapping/flightpathgeometry.h"
#include "sessions/flightsessionrecorder.h"
#include "mapping/lidarmappingcontroller.h"
#include "mission/routelibrary.h"
#include "sessions/replaycontroller.h"
#include "sensors/sensorservicecontroller.h"
#include "testing/testplanrepository.h"
#include "flight/vehicleconnection.h"
#include "inspection/visualsurveycontroller.h"
#include "inspection/gimbalinspectioncontroller.h"

#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QVector3D>

class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dockerAvailable READ dockerAvailable NOTIFY dockerAvailableChanged)
    Q_PROPERTY(bool imageAvailable READ imageAvailable NOTIFY imageAvailableChanged)
    Q_PROPERTY(bool dockerNetworkAvailable READ dockerNetworkAvailable NOTIFY dockerNetworkAvailableChanged)
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
    Q_PROPERTY(bool flightPathAutoClear READ flightPathAutoClear WRITE setFlightPathAutoClear NOTIFY flightPathDisplaySettingsChanged)
    Q_PROPERTY(double cameraRotationSensitivity READ cameraRotationSensitivity WRITE setCameraRotationSensitivity NOTIFY viewPreferencesChanged)
    Q_PROPERTY(double cameraZoomSensitivity READ cameraZoomSensitivity WRITE setCameraZoomSensitivity NOTIFY viewPreferencesChanged)
    Q_PROPERTY(bool invertCameraVertical READ invertCameraVertical WRITE setInvertCameraVertical NOTIFY viewPreferencesChanged)
    Q_PROPERTY(bool groundMarkerVisible READ groundMarkerVisible WRITE setGroundMarkerVisible NOTIFY viewPreferencesChanged)
    Q_PROPERTY(QString groundMarkerSizeMode READ groundMarkerSizeMode WRITE setGroundMarkerSizeMode NOTIFY viewPreferencesChanged)
    Q_PROPERTY(QString defaultCameraMode READ defaultCameraMode WRITE setDefaultCameraMode NOTIFY viewPreferencesChanged)
    Q_PROPERTY(double cityBuildingOpacity READ cityBuildingOpacity WRITE setCityBuildingOpacity NOTIFY viewPreferencesChanged)
    Q_PROPERTY(bool worldRoadsVisible READ worldRoadsVisible WRITE setWorldRoadsVisible NOTIFY viewPreferencesChanged)
    Q_PROPERTY(bool worldGreenAreasVisible READ worldGreenAreasVisible WRITE setWorldGreenAreasVisible NOTIFY viewPreferencesChanged)
    Q_PROPERTY(bool worldWaterVisible READ worldWaterVisible WRITE setWorldWaterVisible NOTIFY viewPreferencesChanged)
    Q_PROPERTY(bool defaultGridVisible READ defaultGridVisible WRITE setDefaultGridVisible NOTIFY viewPreferencesChanged)
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
    Q_PROPERTY(QString selectedSensorType READ selectedSensorType NOTIFY selectedModelChanged)
    Q_PROPERTY(QString selectedSensorLabel READ selectedSensorLabel NOTIFY selectedModelChanged)
    Q_PROPERTY(bool sensorServiceSupported READ sensorServiceSupported NOTIFY selectedModelChanged)
    Q_PROPERTY(bool sensorServiceEnabled READ sensorServiceEnabled WRITE setSensorServiceEnabled NOTIFY sensorServicePreferenceChanged)
    Q_PROPERTY(bool sensorServiceImageAvailable READ sensorServiceImageAvailable NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(bool sensorServiceRunning READ sensorServiceRunning NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(bool sensorServiceConnected READ sensorServiceConnected NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(bool sensorServiceBusy READ sensorServiceBusy NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(QString sensorServiceStatus READ sensorServiceStatus NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(QString sensorServiceProblemCode READ sensorServiceProblemCode NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(QString sensorPerformanceMode READ sensorPerformanceMode NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(QString sensorEffectivePerformanceMode READ sensorEffectivePerformanceMode NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(int sensorTargetFramesPerSecond READ sensorTargetFramesPerSecond NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(bool sensorSessionRecording READ sensorSessionRecording NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(QVariantList lidarScanPoints READ lidarScanPoints NOTIFY lidarScanChanged)
    Q_PROPERTY(double lidarNearestDistanceMeters READ lidarNearestDistanceMeters NOTIFY lidarScanChanged)
    Q_PROPERTY(double lidarNearestAngleDegrees READ lidarNearestAngleDegrees NOTIFY lidarScanChanged)
    Q_PROPERTY(double lidarScanHz READ lidarScanHz NOTIFY lidarScanChanged)
    Q_PROPERTY(double lidarFieldOfViewDegrees READ lidarFieldOfViewDegrees NOTIFY lidarScanChanged)
    Q_PROPERTY(LidarMappingController *lidarMapping READ lidarMapping CONSTANT)
    Q_PROPERTY(DepthMappingController *depthMapping READ depthMapping CONSTANT)
    Q_PROPERTY(VisualSurveyController *visualSurvey READ visualSurvey CONSTANT)
    Q_PROPERTY(GimbalInspectionController *gimbalInspection READ gimbalInspection CONSTANT)
    Q_PROPERTY(QString sensorServiceKind READ sensorServiceKind NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(QString displayedSensorKind READ displayedSensorKind NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(bool replaySensorAvailable READ replaySensorAvailable NOTIFY sensorServiceStateChanged)
    Q_PROPERTY(QString cameraRgbFrame READ cameraRgbFrame NOTIFY cameraFrameChanged)
    Q_PROPERTY(QString cameraDepthFrame READ cameraDepthFrame NOTIFY depthFrameChanged)
    Q_PROPERTY(bool cameraRgbReady READ cameraRgbReady NOTIFY cameraFrameChanged)
    Q_PROPERTY(bool cameraDepthReady READ cameraDepthReady NOTIFY depthFrameChanged)
    Q_PROPERTY(double cameraRgbFrameHz READ cameraRgbFrameHz NOTIFY cameraFrameChanged)
    Q_PROPERTY(double cameraDepthFrameHz READ cameraDepthFrameHz NOTIFY depthFrameChanged)
    Q_PROPERTY(int cameraRgbLatencyMilliseconds READ cameraRgbLatencyMilliseconds NOTIFY cameraFrameChanged)
    Q_PROPERTY(int cameraDepthLatencyMilliseconds READ cameraDepthLatencyMilliseconds NOTIFY depthFrameChanged)
    Q_PROPERTY(bool cameraRgbStreamInterrupted READ cameraRgbStreamInterrupted NOTIFY cameraStateChanged)
    Q_PROPERTY(bool cameraDepthStreamInterrupted READ cameraDepthStreamInterrupted NOTIFY cameraStateChanged)
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
    Q_PROPERTY(bool cameraRecording READ cameraRecording NOTIFY cameraRecordingChanged)
    Q_PROPERTY(QString sensorCapturesDirectory READ sensorCapturesDirectory CONSTANT)
    Q_PROPERTY(QString lastSensorCapturePath READ lastSensorCapturePath NOTIFY sensorCaptureSaved)
    Q_PROPERTY(QString selectedWorldId READ selectedWorldId WRITE setSelectedWorldId NOTIFY selectedWorldChanged)
    Q_PROPERTY(QString selectedWorldName READ selectedWorldName NOTIFY selectedWorldChanged)
    Q_PROPERTY(QString selectedWorldDescription READ selectedWorldDescription NOTIFY selectedWorldChanged)
    Q_PROPERTY(QUrl selectedWorldPreviewDirectory READ selectedWorldPreviewDirectory NOTIFY selectedWorldChanged)
    Q_PROPERTY(QVariantMap selectedWorldMap READ selectedWorldMap NOTIFY selectedWorldChanged)
    Q_PROPERTY(QString containerName READ containerName WRITE setContainerName NOTIFY containerNameChanged)
    Q_PROPERTY(bool containerNameAvailable READ containerNameAvailable NOTIFY containerNameAvailabilityChanged)
    Q_PROPERTY(QString flightMode READ flightMode NOTIFY flightModeChanged)
    Q_PROPERTY(bool keyboardControlActive READ keyboardControlActive NOTIFY keyboardControlActiveChanged)
    Q_PROPERTY(bool keyboardControlTransitioning READ keyboardControlTransitioning NOTIFY keyboardControlTransitioningChanged)
    Q_PROPERTY(bool landingAndStopping READ landingAndStopping NOTIFY landingAndStoppingChanged)
    Q_PROPERTY(bool preciseTurnActive READ preciseTurnActive NOTIFY preciseTurnChanged)
    Q_PROPERTY(double preciseTurnTargetDegrees READ preciseTurnTargetDegrees NOTIFY preciseTurnChanged)
    Q_PROPERTY(double controlSpeedMetersPerSecond READ controlSpeedMetersPerSecond WRITE setControlSpeedMetersPerSecond NOTIFY controlSpeedChanged)
    Q_PROPERTY(double controlVerticalSpeedMetersPerSecond READ controlVerticalSpeedMetersPerSecond WRITE setControlVerticalSpeedMetersPerSecond NOTIFY controlSpeedChanged)
    Q_PROPERTY(double controlYawRateDegreesPerSecond READ controlYawRateDegreesPerSecond WRITE setControlYawRateDegreesPerSecond NOTIFY controlSpeedChanged)
    Q_PROPERTY(double preciseTurnDefaultDegrees READ preciseTurnDefaultDegrees WRITE setPreciseTurnDefaultDegrees NOTIFY interactionPreferencesChanged)
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
    Q_PROPERTY(QVariantMap currentIssueExplanation READ currentIssueExplanation NOTIFY issueChanged)
    Q_PROPERTY(QStringList diagnosticEntries READ diagnosticEntries NOTIFY diagnosticEntriesChanged)
    Q_PROPERTY(bool flightSessionActive READ flightSessionActive NOTIFY activeFlightSessionChanged)
    Q_PROPERTY(QVariantMap activeFlightSession READ activeFlightSession NOTIFY activeFlightSessionChanged)
    Q_PROPERTY(QVariantList flightSessions READ flightSessions NOTIFY flightSessionsChanged)
    Q_PROPERTY(QVariantList trashedFlightSessions READ trashedFlightSessions NOTIFY trashedFlightSessionsChanged)
    Q_PROPERTY(int trashSessionCount READ trashSessionCount NOTIFY trashedFlightSessionsChanged)
    Q_PROPERTY(QString sessionsDirectory READ sessionsDirectory CONSTANT)
    Q_PROPERTY(int telemetryRecordingHz READ telemetryRecordingHz WRITE setTelemetryRecordingHz NOTIFY recordingPreferencesChanged)
    Q_PROPERTY(bool autoGenerateReports READ autoGenerateReports WRITE setAutoGenerateReports NOTIFY recordingPreferencesChanged)
    Q_PROPERTY(int trashRetentionDays READ trashRetentionDays WRITE setTrashRetentionDays NOTIFY recordingPreferencesChanged)
    Q_PROPERTY(qint64 sessionStorageBytes READ sessionStorageBytes NOTIFY sessionStorageChanged)
    Q_PROPERTY(QString sessionStorageSizeLabel READ sessionStorageSizeLabel NOTIFY sessionStorageChanged)
    Q_PROPERTY(QVariantList savedRoutes READ savedRoutes NOTIFY savedRoutesChanged)
    Q_PROPERTY(QString routesDirectory READ routesDirectory CONSTANT)
    Q_PROPERTY(QVariantList testPlans READ testPlans NOTIFY testPlansChanged)
    Q_PROPERTY(QString selectedTestPlanId READ selectedTestPlanId WRITE setSelectedTestPlanId NOTIFY selectedTestPlanChanged)
    Q_PROPERTY(QVariantMap selectedTestPlan READ selectedTestPlan NOTIFY selectedTestPlanChanged)
    Q_PROPERTY(bool selectedTestPlanModified READ selectedTestPlanModified NOTIFY selectedTestPlanChanged)
    Q_PROPERTY(QVariantList testPlanSteps READ testPlanSteps NOTIFY testRunStateChanged)
    Q_PROPERTY(QString testRunStage READ testRunStage NOTIFY testRunStateChanged)
    Q_PROPERTY(QString testRunStageLabel READ testRunStageLabel NOTIFY testRunStateChanged)
    Q_PROPERTY(QString testRunGuidance READ testRunGuidance NOTIFY testRunStateChanged)
    Q_PROPERTY(QVariantMap preflightSummary READ preflightSummary NOTIFY testRunStateChanged)
    Q_PROPERTY(QString plansDirectory READ plansDirectory CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
    Q_PROPERTY(QString qtRuntimeVersion READ qtRuntimeVersion CONSTANT)
    Q_PROPERTY(QString mavsdkVersion READ mavsdkVersion CONSTANT)
    Q_PROPERTY(QString buildDescription READ buildDescription CONSTANT)
    Q_PROPERTY(QString platformDescription READ platformDescription CONSTANT)
    Q_PROPERTY(bool replayActive READ replayActive NOTIFY replayStateChanged)
    Q_PROPERTY(bool replayPlaying READ replayPlaying NOTIFY replayPlayingChanged)
    Q_PROPERTY(qint64 replayTimeMs READ replayTimeMs NOTIFY replayTimeChanged)
    Q_PROPERTY(qint64 replayDurationMs READ replayDurationMs NOTIFY replayStateChanged)
    Q_PROPERTY(double replaySpeedMultiplier READ replaySpeedMultiplier NOTIFY replayDataChanged)
    Q_PROPERTY(QVector3D replayPosition READ replayPosition NOTIFY replayPositionChanged)
    Q_PROPERTY(QQuaternion replayRotation READ replayRotation NOTIFY replayPositionChanged)
    Q_PROPERTY(double replayAltitudeMeters READ replayAltitudeMeters NOTIFY replayPositionChanged)
    Q_PROPERTY(double replaySpeedMetersPerSecond READ replaySpeedMetersPerSecond NOTIFY replayPositionChanged)
    Q_PROPERTY(double replayVerticalSpeedMetersPerSecond READ replayVerticalSpeedMetersPerSecond NOTIFY replayPositionChanged)
    Q_PROPERTY(double replayHeadingDegrees READ replayHeadingDegrees NOTIFY replayPositionChanged)
    Q_PROPERTY(QQuick3DGeometry *replayPathGeometry READ replayPathGeometry CONSTANT)
    Q_PROPERTY(int replayPathPointCount READ replayPathPointCount NOTIFY replayTimeChanged)
    Q_PROPERTY(double replayPathLengthMeters READ replayPathLengthMeters NOTIFY replayTimeChanged)
    Q_PROPERTY(QQuick3DGeometry *replayPlannedPathGeometry READ replayPlannedPathGeometry CONSTANT)
    Q_PROPERTY(int replayPlannedPathPointCount READ replayPlannedPathPointCount NOTIFY replayStateChanged)
    Q_PROPERTY(QString replaySessionId READ replaySessionId NOTIFY replayStateChanged)
    Q_PROPERTY(QString replaySessionName READ replaySessionName NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayModelId READ replayModelId NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayModelName READ replayModelName NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayWorldId READ replayWorldId NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayWorldName READ replayWorldName NOTIFY replayStateChanged)
    Q_PROPERTY(QUrl replayWorldPreviewDirectory READ replayWorldPreviewDirectory NOTIFY replayStateChanged)
    Q_PROPERTY(QVariantList replayEvents READ replayEvents NOTIFY replayDataChanged)
    Q_PROPERTY(int replayCurrentEventIndex READ replayCurrentEventIndex NOTIFY replayTimeChanged)
    Q_PROPERTY(QString replayCurrentEventLabel READ replayCurrentEventLabel NOTIFY replayTimeChanged)
    Q_PROPERTY(QVariantList replayTelemetrySeries READ replayTelemetrySeries NOTIFY replayDataChanged)
    Q_PROPERTY(QVariantMap replayRouteAnalysis READ replayRouteAnalysis NOTIFY replayStateChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    [[nodiscard]] bool dockerAvailable() const { return m_docker.dockerAvailable(); }
    [[nodiscard]] bool imageAvailable() const { return m_docker.imageAvailable(); }
    [[nodiscard]] bool dockerNetworkAvailable() const { return m_docker.networkAvailable(); }
    [[nodiscard]] bool simulationRunning() const { return m_docker.simulationRunning(); }
    [[nodiscard]] bool vehicleConnected() const { return m_vehicle.connected(); }
    [[nodiscard]] bool vehicleReady() const { return m_vehicle.ready(); }
    [[nodiscard]] bool armed() const { return m_vehicle.armed(); }
    [[nodiscard]] bool inAir() const { return m_vehicle.inAir(); }
    [[nodiscard]] bool busy() const { return m_docker.busy() || m_sensorService.busy() || m_vehicle.busy() || m_vehicle.missionBusy() || m_runCoordinator.landingAndStopping(); }
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
    [[nodiscard]] bool flightPathVisible() const { return m_preferences.flightPathVisible(); }
    [[nodiscard]] double flightPathWidthMeters() const { return m_flightPathGeometry.widthMeters(); }
    [[nodiscard]] QString flightPathColorPreset() const { return m_preferences.flightPathColorPreset(); }
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
    [[nodiscard]] QString selectedSensorType() const;
    [[nodiscard]] QString selectedSensorLabel() const;
    [[nodiscard]] bool sensorServiceSupported() const;
    [[nodiscard]] bool sensorServiceEnabled() const { return m_sensorServiceEnabled; }
    [[nodiscard]] bool sensorServiceImageAvailable() const
    {
        return m_sensorService.imageAvailable();
    }
    [[nodiscard]] bool sensorServiceRunning() const
    {
        return replayActive() ? m_replay.sensorAvailable() : m_sensorService.running();
    }
    [[nodiscard]] bool sensorServiceConnected() const
    {
        return replayActive() ? m_replay.sensorAvailable() : m_sensorService.connected();
    }
    [[nodiscard]] bool sensorServiceBusy() const { return m_sensorService.busy(); }
    [[nodiscard]] QString sensorServiceStatus() const
    {
        return replayActive() ? QStringLiteral("正在显示飞行记录中的传感器数据")
                              : m_sensorService.statusMessage();
    }
    [[nodiscard]] QString sensorServiceProblemCode() const { return m_sensorService.problemCode(); }
    [[nodiscard]] QVariantList lidarScanPoints() const
    {
        return replayActive() ? m_replay.lidarPoints() : m_sensorService.lidarPoints();
    }
    [[nodiscard]] double lidarNearestDistanceMeters() const
    {
        return replayActive() ? m_replay.lidarNearestDistanceMeters()
                              : m_sensorService.lidarNearestDistanceMeters();
    }
    [[nodiscard]] double lidarNearestAngleDegrees() const
    {
        return replayActive() ? m_replay.lidarNearestAngleDegrees()
                              : m_sensorService.lidarNearestAngleDegrees();
    }
    [[nodiscard]] double lidarScanHz() const
    {
        return replayActive() ? m_replay.lidarScanHz() : m_sensorService.lidarScanHz();
    }
    [[nodiscard]] double lidarFieldOfViewDegrees() const
    {
        return replayActive() ? m_replay.lidarFieldOfViewDegrees()
                              : m_sensorService.lidarFieldOfViewDegrees();
    }
    [[nodiscard]] LidarMappingController *lidarMapping() { return &m_lidarMapping; }
    [[nodiscard]] DepthMappingController *depthMapping() { return &m_depthMapping; }
    [[nodiscard]] VisualSurveyController *visualSurvey() { return &m_visualSurvey; }
    [[nodiscard]] GimbalInspectionController *gimbalInspection() { return &m_gimbalInspection; }
    [[nodiscard]] QString sensorServiceKind() const { return displayedSensorKind(); }
    [[nodiscard]] QString sensorPerformanceMode() const
    {
        return m_sensorService.performanceMode();
    }
    [[nodiscard]] QString sensorEffectivePerformanceMode() const
    {
        return m_sensorService.effectivePerformanceMode();
    }
    [[nodiscard]] int sensorTargetFramesPerSecond() const
    {
        return m_sensorService.targetFramesPerSecond();
    }
    [[nodiscard]] bool sensorSessionRecording() const
    {
        return m_sensorService.sessionRecording();
    }
    [[nodiscard]] QString displayedSensorKind() const
    {
        return replayActive() ? m_replay.sensorKind() : selectedSensorType();
    }
    [[nodiscard]] bool replaySensorAvailable() const { return m_replay.sensorAvailable(); }
    [[nodiscard]] QString cameraRgbFrame() const
    {
        return replayActive() ? m_replay.rgbFrameDataUrl() : m_sensorService.rgbFrameDataUrl();
    }
    [[nodiscard]] QString cameraDepthFrame() const
    {
        return replayActive() ? m_replay.depthFrameDataUrl() : m_sensorService.depthFrameDataUrl();
    }
    [[nodiscard]] bool cameraRgbReady() const
    {
        return replayActive() ? !m_replay.rgbFrameDataUrl().isEmpty() : m_sensorService.rgbFrameReady();
    }
    [[nodiscard]] bool cameraDepthReady() const
    {
        return replayActive() ? !m_replay.depthFrameDataUrl().isEmpty() : m_sensorService.depthFrameReady();
    }
    [[nodiscard]] double cameraRgbFrameHz() const
    {
        return replayActive() ? m_replay.rgbFrameHz() : m_sensorService.rgbFrameHz();
    }
    [[nodiscard]] double cameraDepthFrameHz() const
    {
        return replayActive() ? m_replay.depthFrameHz() : m_sensorService.depthFrameHz();
    }
    [[nodiscard]] int cameraRgbLatencyMilliseconds() const
    {
        return replayActive() ? m_replay.rgbLatencyMilliseconds()
                              : m_sensorService.rgbLatencyMilliseconds();
    }
    [[nodiscard]] int cameraDepthLatencyMilliseconds() const
    {
        return replayActive() ? m_replay.depthLatencyMilliseconds()
                              : m_sensorService.depthLatencyMilliseconds();
    }
    [[nodiscard]] bool cameraRgbStreamInterrupted() const
    {
        return replayActive() ? m_replay.rgbStreamInterrupted()
                              : m_sensorService.rgbStreamInterrupted();
    }
    [[nodiscard]] bool cameraDepthStreamInterrupted() const
    {
        return replayActive() ? m_replay.depthStreamInterrupted()
                              : m_sensorService.depthStreamInterrupted();
    }
    [[nodiscard]] double depthCenterDistanceMeters() const
    {
        return replayActive() ? m_replay.depthCenterDistanceMeters()
                              : m_sensorService.depthCenterDistanceMeters();
    }
    [[nodiscard]] double depthNearestDistanceMeters() const
    {
        return replayActive() ? m_replay.depthNearestDistanceMeters()
                              : m_sensorService.depthNearestDistanceMeters();
    }
    [[nodiscard]] double depthValidRatio() const
    {
        return replayActive() ? m_replay.depthValidRatio() : m_sensorService.depthValidRatio();
    }
    [[nodiscard]] double gimbalYawDegrees() const
    {
        return replayActive() ? m_replay.gimbalYawDegrees() : m_sensorService.gimbalYawDegrees();
    }
    [[nodiscard]] double gimbalPitchDegrees() const
    {
        return replayActive() ? m_replay.gimbalPitchDegrees() : m_sensorService.gimbalPitchDegrees();
    }
    [[nodiscard]] double gimbalRollDegrees() const
    {
        return replayActive() ? m_replay.gimbalRollDegrees() : m_sensorService.gimbalRollDegrees();
    }
    [[nodiscard]] double gimbalTargetYawDegrees() const
    {
        return replayActive() ? m_replay.gimbalTargetYawDegrees()
                              : m_sensorService.gimbalTargetYawDegrees();
    }
    [[nodiscard]] double gimbalTargetPitchDegrees() const
    {
        return replayActive() ? m_replay.gimbalTargetPitchDegrees()
                              : m_sensorService.gimbalTargetPitchDegrees();
    }
    [[nodiscard]] bool gimbalFeedbackAvailable() const
    {
        return replayActive() ? m_replay.gimbalFeedbackAvailable()
                              : m_sensorService.gimbalFeedbackAvailable();
    }
    [[nodiscard]] bool gimbalMoving() const
    {
        return replayActive() ? m_replay.gimbalMoving() : m_sensorService.gimbalMoving();
    }
    [[nodiscard]] bool cameraRecording() const { return m_sensorService.recording(); }
    [[nodiscard]] QString sensorCapturesDirectory() const { return m_sensorService.capturesDirectory(); }
    [[nodiscard]] QString lastSensorCapturePath() const
    {
        return m_lastReplayCapturePath.isEmpty() ? m_sensorService.lastCapturePath()
                                                 : m_lastReplayCapturePath;
    }
    [[nodiscard]] QString selectedWorldId() const { return m_selectedWorldId; }
    [[nodiscard]] QString selectedWorldName() const;
    [[nodiscard]] QString selectedWorldDescription() const;
    [[nodiscard]] QUrl selectedWorldPreviewDirectory() const;
    [[nodiscard]] QVariantMap selectedWorldMap() const;
    [[nodiscard]] QString containerName() const { return m_containerName; }
    [[nodiscard]] bool containerNameAvailable() const { return m_docker.containerNameAvailable(); }
    [[nodiscard]] QString flightMode() const { return m_vehicle.flightMode(); }
    [[nodiscard]] bool keyboardControlActive() const { return m_vehicle.keyboardControlActive(); }
    [[nodiscard]] bool keyboardControlTransitioning() const { return m_vehicle.keyboardControlTransitioning(); }
    [[nodiscard]] bool landingAndStopping() const { return m_runCoordinator.landingAndStopping(); }
    [[nodiscard]] bool preciseTurnActive() const { return m_vehicle.preciseTurnActive(); }
    [[nodiscard]] double preciseTurnTargetDegrees() const { return m_vehicle.preciseTurnTargetDegrees(); }
    [[nodiscard]] double controlSpeedMetersPerSecond() const
    {
        return m_vehicle.controlSpeedMetersPerSecond();
    }
    [[nodiscard]] QString controlCommandText() const { return m_vehicle.controlCommandText(); }
    [[nodiscard]] double controlVerticalSpeedMetersPerSecond() const
    {
        return m_vehicle.controlVerticalSpeedMetersPerSecond();
    }
    [[nodiscard]] bool flightPathAutoClear() const { return m_preferences.flightPathAutoClear(); }
    [[nodiscard]] double cameraRotationSensitivity() const { return m_preferences.cameraRotationSensitivity(); }
    [[nodiscard]] double cameraZoomSensitivity() const { return m_preferences.cameraZoomSensitivity(); }
    [[nodiscard]] bool invertCameraVertical() const { return m_preferences.invertCameraVertical(); }
    [[nodiscard]] bool groundMarkerVisible() const { return m_preferences.groundMarkerVisible(); }
    [[nodiscard]] QString groundMarkerSizeMode() const { return m_preferences.groundMarkerSizeMode(); }
    [[nodiscard]] QString defaultCameraMode() const { return m_preferences.defaultCameraMode(); }
    [[nodiscard]] double cityBuildingOpacity() const { return m_preferences.cityBuildingOpacity(); }
    [[nodiscard]] bool worldRoadsVisible() const { return m_preferences.worldRoadsVisible(); }
    [[nodiscard]] bool worldGreenAreasVisible() const { return m_preferences.worldGreenAreasVisible(); }
    [[nodiscard]] bool worldWaterVisible() const { return m_preferences.worldWaterVisible(); }
    [[nodiscard]] bool defaultGridVisible() const { return m_preferences.defaultGridVisible(); }
    [[nodiscard]] double controlYawRateDegreesPerSecond() const
    {
        return m_vehicle.controlYawRateDegreesPerSecond();
    }
    [[nodiscard]] double preciseTurnDefaultDegrees() const { return m_preferences.preciseTurnDefaultDegrees(); }
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
    [[nodiscard]] QVariantMap currentIssueExplanation() const;
    [[nodiscard]] QStringList diagnosticEntries() const { return m_diagnosticEntries; }
    [[nodiscard]] bool flightSessionActive() const { return m_sessionRecorder.active(); }
    [[nodiscard]] QVariantMap activeFlightSession() const { return m_sessionRecorder.activeSession(); }
    [[nodiscard]] QVariantList flightSessions() const { return m_sessionRecorder.completedSessions(); }
    [[nodiscard]] QVariantList trashedFlightSessions() const { return m_sessionRecorder.trashedSessions(); }
    [[nodiscard]] int trashSessionCount() const { return m_sessionRecorder.trashedSessions().size(); }
    [[nodiscard]] QString sessionsDirectory() const { return m_sessionRecorder.sessionsDirectory(); }
    [[nodiscard]] int telemetryRecordingHz() const { return m_preferences.telemetryRecordingHz(); }
    [[nodiscard]] bool autoGenerateReports() const { return m_preferences.autoGenerateReports(); }
    [[nodiscard]] int trashRetentionDays() const { return m_preferences.trashRetentionDays(); }
    [[nodiscard]] qint64 sessionStorageBytes() const { return m_sessionStorageBytes; }
    [[nodiscard]] QString sessionStorageSizeLabel() const;
    [[nodiscard]] QVariantList savedRoutes() const { return m_routeLibrary.routes(); }
    [[nodiscard]] QString routesDirectory() const { return m_routeLibrary.directory(); }
    [[nodiscard]] QVariantList testPlans() const;
    [[nodiscard]] QString selectedTestPlanId() const { return m_selectedTestPlanId; }
    [[nodiscard]] QVariantMap selectedTestPlan() const;
    [[nodiscard]] bool selectedTestPlanModified() const;
    [[nodiscard]] QVariantList testPlanSteps() const;
    [[nodiscard]] QString testRunStage() const;
    [[nodiscard]] QString testRunStageLabel() const;
    [[nodiscard]] QString testRunGuidance() const;
    [[nodiscard]] QVariantMap preflightSummary() const;
    [[nodiscard]] QString plansDirectory() const { return m_testPlanRepository.directory(); }
    [[nodiscard]] QString applicationVersion() const;
    [[nodiscard]] QString qtRuntimeVersion() const;
    [[nodiscard]] QString mavsdkVersion() const;
    [[nodiscard]] QString buildDescription() const;
    [[nodiscard]] QString platformDescription() const;
    [[nodiscard]] bool replayActive() const { return m_replay.active(); }
    [[nodiscard]] bool replayPlaying() const { return m_replay.playing(); }
    [[nodiscard]] qint64 replayTimeMs() const { return m_replay.timeMs(); }
    [[nodiscard]] qint64 replayDurationMs() const { return m_replay.durationMs(); }
    [[nodiscard]] double replaySpeedMultiplier() const { return m_replay.speedMultiplier(); }
    [[nodiscard]] QVector3D replayPosition() const { return m_replay.position(); }
    [[nodiscard]] QQuaternion replayRotation() const { return m_replay.rotation(); }
    [[nodiscard]] double replayAltitudeMeters() const { return m_replay.altitudeMeters(); }
    [[nodiscard]] double replaySpeedMetersPerSecond() const { return m_replay.horizontalSpeedMetersPerSecond(); }
    [[nodiscard]] double replayVerticalSpeedMetersPerSecond() const { return m_replay.verticalSpeedMetersPerSecond(); }
    [[nodiscard]] double replayHeadingDegrees() const { return m_replay.headingDegrees(); }
    [[nodiscard]] QQuick3DGeometry *replayPathGeometry() { return m_replay.pathGeometry(); }
    [[nodiscard]] int replayPathPointCount() const { return m_replay.pathGeometry()->pointCount(); }
    [[nodiscard]] double replayPathLengthMeters() const { return m_replay.pathGeometry()->lengthMeters(); }
    [[nodiscard]] QQuick3DGeometry *replayPlannedPathGeometry() { return &m_replayPlannedPathGeometry; }
    [[nodiscard]] int replayPlannedPathPointCount() const { return m_replayPlannedPathGeometry.pointCount(); }
    [[nodiscard]] QString replaySessionId() const { return m_replaySessionId; }
    [[nodiscard]] QString replaySessionName() const { return m_replaySessionName; }
    [[nodiscard]] QString replayModelId() const { return m_replayModelId; }
    [[nodiscard]] QString replayModelName() const { return m_replayModelName; }
    [[nodiscard]] QString replayWorldId() const { return m_replayWorldId; }
    [[nodiscard]] QString replayWorldName() const { return m_replayWorldName; }
    [[nodiscard]] QUrl replayWorldPreviewDirectory() const { return m_replayWorldPreviewDirectory; }
    [[nodiscard]] QVariantList replayEvents() const { return m_replay.events(); }
    [[nodiscard]] int replayCurrentEventIndex() const { return m_replay.currentEventIndex(); }
    [[nodiscard]] QString replayCurrentEventLabel() const { return m_replay.currentEventLabel(); }
    [[nodiscard]] QVariantList replayTelemetrySeries() const { return m_replay.telemetrySeries(); }
    [[nodiscard]] QVariantMap replayRouteAnalysis() const { return m_replayRouteAnalysis; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool startSimulation();
    Q_INVOKABLE void stopSimulation();
    Q_INVOKABLE void abortTest();
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
    Q_INVOKABLE QString generateSessionReport(const QString &sessionId);
    Q_INVOKABLE QString exportSessionBundle(const QString &sessionId);
    Q_INVOKABLE bool openSessionFolder(const QString &sessionId);
    Q_INVOKABLE bool openSessionsFolder();
    Q_INVOKABLE bool clearSessionTrash();
    Q_INVOKABLE QString exportApplicationSettings();
    Q_INVOKABLE QVariantMap importApplicationSettings(const QUrl &fileUrl);
    Q_INVOKABLE void resetViewPreferences();
    Q_INVOKABLE void resetInteractionPreferences();
    Q_INVOKABLE void resetRecordingPreferences();
    Q_INVOKABLE void resetAllApplicationPreferences();
    Q_INVOKABLE bool copyEnvironmentInformation();
    Q_INVOKABLE bool updateSessionDetails(const QString &sessionId,
                                          const QString &displayName,
                                          const QString &notes);
    Q_INVOKABLE bool deleteSession(const QString &sessionId);
    Q_INVOKABLE bool restoreSession(const QString &sessionId);
    Q_INVOKABLE bool permanentlyDeleteSession(const QString &sessionId);
    Q_INVOKABLE bool deleteSessionSensorData(const QString &sessionId);
    Q_INVOKABLE bool startSessionReplay(const QString &sessionId);
    Q_INVOKABLE bool setReplayWorldId(const QString &worldId);
    Q_INVOKABLE void stopSessionReplay();
    Q_INVOKABLE void toggleReplay();
    Q_INVOKABLE void seekReplay(qint64 timeMs);
    Q_INVOKABLE void setReplaySpeed(double multiplier);
    Q_INVOKABLE void seekReplayEvent(int eventIndex);
    Q_INVOKABLE void seekPreviousReplayEvent();
    Q_INVOKABLE void seekNextReplayEvent();
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
    Q_INVOKABLE QVariantMap saveCurrentTestPlan(const QVariantMap &draft);
    Q_INVOKABLE QVariantMap duplicateTestPlan(const QString &planId);
    Q_INVOKABLE QVariantMap renameTestPlan(const QString &planId,
                                           const QString &name);
    Q_INVOKABLE QVariantMap deleteTestPlan(const QString &planId);
    Q_INVOKABLE QString exportTestPlan(const QString &planId);
    Q_INVOKABLE QVariantMap importTestPlan(const QUrl &fileUrl);
    Q_INVOKABLE QVariantMap reuseSessionTestPlan(const QString &sessionId);
    Q_INVOKABLE QVariantMap autoFixSelectedPlanRoute();
    Q_INVOKABLE bool openPlansFolder();
    Q_INVOKABLE double depthDistanceAt(double normalizedX, double normalizedY) const
    {
        return replayActive() ? m_replay.depthDistanceAt(normalizedX, normalizedY)
                              : m_sensorService.depthDistanceAt(normalizedX, normalizedY);
    }
    Q_INVOKABLE bool saveCameraSnapshot(const QString &stream);
    Q_INVOKABLE bool startCameraRecording(const QString &stream)
    {
        return m_sensorService.startRecording(stream);
    }
    Q_INVOKABLE bool stopCameraRecording() { return m_sensorService.stopRecording(); }
    Q_INVOKABLE bool setGimbalAngles(double yawDegrees, double pitchDegrees);
    Q_INVOKABLE bool setGimbalPreset(const QString &preset);
    Q_INVOKABLE bool openSensorCapturesFolder();
    Q_INVOKABLE QString saveLidarMap();
    Q_INVOKABLE bool openLidarMapsFolder();
    Q_INVOKABLE QString saveDepthMap();
    Q_INVOKABLE bool openDepthMapsFolder();
    Q_INVOKABLE bool loadSessionDepthMap(const QString &sessionId);
    Q_INVOKABLE void closeLoadedDepthMap();
    Q_INVOKABLE bool startVisualSurvey();
    Q_INVOKABLE void stopVisualSurvey();
    Q_INVOKABLE bool loadSessionVisualSurvey(const QString &sessionId);
    Q_INVOKABLE void closeLoadedVisualSurvey();
    Q_INVOKABLE bool openVisualSurveyFolder();
    Q_INVOKABLE bool exportVisualSurveyKeyframe(int index);
    Q_INVOKABLE bool updateVisualSurveyKeyframe(const QString &notes, bool anomaly);
    Q_INVOKABLE QVariantMap analyzeDepthMapRoute(const QVariantList &waypoints,
                                                 double clearanceMeters = 1.0) const
    {
        return m_depthMapping.analyzeRoute(waypoints, clearanceMeters);
    }
    Q_INVOKABLE QVariantMap analyzeMissionRouteSafety(const QVariantList &waypoints,
                                                      double startEastMeters,
                                                      double startNorthMeters,
                                                      double startAltitudeMeters) const;
    void setTakeoffAltitude(double value);
    void setControlSpeedMetersPerSecond(double value);
    void setControlVerticalSpeedMetersPerSecond(double value);
    void setControlYawRateDegreesPerSecond(double value);
    void setSelectedModelId(const QString &modelId);
    void setSensorServiceEnabled(bool enabled);
    void setSelectedWorldId(const QString &worldId);
    void setSelectedTestPlanId(const QString &planId);
    void setContainerName(const QString &name);
    void setFlightPathVisible(bool visible);
    void setFlightPathWidthMeters(double value);
    void setFlightPathColorPreset(const QString &preset);
    void setFlightPathMaxVisibleLengthMeters(int value);
    void setFlightPathAutoClear(bool value);
    void setCameraRotationSensitivity(double value);
    void setCameraZoomSensitivity(double value);
    void setInvertCameraVertical(bool value);
    void setGroundMarkerVisible(bool value);
    void setGroundMarkerSizeMode(const QString &value);
    void setDefaultCameraMode(const QString &value);
    void setCityBuildingOpacity(double value);
    void setWorldRoadsVisible(bool value);
    void setWorldGreenAreasVisible(bool value);
    void setWorldWaterVisible(bool value);
    void setDefaultGridVisible(bool value);
    void setPreciseTurnDefaultDegrees(double value);
    void setTelemetryRecordingHz(int value);
    void setAutoGenerateReports(bool value);
    void setTrashRetentionDays(int value);
    void prepareFlightScenePreview();

signals:
    void dockerAvailableChanged();
    void imageAvailableChanged();
    void dockerNetworkAvailableChanged();
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
    void viewPreferencesChanged();
    void interactionPreferencesChanged();
    void recordingPreferencesChanged();
    void sessionStorageChanged();
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
    void sensorServicePreferenceChanged();
    void sensorServiceStateChanged();
    void lidarScanChanged();
    void cameraFrameChanged();
    void depthFrameChanged();
    void cameraStateChanged();
    void gimbalStateChanged();
    void cameraRecordingChanged();
    void sensorCaptureSaved();
    void selectedWorldChanged();
    void containerNameChanged();
    void containerNameAvailabilityChanged();
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
    void trashedFlightSessionsChanged();
    void savedRoutesChanged();
    void testPlansChanged();
    void selectedTestPlanChanged();
    void testRunStateChanged();
    void replayStateChanged();
    void replayPlayingChanged();
    void replayTimeChanged();
    void replayPositionChanged();
    void replayDataChanged();

private:
    void useStatus(const QString &message);
    void appendDiagnostic(const QString &source, const QString &message);
    void updateIssueState();
    void tryCompleteLandingAndStop();
    void finalizeSensorSessionSummary();
    void setLandingAndStopping(bool value);
    void resetFlightScene();
    void updateFlightScenePosition();
    struct MappingPoseSample {
        qint64 timestampMs{0};
        QVector3D position;
        double headingDegrees{0.0};
        double rollDegrees{0.0};
        double pitchDegrees{0.0};
        double altitudeMeters{0.0};
        bool valid{false};
    };
    void appendMappingPoseSample();
    [[nodiscard]] MappingPoseSample mappingPoseAt(qint64 timestampMs) const;
    void updateMissionMonitor();
    void updateMissionGimbalForProgress();
    void resetMissionMonitor();
    void triggerMissionProtection(const QString &reason);
    void refreshSessionStorageUsage();

    DockerController m_docker;
    SensorServiceController m_sensorService;
    LidarMappingController m_lidarMapping;
    DepthMappingController m_depthMapping;
    VisualSurveyController m_visualSurvey;
    GimbalInspectionController m_gimbalInspection;
    VehicleConnection m_vehicle;
    FlightPathGeometry m_flightPathGeometry;
    FlightSessionRecorder m_sessionRecorder;
    ReplayController m_replay;
    qint64 m_lastReplayMappingTimeMs{-1};
    qint64 m_lastReplayDepthMappingTimeMs{-1};
    QString m_loadedVisualSurveySessionId;
    FlightPathGeometry m_replayPlannedPathGeometry;
    QVariantMap m_replayRouteAnalysis;
    RouteLibrary m_routeLibrary;
    TestPlanRepository m_testPlanRepository;
    QTimer m_sessionSampleTimer;
    QTimer m_connectionTimeout;
    QTimer m_healthTimeout;
    QTimer m_missionMonitorTimer;
    bool m_connectionTimedOut{false};
    bool m_healthTimedOut{false};
    bool m_commandIssue{false};
    SimulationRunCoordinator m_runCoordinator;
    bool m_scenePreviewActive{false};
    bool m_sceneOriginAvailable{false};
    double m_sceneOriginNorthMeters{0.0};
    double m_sceneOriginEastMeters{0.0};
    double m_sceneOriginDownMeters{0.0};
    QVector3D m_vehicleScenePosition;
    QVector<MappingPoseSample> m_mappingPoseHistory;
    QVector3D m_missionStartScenePosition;
    QVector<QVector3D> m_missionSceneWaypoints;
    QVector<double> m_missionWaypointSpeeds;
    QVariantList m_missionWaypointPayloads;
    int m_lastGimbalMissionTargetIndex{-1};
    int m_lastGimbalCaptureIndex{-1};
    int m_pendingGimbalCaptureIndex{-1};
    quint64 m_pendingGimbalCaptureFrameSequence{0};
    qint64 m_pendingGimbalCaptureDeadlineMs{0};
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
    QString m_statusMessage{QStringLiteral("FlightRig 正在检查本机环境…")};
    QStringList m_diagnosticEntries;
    QString m_selectedModelId{QStringLiteral("gz_x500")};
    QString m_selectedWorldId{QStringLiteral("default")};
    QString m_containerName{QStringLiteral("flightbench-px4")};
    QString m_selectedTestPlanId;
    bool m_sensorServiceEnabled{false};
    ApplicationPreferences m_preferences;
    qint64 m_sessionStorageBytes{0};
    QString m_lastRecordedIssueCode;
    QString m_replaySessionId;
    QString m_replaySessionName;
    QString m_replayModelId;
    QString m_replayModelName;
    QString m_replayWorldId;
    QString m_replayWorldName;
    QString m_lastReplayCapturePath;
    QString m_sessionEndReason;
    QUrl m_replayWorldPreviewDirectory;
};

// Keep AppController allocation in the implementation translation unit. The
// controller is intentionally large; callers should not embed it on their
// stack or depend on its private layout.
[[nodiscard]] AppController *createAppController(QObject *parent = nullptr);
