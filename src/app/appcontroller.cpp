#include "sessions/sensorsessionfinalizer.h"
#include "infrastructure/runtimeinformation.h"
#include "infrastructure/simulationcatalog.h"
#include "app/appcontroller.h"
#include "testing/preflightevaluator.h"
#include "infrastructure/storagepaths.h"
#include "flight/px4eventtranslator.h"
#include "sessions/testreportgenerator.h"
#include "mapping/worldmapcatalog.h"

#include <QCoreApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QProcess>
#include <QSettings>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <utility>

using SimulationCatalog::simulationModelDefinitions;
using SimulationCatalog::simulationWorldDefinitions;
using SimulationCatalog::findSimulationModel;
using SimulationCatalog::findSimulationWorld;

namespace {
bool isValidContainerName(const QString &name)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,62}$"));
    return expression.match(name).hasMatch();
}

}

AppController *createAppController(QObject *parent)
{
    return new AppController(parent);
}

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    m_runCoordinator.bind({
        [this] { return simulationRunning(); },
        [this] { return vehicleConnected(); },
        [this] { return inAir(); },
        [this] { return armed(); },
        [this] { return m_vehicle.busy() || m_docker.busy(); },
        [this] { m_vehicle.releaseKeyboardControl(); },
        [this] { m_sessionRecorder.markLandingRequested(); },
        [this] { m_vehicle.land(); },
        [this] { finalizeSensorSessionSummary(); },
        [this] { if (m_sensorService.running()) m_sensorService.stop(); },
        [this] { m_docker.stopSimulation(); },
        [this] { emit landingAndStoppingChanged(); emit busyChanged(); },
        [this](const QString &message) { useStatus(message); },
        [this](const QString &message) { appendDiagnostic(QStringLiteral("安全"), message); },
        [this](int delay, std::function<void()> callback) {
            QTimer::singleShot(delay, this, std::move(callback));
        }
    });

    m_replayPlannedPathGeometry.setDashed(true);
    m_replayPlannedPathGeometry.setWidthMeters(0.035);
    m_replayPlannedPathGeometry.setMaxVisibleLengthMeters(0);
    connect(&m_replay, &ReplayController::activeChanged,
            this, &AppController::replayStateChanged);
    connect(&m_replay, &ReplayController::playingChanged,
            this, &AppController::replayPlayingChanged);
    connect(&m_replay, &ReplayController::timeChanged,
            this, &AppController::replayTimeChanged);
    connect(&m_replay, &ReplayController::positionChanged,
            this, &AppController::replayPositionChanged);
    connect(&m_replay, &ReplayController::dataChanged,
            this, &AppController::replayDataChanged);
    connect(&m_replay, &ReplayController::sensorDataChanged, this, [this] {
        if (m_replay.sensorKind() == QStringLiteral("lidar-2d")
            && !m_replay.lidarPointsForMapping().isEmpty()) {
            if (m_lastReplayMappingTimeMs >= 0
                && m_replay.timeMs() < m_lastReplayMappingTimeMs) {
                m_lidarMapping.clear();
            }
            const QVector3D position = m_replay.position();
            m_lidarMapping.ingestScan(m_replay.lidarPointsForMapping(), position.x(), -position.z(),
                                      m_replay.headingDegrees(), m_replay.rollDegrees(),
                                      m_replay.pitchDegrees(), m_replay.altitudeMeters(),
                                      m_replay.timeMs(), true);
            m_lastReplayMappingTimeMs = m_replay.timeMs();
        }
        if (m_replay.sensorKind() == QStringLiteral("depth-camera")
            && !m_replay.depthMillimetresForMapping().isEmpty()) {
            if (m_lastReplayDepthMappingTimeMs >= 0
                && m_replay.timeMs() < m_lastReplayDepthMappingTimeMs) {
                m_depthMapping.clear();
            }
            m_depthMapping.ingestDepthFrame(
                m_replay.depthMillimetresForMapping(),
                m_replay.depthGridWidthForMapping(),
                m_replay.depthGridHeightForMapping(),
                m_replay.position(), m_replay.headingDegrees(),
                m_replay.rollDegrees(), m_replay.pitchDegrees(),
                m_replay.timeMs(), true);
            m_lastReplayDepthMappingTimeMs = m_replay.timeMs();
        }
        emit replayDataChanged();
        emit sensorServiceStateChanged();
        emit lidarScanChanged();
        emit cameraFrameChanged();
        emit depthFrameChanged();
        emit cameraStateChanged();
        emit gimbalStateChanged();
    });
    connect(&m_sessionRecorder, &FlightSessionRecorder::activeSessionChanged,
            this, &AppController::activeFlightSessionChanged);
    connect(&m_sessionRecorder, &FlightSessionRecorder::completedSessionsChanged,
            this, &AppController::flightSessionsChanged);
    connect(&m_sessionRecorder, &FlightSessionRecorder::trashedSessionsChanged,
            this, &AppController::trashedFlightSessionsChanged);
    connect(&m_sessionRecorder, &FlightSessionRecorder::completedSessionsChanged,
            this, &AppController::refreshSessionStorageUsage);
    connect(&m_sessionRecorder, &FlightSessionRecorder::trashedSessionsChanged,
            this, &AppController::refreshSessionStorageUsage);
    connect(&m_routeLibrary, &RouteLibrary::routesChanged,
            this, &AppController::savedRoutesChanged);
    connect(&m_routeLibrary, &RouteLibrary::routesChanged,
            this, &AppController::testPlansChanged);
    connect(&m_routeLibrary, &RouteLibrary::routesChanged,
            this, &AppController::selectedTestPlanChanged);
    connect(&m_testPlanRepository, &TestPlanRepository::plansChanged,
            this, &AppController::testPlansChanged);
    connect(&m_testPlanRepository, &TestPlanRepository::plansChanged,
            this, &AppController::selectedTestPlanChanged);
    connect(this, &AppController::selectedModelChanged,
            this, &AppController::selectedTestPlanChanged);
    connect(this, &AppController::selectedModelChanged,
            this, &AppController::sensorServiceStateChanged);
    connect(this, &AppController::selectedWorldChanged,
            this, &AppController::selectedTestPlanChanged);
    connect(this, &AppController::takeoffAltitudeChanged,
            this, &AppController::selectedTestPlanChanged);
    connect(this, &AppController::controlSpeedChanged,
            this, &AppController::selectedTestPlanChanged);
    connect(this, &AppController::selectedTestPlanChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::dockerAvailableChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::imageAvailableChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::simulationRunningChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::vehicleConnectedChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::vehicleReadyChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::armedChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::inAirChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::missionStateChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::missionProgressChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::missionMonitorChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::issueChanged,
            this, &AppController::testRunStateChanged);
    connect(this, &AppController::activeFlightSessionChanged,
            this, &AppController::testRunStateChanged);
    connect(&m_sessionRecorder, &FlightSessionRecorder::recorderError,
            this, [this](const QString &message) {
                appendDiagnostic(QStringLiteral("记录"), message);
                useStatus(message);
            });

    connect(&m_docker, &DockerController::dockerAvailableChanged,
            this, &AppController::dockerAvailableChanged);
    connect(&m_docker, &DockerController::imageAvailableChanged,
            this, &AppController::imageAvailableChanged);
    connect(&m_docker, &DockerController::networkAvailableChanged,
            this, &AppController::dockerNetworkAvailableChanged);
    connect(&m_docker, &DockerController::networkAvailableChanged,
            this, &AppController::testRunStateChanged);
    connect(&m_docker, &DockerController::simulationRunningChanged,
            this, &AppController::simulationRunningChanged);
    connect(&m_docker, &DockerController::containerNameAvailableChanged,
            this, &AppController::containerNameAvailabilityChanged);
    connect(&m_docker, &DockerController::containerNameAvailableChanged,
            this, &AppController::testRunStateChanged);
    connect(&m_docker, &DockerController::busyChanged,
            this, &AppController::busyChanged);
    connect(&m_docker, &DockerController::statusMessageChanged, this, [this] {
        appendDiagnostic(QStringLiteral("Docker"), m_docker.statusMessage());
        useStatus(m_docker.statusMessage());
        updateIssueState();
    });
    connect(&m_docker, &DockerController::problemCodeChanged,
            this, &AppController::updateIssueState);
    connect(&m_sensorService, &SensorServiceController::imageAvailableChanged,
            this, &AppController::sensorServiceStateChanged);
    connect(&m_sensorService, &SensorServiceController::runningChanged,
            this, &AppController::sensorServiceStateChanged);
    connect(&m_sensorService, &SensorServiceController::connectedChanged,
            this, &AppController::sensorServiceStateChanged);
    connect(&m_sensorService, &SensorServiceController::busyChanged, this, [this] {
        emit sensorServiceStateChanged();
        emit busyChanged();
    });
    connect(&m_sensorService, &SensorServiceController::statusMessageChanged, this, [this] {
        appendDiagnostic(QStringLiteral("ROS 2"), m_sensorService.statusMessage());
        emit sensorServiceStateChanged();
    });
    connect(&m_sensorService, &SensorServiceController::problemCodeChanged,
            this, &AppController::sensorServiceStateChanged);
    connect(this, &AppController::sensorServiceStateChanged,
            this, &AppController::testRunStateChanged);
    connect(&m_sensorService, &SensorServiceController::lidarScanChanged, this, [this] {
        const qint64 scanTimestamp = m_sensorService.lidarTimestampEpochMs() > 0
            ? m_sensorService.lidarTimestampEpochMs()
            : QDateTime::currentMSecsSinceEpoch();
        const MappingPoseSample pose = mappingPoseAt(scanTimestamp);
        m_lidarMapping.ingestScan(
            m_sensorService.lidarPointsForMapping(), pose.position.x(),
            -pose.position.z(), pose.headingDegrees,
            pose.rollDegrees, pose.pitchDegrees, pose.altitudeMeters,
            scanTimestamp, pose.valid);
        emit lidarScanChanged();
    });
    connect(&m_sensorService, &SensorServiceController::sensorKindChanged,
            this, &AppController::sensorServiceStateChanged);
    connect(&m_sensorService, &SensorServiceController::cameraFrameChanged, this, [this] {
        const qint64 timestamp = m_sensorService.rgbTimestampEpochMs() > 0
            ? m_sensorService.rgbTimestampEpochMs() : QDateTime::currentMSecsSinceEpoch();
        const MappingPoseSample pose = mappingPoseAt(timestamp);
        m_visualSurvey.ingestFrame(m_sensorService.rgbJpegForSurvey(),
            m_sensorService.rgbWidthForSurvey(), m_sensorService.rgbHeightForSurvey(),
            timestamp, pose.position, pose.headingDegrees, pose.pitchDegrees, pose.valid);
        if (selectedSensorType() == QStringLiteral("gimbal-camera") && !replayActive()
            && m_sensorService.sessionRecording())
            m_gimbalInspection.ingestFrame(m_sensorService.rgbJpegForSurvey(),
                m_sensorService.rgbFrameSequence(), timestamp, pose.position,
                GimbalInspectionController::aircraftOrientation(pose.headingDegrees,
                    pose.pitchDegrees, pose.rollDegrees), pose.valid);
        emit cameraFrameChanged();
    });
    auto *inspectionTimer = new QTimer(this);
    inspectionTimer->setInterval(100);
    connect(inspectionTimer, &QTimer::timeout, this, [this] {
        if (replayActive()) return;
        const auto pose = mappingPoseAt(QDateTime::currentMSecsSinceEpoch());
        m_gimbalInspection.updatePose(pose.position, pose.headingDegrees, pose.pitchDegrees,
            pose.rollDegrees, simulationRunning() && vehicleConnected() && pose.valid
                && std::abs(QDateTime::currentMSecsSinceEpoch() - pose.timestampMs) <= 350
                && selectedSensorType() == QStringLiteral("gimbal-camera")
                && m_sensorService.running() && m_sensorService.connected(),
            m_sensorService.gimbalFeedbackFresh(), m_sensorService.gimbalYawDegrees(),
            m_sensorService.gimbalPitchDegrees(), m_sensorService.gimbalRollDegrees());
    });
    inspectionTimer->start();
    connect(&m_gimbalInspection, &GimbalInspectionController::commandRequested, this,
        [this](double yaw, double pitch) {
            m_pendingGimbalCaptureIndex = -1;
            if (replayActive() || !m_sensorService.setGimbalAngles(yaw, pitch, 0.0))
                m_gimbalInspection.pause();
        });
    connect(&m_replay, &ReplayController::timeChanged, this, [this] {
        m_gimbalInspection.seek(m_replay.timeMs());
    });
    connect(&m_sensorService, &SensorServiceController::depthFrameChanged, this, [this] {
        const qint64 frameTimestamp = m_sensorService.depthTimestampEpochMs() > 0
            ? m_sensorService.depthTimestampEpochMs()
            : QDateTime::currentMSecsSinceEpoch();
        const MappingPoseSample pose = mappingPoseAt(frameTimestamp);
        m_depthMapping.ingestDepthFrame(
            m_sensorService.depthMillimetresForMapping(),
            m_sensorService.depthGridWidthForMapping(),
            m_sensorService.depthGridHeightForMapping(),
            pose.position, pose.headingDegrees, pose.rollDegrees,
            pose.pitchDegrees, frameTimestamp, pose.valid);
        emit depthFrameChanged();
    });
    connect(&m_sensorService, &SensorServiceController::cameraStateChanged,
            this, &AppController::cameraStateChanged);
    connect(&m_sensorService, &SensorServiceController::gimbalStateChanged,
            this, &AppController::gimbalStateChanged);
    connect(&m_sensorService, &SensorServiceController::recordingChanged,
            this, &AppController::cameraRecordingChanged);
    connect(&m_sensorService, &SensorServiceController::captureSaved,
            this, &AppController::sensorCaptureSaved);
    connect(&m_sensorService, &SensorServiceController::performanceModeChanged,
            this, &AppController::sensorServiceStateChanged);
    connect(&m_sensorService, &SensorServiceController::sessionRecordingChanged,
            this, &AppController::sensorServiceStateChanged);
    connect(&m_docker, &DockerController::simulationRunningChanged, this, [this] {
        m_vehicle.setSitlExpected(simulationRunning());
        if (simulationRunning()) {
            resetFlightScene();
            m_lidarMapping.clear();
            m_depthMapping.clear();
            m_visualSurvey.clear();
            m_gimbalInspection.clear();
            m_lastReplayMappingTimeMs = -1;
            m_lastReplayDepthMappingTimeMs = -1;
            m_lastRecordedIssueCode.clear();
            if (m_sessionRecorder.startSession(
                    selectedModelId(), selectedModelName(), takeoffAltitude(),
                    selectedWorldId(), selectedWorldName(), applicationVersion(),
                    QStringLiteral("px4io/px4-sitl-gazebo:latest"),
                    qtRuntimeVersion(), mavsdkVersion())) {
                m_sessionRecorder.setTestPlan(selectedTestPlan());
                const QVariantMap sensorSettings = selectedTestPlan()
                    .value(QStringLiteral("sensorSettings")).toMap();
                QString sensorUsage = sensorSettings.value(
                    QStringLiteral("usage"), QStringLiteral("off")).toString();
                if (m_sensorServiceEnabled && sensorServiceSupported()
                    && sensorUsage == QStringLiteral("off")) {
                    sensorUsage = QStringLiteral("preview");
                }
                const QString activeSessionFolder = m_sessionRecorder.folderPathForSession(
                    m_sessionRecorder.activeSessionId());
                m_sensorService.beginSessionRecording(
                    activeSessionFolder,
                    sensorUsage,
                    sensorSettings.value(QStringLiteral("performance"),
                                         QStringLiteral("standard")).toString());
                m_visualSurvey.prepareSession(activeSessionFolder,
                    sensorUsage == QStringLiteral("record")
                    && selectedSensorType() == QStringLiteral("mono-camera"));
                m_gimbalInspection.prepareSession(activeSessionFolder, selectedWorldId(),
                    sensorUsage == QStringLiteral("record")
                        && selectedSensorType() == QStringLiteral("gimbal-camera"),
                    m_sessionRecorder.startedAtEpochMs());
            }
            m_connectionTimedOut = false;
            if (!vehicleConnected()) m_connectionTimeout.start();
            if (m_sensorServiceEnabled && sensorServiceSupported()) {
                QTimer::singleShot(500, this, [this] {
                    if (simulationRunning() && m_sensorServiceEnabled
                        && sensorServiceSupported()) {
                        const QVariantMap settings = selectedTestPlan()
                            .value(QStringLiteral("sensorSettings")).toMap();
                        m_sensorService.start(
                            selectedModelId(), selectedWorldId(),
                            settings.value(QStringLiteral("performance"),
                                           QStringLiteral("standard")).toString());
                    }
                });
            }
        } else {
            finalizeSensorSessionSummary();
            if (m_sensorService.running()) m_sensorService.stop();
            setLandingAndStopping(false);
            const QString completedSessionId = m_sessionRecorder.activeSessionId();
            m_sessionRecorder.finishSession(
                m_sessionEndReason.isEmpty() ? QStringLiteral("仿真已停止")
                                             : m_sessionEndReason);
            if (m_preferences.autoGenerateReports() && !completedSessionId.isEmpty()) {
                const QVariantMap details = m_sessionRecorder.sessionDetails(completedSessionId);
                const QString reportsDirectory = StoragePaths::directory(
                    QStringLiteral("Reports"));
                QString reportError;
                const QString reportPath = TestReportGenerator::generate(
                    details, reportsDirectory, &reportError);
                if (reportPath.isEmpty()) {
                    appendDiagnostic(QStringLiteral("报告"),
                                     QStringLiteral("自动报告生成失败：%1").arg(reportError));
                } else {
                    m_sessionRecorder.setSessionReport(completedSessionId, reportPath);
                    appendDiagnostic(QStringLiteral("报告"),
                                     QStringLiteral("已自动生成：%1").arg(reportPath));
                }
            }
            m_sessionEndReason.clear();
            resetMissionMonitor();
            m_connectionTimeout.stop();
            m_healthTimeout.stop();
            m_connectionTimedOut = false;
            m_healthTimedOut = false;
            if (m_runCoordinator.consumeRestart()) {
                QTimer::singleShot(250, this, [this] { startSimulation(); });
            }
        }
        updateIssueState();
    });

    connect(&m_vehicle, &VehicleConnection::connectedChanged,
            this, &AppController::vehicleConnectedChanged);
    connect(&m_vehicle, &VehicleConnection::readyChanged,
            this, &AppController::vehicleReadyChanged);
    connect(&m_vehicle, &VehicleConnection::armedChanged,
            this, &AppController::armedChanged);
    connect(&m_vehicle, &VehicleConnection::inAirChanged,
            this, &AppController::inAirChanged);
    connect(&m_vehicle, &VehicleConnection::armedChanged,
            this, &AppController::updateFlightScenePosition);
    connect(&m_vehicle, &VehicleConnection::inAirChanged,
            this, &AppController::updateFlightScenePosition);
    connect(&m_vehicle, &VehicleConnection::busyChanged,
            this, &AppController::busyChanged);
    connect(&m_vehicle, &VehicleConnection::healthChanged,
            this, &AppController::healthChanged);
    connect(&m_vehicle, &VehicleConnection::altitudeMetersChanged,
            this, &AppController::altitudeMetersChanged);
    connect(&m_vehicle, &VehicleConnection::speedMetersPerSecondChanged,
            this, &AppController::speedMetersPerSecondChanged);
    connect(&m_vehicle, &VehicleConnection::velocityChanged,
            this, &AppController::velocityChanged);
    connect(&m_vehicle, &VehicleConnection::localPositionChanged, this, [this] {
        emit localPositionChanged();
        updateFlightScenePosition();
    });
    connect(&m_vehicle, &VehicleConnection::sceneRotationChanged,
            this, &AppController::vehicleSceneRotationChanged);
    connect(&m_flightPathGeometry, &FlightPathGeometry::pointCountChanged,
            this, &AppController::flightPathPointCountChanged);
    connect(&m_vehicle, &VehicleConnection::headingChanged,
            this, &AppController::headingChanged);
    connect(&m_vehicle, &VehicleConnection::attitudeChanged,
            this, &AppController::attitudeChanged);
    connect(&m_vehicle, &VehicleConnection::angularVelocityChanged,
            this, &AppController::angularVelocityChanged);
    connect(&m_vehicle, &VehicleConnection::batteryChanged,
            this, &AppController::batteryChanged);
    connect(&m_vehicle, &VehicleConnection::gpsChanged,
            this, &AppController::gpsChanged);
    connect(&m_vehicle, &VehicleConnection::telemetryAvailableChanged,
            this, &AppController::telemetryAvailableChanged);
    connect(&m_vehicle, &VehicleConnection::imuAvailableChanged,
            this, &AppController::imuAvailableChanged);
    connect(&m_vehicle, &VehicleConnection::batteryAvailableChanged,
            this, &AppController::batteryAvailableChanged);
    connect(&m_vehicle, &VehicleConnection::gpsAvailableChanged,
            this, &AppController::gpsAvailableChanged);
    connect(&m_vehicle, &VehicleConnection::takeoffAltitudeChanged,
            this, &AppController::takeoffAltitudeChanged);
    connect(&m_vehicle, &VehicleConnection::flightModeChanged,
            this, &AppController::flightModeChanged);
    connect(&m_vehicle, &VehicleConnection::keyboardControlActiveChanged,
            this, &AppController::keyboardControlActiveChanged);
    connect(&m_vehicle, &VehicleConnection::keyboardControlTransitioningChanged,
            this, &AppController::keyboardControlTransitioningChanged);
    connect(&m_vehicle, &VehicleConnection::preciseTurnChanged,
            this, &AppController::preciseTurnChanged);
    connect(&m_vehicle, &VehicleConnection::controlSpeedChanged,
            this, &AppController::controlSpeedChanged);
    connect(&m_vehicle, &VehicleConnection::controlCommandChanged, this, [this] {
        emit controlCommandChanged();
        if (m_sessionRecorder.active()) {
            appendDiagnostic(QStringLiteral("控制"),
                             QStringLiteral("键盘指令：%1").arg(controlCommandText()));
        }
    });
    connect(&m_vehicle, &VehicleConnection::globalPositionChanged,
            this, &AppController::globalPositionChanged);
    connect(&m_vehicle, &VehicleConnection::missionStateChanged, this, [this] {
        emit missionStateChanged();
        emit busyChanged();
        appendDiagnostic(QStringLiteral("任务"), missionStatusMessage());
        if (missionTotalItems() > 0 && missionCurrentItem() >= missionTotalItems()) {
            m_missionMonitorState = QStringLiteral("completed");
            m_missionMonitorMessage = QStringLiteral("任务完成");
            m_missionEstimatedSeconds = 0;
            m_missionRemainingDistanceMeters = 0.0;
            m_sessionRecorder.updateMissionMetrics(m_missionMaximumCrossTrackErrorMeters,
                                                   missionTotalItems(),
                                                   missionTotalItems());
            emit missionMonitorChanged();
        }
    });
    connect(&m_vehicle, &VehicleConnection::missionProgressChanged, this, [this] {
        emit missionProgressChanged();
        updateMissionGimbalForProgress();
        updateMissionMonitor();
    });
    connect(&m_vehicle, &VehicleConnection::statusMessageChanged, this, [this] {
        appendDiagnostic(QStringLiteral("MAVSDK"), m_vehicle.statusMessage());
        useStatus(m_vehicle.statusMessage());
        const QString message = m_vehicle.statusMessage();
        if (message.contains(QStringLiteral("失败"))
            || message.contains(QStringLiteral("被阻止"))) {
            m_commandIssue = true;
            m_commandIssueMessage = message;
        } else if (message.contains(QStringLiteral("正在"))
                   || message.contains(QStringLiteral("已接受"))) {
            m_commandIssue = false;
            m_commandIssueMessage.clear();
        }
        updateIssueState();
    });

    connect(&m_vehicle, &VehicleConnection::connectedChanged, this, [this] {
        if (vehicleConnected()) {
            m_connectionTimeout.stop();
            m_connectionTimedOut = false;
            m_healthTimedOut = false;
            if (!vehicleReady()) m_healthTimeout.start();
        } else {
            m_healthTimeout.stop();
            m_healthTimedOut = false;
            if (simulationRunning()) m_connectionTimeout.start();
            if (m_runCoordinator.landingAndStopping()) {
                useStatus(QStringLiteral("飞行器连接暂时中断，正在等待重连并确认落地…"));
            }
        }
        updateIssueState();
    });
    connect(&m_vehicle, &VehicleConnection::readyChanged, this, [this] {
        if (vehicleReady()) {
            m_sessionRecorder.markHealthReady();
            m_healthTimeout.stop();
            m_healthTimedOut = false;
            m_commandIssue = false;
            m_commandIssueMessage.clear();
        } else if (vehicleConnected()) {
            m_healthTimeout.start();
        }
        updateIssueState();
    });
    connect(&m_vehicle, &VehicleConnection::armedChanged, this, [this] {
        m_sessionRecorder.updateFlightState(armed(), inAir());
        tryCompleteLandingAndStop();
    });
    connect(&m_vehicle, &VehicleConnection::inAirChanged, this, [this] {
        m_sessionRecorder.updateFlightState(armed(), inAir());
        tryCompleteLandingAndStop();
    });
    connect(&m_vehicle, &VehicleConnection::landingCommandFinished,
            this, [this](bool success, const QString &) {
                if (!m_runCoordinator.landingAndStopping()) return;
                if (!success) {
                    setLandingAndStopping(false);
                    return;
                }
                useStatus(QStringLiteral("正在降落，落地并锁定后将自动停止仿真…"));
                tryCompleteLandingAndStop();
            });

    connect(&m_vehicle, &VehicleConnection::healthChanged, this, [this] {
        appendDiagnostic(QStringLiteral("PX4"),
                         QStringLiteral("健康状态：传感器 %1，本地位置 %2，全局位置 %3，Home %4，可解锁 %5")
                             .arg(gyrometerOk() && accelerometerOk() && magnetometerOk() ? QStringLiteral("正常") : QStringLiteral("等待"),
                                  localPositionOk() ? QStringLiteral("正常") : QStringLiteral("等待"),
                                  globalPositionOk() ? QStringLiteral("正常") : QStringLiteral("等待"),
                                  homePositionOk() ? QStringLiteral("正常") : QStringLiteral("等待"),
                                  armable() ? QStringLiteral("是") : QStringLiteral("否")));
    });

    m_connectionTimeout.setSingleShot(true);
    m_connectionTimeout.setInterval(45000);
    connect(&m_connectionTimeout, &QTimer::timeout, this, [this] {
        if (simulationRunning() && !vehicleConnected()) {
            m_connectionTimedOut = true;
            appendDiagnostic(QStringLiteral("MAVSDK"),
                             QStringLiteral("等待 PX4 心跳超过 45 秒"));
            updateIssueState();
        }
    });

    m_healthTimeout.setSingleShot(true);
    m_healthTimeout.setInterval(90000);
    connect(&m_healthTimeout, &QTimer::timeout, this, [this] {
        if (vehicleConnected() && !vehicleReady()) {
            m_healthTimedOut = true;
            appendDiagnostic(QStringLiteral("PX4"),
                             QStringLiteral("健康检查超过 90 秒仍未稳定"));
            updateIssueState();
        }
    });

    QSettings settings;
    const auto flightControls = ApplicationPreferences::readFlightControls(settings);
    m_vehicle.setTakeoffAltitude(flightControls.takeoffAltitude);
    m_vehicle.setControlSpeedMetersPerSecond(flightControls.horizontalSpeed);
    if (flightControls.needsLegacySpeedMigration)
        m_preferences.completeLegacySpeedMigration(m_vehicle.controlSpeedMetersPerSecond());
    m_vehicle.setControlVerticalSpeedMetersPerSecond(flightControls.verticalSpeed);
    m_vehicle.setControlYawRateDegreesPerSecond(flightControls.yawRate);
    m_preferences.loadFlightPath(settings);
    m_flightPathGeometry.setWidthMeters(
        settings.value(QStringLiteral("flightPath/widthMeters"), 0.06).toDouble());
    m_replay.pathGeometry()->setWidthMeters(m_flightPathGeometry.widthMeters());
    m_flightPathGeometry.setMaxVisibleLengthMeters(
        settings.value(QStringLiteral("flightPath/maxVisibleLengthMeters"), 100).toInt());
    m_replay.pathGeometry()->setMaxVisibleLengthMeters(
        m_flightPathGeometry.maxVisibleLengthMeters());
    m_preferences.load(settings);
    const QString savedModelId = settings.value(QStringLiteral("simulation/modelId"),
                                                QStringLiteral("gz_x500")).toString();
    if (findSimulationModel(savedModelId)) m_selectedModelId = savedModelId;
    const QString savedWorldId = settings.value(QStringLiteral("simulation/worldId"),
                                                QStringLiteral("default")).toString();
    if (findSimulationWorld(savedWorldId)) m_selectedWorldId = savedWorldId;
    const QString savedContainerName = settings.value(
        QStringLiteral("simulation/containerName"), QStringLiteral("flightbench-px4")).toString();
    if (isValidContainerName(savedContainerName)) m_containerName = savedContainerName;
    m_sensorServiceEnabled = settings.value(
        QStringLiteral("simulation/sensorServiceEnabled"), false).toBool();
    m_docker.setContainerName(m_containerName);
    const QString savedTestPlanId = settings.value(
        QStringLiteral("testPlans/selectedId")).toString();
    if (!m_testPlanRepository.plan(savedTestPlanId).isEmpty()) {
        setSelectedTestPlanId(savedTestPlanId);
    }
    appendDiagnostic(QStringLiteral("应用"), QStringLiteral("FlightBench 已启动，正在检查本机环境"));

    m_sessionRecorder.purgeTrashOlderThan(m_preferences.trashRetentionDays());
    refreshSessionStorageUsage();
    m_sessionSampleTimer.setInterval(1000 / m_preferences.telemetryRecordingHz());
    connect(&m_sessionSampleTimer, &QTimer::timeout, this, [this] {
        m_sessionRecorder.recordSample(vehicleConnected(),
                                       vehicleReady(),
                                       armed(),
                                       inAir(),
                                       telemetryAvailable(),
                                       altitudeMeters(),
                                       speedMetersPerSecond(),
                                       verticalSpeedMetersPerSecond(),
                                       headingDegrees(),
                                       rollDegrees(),
                                       pitchDegrees(),
                                       yawDegrees(),
                                       imuAvailable(),
                                       gyroRollRateDegreesPerSecond(),
                                       gyroPitchRateDegreesPerSecond(),
                                       gyroYawRateDegreesPerSecond(),
                                       gpsAvailable(),
                                       gpsFixType(),
                                       gpsSatellites(),
                                       batteryAvailable(),
                                       batteryRemainingPercent(),
                                       batteryVoltageVolts(),
                                       keyboardControlActive(),
                                       m_vehicle.controlForwardMetersPerSecond(),
                                       m_vehicle.controlRightMetersPerSecond(),
                                       m_vehicle.controlUpMetersPerSecond(),
                                       m_vehicle.controlYawDegreesPerSecond(),
                                       preciseTurnActive(),
                                       preciseTurnTargetDegrees(),
                                       localPositionAvailable(),
                                       vehicleScenePosition().x(),
                                       vehicleScenePosition().y(),
                                       vehicleScenePosition().z());
        if (m_sessionRecorder.active() && localPositionAvailable()) {
            const QVariantMap clearance = WorldMapCatalog::analyzePosition(
                selectedWorldId(), vehicleScenePosition().x(),
                -vehicleScenePosition().z(), vehicleScenePosition().y());
            m_sessionRecorder.updateObstacleClearance(
                clearance.value(QStringLiteral("available")).toBool(),
                clearance.value(QStringLiteral("effectiveClearanceMeters")).toDouble(),
                clearance.value(QStringLiteral("infringed")).toBool());
        }
    });
    m_sessionSampleTimer.start();

    m_missionMonitorTimer.setInterval(250);
    connect(&m_missionMonitorTimer, &QTimer::timeout,
            this, &AppController::updateMissionMonitor);
    m_missionMonitorTimer.start();

    QTimer::singleShot(0, &m_vehicle, &VehicleConnection::start);
}

AppController::~AppController()
{
    finalizeSensorSessionSummary();
    // Member QObjects are destroyed after this destructor body, while value
    // members such as m_diagnosticEntries are destroyed before m_docker. A
    // finishing QProcess may emit status signals during DockerController's
    // destructor, so disconnect every internal sender before any AppController
    // state can disappear.
    QObject::disconnect(&m_docker, nullptr, this, nullptr);
    QObject::disconnect(&m_sensorService, nullptr, this, nullptr);
    QObject::disconnect(&m_vehicle, nullptr, this, nullptr);
    QObject::disconnect(&m_sessionRecorder, nullptr, this, nullptr);
    QObject::disconnect(&m_routeLibrary, nullptr, this, nullptr);
    QObject::disconnect(&m_testPlanRepository, nullptr, this, nullptr);
    QObject::disconnect(&m_replay, nullptr, this, nullptr);

    // Do not mutate QQuick3D geometry during application teardown. The QML
    // engine and render resources have already been released at this point.
    m_sessionSampleTimer.stop();
    m_connectionTimeout.stop();
    m_healthTimeout.stop();
    m_missionMonitorTimer.stop();
    m_sessionRecorder.finishSession(QStringLiteral("程序退出"));
}

void AppController::finalizeSensorSessionSummary()
{
    SensorSessionFinalizer::finalize(selectedSensorType(), m_sensorService,
        m_sessionRecorder, m_lidarMapping, m_depthMapping, m_visualSurvey,
        m_gimbalInspection);
}

void AppController::refresh()
{
    m_connectionTimedOut = false;
    m_healthTimedOut = false;
    m_commandIssue = false;
    m_commandIssueMessage.clear();
    updateIssueState();
    m_vehicle.start();
    m_docker.probe();
    m_sensorService.probe();
}

bool AppController::startSimulation()
{
    return m_runCoordinator.start(
        [this] { return replayActive(); },
        [this] { return containerNameAvailable(); },
        [this] { return preflightSummary(); },
        [this] {
        m_connectionTimedOut = false;
        m_healthTimedOut = false;
        m_commandIssue = false;
        m_commandIssueMessage.clear();
        updateIssueState();
        m_vehicle.start();
        const QString worldAssetsDirectory =
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("simulation"));
        const QVariantMap sensorSettings = selectedTestPlan()
            .value(QStringLiteral("sensorSettings")).toMap();
        m_docker.startSimulation(m_selectedModelId,
                                 m_selectedWorldId,
                                 worldAssetsDirectory,
                                 m_containerName,
                                 sensorSettings.value(QStringLiteral("performance"),
                                                      QStringLiteral("standard")).toString());
        });
}

void AppController::stopSimulation()
{
    m_runCoordinator.stop();
}

void AppController::abortTest()
{
    if (!simulationRunning()) return;
    m_sessionEndReason = QStringLiteral("用户中止测试");
    appendDiagnostic(QStringLiteral("测试"), m_sessionEndReason);
    if (vehicleConnected() && (inAir() || armed())) {
        landAndStopSimulation();
    } else {
        stopSimulation();
    }
}

void AppController::landAndStopSimulation()
{
    m_runCoordinator.landAndStop();
}

void AppController::takeoff()
{
    m_sessionRecorder.markTakeoffRequested();
    m_vehicle.takeoff();
}

void AppController::land()
{
    m_sessionRecorder.markLandingRequested();
    m_vehicle.land();
}

void AppController::startKeyboardControl()
{
    m_vehicle.startKeyboardControl();
}

void AppController::stopKeyboardControl()
{
    m_vehicle.stopKeyboardControl();
}

void AppController::releaseKeyboardControl()
{
    m_vehicle.releaseKeyboardControl();
}

void AppController::heartbeatKeyboardControl()
{
    m_vehicle.heartbeatKeyboardControl();
}

void AppController::turnKeyboardControl(double degrees)
{
    m_vehicle.turnKeyboardControl(degrees);
}

void AppController::setKeyboardControlAxes(double forward,
                                           double right,
                                           double up,
                                           double yaw)
{
    m_vehicle.setKeyboardControlAxes(forward, right, up, yaw);
}

void AppController::uploadAndStartMission(const QVariantList &waypoints,
                                           const QString &endAction,
                                           const QString &routeId,
                                           const QString &routeName)
{
    appendDiagnostic(QStringLiteral("任务"),
                     QStringLiteral("请求上传 %1 个航点，结束动作 %2")
                         .arg(waypoints.size())
                         .arg(endAction));

    if (!m_sceneOriginAvailable || !m_vehicle.localPositionAvailable()) {
        appendDiagnostic(QStringLiteral("任务"),
                         QStringLiteral("上传被阻止：尚未建立稳定的本地坐标原点"));
        return;
    }

    // MissionPlanner stores positions in scene coordinates. The scene origin is
    // fixed when local telemetry first becomes available, whereas MAVSDK expects
    // offsets from the vehicle's current global position at upload time. Convert
    // every scene waypoint into a current-position-relative NED offset so repeated
    // uploads cannot translate the entire route as the vehicle moves.
    const double currentNorth = m_vehicle.localNorthMeters();
    const double currentEast = m_vehicle.localEastMeters();
    resetMissionMonitor();
    m_missionStartScenePosition = m_vehicleScenePosition;
    m_missionEndAction = endAction;
    QVariantList relativeWaypoints;
    relativeWaypoints.reserve(waypoints.size());
    for (const QVariant &waypoint : waypoints) {
        QVariantMap converted = waypoint.toMap();
        m_missionWaypointPayloads.append(converted);
        bool northOk = false;
        bool eastOk = false;
        const double sceneNorth = converted.value(QStringLiteral("northMeters"))
                                      .toDouble(&northOk);
        const double sceneEast = converted.value(QStringLiteral("eastMeters"))
                                     .toDouble(&eastOk);
        const double altitude = converted.value(QStringLiteral("altitudeMeters"),
                                                takeoffAltitude()).toDouble();
        const double speed = converted.value(QStringLiteral("speedMetersPerSecond"), 3.0)
                                 .toDouble();
        if (northOk && eastOk) {
            m_missionSceneWaypoints.append(
                QVector3D(static_cast<float>(sceneEast),
                          static_cast<float>(altitude),
                          static_cast<float>(-sceneNorth)));
            m_missionWaypointSpeeds.append(std::max(1.0, speed));
        }
        if (northOk) {
            converted.insert(QStringLiteral("northMeters"),
                             m_sceneOriginNorthMeters + sceneNorth - currentNorth);
        }
        if (eastOk) {
            converted.insert(QStringLiteral("eastMeters"),
                             m_sceneOriginEastMeters + sceneEast - currentEast);
        }
        relativeWaypoints.append(converted);
    }

    if (!relativeWaypoints.isEmpty()) {
        const QVariantMap first = relativeWaypoints.constFirst().toMap();
        appendDiagnostic(
            QStringLiteral("任务"),
            QStringLiteral("坐标基准已固定：当前 东 %1 / 北 %2 米，首航点相对当前 东 %3 / 北 %4 米")
                .arg(QString::number(currentEast - m_sceneOriginEastMeters, 'f', 1),
                     QString::number(currentNorth - m_sceneOriginNorthMeters, 'f', 1),
                     QString::number(first.value(QStringLiteral("eastMeters")).toDouble(), 'f', 1),
                     QString::number(first.value(QStringLiteral("northMeters")).toDouble(), 'f', 1)));
    }
    if (m_missionSceneWaypoints.size() != waypoints.size()) {
        resetMissionMonitor();
        appendDiagnostic(QStringLiteral("任务"),
                         QStringLiteral("上传被阻止：航点坐标不完整"));
        return;
    }
    m_missionMonitorState = QStringLiteral("preparing");
    m_missionMonitorMessage = QStringLiteral("正在准备任务监控");
    emit missionMonitorChanged();

    QVariantList missionSnapshot;
    missionSnapshot.reserve(waypoints.size());
    const double startEast = m_vehicleScenePosition.x();
    const double startNorth = -m_vehicleScenePosition.z();
    for (const QVariant &entry : waypoints) {
        QVariantMap point = entry.toMap();
        point.insert(QStringLiteral("eastMeters"),
                     point.value(QStringLiteral("eastMeters")).toDouble() - startEast);
        point.insert(QStringLiteral("northMeters"),
                     point.value(QStringLiteral("northMeters")).toDouble() - startNorth);
        missionSnapshot.append(point);
    }
    m_sessionRecorder.setMissionPlan(routeId, routeName, missionSnapshot, endAction,
                                     m_missionStartScenePosition.x(),
                                     m_missionStartScenePosition.y(),
                                     m_missionStartScenePosition.z());
    m_vehicle.uploadAndStartMission(relativeWaypoints, endAction);
    updateMissionGimbalForProgress();
}

void AppController::pauseMission()
{
    m_vehicle.pauseMission();
}

void AppController::resumeMission()
{
    m_vehicle.resumeMission();
}

void AppController::clearMission()
{
    m_vehicle.clearMission();
    if (!missionRunning() && !missionPaused()) resetMissionMonitor();
}

void AppController::abortMissionToHold()
{
    if (!missionRunning() && !missionPaused() && !missionBusy()) return;
    m_missionProtectionTriggered = true;
    m_missionMonitorState = QStringLiteral("stopped");
    m_missionMonitorMessage = QStringLiteral("任务已由用户终止，等待悬停确认");
    m_sessionRecorder.updateMissionMetrics(m_missionMaximumCrossTrackErrorMeters,
                                           missionCurrentItem(),
                                           missionTotalItems(),
                                           QStringLiteral("用户终止任务"));
    appendDiagnostic(QStringLiteral("任务"), QStringLiteral("用户终止任务并请求悬停"));
    emit missionMonitorChanged();
    m_vehicle.abortMissionToHold();
}

void AppController::returnToLaunch()
{
    m_sessionRecorder.updateMissionMetrics(m_missionMaximumCrossTrackErrorMeters,
                                           missionCurrentItem(),
                                           missionTotalItems(),
                                           QStringLiteral("任务终止后返航"));
    appendDiagnostic(QStringLiteral("任务"), QStringLiteral("已请求飞行器返航"));
    m_vehicle.returnToLaunch();
}

void AppController::clearFlightPath()
{
    m_flightPathGeometry.clearPath();
    appendDiagnostic(QStringLiteral("三维"), QStringLiteral("已清除当前轨迹显示"));
}

void AppController::prepareFlightScenePreview()
{
    m_scenePreviewActive = true;
    m_flightPathGeometry.clearPath();
    const QVector<QVector3D> previewPath{
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(0.0f, 1.0f, -0.05f),
        QVector3D(0.1f, 2.4f, -0.25f),
        QVector3D(0.45f, 3.7f, -0.75f),
        QVector3D(1.1f, 4.6f, -1.55f),
        QVector3D(2.0f, 5.0f, -2.6f),
        QVector3D(3.1f, 5.15f, -3.8f),
    };
    for (const QVector3D &position : previewPath) {
        m_flightPathGeometry.appendPoint(position);
    }
    m_vehicleScenePosition = previewPath.constLast();
    emit localPositionChanged();
    emit vehicleScenePositionChanged();
}

void AppController::resetFlightScene()
{
    m_sceneOriginAvailable = false;
    m_sceneOriginNorthMeters = 0.0;
    m_sceneOriginEastMeters = 0.0;
    m_sceneOriginDownMeters = 0.0;
    m_vehicleScenePosition = QVector3D();
    m_mappingPoseHistory.clear();
    if (m_preferences.flightPathAutoClear()) m_flightPathGeometry.clearPath();
    emit vehicleScenePositionChanged();
}

void AppController::updateFlightScenePosition()
{
    if (!m_vehicle.localPositionAvailable()) return;

    if (!m_sceneOriginAvailable) {
        m_sceneOriginAvailable = true;
        m_sceneOriginNorthMeters = m_vehicle.localNorthMeters();
        m_sceneOriginEastMeters = m_vehicle.localEastMeters();
        m_sceneOriginDownMeters = m_vehicle.localDownMeters();
    }

    const QVector3D position(
        static_cast<float>(m_vehicle.localEastMeters() - m_sceneOriginEastMeters),
        static_cast<float>(-(m_vehicle.localDownMeters() - m_sceneOriginDownMeters)),
        static_cast<float>(-(m_vehicle.localNorthMeters() - m_sceneOriginNorthMeters)));
    if (m_vehicleScenePosition != position) {
        m_vehicleScenePosition = position;
        emit vehicleScenePositionChanged();
    }
    appendMappingPoseSample();
    if (armed() || inAir()) m_flightPathGeometry.appendPoint(position);
}

void AppController::appendMappingPoseSample()
{
    if (!m_sceneOriginAvailable || !m_vehicle.localPositionAvailable()) return;
    const qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
    MappingPoseSample sample;
    sample.timestampMs = timestampMs;
    sample.position = m_vehicleScenePosition;
    sample.headingDegrees = headingDegrees();
    sample.rollDegrees = rollDegrees();
    sample.pitchDegrees = pitchDegrees();
    sample.altitudeMeters = altitudeMeters();
    sample.valid = localPositionAvailable() && localPositionOk();
    if (!m_mappingPoseHistory.isEmpty()
        && timestampMs <= m_mappingPoseHistory.constLast().timestampMs) {
        m_mappingPoseHistory.last() = sample;
    } else {
        m_mappingPoseHistory.append(sample);
    }
    constexpr qint64 HistoryWindowMs = 5000;
    while (m_mappingPoseHistory.size() > 2
           && timestampMs - m_mappingPoseHistory.constFirst().timestampMs
                  > HistoryWindowMs) {
        m_mappingPoseHistory.removeFirst();
    }
}

AppController::MappingPoseSample AppController::mappingPoseAt(qint64 timestampMs) const
{
    if (m_mappingPoseHistory.isEmpty()) {
        return {timestampMs, m_vehicleScenePosition, headingDegrees(), rollDegrees(),
                pitchDegrees(), altitudeMeters(),
                localPositionAvailable() && localPositionOk()};
    }
    if (timestampMs <= m_mappingPoseHistory.constFirst().timestampMs)
        return m_mappingPoseHistory.constFirst();
    if (timestampMs >= m_mappingPoseHistory.constLast().timestampMs) {
        MappingPoseSample latest = m_mappingPoseHistory.constLast();
        // A scan that is much newer than the latest telemetry cannot be paired
        // reliably. Keep the values for diagnostics but reject map insertion.
        if (timestampMs - latest.timestampMs > 750) latest.valid = false;
        return latest;
    }
    for (qsizetype index = 1; index < m_mappingPoseHistory.size(); ++index) {
        const MappingPoseSample &after = m_mappingPoseHistory.at(index);
        if (after.timestampMs < timestampMs) continue;
        const MappingPoseSample &before = m_mappingPoseHistory.at(index - 1);
        const double span = std::max<qint64>(1, after.timestampMs - before.timestampMs);
        const double factor = std::clamp((timestampMs - before.timestampMs) / span,
                                         0.0, 1.0);
        MappingPoseSample interpolated;
        interpolated.timestampMs = timestampMs;
        interpolated.position = before.position * static_cast<float>(1.0 - factor)
                                + after.position * static_cast<float>(factor);
        const double headingDelta = std::remainder(
            after.headingDegrees - before.headingDegrees, 360.0);
        interpolated.headingDegrees = before.headingDegrees + headingDelta * factor;
        interpolated.rollDegrees = before.rollDegrees
                                   + (after.rollDegrees - before.rollDegrees) * factor;
        interpolated.pitchDegrees = before.pitchDegrees
                                    + (after.pitchDegrees - before.pitchDegrees) * factor;
        interpolated.altitudeMeters = before.altitudeMeters
                                      + (after.altitudeMeters - before.altitudeMeters) * factor;
        interpolated.valid = before.valid && after.valid;
        return interpolated;
    }
    return m_mappingPoseHistory.constLast();
}

void AppController::resetMissionMonitor()
{
    m_missionSceneWaypoints.clear();
    m_missionWaypointSpeeds.clear();
    m_missionWaypointPayloads.clear();
    m_lastGimbalMissionTargetIndex = -1;
    m_lastGimbalCaptureIndex = -1;
    m_pendingGimbalCaptureIndex = -1;
    m_pendingGimbalCaptureFrameSequence = 0;
    m_pendingGimbalCaptureDeadlineMs = 0;
    m_missionEndAction.clear();
    m_missionStartScenePosition = QVector3D();
    m_missionDistanceToNextMeters = 0.0;
    m_missionCrossTrackErrorMeters = 0.0;
    m_missionRemainingDistanceMeters = 0.0;
    m_missionMaximumCrossTrackErrorMeters = 0.0;
    m_missionBestDistanceToNextMeters = 0.0;
    m_missionEstimatedSeconds = 0;
    m_missionDeviationDurationMs = 0;
    m_missionTelemetryMissingDurationMs = 0;
    m_missionUnexpectedModeDurationMs = 0;
    m_missionStalledDurationMs = 0;
    m_missionLastProgressItem = -1;
    m_missionProtectionTriggered = false;
    m_missionMonitorState = QStringLiteral("idle");
    m_missionMonitorMessage.clear();
    emit missionMonitorChanged();
}

void AppController::updateMissionGimbalForProgress()
{
    if (m_gimbalInspection.active() || m_gimbalInspection.picking()) {
        m_pendingGimbalCaptureIndex = -1;
        return;
    }
    if (selectedSensorType() != QStringLiteral("gimbal-camera")
        || !m_sensorService.running() || m_missionWaypointPayloads.isEmpty()) {
        return;
    }

    const int total = static_cast<int>(m_missionWaypointPayloads.size());
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_pendingGimbalCaptureIndex >= 0) {
        const bool freshFrame = m_sensorService.rgbFrameSequence()
                                > m_pendingGimbalCaptureFrameSequence;
        const bool angleReady = m_sensorService.gimbalFeedbackAvailable()
                                && !m_sensorService.gimbalMoving();
        if (freshFrame && angleReady) {
            const int captureIndex = m_pendingGimbalCaptureIndex;
            const QVariantMap completed = m_missionWaypointPayloads.at(captureIndex).toMap();
            const bool saved = m_sensorService.saveSnapshot(QStringLiteral("rgb"));
            appendDiagnostic(QStringLiteral("云台"),
                             saved
                                 ? QStringLiteral("云台到位并收到新画面，已在航点 %1 自动截图")
                                       .arg(completed.value(QStringLiteral("name"),
                                                            captureIndex + 1).toString())
                                 : QStringLiteral("航点 %1 自动截图失败：新画面无法保存")
                                       .arg(captureIndex + 1));
            m_lastGimbalCaptureIndex = captureIndex;
            m_pendingGimbalCaptureIndex = -1;
        } else if (nowMs >= m_pendingGimbalCaptureDeadlineMs) {
            appendDiagnostic(QStringLiteral("云台"),
                             QStringLiteral("航点 %1 自动截图取消：4 秒内未同时获得到位反馈和新画面")
                                 .arg(m_pendingGimbalCaptureIndex + 1));
            m_lastGimbalCaptureIndex = m_pendingGimbalCaptureIndex;
            m_pendingGimbalCaptureIndex = -1;
        } else {
            return;
        }
    }
    const int current = std::clamp(missionCurrentItem(), 0, total);
    const int completedIndex = current >= total ? total - 1 : current - 1;
    if (completedIndex >= 0 && completedIndex > m_lastGimbalCaptureIndex) {
        const QVariantMap completed = m_missionWaypointPayloads.at(completedIndex).toMap();
        if (completed.value(QStringLiteral("capturePhoto"), false).toBool()) {
            m_pendingGimbalCaptureIndex = completedIndex;
            m_pendingGimbalCaptureFrameSequence = m_sensorService.rgbFrameSequence();
            m_pendingGimbalCaptureDeadlineMs = nowMs + 4000;
            return;
        }
        m_lastGimbalCaptureIndex = completedIndex;
    }

    if (current >= total || current == m_lastGimbalMissionTargetIndex) return;
    const QVariantMap target = m_missionWaypointPayloads.at(current).toMap();
    const double yaw = target.value(QStringLiteral("gimbalYawDegrees"), 0.0).toDouble();
    const double pitch = target.value(QStringLiteral("gimbalPitchDegrees"), -45.0).toDouble();
    if (m_sensorService.setGimbalAngles(yaw, pitch, 0.0)) {
        m_lastGimbalMissionTargetIndex = current;
        appendDiagnostic(QStringLiteral("云台"),
                         QStringLiteral("航点 %1 视角：水平 %2°，俯仰 %3°")
                             .arg(current + 1)
                             .arg(QString::number(yaw, 'f', 0),
                                  QString::number(pitch, 'f', 0)));
    }
}

void AppController::triggerMissionProtection(const QString &reason)
{
    if (m_missionProtectionTriggered) return;
    m_missionProtectionTriggered = true;
    m_missionMonitorState = QStringLiteral("stopped");
    m_missionMonitorMessage = reason + QStringLiteral("，任务已终止并请求悬停");
    m_sessionRecorder.updateMissionMetrics(m_missionMaximumCrossTrackErrorMeters,
                                           missionCurrentItem(),
                                           missionTotalItems(),
                                           reason);
    m_sessionRecorder.markIssue(reason);
    appendDiagnostic(QStringLiteral("任务保护"), m_missionMonitorMessage);
    emit missionMonitorChanged();
    m_vehicle.abortMissionToHold();
}

void AppController::updateMissionMonitor()
{
    if (selectedSensorType() == QStringLiteral("gimbal-camera"))
        updateMissionGimbalForProgress();
    if (m_missionSceneWaypoints.isEmpty()) return;

    const int total = missionTotalItems() > 0
                          ? missionTotalItems()
                          : m_missionSceneWaypoints.size();
    const int completed = std::clamp(missionCurrentItem(), 0, total);
    if (total > 0 && completed >= total) {
        m_missionDistanceToNextMeters = 0.0;
        m_missionRemainingDistanceMeters = 0.0;
        m_missionEstimatedSeconds = 0;
        m_missionMonitorState = QStringLiteral("completed");
        m_missionMonitorMessage = QStringLiteral("任务完成");
        m_sessionRecorder.updateMissionMetrics(m_missionMaximumCrossTrackErrorMeters,
                                               total,
                                               total);
        emit missionMonitorChanged();
        return;
    }

    if (!m_vehicle.localPositionAvailable()) {
        if (missionRunning()) {
            m_missionTelemetryMissingDurationMs += m_missionMonitorTimer.interval();
            m_missionMonitorState = QStringLiteral("warning");
            m_missionMonitorMessage = QStringLiteral("等待本地位置数据");
            if (m_missionTelemetryMissingDurationMs >= 2000) {
                triggerMissionProtection(QStringLiteral("本地位置数据中断"));
                return;
            }
        }
        emit missionMonitorChanged();
        return;
    }

    const int activeIndex = std::clamp(
        completed, 0, static_cast<int>(m_missionSceneWaypoints.size()) - 1);
    const QVector3D segmentStart = activeIndex == 0
                                       ? m_missionStartScenePosition
                                       : m_missionSceneWaypoints.at(activeIndex - 1);
    const QVector3D segmentEnd = m_missionSceneWaypoints.at(activeIndex);
    const QVector3D current = m_vehicleScenePosition;

    const auto distance3d = [](const QVector3D &a, const QVector3D &b) {
        const QVector3D delta = a - b;
        return std::sqrt(static_cast<double>(QVector3D::dotProduct(delta, delta)));
    };
    const double segmentEast = segmentEnd.x() - segmentStart.x();
    const double segmentNorth = segmentEnd.z() - segmentStart.z();
    const double segmentLengthSquared = segmentEast * segmentEast
                                        + segmentNorth * segmentNorth;
    double interpolation = 0.0;
    if (segmentLengthSquared > 0.0001) {
        interpolation = ((current.x() - segmentStart.x()) * segmentEast
                         + (current.z() - segmentStart.z()) * segmentNorth)
                        / segmentLengthSquared;
        interpolation = std::clamp(interpolation, 0.0, 1.0);
    }
    const double nearestEast = segmentStart.x() + interpolation * segmentEast;
    const double nearestNorth = segmentStart.z() + interpolation * segmentNorth;
    m_missionCrossTrackErrorMeters = std::hypot(current.x() - nearestEast,
                                                current.z() - nearestNorth);
    m_missionMaximumCrossTrackErrorMeters =
        std::max(m_missionMaximumCrossTrackErrorMeters,
                 m_missionCrossTrackErrorMeters);
    m_missionDistanceToNextMeters = distance3d(current, segmentEnd);
    m_missionRemainingDistanceMeters = m_missionDistanceToNextMeters;
    for (int index = activeIndex + 1; index < m_missionSceneWaypoints.size(); ++index) {
        m_missionRemainingDistanceMeters +=
            distance3d(m_missionSceneWaypoints.at(index - 1),
                       m_missionSceneWaypoints.at(index));
    }
    const double plannedSpeed = activeIndex < m_missionWaypointSpeeds.size()
                                    ? m_missionWaypointSpeeds.at(activeIndex)
                                    : 3.0;
    m_missionEstimatedSeconds = static_cast<int>(std::ceil(
        m_missionRemainingDistanceMeters / std::max(1.0, plannedSpeed)));
    m_sessionRecorder.updateMissionMetrics(m_missionCrossTrackErrorMeters,
                                           completed,
                                           total);

    if (completed != m_missionLastProgressItem) {
        m_missionLastProgressItem = completed;
        m_missionStalledDurationMs = 0;
        m_missionBestDistanceToNextMeters = m_missionDistanceToNextMeters;
    } else if (m_missionDistanceToNextMeters
               < m_missionBestDistanceToNextMeters - 0.5) {
        m_missionBestDistanceToNextMeters = m_missionDistanceToNextMeters;
        m_missionStalledDurationMs = 0;
    } else if (missionRunning()) {
        m_missionStalledDurationMs += m_missionMonitorTimer.interval();
    }

    if (!missionRunning()) {
        m_missionTelemetryMissingDurationMs = 0;
        m_missionUnexpectedModeDurationMs = 0;
        m_missionDeviationDurationMs = 0;
        if (m_missionProtectionTriggered) {
            m_missionMonitorState = QStringLiteral("stopped");
        } else if (missionPaused()) {
            m_missionMonitorState = QStringLiteral("paused");
            m_missionMonitorMessage = QStringLiteral("任务已暂停，飞行器保持当前位置");
        } else if (missionBusy()) {
            m_missionMonitorState = QStringLiteral("preparing");
            m_missionMonitorMessage = QStringLiteral("正在准备任务");
        }
        emit missionMonitorChanged();
        return;
    }

    const bool telemetryHealthy = vehicleConnected() && telemetryAvailable();
    m_missionTelemetryMissingDurationMs = telemetryHealthy
                                              ? 0
                                              : m_missionTelemetryMissingDurationMs
                                                    + m_missionMonitorTimer.interval();

    const bool finalAutomaticAction = completed >= total - 1
                                      && (m_missionEndAction == QStringLiteral("land")
                                          || m_missionEndAction == QStringLiteral("rtl"));
    const bool expectedMode = flightMode() == QStringLiteral("任务")
                              || (finalAutomaticAction
                                  && (flightMode() == QStringLiteral("降落")
                                      || flightMode() == QStringLiteral("返航")));
    m_missionUnexpectedModeDurationMs = expectedMode
                                            ? 0
                                            : m_missionUnexpectedModeDurationMs
                                                  + m_missionMonitorTimer.interval();

    m_missionDeviationDurationMs = m_missionCrossTrackErrorMeters > 6.0
                                       ? m_missionDeviationDurationMs
                                             + m_missionMonitorTimer.interval()
                                       : 0;

    if (m_missionTelemetryMissingDurationMs >= 2000) {
        triggerMissionProtection(QStringLiteral("飞行遥测中断"));
        return;
    }
    if (m_missionUnexpectedModeDurationMs >= 2000) {
        triggerMissionProtection(QStringLiteral("飞行器意外退出任务模式"));
        return;
    }
    if (m_missionDeviationDurationMs >= 3000) {
        triggerMissionProtection(
            QStringLiteral("航线偏差持续超过 6 米"));
        return;
    }

    if (m_missionCrossTrackErrorMeters > 3.0) {
        m_missionMonitorState = QStringLiteral("warning");
        m_missionMonitorMessage = QStringLiteral("偏离航线 %1 米，请留意")
                                      .arg(QString::number(m_missionCrossTrackErrorMeters,
                                                          'f', 1));
    } else if (m_missionStalledDurationMs >= 20000) {
        m_missionMonitorState = QStringLiteral("warning");
        m_missionMonitorMessage = QStringLiteral("位置推进缓慢，可能受阻");
    } else {
        m_missionMonitorState = QStringLiteral("normal");
        m_missionMonitorMessage = QStringLiteral("航线正常");
    }
    emit missionMonitorChanged();
}

void AppController::recover()
{
    if (m_recoveryAction == QStringLiteral("refresh")) {
        refresh();
        return;
    }
    if (m_recoveryAction == QStringLiteral("restart")) {
        m_connectionTimedOut = false;
        m_healthTimedOut = false;
        m_commandIssue = false;
        m_commandIssueMessage.clear();
        if (simulationRunning()) {
            m_runCoordinator.requestRestart();
            updateIssueState();
            if (vehicleConnected() && (inAir() || armed())) {
                landAndStopSimulation();
            } else {
                stopSimulation();
            }
        } else {
            startSimulation();
        }
    }
}

void AppController::tryCompleteLandingAndStop()
{
    m_runCoordinator.checkLanding();
}

void AppController::setLandingAndStopping(bool value)
{
    m_runCoordinator.setLandingAndStopping(value);
}

void AppController::setTakeoffAltitude(double value)
{
    m_vehicle.setTakeoffAltitude(value);
    m_preferences.saveTakeoffAltitude(m_vehicle.takeoffAltitude());
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("默认起飞高度设为 %1 米")
                         .arg(QString::number(m_vehicle.takeoffAltitude(), 'f', 1)));
}

void AppController::setControlSpeedMetersPerSecond(double value)
{
    const double previousValue = m_vehicle.controlSpeedMetersPerSecond();
    m_vehicle.setControlSpeedMetersPerSecond(value);
    if (qFuzzyCompare(previousValue, m_vehicle.controlSpeedMetersPerSecond())) return;
    m_preferences.saveHorizontalSpeed(m_vehicle.controlSpeedMetersPerSecond());
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("键盘水平速度设为 %1 m/s")
                         .arg(QString::number(m_vehicle.controlSpeedMetersPerSecond(), 'f', 1)));
}

void AppController::setFlightPathVisible(bool visible)
{
    if (!m_preferences.setFlightPathVisible(visible)) return;
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathWidthMeters(double value)
{
    const double previousValue = m_flightPathGeometry.widthMeters();
    m_flightPathGeometry.setWidthMeters(value);
    m_replay.pathGeometry()->setWidthMeters(m_flightPathGeometry.widthMeters());
    if (qFuzzyCompare(previousValue, m_flightPathGeometry.widthMeters())) return;
    m_preferences.saveFlightPathWidth(m_flightPathGeometry.widthMeters());
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathColorPreset(const QString &preset)
{
    if (!m_preferences.setFlightPathColorPreset(preset)) return;
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathMaxVisibleLengthMeters(int value)
{
    const int previousValue = m_flightPathGeometry.maxVisibleLengthMeters();
    m_flightPathGeometry.setMaxVisibleLengthMeters(value);
    m_replay.pathGeometry()->setMaxVisibleLengthMeters(
        m_flightPathGeometry.maxVisibleLengthMeters());
    if (previousValue == m_flightPathGeometry.maxVisibleLengthMeters()) return;
    m_preferences.saveFlightPathLength(m_flightPathGeometry.maxVisibleLengthMeters());
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathAutoClear(bool value)
{
    if (!m_preferences.setFlightPathAutoClear(value)) return;
    emit flightPathDisplaySettingsChanged();
}

void AppController::setCameraRotationSensitivity(double value)
{
    if (!m_preferences.setCameraRotationSensitivity(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setCameraZoomSensitivity(double value)
{
    if (!m_preferences.setCameraZoomSensitivity(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setInvertCameraVertical(bool value)
{
    if (!m_preferences.setInvertCameraVertical(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setGroundMarkerVisible(bool value)
{
    if (!m_preferences.setGroundMarkerVisible(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setGroundMarkerSizeMode(const QString &value)
{
    if (!m_preferences.setGroundMarkerSizeMode(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setDefaultCameraMode(const QString &value)
{
    if (!m_preferences.setDefaultCameraMode(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setCityBuildingOpacity(double value)
{
    if (!m_preferences.setCityBuildingOpacity(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setWorldRoadsVisible(bool value)
{
    if (!m_preferences.setWorldRoadsVisible(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setWorldGreenAreasVisible(bool value)
{
    if (!m_preferences.setWorldGreenAreasVisible(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setWorldWaterVisible(bool value)
{
    if (!m_preferences.setWorldWaterVisible(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setDefaultGridVisible(bool value)
{
    if (!m_preferences.setDefaultGridVisible(value)) return;
    emit viewPreferencesChanged();
}

void AppController::setPreciseTurnDefaultDegrees(double value)
{
    if (!m_preferences.setPreciseTurnDefaultDegrees(value)) return;
    emit interactionPreferencesChanged();
}

void AppController::setTelemetryRecordingHz(int value)
{
    if (!m_preferences.setTelemetryRecordingHz(value)) return;
    m_sessionSampleTimer.setInterval(1000 / value);
    emit recordingPreferencesChanged();
}

void AppController::setAutoGenerateReports(bool value)
{
    if (!m_preferences.setAutoGenerateReports(value)) return;
    emit recordingPreferencesChanged();
}

void AppController::setTrashRetentionDays(int value)
{
    if (!m_preferences.setTrashRetentionDays(value)) return;
    if (value > 0) m_sessionRecorder.purgeTrashOlderThan(value);
    refreshSessionStorageUsage();
    emit recordingPreferencesChanged();
}

void AppController::resetFlightPathDisplaySettings()
{
    setFlightPathVisible(true);
    setFlightPathWidthMeters(0.06);
    setFlightPathColorPreset(QStringLiteral("orange"));
    setFlightPathMaxVisibleLengthMeters(100);
    setFlightPathAutoClear(true);
}

QVariantList AppController::simulationModels() const
{
    QVariantList models;
    for (const auto &definition : simulationModelDefinitions()) {
        if (!definition.visible) continue;
        QVariantMap model;
        model.insert(QStringLiteral("id"), definition.id);
        model.insert(QStringLiteral("name"), definition.name);
        model.insert(QStringLiteral("description"), definition.description);
        model.insert(QStringLiteral("recommended"), definition.recommended);
        model.insert(QStringLiteral("sensorType"), definition.sensorType);
        model.insert(QStringLiteral("sensorLabel"), definition.sensorLabel);
        model.insert(QStringLiteral("sensorServiceSupported"),
                     definition.sensorType == QStringLiteral("lidar-2d")
                         || definition.sensorType == QStringLiteral("mono-camera")
                         || definition.sensorType == QStringLiteral("depth-camera")
                         || definition.sensorType == QStringLiteral("gimbal-camera"));
        models.append(model);
    }
    return models;
}

QVariantList AppController::simulationWorlds() const
{
    QVariantList worlds;
    for (const auto &definition : simulationWorldDefinitions()) {
        QVariantMap world;
        world.insert(QStringLiteral("id"), definition.id);
        world.insert(QStringLiteral("name"), definition.name);
        world.insert(QStringLiteral("description"), definition.description);
        world.insert(QStringLiteral("sceneType"), definition.sceneType);
        world.insert(QStringLiteral("hasPreview"), !definition.previewModelDirectory.isEmpty());
        const QVariantMap map = WorldMapCatalog::metadata(definition.id);
        world.insert(QStringLiteral("hasMap"),
                     map.value(QStringLiteral("available")).toBool());
        world.insert(QStringLiteral("lightMapSource"),
                     map.value(QStringLiteral("lightSource")));
        world.insert(QStringLiteral("darkMapSource"),
                     map.value(QStringLiteral("darkSource")));
        world.insert(QStringLiteral("recommendedTakeoffClearanceMeters"),
                     map.value(QStringLiteral("recommendedTakeoffClearanceMeters")));
        world.insert(QStringLiteral("widthMeters"),
                     map.value(QStringLiteral("maxEastMeters")).toDouble()
                         - map.value(QStringLiteral("minEastMeters")).toDouble());
        world.insert(QStringLiteral("heightMeters"),
                     map.value(QStringLiteral("maxNorthMeters")).toDouble()
                         - map.value(QStringLiteral("minNorthMeters")).toDouble());
        worlds.append(world);
    }
    return worlds;
}

QString AppController::selectedModelName() const
{
    const auto *definition = findSimulationModel(m_selectedModelId);
    return definition ? definition->name : m_selectedModelId;
}

QString AppController::selectedModelDescription() const
{
    const auto *definition = findSimulationModel(m_selectedModelId);
    return definition ? definition->description : QString();
}

QString AppController::selectedWorldName() const
{
    const auto *definition = findSimulationWorld(m_selectedWorldId);
    return definition ? definition->name : m_selectedWorldId;
}

QString AppController::selectedWorldDescription() const
{
    const auto *definition = findSimulationWorld(m_selectedWorldId);
    return definition ? definition->description : QString();
}

QUrl AppController::selectedWorldPreviewDirectory() const
{
    const auto *definition = findSimulationWorld(m_selectedWorldId);
    if (!definition || definition->previewModelDirectory.isEmpty()) return {};
    const QString directory = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("simulation/models/%1")
                                                .arg(definition->previewModelDirectory));
    return QUrl::fromLocalFile(QDir::cleanPath(directory));
}

QString AppController::selectedSensorType() const
{
    const auto *definition = findSimulationModel(m_selectedModelId);
    return definition ? definition->sensorType : QStringLiteral("none");
}

QString AppController::selectedSensorLabel() const
{
    const auto *definition = findSimulationModel(m_selectedModelId);
    return definition ? definition->sensorLabel : QStringLiteral("无额外载荷");
}

bool AppController::sensorServiceSupported() const
{
    const QString type = selectedSensorType();
    return type == QStringLiteral("lidar-2d")
           || type == QStringLiteral("mono-camera")
           || type == QStringLiteral("depth-camera")
           || type == QStringLiteral("gimbal-camera");
}

bool AppController::saveCameraSnapshot(const QString &stream)
{
    if (!replayActive()) {
        m_lastReplayCapturePath.clear();
        return m_sensorService.saveSnapshot(stream);
    }
    QString savedPath;
    const bool saved = m_replay.saveCurrentCameraFrame(
        stream, m_sensorService.capturesDirectory(), &savedPath);
    if (saved) {
        m_lastReplayCapturePath = savedPath;
        emit sensorCaptureSaved();
        useStatus(QStringLiteral("历史画面已保存到 Captures 文件夹"));
    } else {
        useStatus(QStringLiteral("当前回放时刻没有可保存的相机画面"));
    }
    return saved;
}

bool AppController::openSensorCapturesFolder()
{
    QDir directory(m_sensorService.capturesDirectory());
    if (!directory.mkpath(QStringLiteral("."))) {
        useStatus(QStringLiteral("无法创建传感器文件保存目录"));
        return false;
    }
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath()));
    useStatus(opened ? QStringLiteral("已打开 Captures 文件夹")
                     : QStringLiteral("无法打开 Captures 文件夹"));
    return opened;
}

QString AppController::saveLidarMap()
{
    if (!m_lidarMapping.available()) {
        useStatus(QStringLiteral("当前还没有可保存的累计雷达地图"));
        return {};
    }
    const QString directory = StoragePaths::directory(QStringLiteral("LidarMaps"));
    const QString folder = QDir(directory).filePath(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QVariantMap result = m_lidarMapping.saveToDirectory(folder);
    if (!result.value(QStringLiteral("saved")).toBool()) {
        useStatus(result.value(QStringLiteral("error"),
                               QStringLiteral("雷达地图保存失败")).toString());
        return {};
    }
    useStatus(QStringLiteral("累计雷达地图已保存"));
    return QDir::toNativeSeparators(folder);
}

QString AppController::saveDepthMap()
{
    if (!m_depthMapping.available()) {
        useStatus(QStringLiteral("当前还没有经过重复确认的三维障碍地图"));
        return {};
    }
    const QString directory = StoragePaths::directory(QStringLiteral("DepthMaps"));
    const QString folder = QDir(directory).filePath(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QVariantMap result = m_depthMapping.saveToDirectory(folder);
    if (!result.value(QStringLiteral("saved")).toBool()) {
        useStatus(result.value(QStringLiteral("error"),
                               QStringLiteral("三维障碍地图保存失败")).toString());
        return {};
    }
    useStatus(QStringLiteral("DepthFusion 地图已保存为点云、CSV、OBJ 网格和俯视预览"));
    return QDir::toNativeSeparators(folder);
}

bool AppController::loadSessionDepthMap(const QString &sessionId)
{
    if (sessionId.trimmed().isEmpty() || m_sessionRecorder.active()) {
        useStatus(QStringLiteral("飞行记录进行中，暂时不能打开历史三维地图"));
        return false;
    }
    const QString sessionFolder = m_sessionRecorder.folderPathForSession(sessionId);
    if (sessionFolder.isEmpty()) {
        useStatus(QStringLiteral("找不到所选会话目录"));
        return false;
    }
    const QString mapFolder = QDir(sessionFolder).filePath(QStringLiteral("sensor/depth-map"));
    if (!m_depthMapping.loadMap(mapFolder)) {
        useStatus(QStringLiteral("这条会话没有可读取的 DepthFusion 地图"));
        return false;
    }
    useStatus(QStringLiteral("已直接加载会话保存的三维地图，无需重新播放"));
    return true;
}

void AppController::closeLoadedDepthMap()
{
    if (m_depthMapping.loadedFromDisk()) m_depthMapping.clear();
}

bool AppController::startVisualSurvey()
{
    if (selectedSensorType() != QStringLiteral("mono-camera")) {
        useStatus(QStringLiteral("视觉巡检影像地图仅用于单目相机机型"));
        return false;
    }
    if (!m_sessionRecorder.active() || !m_sensorService.running()) {
        useStatus(QStringLiteral("启动仿真和相机数据服务后才能开始视觉巡检"));
        return false;
    }
    const bool started = m_visualSurvey.start();
    useStatus(started ? QStringLiteral("视觉巡检已开始，将按位置、方向和画面质量自动保存关键帧")
                      : m_visualSurvey.statusMessage());
    return started;
}

void AppController::stopVisualSurvey()
{
    m_visualSurvey.stop();
    useStatus(QStringLiteral("视觉巡检已暂停，已保存的关键帧不会丢失"));
}

bool AppController::loadSessionVisualSurvey(const QString &sessionId)
{
    if (m_sessionRecorder.active() || simulationRunning()) {
        useStatus(QStringLiteral("停止当前仿真后才能打开历史影像地图"));
        return false;
    }
    const QString folder = m_sessionRecorder.folderPathForSession(sessionId);
    if (folder.isEmpty() || !m_visualSurvey.loadFromSession(folder)) {
        useStatus(QStringLiteral("这条会话没有可读取的视觉巡检影像地图"));
        return false;
    }
    m_loadedVisualSurveySessionId = sessionId;
    useStatus(QStringLiteral("已加载会话保存的视觉巡检影像地图"));
    return true;
}

void AppController::closeLoadedVisualSurvey()
{
    if (m_visualSurvey.historical()) m_visualSurvey.clear();
    m_loadedVisualSurveySessionId.clear();
}

bool AppController::openVisualSurveyFolder()
{
    const QVariantMap frame = m_visualSurvey.selectedKeyframe();
    const QString source = frame.value(QStringLiteral("source")).toUrl().toLocalFile();
    const QString path = source.isEmpty() ? QString() : QFileInfo(source).absolutePath();
    if (path.isEmpty()) {
        useStatus(QStringLiteral("当前没有可打开的影像地图目录"));
        return false;
    }
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    useStatus(opened ? QStringLiteral("已打开视觉巡检关键帧目录")
                     : QStringLiteral("无法打开视觉巡检关键帧目录"));
    return opened;
}

bool AppController::exportVisualSurveyKeyframe(int index)
{
    m_visualSurvey.setSelectedIndex(index);
    const QVariantMap frame = m_visualSurvey.selectedKeyframe();
    const QString source = frame.value(QStringLiteral("source")).toUrl().toLocalFile();
    if (source.isEmpty() || !QFileInfo::exists(source)) {
        useStatus(QStringLiteral("找不到所选关键帧原图"));
        return false;
    }
    const QString destinationDirectory = m_sensorService.capturesDirectory();
    QDir().mkpath(destinationDirectory);
    const QString stem = QStringLiteral("survey-%1-%2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
        .arg(index + 1);
    QString destination = QDir(destinationDirectory).filePath(stem + QStringLiteral(".jpg"));
    int suffix = 2;
    while (QFileInfo::exists(destination))
        destination = QDir(destinationDirectory).filePath(
            QStringLiteral("%1-%2.jpg").arg(stem).arg(suffix++));
    const bool copied = QFile::copy(source, destination);
    useStatus(copied ? QStringLiteral("关键帧原图已导出到 Captures 文件夹")
                     : QStringLiteral("关键帧导出失败，请检查目标目录权限"));
    return copied;
}

bool AppController::updateVisualSurveyKeyframe(const QString &notes, bool anomaly)
{
    if (!m_visualSurvey.updateSelected(notes, anomaly)) {
        useStatus(QStringLiteral("视觉巡检备注保存失败"));
        return false;
    }
    if (!m_loadedVisualSurveySessionId.isEmpty()) {
        QVariantMap details = m_sessionRecorder.sessionDetails(m_loadedVisualSurveySessionId);
        QVariantMap sensor = details.value(QStringLiteral("sensorRecording")).toMap();
        sensor.insert(QStringLiteral("visualSurveyAvailable"), m_visualSurvey.available());
        sensor.insert(QStringLiteral("visualSurvey"), m_visualSurvey.finishSession());
        if (!m_sessionRecorder.updateSessionSensorSummary(m_loadedVisualSurveySessionId, sensor)) {
            useStatus(QStringLiteral("原图备注已保存，但会话摘要更新失败"));
            return false;
        }
    }
    useStatus(QStringLiteral("关键帧备注与异常标记已保存"));
    return true;
}

bool AppController::openDepthMapsFolder()
{
    const QString directory = StoragePaths::directory(QStringLiteral("DepthMaps"));
    QDir().mkpath(directory);
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    useStatus(opened ? QStringLiteral("已打开三维障碍地图文件夹")
                     : QStringLiteral("无法打开三维障碍地图保存位置"));
    return opened;
}

bool AppController::openLidarMapsFolder()
{
    const QString directory = StoragePaths::directory(QStringLiteral("LidarMaps"));
    QDir().mkpath(directory);
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    useStatus(opened ? QStringLiteral("已打开雷达地图文件夹")
                     : QStringLiteral("无法打开雷达地图保存位置"));
    return opened;
}

bool AppController::setGimbalAngles(double yawDegrees, double pitchDegrees)
{
    if (replayActive()) return false;
    m_gimbalInspection.pause();
    m_pendingGimbalCaptureIndex = -1;
    return m_sensorService.setGimbalAngles(yawDegrees, pitchDegrees, 0.0);
}

bool AppController::setGimbalPreset(const QString &preset)
{
    if (replayActive()) return false;
    m_gimbalInspection.pause();
    m_pendingGimbalCaptureIndex = -1;
    if (preset == QStringLiteral("front"))
        return m_sensorService.setGimbalAngles(0.0, 0.0, 0.0);
    if (preset == QStringLiteral("forward-down"))
        return m_sensorService.setGimbalAngles(0.0, -45.0, 0.0);
    if (preset == QStringLiteral("down"))
        return m_sensorService.setGimbalAngles(0.0, -90.0, 0.0);
    if (preset == QStringLiteral("left"))
        return m_sensorService.setGimbalAngles(-90.0, -20.0, 0.0);
    if (preset == QStringLiteral("right"))
        return m_sensorService.setGimbalAngles(90.0, -20.0, 0.0);
    useStatus(QStringLiteral("未知的云台预设视角"));
    return false;
}

void AppController::setControlVerticalSpeedMetersPerSecond(double value)
{
    const double previousValue = m_vehicle.controlVerticalSpeedMetersPerSecond();
    m_vehicle.setControlVerticalSpeedMetersPerSecond(value);
    if (qFuzzyCompare(previousValue, m_vehicle.controlVerticalSpeedMetersPerSecond())) return;
    m_preferences.saveVerticalSpeed(m_vehicle.controlVerticalSpeedMetersPerSecond());
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("键盘垂直速度设为 %1 m/s")
                         .arg(QString::number(m_vehicle.controlVerticalSpeedMetersPerSecond(), 'f', 1)));
}

void AppController::setControlYawRateDegreesPerSecond(double value)
{
    const double previousValue = m_vehicle.controlYawRateDegreesPerSecond();
    m_vehicle.setControlYawRateDegreesPerSecond(value);
    if (qFuzzyCompare(previousValue, m_vehicle.controlYawRateDegreesPerSecond())) return;
    m_preferences.saveYawRate(m_vehicle.controlYawRateDegreesPerSecond());
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("键盘转向速度设为 %1°/s")
                         .arg(QString::number(m_vehicle.controlYawRateDegreesPerSecond(), 'f', 0)));
}

QVariantMap AppController::selectedWorldMap() const
{
    return WorldMapCatalog::metadata(m_selectedWorldId);
}

QVariantList AppController::testPlans() const
{
    QVariantList enrichedPlans;
    for (const QVariant &entry : m_testPlanRepository.plans()) {
        QVariantMap plan = entry.toMap();
        const QString modelId = plan.value(QStringLiteral("modelId")).toString();
        const QString worldId = plan.value(QStringLiteral("worldId")).toString();
        const auto *model = findSimulationModel(modelId);
        const auto *world = findSimulationWorld(worldId);
        plan.insert(QStringLiteral("modelName"), model ? model->name : modelId);
        plan.insert(QStringLiteral("worldName"), world ? world->name : worldId);

        const QString routeId = plan.value(QStringLiteral("routeId")).toString();
        QString routeName;
        for (const QVariant &routeEntry : m_routeLibrary.routes()) {
            const QVariantMap route = routeEntry.toMap();
            if (route.value(QStringLiteral("id")).toString() == routeId) {
                routeName = route.value(QStringLiteral("name")).toString();
                break;
            }
        }
        const bool requiresRoute = plan.value(QStringLiteral("requiresRoute")).toBool();
        plan.insert(QStringLiteral("routeName"), routeName);
        plan.insert(QStringLiteral("routeAvailable"), !routeId.isEmpty() && !routeName.isEmpty());
        plan.insert(QStringLiteral("complete"), model && world
                    && (!requiresRoute || (!routeId.isEmpty() && !routeName.isEmpty())));
        const QString type = plan.value(QStringLiteral("type")).toString();
        plan.insert(QStringLiteral("typeLabel"),
                    type == QStringLiteral("mission") ? QStringLiteral("航点任务")
                                                       : QStringLiteral("自由飞行"));
        const QString preset = plan.value(QStringLiteral("acceptancePreset")).toString();
        plan.insert(QStringLiteral("acceptancePresetLabel"),
                    preset == QStringLiteral("strict") ? QStringLiteral("严格检查")
                    : preset == QStringLiteral("custom") ? QStringLiteral("自定义检查")
                                                          : QStringLiteral("标准检查"));
        enrichedPlans.append(plan);
    }
    return enrichedPlans;
}

QVariantMap AppController::selectedTestPlan() const
{
    QVariantMap plan = m_testPlanRepository.plan(m_selectedTestPlanId);
    if (plan.isEmpty()) return {};

    const QString modelId = plan.value(QStringLiteral("modelId")).toString();
    const QString worldId = plan.value(QStringLiteral("worldId")).toString();
    const auto *model = findSimulationModel(modelId);
    const auto *world = findSimulationWorld(worldId);
    plan.insert(QStringLiteral("modelName"), model ? model->name : modelId);
    plan.insert(QStringLiteral("worldName"), world ? world->name : worldId);

    const QString routeId = plan.value(QStringLiteral("routeId")).toString();
    QString routeName;
    for (const QVariant &entry : m_routeLibrary.routes()) {
        const QVariantMap route = entry.toMap();
        if (route.value(QStringLiteral("id")).toString() == routeId) {
            routeName = route.value(QStringLiteral("name")).toString();
            plan.insert(QStringLiteral("routeWaypointCount"),
                        route.value(QStringLiteral("waypointCount"), 0));
            plan.insert(QStringLiteral("routeDistanceMeters"),
                        route.value(QStringLiteral("distanceMeters"), 0));
            break;
        }
    }
    const bool requiresRoute = plan.value(QStringLiteral("requiresRoute")).toBool();
    const bool routeAvailable = !routeId.isEmpty() && !routeName.isEmpty();
    plan.insert(QStringLiteral("routeName"), routeName);
    plan.insert(QStringLiteral("routeAvailable"), routeAvailable);
    plan.insert(QStringLiteral("complete"), model && world && (!requiresRoute || routeAvailable));

    const QString type = plan.value(QStringLiteral("type")).toString();
    plan.insert(QStringLiteral("typeLabel"),
                type == QStringLiteral("mission") ? QStringLiteral("航点任务")
                                                   : QStringLiteral("自由飞行"));
    const QString preset = plan.value(QStringLiteral("acceptancePreset")).toString();
    plan.insert(QStringLiteral("acceptancePresetLabel"),
                preset == QStringLiteral("strict") ? QStringLiteral("严格检查")
                : preset == QStringLiteral("custom") ? QStringLiteral("自定义检查")
                                                      : QStringLiteral("标准检查"));
    const QString endAction = plan.value(QStringLiteral("endAction")).toString();
    plan.insert(QStringLiteral("endActionLabel"),
                endAction == QStringLiteral("land") ? QStringLiteral("自动降落")
                : endAction == QStringLiteral("rtl") ? QStringLiteral("返航")
                                                      : QStringLiteral("悬停"));
    return plan;
}

bool AppController::selectedTestPlanModified() const
{
    const QVariantMap plan = m_testPlanRepository.plan(m_selectedTestPlanId);
    if (plan.isEmpty()) return false;
    return plan.value(QStringLiteral("modelId")).toString() != m_selectedModelId
           || plan.value(QStringLiteral("worldId")).toString() != m_selectedWorldId
           || std::abs(plan.value(QStringLiteral("takeoffAltitudeMeters"), 5.0).toDouble()
                       - takeoffAltitude()) > 0.05
           || std::abs(plan.value(QStringLiteral("controlSettings")).toMap()
                           .value(QStringLiteral("horizontalSpeedMetersPerSecond"), 1.0).toDouble()
                       - controlSpeedMetersPerSecond()) > 0.05
           || std::abs(plan.value(QStringLiteral("controlSettings")).toMap()
                           .value(QStringLiteral("verticalSpeedMetersPerSecond"), 0.5).toDouble()
                       - controlVerticalSpeedMetersPerSecond()) > 0.05
           || std::abs(plan.value(QStringLiteral("controlSettings")).toMap()
                           .value(QStringLiteral("yawRateDegreesPerSecond"), 30.0).toDouble()
                       - controlYawRateDegreesPerSecond()) > 0.5;
}

QVariantList AppController::testPlanSteps() const
{
    const QVariantMap plan = selectedTestPlan();
    const bool planComplete = !plan.isEmpty()
                              && plan.value(QStringLiteral("complete")).toBool();
    const bool sensorReady = !m_sensorServiceEnabled || !sensorServiceSupported()
                             || sensorServiceImageAvailable();
    const bool environmentAvailable = dockerAvailable() && imageAvailable()
                                      && dockerNetworkAvailable() && sensorReady;

    auto step = [](const QString &id, const QString &label,
                   const QString &value, const QString &state) {
        return QVariantMap{{QStringLiteral("id"), id},
                           {QStringLiteral("label"), label},
                           {QStringLiteral("value"), value},
                           {QStringLiteral("state"), state}};
    };

    QVariantList steps;
    steps.append(step(QStringLiteral("plan"), QStringLiteral("测试方案"),
                      planComplete ? QStringLiteral("已完整") : QStringLiteral("需完善"),
                      planComplete ? QStringLiteral("complete") : QStringLiteral("problem")));

    QString environmentValue;
    QString environmentState;
    if (simulationRunning()) {
        environmentValue = QStringLiteral("运行中");
        environmentState = QStringLiteral("complete");
    } else if (!environmentAvailable) {
        environmentValue = !dockerAvailable() ? QStringLiteral("Docker 未连接")
                           : !imageAvailable() ? QStringLiteral("PX4 镜像未找到")
                           : !dockerNetworkAvailable() ? QStringLiteral("容器网络未就绪")
                                                       : QStringLiteral("ROS 镜像未找到");
        environmentState = planComplete ? QStringLiteral("problem")
                                        : QStringLiteral("pending");
    } else {
        environmentValue = QStringLiteral("可启动");
        environmentState = planComplete ? QStringLiteral("current")
                                        : QStringLiteral("pending");
    }
    steps.append(step(QStringLiteral("environment"), QStringLiteral("仿真环境"),
                      environmentValue, environmentState));

    steps.append(step(QStringLiteral("connection"), QStringLiteral("飞行器连接"),
                      vehicleConnected() ? QStringLiteral("已连接")
                                         : simulationRunning() ? QStringLiteral("等待中")
                                                               : QStringLiteral("未启动"),
                      vehicleConnected() ? QStringLiteral("complete")
                      : simulationRunning() ? QStringLiteral("current")
                                            : QStringLiteral("pending")));

    steps.append(step(QStringLiteral("health"), QStringLiteral("起飞准备"),
                      hasIssue() ? QStringLiteral("需处理")
                      : vehicleReady() ? QStringLiteral("已通过")
                      : vehicleConnected() ? QStringLiteral("检查中")
                                           : QStringLiteral("等待"),
                      hasIssue() ? QStringLiteral("problem")
                      : vehicleReady() ? QStringLiteral("complete")
                      : vehicleConnected() ? QStringLiteral("current")
                                           : QStringLiteral("pending")));
    return steps;
}

QString AppController::testRunStage() const
{
    const QVariantMap plan = selectedTestPlan();
    if (plan.isEmpty() || !plan.value(QStringLiteral("complete")).toBool())
        return QStringLiteral("plan");
    if (!dockerAvailable() || !imageAvailable() || !dockerNetworkAvailable()
        || (m_sensorServiceEnabled && sensorServiceSupported()
            && !sensorServiceImageAvailable())) {
        return QStringLiteral("environment");
    }
    if (!simulationRunning()) return QStringLiteral("ready");
    if (!vehicleConnected()) return QStringLiteral("connecting");
    if (hasIssue()) return QStringLiteral("issue");
    if (!vehicleReady()) return QStringLiteral("health");
    if (missionMonitorState() == QStringLiteral("completed")) return QStringLiteral("complete");
    if (missionRunning() || missionPaused()) return QStringLiteral("mission");
    if (inAir()) return QStringLiteral("flight");
    return QStringLiteral("takeoff");
}

QString AppController::testRunStageLabel() const
{
    const QString stage = testRunStage();
    if (stage == QStringLiteral("plan")) return QStringLiteral("完善测试方案");
    if (stage == QStringLiteral("environment")) return QStringLiteral("检查本机环境");
    if (stage == QStringLiteral("ready")) return QStringLiteral("可以启动仿真");
    if (stage == QStringLiteral("connecting")) return QStringLiteral("正在连接飞行器");
    if (stage == QStringLiteral("health")) return QStringLiteral("正在检查起飞条件");
    if (stage == QStringLiteral("issue")) return QStringLiteral("需要处理异常");
    if (stage == QStringLiteral("mission")) return QStringLiteral("正在执行航线");
    if (stage == QStringLiteral("flight")) return QStringLiteral("飞行进行中");
    if (stage == QStringLiteral("complete")) return QStringLiteral("本次测试已完成");
    return QStringLiteral("可以起飞");
}

QString AppController::testRunGuidance() const
{
    const QString stage = testRunStage();
    if (stage == QStringLiteral("plan"))
        return QStringLiteral("为当前方案选择一条已保存航线");
    if (stage == QStringLiteral("environment")) {
        if (!dockerAvailable()) return QStringLiteral("请先启动 Docker Desktop");
        if (!imageAvailable()) return QStringLiteral("请安装已支持的 PX4 SITL 镜像");
        if (!dockerNetworkAvailable()) return QStringLiteral("请重新检测并创建容器网络");
        return QStringLiteral("请先生成 FlightBench ROS 2 数据服务镜像");
    }
    if (stage == QStringLiteral("ready"))
        return QStringLiteral("环境与方案都已就绪");
    if (stage == QStringLiteral("connecting"))
        return QStringLiteral("正在等待 PX4 与 MAVSDK 建立连接");
    if (stage == QStringLiteral("health"))
        return QStringLiteral("传感器与位置状态稳定后即可起飞");
    if (stage == QStringLiteral("issue")) return issueMessage();
    if (stage == QStringLiteral("mission")) return missionMonitorMessage();
    if (stage == QStringLiteral("flight"))
        return QStringLiteral("可使用键盘控制或执行航点任务");
    if (stage == QStringLiteral("complete"))
        return QStringLiteral("停止仿真后会自动保存测试结果");
    return QStringLiteral("进入飞行页面执行下一步");
}

QVariantMap AppController::preflightSummary() const
{
    const QVariantMap plan = selectedTestPlan();
    QVariantMap route;
    const QString routeId = plan.value(QStringLiteral("routeId")).toString();
    if (!routeId.isEmpty()) route = m_routeLibrary.loadRoute(routeId, 0.0, 0.0);
    const auto *model = findSimulationModel(plan.value(QStringLiteral("modelId")).toString());
    const auto *world = findSimulationWorld(plan.value(QStringLiteral("worldId")).toString());
    bool previewAvailable = false;
    if (world && !world->previewModelDirectory.isEmpty()) {
        const QString directory = QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("simulation/models/%1")
                                                    .arg(world->previewModelDirectory));
        previewAvailable = QDir(directory).exists();
    }
    QVariantMap result = PreflightEvaluator::evaluate(
        plan, route, WorldMapCatalog::metadata(
                         plan.value(QStringLiteral("worldId")).toString()),
        dockerAvailable(), imageAvailable(), model != nullptr, world != nullptr,
        previewAvailable, isValidContainerName(m_containerName));

    if (!containerNameAvailable()) {
        QVariantList checks = result.value(QStringLiteral("checks")).toList();
        bool addedBlockingCheck = false;
        for (QVariant &entry : checks) {
            QVariantMap check = entry.toMap();
            if (check.value(QStringLiteral("id")).toString() != QStringLiteral("container"))
                continue;
            addedBlockingCheck = check.value(QStringLiteral("status")).toString()
                                 != QStringLiteral("block");
            check.insert(QStringLiteral("status"), QStringLiteral("block"));
            check.insert(QStringLiteral("detail"),
                         QStringLiteral("本机已有同名容器，请更换名称或处理旧容器"));
            entry = check;
            break;
        }
        const int previousBlockCount = result.value(QStringLiteral("blockCount")).toInt();
        const int blockCount = previousBlockCount + (addedBlockingCheck ? 1 : 0);
        result.insert(QStringLiteral("checks"), checks);
        result.insert(QStringLiteral("blockCount"), blockCount);
        result.insert(QStringLiteral("canStart"), false);
        result.insert(QStringLiteral("summary"),
                      QStringLiteral("发现 %1 个必须处理的问题")
                          .arg(blockCount));
    }
    QVariantList externalChecks = result.value(QStringLiteral("checks")).toList();
    int blockCount = result.value(QStringLiteral("blockCount")).toInt();
    if (!dockerNetworkAvailable()) {
        externalChecks.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("docker-network")},
            {QStringLiteral("label"), QStringLiteral("容器网络")},
            {QStringLiteral("status"), QStringLiteral("block")},
            {QStringLiteral("detail"), QStringLiteral("需要 flightbench-net 连接 PX4 与可选 ROS 2 服务")},
            {QStringLiteral("action"), QStringLiteral("environment")},
        });
        ++blockCount;
    }
    if (m_sensorServiceEnabled && sensorServiceSupported()) {
        const bool available = sensorServiceImageAvailable();
        externalChecks.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("ros-sensor-image")},
            {QStringLiteral("label"), QStringLiteral("ROS 2 传感器服务")},
            {QStringLiteral("status"), available ? QStringLiteral("pass")
                                                   : QStringLiteral("block")},
            {QStringLiteral("detail"), available
                 ? QStringLiteral("已选择随本次仿真启动独立 ROS 2 数据容器")
                 : QStringLiteral("未找到 flightbench/ros-sensor-bridge:0.12.0 镜像")},
            {QStringLiteral("action"), available ? QString() : QStringLiteral("environment")},
        });
        if (!available) ++blockCount;
    }
    result.insert(QStringLiteral("checks"), externalChecks);
    result.insert(QStringLiteral("blockCount"), blockCount);
    result.insert(QStringLiteral("canStart"), blockCount == 0);
    if (blockCount > 0) {
        result.insert(QStringLiteral("summary"),
                      QStringLiteral("发现 %1 个必须处理的问题").arg(blockCount));
    }
    return result;
}

QVariantMap AppController::analyzeMissionRouteSafety(const QVariantList &waypoints,
                                                     double startEastMeters,
                                                     double startNorthMeters,
                                                     double startAltitudeMeters) const
{
    return WorldMapCatalog::analyzeRoute(m_selectedWorldId, waypoints,
                                         startEastMeters, startNorthMeters,
                                         startAltitudeMeters);
}

void AppController::setSelectedModelId(const QString &modelId)
{
    if (modelId == m_selectedModelId) return;
    const auto *definition = findSimulationModel(modelId);
    if (!definition || simulationRunning() || busy()) return;

    m_selectedModelId = modelId;
    QSettings settings;
    settings.setValue(QStringLiteral("simulation/modelId"), m_selectedModelId);
    emit selectedModelChanged();
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("仿真机型已设为 %1（%2）")
                         .arg(definition->name, definition->id));
    useStatus(QStringLiteral("已选择 %1").arg(definition->name));
}

void AppController::setSelectedWorldId(const QString &worldId)
{
    if (worldId == m_selectedWorldId) return;
    const auto *definition = findSimulationWorld(worldId);
    if (!definition || simulationRunning() || busy()) return;

    m_selectedWorldId = worldId;
    QSettings().setValue(QStringLiteral("simulation/worldId"), m_selectedWorldId);
    emit selectedWorldChanged();
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("仿真世界已设为 %1（%2）")
                         .arg(definition->name, definition->id));
    useStatus(QStringLiteral("已选择 %1").arg(definition->name));
}

void AppController::setSensorServiceEnabled(bool enabled)
{
    if (m_sensorServiceEnabled == enabled) return;
    if (simulationRunning() || busy()) {
        useStatus(QStringLiteral("停止仿真后才能更改 ROS 2 数据服务"));
        return;
    }
    if (enabled && !sensorServiceSupported()) {
        useStatus(QStringLiteral("当前机型尚未接入 ROS 2 传感器服务"));
        return;
    }
    m_sensorServiceEnabled = enabled;
    QSettings().setValue(QStringLiteral("simulation/sensorServiceEnabled"), enabled);
    emit sensorServicePreferenceChanged();
    emit testRunStateChanged();
    appendDiagnostic(QStringLiteral("ROS 2"), enabled
                         ? QStringLiteral("本次仿真将启动可选传感器数据容器")
                         : QStringLiteral("本次仿真不启动 ROS 2 数据容器"));
    useStatus(enabled ? QStringLiteral("已选择启动 ROS 2 数据服务")
                      : QStringLiteral("已选择仅启动 PX4 仿真"));
}

void AppController::setSelectedTestPlanId(const QString &planId)
{
    if (simulationRunning() || busy()) {
        useStatus(QStringLiteral("停止仿真后才能更换测试方案"));
        return;
    }
    if (planId.trimmed().isEmpty()) {
        if (m_selectedTestPlanId.isEmpty()) return;
        m_selectedTestPlanId.clear();
        QSettings().remove(QStringLiteral("testPlans/selectedId"));
        emit selectedTestPlanChanged();
        useStatus(QStringLiteral("当前没有测试方案，请保存当前配置"));
        return;
    }

    const QVariantMap plan = m_testPlanRepository.plan(planId);
    if (plan.isEmpty()) {
        useStatus(QStringLiteral("找不到这个测试方案"));
        return;
    }
    const QString modelId = plan.value(QStringLiteral("modelId")).toString();
    const QString worldId = plan.value(QStringLiteral("worldId")).toString();
    if (!findSimulationModel(modelId)) {
        useStatus(QStringLiteral("测试方案使用的飞行器当前不可用"));
        return;
    }
    if (!findSimulationWorld(worldId)) {
        useStatus(QStringLiteral("测试方案使用的仿真世界当前不可用"));
        return;
    }

    const bool idChanged = m_selectedTestPlanId != planId;
    m_selectedTestPlanId = planId;
    QSettings().setValue(QStringLiteral("testPlans/selectedId"), m_selectedTestPlanId);
    setSelectedModelId(modelId);
    setSelectedWorldId(worldId);
    setTakeoffAltitude(plan.value(QStringLiteral("takeoffAltitudeMeters"), 5.0).toDouble());
    const QVariantMap control = plan.value(QStringLiteral("controlSettings")).toMap();
    setControlSpeedMetersPerSecond(
        control.value(QStringLiteral("horizontalSpeedMetersPerSecond"), 1.0).toDouble());
    setControlVerticalSpeedMetersPerSecond(
        control.value(QStringLiteral("verticalSpeedMetersPerSecond"), 0.5).toDouble());
    setControlYawRateDegreesPerSecond(
        control.value(QStringLiteral("yawRateDegreesPerSecond"), 30.0).toDouble());
    const QVariantMap sensor = plan.value(QStringLiteral("sensorSettings")).toMap();
    const QString sensorUsage = sensor.value(QStringLiteral("usage"),
                                             QStringLiteral("off")).toString();
    setSensorServiceEnabled(sensorServiceSupported()
                            && sensorUsage != QStringLiteral("off"));
    if (idChanged) emit selectedTestPlanChanged();
    appendDiagnostic(QStringLiteral("测试方案"),
                     QStringLiteral("已应用：%1").arg(plan.value(QStringLiteral("name")).toString()));
    useStatus(QStringLiteral("已应用测试方案：%1")
                  .arg(plan.value(QStringLiteral("name")).toString()));
}

void AppController::setContainerName(const QString &name)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName == m_containerName) return;
    if (simulationRunning() || busy()) {
        useStatus(QStringLiteral("停止仿真后才能修改容器名称"));
        return;
    }
    if (!isValidContainerName(trimmedName)) {
        useStatus(QStringLiteral("容器名称需以字母或数字开头，只能包含字母、数字、点、短横线和下划线"));
        return;
    }
    m_containerName = trimmedName;
    m_docker.setContainerName(m_containerName);
    QSettings().setValue(QStringLiteral("simulation/containerName"), m_containerName);
    emit containerNameChanged();
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("临时容器名称设为 %1").arg(m_containerName));
}

QString AppController::applicationVersion() const
{
    return RuntimeInformation::applicationVersion();
}

QString AppController::qtRuntimeVersion() const
{
    return RuntimeInformation::qtRuntimeVersion();
}

QString AppController::mavsdkVersion() const
{
    return RuntimeInformation::mavsdkVersion();
}

QString AppController::buildDescription() const
{
    return RuntimeInformation::buildDescription();
}

QString AppController::platformDescription() const
{
    return RuntimeInformation::platformDescription();
}

QVariantMap AppController::sessionDetails(const QString &sessionId) const
{
    QVariantMap details = m_sessionRecorder.sessionDetails(sessionId);
    if (!details.isEmpty()) {
        details.insert(QStringLiteral("replayAvailable"),
                       ReplayController::containsPositionSamples(
                           details.value(QStringLiteral("csvPath")).toString()));
    }
    return details;
}

QString AppController::exportSessionCsv(const QString &sessionId)
{
    const QString sourcePath = m_sessionRecorder.csvPathForSession(sessionId);
    if (sourcePath.isEmpty()) return {};

    const QString exportDirectory = StoragePaths::directory(QStringLiteral("Exports"));
    if (!QDir().mkpath(exportDirectory)) {
        useStatus(QStringLiteral("无法创建 CSV 导出目录"));
        return {};
    }
    QString destinationPath = QDir(exportDirectory).filePath(sessionId + QStringLiteral(".csv"));
    for (int suffix = 2; QFile::exists(destinationPath); ++suffix) {
        destinationPath = QDir(exportDirectory)
                              .filePath(QStringLiteral("%1-%2.csv").arg(sessionId).arg(suffix));
    }
    if (!QFile::copy(sourcePath, destinationPath)) {
        useStatus(QStringLiteral("CSV 导出失败"));
        return {};
    }
    appendDiagnostic(QStringLiteral("记录"),
                     QStringLiteral("会话 CSV 已导出到 %1").arg(destinationPath));
    useStatus(QStringLiteral("会话 CSV 已导出"));
    return QDir::toNativeSeparators(destinationPath);
}

bool AppController::openSessionFolder(const QString &sessionId)
{
    const QString folderPath = m_sessionRecorder.folderPathForSession(sessionId);
    if (folderPath.isEmpty()) return false;
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
    if (!opened) useStatus(QStringLiteral("无法打开会话目录"));
    return opened;
}

bool AppController::openSessionsFolder()
{
    const bool opened = QDesktopServices::openUrl(
        QUrl::fromLocalFile(m_sessionRecorder.sessionsDirectory()));
    if (!opened) useStatus(QStringLiteral("无法打开飞行会话目录"));
    return opened;
}

QString AppController::sessionStorageSizeLabel() const
{
    const double bytes = static_cast<double>(m_sessionStorageBytes);
    if (bytes < 1024.0) return QStringLiteral("%1 B").arg(m_sessionStorageBytes);
    if (bytes < 1024.0 * 1024.0)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    if (bytes < 1024.0 * 1024.0 * 1024.0)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    return QStringLiteral("%1 GB")
        .arg(QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2));
}

void AppController::refreshSessionStorageUsage()
{
    qint64 totalBytes = 0;
    QDirIterator iterator(m_sessionRecorder.sessionsDirectory(),
                          QDir::Files | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        totalBytes += iterator.fileInfo().size();
    }
    if (m_sessionStorageBytes == totalBytes) return;
    m_sessionStorageBytes = totalBytes;
    emit sessionStorageChanged();
}

bool AppController::clearSessionTrash()
{
    const int count = m_sessionRecorder.trashedSessions().size();
    if (count == 0) {
        useStatus(QStringLiteral("会话回收站已经是空的"));
        return true;
    }
    const int removed = m_sessionRecorder.clearTrash();
    refreshSessionStorageUsage();
    if (removed != count) {
        useStatus(QStringLiteral("部分会话未能删除，请检查文件是否被占用"));
        return false;
    }
    appendDiagnostic(QStringLiteral("记录"),
                     QStringLiteral("已清空会话回收站，共 %1 条").arg(removed));
    useStatus(QStringLiteral("会话回收站已清空"));
    return true;
}

QString AppController::exportApplicationSettings()
{
    const QVariantMap document = m_preferences.exportDocument(QVariantMap{
        {QStringLiteral("visible"), m_preferences.flightPathVisible()},
        {QStringLiteral("widthMeters"), flightPathWidthMeters()},
        {QStringLiteral("colorPreset"), m_preferences.flightPathColorPreset()},
        {QStringLiteral("maxVisibleLengthMeters"), flightPathMaxVisibleLengthMeters()},
        {QStringLiteral("autoClear"), m_preferences.flightPathAutoClear()}});
    QString error;
    const QString path = ApplicationPreferences::writeDocument(
        document, StoragePaths::directory(QStringLiteral("Exports")), &error);
    if (path.isEmpty()) {
        useStatus(error);
        return {};
    }
    useStatus(QStringLiteral("设置已导出"));
    return QDir::toNativeSeparators(path);
}

QVariantMap AppController::importApplicationSettings(const QUrl &fileUrl)
{
    QVariantMap result{{QStringLiteral("success"), false}};
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.insert(QStringLiteral("message"), QStringLiteral("无法打开设置文件"));
        return result;
    }
    QString error;
    const QVariantMap document = ApplicationPreferences::decodeDocument(file.readAll(), &error);
    if (document.isEmpty()) {
        result.insert(QStringLiteral("message"), error);
        return result;
    }

    const QVariantMap appearance = document.value(QStringLiteral("appearance")).toMap();
    const QString themeMode = appearance.value(QStringLiteral("mode"),
                                                QStringLiteral("light")).toString();
    const int textScale = std::clamp(appearance.value(
        QStringLiteral("textScalePercent"), 100).toInt(), 90, 110);
    const bool reducedMotion = appearance.value(QStringLiteral("reducedMotion"), false).toBool();
    if (QStringList{QStringLiteral("light"), QStringLiteral("dark"),
                    QStringLiteral("system")}.contains(themeMode)) {
        QSettings settings;
        settings.setValue(QStringLiteral("Appearance/mode"), themeMode);
        settings.setValue(QStringLiteral("Appearance/textScalePercent"), textScale);
        settings.setValue(QStringLiteral("Appearance/reducedMotion"), reducedMotion);
        result.insert(QStringLiteral("themeMode"), themeMode);
        result.insert(QStringLiteral("textScalePercent"), textScale);
        result.insert(QStringLiteral("reducedMotion"), reducedMotion);
    }

    const QVariantMap interaction = document.value(QStringLiteral("interaction")).toMap();
    setPreciseTurnDefaultDegrees(interaction.value(QStringLiteral("preciseTurnDegrees"), 45.0).toDouble());
    setCameraRotationSensitivity(interaction.value(QStringLiteral("cameraRotationSensitivity"), 1.0).toDouble());
    setCameraZoomSensitivity(interaction.value(QStringLiteral("cameraZoomSensitivity"), 1.0).toDouble());
    setInvertCameraVertical(interaction.value(QStringLiteral("invertCameraVertical"), false).toBool());

    const QVariantMap view = document.value(QStringLiteral("view")).toMap();
    setGroundMarkerVisible(view.value(QStringLiteral("groundMarkerVisible"), true).toBool());
    setGroundMarkerSizeMode(view.value(QStringLiteral("groundMarkerSize"), QStringLiteral("auto")).toString());
    setDefaultCameraMode(view.value(QStringLiteral("defaultCameraMode"), QStringLiteral("follow-both")).toString());
    setCityBuildingOpacity(view.value(QStringLiteral("buildingOpacity"), 1.0).toDouble());
    setWorldRoadsVisible(view.value(QStringLiteral("roadsVisible"), true).toBool());
    setWorldGreenAreasVisible(view.value(QStringLiteral("greenAreasVisible"), true).toBool());
    setWorldWaterVisible(view.value(QStringLiteral("waterVisible"), true).toBool());
    setDefaultGridVisible(view.value(QStringLiteral("defaultGridVisible"), true).toBool());

    const QVariantMap flightPath = document.value(QStringLiteral("flightPath")).toMap();
    setFlightPathVisible(flightPath.value(QStringLiteral("visible"), true).toBool());
    setFlightPathWidthMeters(flightPath.value(QStringLiteral("widthMeters"), 0.06).toDouble());
    setFlightPathColorPreset(flightPath.value(QStringLiteral("colorPreset"),
                                              QStringLiteral("orange")).toString());
    setFlightPathMaxVisibleLengthMeters(flightPath.value(
        QStringLiteral("maxVisibleLengthMeters"), 100).toInt());
    setFlightPathAutoClear(flightPath.value(QStringLiteral("autoClear"), true).toBool());

    const QVariantMap recording = document.value(QStringLiteral("recording")).toMap();
    setTelemetryRecordingHz(recording.value(QStringLiteral("telemetryHz"), 5).toInt());
    setAutoGenerateReports(recording.value(QStringLiteral("autoGenerateReports"), false).toBool());
    setTrashRetentionDays(recording.value(QStringLiteral("trashRetentionDays"), 30).toInt());
    result.insert(QStringLiteral("success"), true);
    result.insert(QStringLiteral("message"), QStringLiteral("设置已导入"));
    useStatus(QStringLiteral("设置已导入"));
    return result;
}

void AppController::resetViewPreferences()
{
    setGroundMarkerVisible(true);
    setGroundMarkerSizeMode(QStringLiteral("auto"));
    setDefaultCameraMode(QStringLiteral("follow-both"));
    setCityBuildingOpacity(1.0);
    setWorldRoadsVisible(true);
    setWorldGreenAreasVisible(true);
    setWorldWaterVisible(true);
    setDefaultGridVisible(true);
    useStatus(QStringLiteral("三维视图设置已恢复"));
}

void AppController::resetInteractionPreferences()
{
    setPreciseTurnDefaultDegrees(45.0);
    setCameraRotationSensitivity(1.0);
    setCameraZoomSensitivity(1.0);
    setInvertCameraVertical(false);
    useStatus(QStringLiteral("操作设置已恢复"));
}

void AppController::resetRecordingPreferences()
{
    setTelemetryRecordingHz(5);
    setAutoGenerateReports(false);
    setTrashRetentionDays(30);
    useStatus(QStringLiteral("记录设置已恢复"));
}

void AppController::resetAllApplicationPreferences()
{
    resetViewPreferences();
    resetInteractionPreferences();
    resetRecordingPreferences();
    resetFlightPathDisplaySettings();
    useStatus(QStringLiteral("应用设置已全部恢复"));
}

bool AppController::copyEnvironmentInformation()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) return false;
    const QString information = QStringList{
        QStringLiteral("FlightBench %1").arg(applicationVersion()),
        QStringLiteral("Qt %1").arg(qtRuntimeVersion()),
        QStringLiteral("MAVSDK %1").arg(mavsdkVersion()),
        buildDescription(),
        platformDescription(),
        QStringLiteral("当前机型：%1 (%2)").arg(selectedModelName(), selectedModelId()),
        QStringLiteral("当前世界：%1 (%2)").arg(selectedWorldName(), selectedWorldId()),
        QStringLiteral("会话目录：%1").arg(QDir::toNativeSeparators(sessionsDirectory())),
        QStringLiteral("航线目录：%1").arg(QDir::toNativeSeparators(routesDirectory())),
        QStringLiteral("方案目录：%1").arg(QDir::toNativeSeparators(plansDirectory()))
    }.join(QLatin1Char('\n'));
    clipboard->setText(information);
    useStatus(QStringLiteral("软件与环境信息已复制"));
    return true;
}

QString AppController::generateSessionReport(const QString &sessionId)
{
    const QVariantMap details = m_sessionRecorder.sessionDetails(sessionId);
    if (details.isEmpty()) {
        useStatus(QStringLiteral("找不到要生成报告的飞行会话"));
        return {};
    }
    const QString reportsDirectory = StoragePaths::directory(QStringLiteral("Reports"));
    QString error;
    const QString path = TestReportGenerator::generate(details, reportsDirectory, &error);
    if (path.isEmpty()) {
        useStatus(QStringLiteral("测试报告生成失败：%1").arg(error));
        return {};
    }
    m_sessionRecorder.setSessionReport(sessionId, path);
    appendDiagnostic(QStringLiteral("报告"), QStringLiteral("已生成：%1").arg(path));
    useStatus(QStringLiteral("测试报告已生成"));
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        useStatus(QStringLiteral("报告已生成，但无法自动打开浏览器"));
    }
    return path;
}

QString AppController::exportSessionBundle(const QString &sessionId)
{
    const QVariantMap details = m_sessionRecorder.sessionDetails(sessionId);
    const QString folder = details.value(QStringLiteral("folderPath")).toString();
    if (details.isEmpty() || folder.isEmpty() || !QDir(folder).exists()) {
        useStatus(QStringLiteral("找不到要导出的完整会话"));
        return {};
    }
    QString reportError;
    const QString reportPath = TestReportGenerator::generate(details, folder, &reportError);
    if (reportPath.isEmpty()) {
        useStatus(QStringLiteral("完整会话包导出失败：无法生成测试报告（%1）")
                      .arg(reportError));
        return {};
    }
    const QString exportDirectory = StoragePaths::directory(QStringLiteral("Exports"));
    if (!QDir().mkpath(exportDirectory)) {
        useStatus(QStringLiteral("无法创建会话导出目录"));
        return {};
    }
    const QString destination = QDir(exportDirectory).filePath(sessionId + QStringLiteral(".zip"));
    auto quotePowerShell = [](QString value) {
        value.replace(QLatin1Char('\''), QStringLiteral("''"));
        return QLatin1Char('\'') + value + QLatin1Char('\'');
    };
    const QString command = QStringLiteral("Compress-Archive -Path %1 -DestinationPath %2 -Force")
                                .arg(quotePowerShell(QDir(folder).filePath(QStringLiteral("*"))),
                                     quotePowerShell(destination));
    QProcess process;
    process.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                   QStringLiteral("-Command"), command});
    if (!process.waitForStarted(5000) || !process.waitForFinished(60000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
        || !QFileInfo::exists(destination)) {
        useStatus(QStringLiteral("完整会话包导出失败"));
        return {};
    }
    const QString nativePath = QDir::toNativeSeparators(destination);
    appendDiagnostic(QStringLiteral("记录"), QStringLiteral("已导出完整会话包：%1").arg(nativePath));
    useStatus(QStringLiteral("完整会话包已导出"));
    return nativePath;
}

bool AppController::updateSessionDetails(const QString &sessionId,
                                         const QString &displayName,
                                         const QString &notes)
{
    if (!m_sessionRecorder.updateSessionDetails(sessionId, displayName, notes)) return false;
    if (replayActive() && m_replaySessionId == sessionId) {
        const QVariantMap details = m_sessionRecorder.sessionDetails(sessionId);
        const QString customName = details.value(QStringLiteral("displayName")).toString();
        m_replaySessionName = customName.isEmpty()
                                  ? details.value(QStringLiteral("missionRouteName")).toString()
                                  : customName;
        if (m_replaySessionName.isEmpty()) {
            m_replaySessionName = details.value(
                QStringLiteral("startedLabel"), QStringLiteral("历史会话")).toString();
        }
        emit replayStateChanged();
    }
    appendDiagnostic(QStringLiteral("记录"), QStringLiteral("已更新飞行会话信息：%1").arg(sessionId));
    useStatus(QStringLiteral("会话信息已保存"));
    return true;
}

bool AppController::deleteSession(const QString &sessionId)
{
    if (replayActive() && m_replaySessionId == sessionId) stopSessionReplay();
    if (!m_sessionRecorder.deleteSession(sessionId)) return false;
    appendDiagnostic(QStringLiteral("记录"), QStringLiteral("已移入会话回收站：%1").arg(sessionId));
    useStatus(m_preferences.trashRetentionDays() > 0
                  ? QStringLiteral("会话已移到回收站，将保留 %1 天")
                        .arg(m_preferences.trashRetentionDays())
                  : QStringLiteral("会话已移到回收站，可随时恢复"));
    return true;
}

bool AppController::deleteSessionSensorData(const QString &sessionId)
{
    if (replayActive() && m_replaySessionId == sessionId) stopSessionReplay();
    if (!m_sessionRecorder.deleteSessionSensorData(sessionId)) {
        useStatus(QStringLiteral("没有可删除的传感器数据，或会话仍在记录中"));
        return false;
    }
    appendDiagnostic(QStringLiteral("记录"),
                     QStringLiteral("已删除会话传感器数据：%1").arg(sessionId));
    useStatus(QStringLiteral("传感器数据已删除，遥测、事件和报告仍然保留"));
    return true;
}

bool AppController::restoreSession(const QString &sessionId)
{
    if (!m_sessionRecorder.restoreSession(sessionId)) return false;
    appendDiagnostic(QStringLiteral("记录"), QStringLiteral("已恢复飞行会话：%1").arg(sessionId));
    useStatus(QStringLiteral("飞行会话已恢复"));
    return true;
}

bool AppController::permanentlyDeleteSession(const QString &sessionId)
{
    if (!m_sessionRecorder.permanentlyDeleteSession(sessionId)) return false;
    appendDiagnostic(QStringLiteral("记录"), QStringLiteral("已永久删除飞行会话：%1").arg(sessionId));
    useStatus(QStringLiteral("飞行会话已永久删除"));
    return true;
}

bool AppController::setReplayWorldId(const QString &worldId)
{
    if (!replayActive()) return false;
    const auto *world = findSimulationWorld(worldId);
    if (!world) {
        useStatus(QStringLiteral("没有找到这个回放场景"));
        return false;
    }
    m_replayWorldId = world->id;
    m_replayWorldName = world->name;
    m_replayWorldPreviewDirectory = {};
    if (!world->previewModelDirectory.isEmpty()) {
        const QString directory = QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("simulation/models/%1")
                                                    .arg(world->previewModelDirectory));
        m_replayWorldPreviewDirectory = QUrl::fromLocalFile(QDir::cleanPath(directory));
    }
    emit replayStateChanged();
    useStatus(QStringLiteral("回放场景已切换为 %1").arg(m_replayWorldName));
    return true;
}

bool AppController::startSessionReplay(const QString &sessionId)
{
    if (simulationRunning() || flightSessionActive()) {
        useStatus(QStringLiteral("停止当前仿真后才能回放历史会话"));
        return false;
    }
    m_lastReplayCapturePath.clear();
    m_lidarMapping.clear();
    m_depthMapping.clear();
    m_lastReplayMappingTimeMs = -1;
    m_lastReplayDepthMappingTimeMs = -1;
    const QVariantMap details = m_sessionRecorder.sessionDetails(sessionId);
    if (details.isEmpty()) {
        useStatus(QStringLiteral("没有找到这个飞行会话"));
        return false;
    }
    const QString recordedWorldId = details.value(QStringLiteral("worldId")).toString();
    const auto *recordedWorld = findSimulationWorld(recordedWorldId);
    if (recordedWorldId.isEmpty() || !recordedWorld) {
        useStatus(QStringLiteral("该会话没有当前可用的仿真场景，无法回放"));
        return false;
    }
    const QString csvPath = details.value(QStringLiteral("csvPath")).toString();
    if (!m_replay.start(csvPath,
                        details.value(QStringLiteral("eventsPath")).toString(),
                        details.value(QStringLiteral("startedAt")).toString(),
                        details.value(QStringLiteral("folderPath")).toString())) {
        useStatus(m_replay.errorMessage().isEmpty()
                      ? QStringLiteral("该会话缺少可回放的位置数据")
                      : m_replay.errorMessage());
        return false;
    }
    m_replaySessionId = sessionId;
    m_gimbalInspection.loadSession(details.value(QStringLiteral("folderPath")).toString());
    m_gimbalInspection.seek(m_replay.timeMs());
    m_replaySessionName = details.value(QStringLiteral("displayName")).toString();
    if (m_replaySessionName.isEmpty())
        m_replaySessionName = details.value(QStringLiteral("missionRouteName")).toString();
    if (m_replaySessionName.isEmpty())
        m_replaySessionName = details.value(QStringLiteral("startedLabel"), QStringLiteral("历史会话")).toString();
    m_replayModelId = details.value(QStringLiteral("modelId")).toString();
    m_replayModelName = details.value(QStringLiteral("modelName")).toString();
    m_replayWorldId = recordedWorldId;
    const auto *world = recordedWorld;
    m_replayWorldName = details.value(QStringLiteral("worldName")).toString();
    m_replayWorldPreviewDirectory = {};
    if (world && !world->previewModelDirectory.isEmpty()) {
        const QString directory = QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("simulation/models/%1")
                                                    .arg(world->previewModelDirectory));
        m_replayWorldPreviewDirectory = QUrl::fromLocalFile(QDir::cleanPath(directory));
    }
    m_replayRouteAnalysis = details.value(QStringLiteral("routeAnalysis")).toMap();
    QVector<QVector3D> plannedPoints;
    const QVariantList missionWaypoints = details.value(QStringLiteral("missionWaypoints")).toList();
    if (!missionWaypoints.isEmpty()) {
        const QVector3D missionStart(
            static_cast<float>(details.value(QStringLiteral("missionStartEastMeters")).toDouble()),
            static_cast<float>(details.value(QStringLiteral("missionStartUpMeters")).toDouble()),
            static_cast<float>(details.value(QStringLiteral("missionStartSouthMeters")).toDouble()));
        plannedPoints.append(missionStart);
        for (const QVariant &value : missionWaypoints) {
            const QVariantMap waypoint = value.toMap();
            plannedPoints.append(QVector3D(
                missionStart.x() + static_cast<float>(waypoint.value(QStringLiteral("eastMeters")).toDouble()),
                static_cast<float>(waypoint.value(QStringLiteral("altitudeMeters"), missionStart.y()).toDouble()),
                missionStart.z() - static_cast<float>(waypoint.value(QStringLiteral("northMeters")).toDouble())));
        }
    }
    m_replayPlannedPathGeometry.setPoints(plannedPoints);
    emit replayStateChanged();
    appendDiagnostic(QStringLiteral("回放"),
                     QStringLiteral("开始回放会话：%1").arg(m_replaySessionName));
    useStatus(QStringLiteral("正在回放历史飞行"));
    return true;
}

void AppController::stopSessionReplay()
{
    if (!replayActive()) return;
    m_replay.stop();
    m_gimbalInspection.clear();
    m_lidarMapping.clear();
    m_depthMapping.clear();
    m_lastReplayMappingTimeMs = -1;
    m_lastReplayDepthMappingTimeMs = -1;
    m_replaySessionId.clear();
    m_replaySessionName.clear();
    m_replayModelId.clear();
    m_replayModelName.clear();
    m_replayWorldId.clear();
    m_replayWorldName.clear();
    m_replayWorldPreviewDirectory = {};
    m_replayRouteAnalysis.clear();
    m_replayPlannedPathGeometry.clearPath();
    if (!m_lastReplayCapturePath.isEmpty()) {
        m_lastReplayCapturePath.clear();
        emit sensorCaptureSaved();
    }
    emit replayStateChanged();
    useStatus(QStringLiteral("已退出历史回放"));
}

void AppController::toggleReplay()
{
    if (!replayActive()) return;
    if (replayPlaying()) m_replay.pause();
    else m_replay.resume();
}

void AppController::seekReplay(qint64 timeMs) { m_replay.seek(timeMs); }
void AppController::setReplaySpeed(double multiplier) { m_replay.setSpeedMultiplier(multiplier); }
void AppController::seekReplayEvent(int eventIndex) { m_replay.seekEvent(eventIndex); }
void AppController::seekPreviousReplayEvent() { m_replay.seekPreviousEvent(); }
void AppController::seekNextReplayEvent() { m_replay.seekNextEvent(); }

QVariantMap AppController::saveRoute(const QString &routeId,
                                     const QString &name,
                                     const QVariantList &waypoints,
                                     const QString &endAction,
                                     double anchorEastMeters,
                                     double anchorNorthMeters)
{
    const QVariantMap response = m_routeLibrary.saveRoute(routeId, name, waypoints,
                                                          endAction, anchorEastMeters,
                                                          anchorNorthMeters);
    const QString message = response.value(QStringLiteral("message")).toString();
    useStatus(message);
    appendDiagnostic(QStringLiteral("航线库"), message);
    return response;
}

QVariantMap AppController::loadSavedRoute(const QString &routeId,
                                          double targetEastMeters,
                                          double targetNorthMeters) const
{
    return m_routeLibrary.loadRoute(routeId, targetEastMeters, targetNorthMeters);
}

QVariantMap AppController::duplicateSavedRoute(const QString &routeId)
{
    const QVariantMap response = m_routeLibrary.duplicateRoute(routeId);
    const QString message = response.value(QStringLiteral("message")).toString();
    useStatus(message);
    appendDiagnostic(QStringLiteral("航线库"), message);
    return response;
}

QVariantMap AppController::renameSavedRoute(const QString &routeId,
                                            const QString &name)
{
    const QVariantMap response = m_routeLibrary.renameRoute(routeId, name);
    const QString message = response.value(QStringLiteral("message")).toString();
    useStatus(message);
    appendDiagnostic(QStringLiteral("航线库"), message);
    return response;
}

QVariantMap AppController::deleteSavedRoute(const QString &routeId)
{
    const QVariantMap response = m_routeLibrary.deleteRoute(routeId);
    const QString message = response.value(QStringLiteral("message")).toString();
    useStatus(message);
    appendDiagnostic(QStringLiteral("航线库"), message);
    return response;
}

bool AppController::openRoutesFolder()
{
    const bool opened = QDesktopServices::openUrl(
        QUrl::fromLocalFile(m_routeLibrary.directory()));
    if (!opened) useStatus(QStringLiteral("无法打开航线目录"));
    return opened;
}

QVariantMap AppController::saveCurrentTestPlan(const QVariantMap &draft)
{
    if (simulationRunning() || busy()) {
        const QString message = QStringLiteral("停止仿真后才能保存测试方案");
        useStatus(message);
        return {{QStringLiteral("success"), false},
                {QStringLiteral("message"), message}};
    }
    const QVariantMap response = m_testPlanRepository.savePlan(
        draft.value(QStringLiteral("id")).toString(),
        draft.value(QStringLiteral("name")).toString(),
        draft.value(QStringLiteral("description")).toString(),
        draft.value(QStringLiteral("type"), QStringLiteral("free-flight")).toString(),
        draft.value(QStringLiteral("modelId"), selectedModelId()).toString(),
        draft.value(QStringLiteral("worldId"), selectedWorldId()).toString(),
        draft.value(QStringLiteral("takeoffAltitudeMeters"), takeoffAltitude()).toDouble(),
        draft.value(QStringLiteral("routeId")).toString(),
        draft.value(QStringLiteral("endAction"), QStringLiteral("hold")).toString(),
        draft.value(QStringLiteral("acceptancePreset"), QStringLiteral("standard")).toString(),
        {{QStringLiteral("acceptanceRules"), draft.value(QStringLiteral("acceptanceRules"))},
         {QStringLiteral("controlSettings"), draft.value(QStringLiteral("controlSettings"))},
         {QStringLiteral("sensorSettings"), draft.value(QStringLiteral("sensorSettings"))}});
    const QString message = response.value(QStringLiteral("message")).toString();
    if (response.value(QStringLiteral("success")).toBool()) {
        setSelectedTestPlanId(response.value(QStringLiteral("id")).toString());
    }
    useStatus(message);
    appendDiagnostic(QStringLiteral("测试方案"), message);
    return response;
}

QVariantMap AppController::duplicateTestPlan(const QString &planId)
{
    if (simulationRunning() || busy()) {
        const QString message = QStringLiteral("停止仿真后才能复制测试方案");
        useStatus(message);
        return {{QStringLiteral("success"), false},
                {QStringLiteral("message"), message}};
    }
    const QVariantMap response = m_testPlanRepository.duplicatePlan(planId);
    const QString message = response.value(QStringLiteral("message")).toString();
    if (response.value(QStringLiteral("success")).toBool()) {
        setSelectedTestPlanId(response.value(QStringLiteral("id")).toString());
    } else {
        useStatus(message);
    }
    appendDiagnostic(QStringLiteral("测试方案"), message);
    return response;
}

QVariantMap AppController::renameTestPlan(const QString &planId,
                                          const QString &name)
{
    if (simulationRunning() || busy()) {
        const QString message = QStringLiteral("停止仿真后才能重命名测试方案");
        useStatus(message);
        return {{QStringLiteral("success"), false},
                {QStringLiteral("message"), message}};
    }
    const QVariantMap response = m_testPlanRepository.renamePlan(planId, name);
    const QString message = response.value(QStringLiteral("message")).toString();
    useStatus(message);
    appendDiagnostic(QStringLiteral("测试方案"), message);
    if (response.value(QStringLiteral("success")).toBool()
        && planId == m_selectedTestPlanId) {
        emit selectedTestPlanChanged();
    }
    return response;
}

QVariantMap AppController::deleteTestPlan(const QString &planId)
{
    if (simulationRunning() || busy()) {
        const QString message = QStringLiteral("停止仿真后才能删除测试方案");
        useStatus(message);
        return {{QStringLiteral("success"), false},
                {QStringLiteral("message"), message}};
    }
    const QVariantMap response = m_testPlanRepository.deletePlan(planId);
    const QString message = response.value(QStringLiteral("message")).toString();
    if (response.value(QStringLiteral("success")).toBool()
        && planId == m_selectedTestPlanId) {
        const QVariantList remainingPlans = m_testPlanRepository.plans();
        setSelectedTestPlanId(remainingPlans.isEmpty()
                                  ? QString()
                                  : remainingPlans.constFirst().toMap()
                                        .value(QStringLiteral("id")).toString());
    } else {
        useStatus(message);
    }
    appendDiagnostic(QStringLiteral("测试方案"), message);
    return response;
}

QString AppController::exportTestPlan(const QString &planId)
{
    QVariantMap plan = m_testPlanRepository.plan(planId);
    if (plan.isEmpty()) {
        useStatus(QStringLiteral("找不到要导出的测试方案"));
        return {};
    }
    plan.insert(QStringLiteral("format"), QStringLiteral("FlightBenchTestPlan"));
    plan.insert(QStringLiteral("version"), 2);
    QString fileName = plan.value(QStringLiteral("name"), QStringLiteral("test-plan")).toString();
    fileName.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")), QStringLiteral("-"));
    const QString directory = StoragePaths::directory(QStringLiteral("Exports"));
    if (!QDir().mkpath(directory)) return {};
    const QString path = QDir(directory).filePath(fileName + QStringLiteral(".fbplan.json"));
    QSaveFile file(path);
    const QByteArray data = QJsonDocument(QJsonObject::fromVariantMap(plan))
                                .toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(data) != data.size() || !file.commit()) {
        useStatus(QStringLiteral("测试方案导出失败"));
        return {};
    }
    useStatus(QStringLiteral("测试方案已导出"));
    return QDir::toNativeSeparators(path);
}

QVariantMap AppController::importTestPlan(const QUrl &fileUrl)
{
    auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("success"), false},
                           {QStringLiteral("message"), message}};
    };
    if (simulationRunning() || busy()) {
        const QString message = QStringLiteral("停止仿真后才能导入测试方案");
        useStatus(message);
        return failure(message);
    }
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("无法打开测试方案文件"));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QVariantMap plan = document.object().toVariantMap();
    if (!document.isObject()
        || plan.value(QStringLiteral("format")).toString() != QStringLiteral("FlightBenchTestPlan")) {
        return failure(QStringLiteral("这不是有效的 FlightBench 测试方案"));
    }
    const QVariantMap saved = m_testPlanRepository.savePlan(
        {}, plan.value(QStringLiteral("name"), QStringLiteral("导入的测试方案")).toString(),
        plan.value(QStringLiteral("description")).toString(),
        plan.value(QStringLiteral("type"), QStringLiteral("free-flight")).toString(),
        plan.value(QStringLiteral("modelId"), QStringLiteral("gz_x500")).toString(),
        plan.value(QStringLiteral("worldId"), QStringLiteral("default")).toString(),
        plan.value(QStringLiteral("takeoffAltitudeMeters"), 5.0).toDouble(),
        plan.value(QStringLiteral("routeId")).toString(),
        plan.value(QStringLiteral("endAction"), QStringLiteral("hold")).toString(),
        plan.value(QStringLiteral("acceptancePreset"), QStringLiteral("standard")).toString(),
        {{QStringLiteral("acceptanceRules"), plan.value(QStringLiteral("acceptanceRules"))},
         {QStringLiteral("controlSettings"), plan.value(QStringLiteral("controlSettings"))},
         {QStringLiteral("sensorSettings"), plan.value(QStringLiteral("sensorSettings"))}});
    const QString message = saved.value(QStringLiteral("message")).toString();
    useStatus(message);
    if (saved.value(QStringLiteral("success")).toBool())
        setSelectedTestPlanId(saved.value(QStringLiteral("id")).toString());
    return saved;
}

QVariantMap AppController::reuseSessionTestPlan(const QString &sessionId)
{
    auto response = [](bool success, const QString &message,
                       const QString &planId = {}) {
        return QVariantMap{{QStringLiteral("success"), success},
                           {QStringLiteral("message"), message},
                           {QStringLiteral("id"), planId}};
    };

    if (simulationRunning() || busy()) {
        const QString message = QStringLiteral("停止当前仿真后才能复用历史方案");
        useStatus(message);
        return response(false, message);
    }

    const QVariantMap session = m_sessionRecorder.sessionDetails(sessionId);
    if (session.isEmpty()) {
        const QString message = QStringLiteral("找不到这次飞行会话");
        useStatus(message);
        return response(false, message);
    }

    const QString recordedPlanId = session.value(QStringLiteral("testPlanId")).toString();
    if (!recordedPlanId.isEmpty() && !m_testPlanRepository.plan(recordedPlanId).isEmpty()) {
        setSelectedTestPlanId(recordedPlanId);
        if (selectedTestPlanId() != recordedPlanId) {
            const QString message = QStringLiteral("历史方案存在，但当前机型或场景已不可用");
            useStatus(message);
            return response(false, message);
        }
        const QString message = QStringLiteral("已恢复测试方案：%1")
                                    .arg(selectedTestPlan().value(QStringLiteral("name")).toString());
        useStatus(message);
        appendDiagnostic(QStringLiteral("测试方案"), message);
        return response(true, message, recordedPlanId);
    }

    const QString modelId = session.value(QStringLiteral("modelId")).toString();
    const QString worldId = session.value(QStringLiteral("worldId")).toString();
    if (!findSimulationModel(modelId) || !findSimulationWorld(worldId)) {
        const QString message = QStringLiteral("会话缺少当前可用的机型或仿真场景，无法直接复用");
        useStatus(message);
        return response(false, message);
    }

    QString planType = session.value(QStringLiteral("testPlanType")).toString();
    if (planType.isEmpty()) {
        planType = session.value(QStringLiteral("missionWaypoints")).toList().isEmpty()
                       ? QStringLiteral("free-flight") : QStringLiteral("mission");
    }
    QString routeId = session.value(QStringLiteral("missionRouteId")).toString();
    bool routeAvailable = false;
    for (const QVariant &entry : m_routeLibrary.routes()) {
        if (entry.toMap().value(QStringLiteral("id")).toString() == routeId) {
            routeAvailable = true;
            break;
        }
    }
    if (!routeAvailable) routeId.clear();

    QString planName = session.value(QStringLiteral("testPlanName")).toString().trimmed();
    if (planName.isEmpty()) planName = QStringLiteral("%1 的测试配置")
                                           .arg(session.value(QStringLiteral("startedLabel")).toString());
    const QVariantMap saved = m_testPlanRepository.savePlan(
        {}, planName + QStringLiteral(" 复用"),
        session.value(QStringLiteral("testPlanDescription")).toString(),
        planType, modelId, worldId,
        session.value(QStringLiteral("takeoffAltitude"), 5.0).toDouble(),
        routeId,
        session.value(QStringLiteral("testPlanEndAction"),
                      session.value(QStringLiteral("missionEndAction"),
                                    QStringLiteral("hold"))).toString(),
        session.value(QStringLiteral("testPlanAcceptancePreset"),
                      QStringLiteral("standard")).toString(),
        {{QStringLiteral("acceptanceRules"), session.value(QStringLiteral("testPlanAcceptanceRules"))},
         {QStringLiteral("controlSettings"), session.value(QStringLiteral("testPlanControlSettings"))},
         {QStringLiteral("sensorSettings"), session.value(QStringLiteral("testPlanSensorSettings"))}});
    const QString message = saved.value(QStringLiteral("message")).toString();
    if (!saved.value(QStringLiteral("success")).toBool()) {
        useStatus(message);
        appendDiagnostic(QStringLiteral("测试方案"), message);
        return saved;
    }

    setSelectedTestPlanId(saved.value(QStringLiteral("id")).toString());
    const QString finalMessage = routeAvailable || planType == QStringLiteral("free-flight")
                                     ? QStringLiteral("已从历史会话创建可复用方案")
                                     : QStringLiteral("已恢复配置；原航线不存在，请重新选择航线");
    useStatus(finalMessage);
    appendDiagnostic(QStringLiteral("测试方案"), finalMessage);
    return response(true, finalMessage, selectedTestPlanId());
}

QVariantMap AppController::autoFixSelectedPlanRoute()
{
    const QVariantMap plan = selectedTestPlan();
    const QString routeId = plan.value(QStringLiteral("routeId")).toString();
    if (routeId.isEmpty()) {
        return {{QStringLiteral("success"), false},
                {QStringLiteral("message"), QStringLiteral("当前方案没有可修正的航线")}};
    }
    const QVariantMap route = m_routeLibrary.loadRoute(routeId, 0.0, 0.0);
    if (!route.value(QStringLiteral("success")).toBool()) return route;
    QVariantList waypoints = route.value(QStringLiteral("waypoints")).toList();
    const QVariantMap safety = WorldMapCatalog::analyzeRoute(
        plan.value(QStringLiteral("worldId")).toString(), waypoints, 0.0, 0.0,
        plan.value(QStringLiteral("takeoffAltitudeMeters"), 5.0).toDouble());
    if (!safety.value(QStringLiteral("available")).toBool()
        || safety.value(QStringLiteral("safe"), true).toBool()) {
        return {{QStringLiteral("success"), true},
                {QStringLiteral("message"), QStringLiteral("航线不需要自动修正")}};
    }
    const double requiredAltitude = std::clamp(
        safety.value(QStringLiteral("requiredAltitudeMeters")).toDouble(), 1.0, 100.0);
    for (QVariant &entry : waypoints) {
        QVariantMap point = entry.toMap();
        if (point.value(QStringLiteral("altitudeMeters")).toDouble() < requiredAltitude)
            point.insert(QStringLiteral("altitudeMeters"), requiredAltitude);
        entry = point;
    }
    const QVariantMap saved = m_routeLibrary.saveRoute(
        routeId, route.value(QStringLiteral("name")).toString(), waypoints,
        route.value(QStringLiteral("endAction"), QStringLiteral("land")).toString(),
        0.0, 0.0);
    const QString message = saved.value(QStringLiteral("success")).toBool()
                                ? QStringLiteral("已将低空航点调整至至少 %1 米")
                                      .arg(requiredAltitude, 0, 'f', 1)
                                : saved.value(QStringLiteral("message")).toString();
    useStatus(message);
    appendDiagnostic(QStringLiteral("启动检查"), message);
    QVariantMap response = saved;
    response.insert(QStringLiteral("message"), message);
    return response;
}

bool AppController::openPlansFolder()
{
    const bool opened = QDesktopServices::openUrl(
        QUrl::fromLocalFile(m_testPlanRepository.directory()));
    if (!opened) useStatus(QStringLiteral("无法打开测试方案目录"));
    return opened;
}

QString AppController::exportDiagnosticReport()
{
    const QString directoryPath = StoragePaths::rootDirectory();
    if (!QDir().mkpath(directoryPath)) {
        useStatus(QStringLiteral("无法创建诊断报告目录"));
        return {};
    }

    const QString fileName = QStringLiteral("diagnostics-%1.txt")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString filePath = QDir(directoryPath).filePath(fileName);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        useStatus(QStringLiteral("诊断报告导出失败：%1").arg(file.errorString()));
        return {};
    }

    QTextStream stream(&file);
    stream << "FlightBench 诊断报告" << Qt::endl;
    stream << "生成时间：" << QDateTime::currentDateTime().toString(Qt::ISODate) << Qt::endl;
    stream << "FlightBench：" << applicationVersion() << Qt::endl;
    stream << "Qt：" << qtRuntimeVersion() << Qt::endl;
    stream << "MAVSDK：" << mavsdkVersion() << Qt::endl;
    stream << "构建：" << buildDescription() << Qt::endl;
    stream << "系统：" << platformDescription() << Qt::endl;
    stream << "仿真机型：" << selectedModelName() << " (" << selectedModelId() << ")" << Qt::endl;
    stream << "仿真世界：" << selectedWorldName() << " (" << selectedWorldId() << ")" << Qt::endl;
    stream << "容器名称：" << containerName() << Qt::endl;
    stream << "Docker：" << (dockerAvailable() ? "可用" : "不可用") << Qt::endl;
    stream << "PX4 镜像：" << (imageAvailable() ? "已找到" : "未就绪") << Qt::endl;
    stream << "仿真：" << (simulationRunning() ? "运行中" : "未运行") << Qt::endl;
    stream << "MAVSDK：" << (vehicleConnected() ? "已连接" : "未连接") << Qt::endl;
    stream << "健康稳定：" << (vehicleReady() ? "是" : "否") << Qt::endl;
    if (hasIssue()) {
        stream << "当前问题：" << issueMessage() << Qt::endl;
    }
    stream << "默认起飞高度：" << takeoffAltitude() << " m" << Qt::endl << Qt::endl;
    stream << "键盘水平速度：" << controlSpeedMetersPerSecond() << " m/s" << Qt::endl;
    stream << "遥测完整：" << (telemetryAvailable() ? "是" : "否") << Qt::endl;
    stream << "相对高度：" << altitudeMeters() << " m" << Qt::endl;
    stream << "水平速度：" << speedMetersPerSecond() << " m/s" << Qt::endl;
    stream << "垂直速度（上升为正）：" << verticalSpeedMetersPerSecond() << " m/s" << Qt::endl;
    stream << "航向：" << headingDegrees() << " deg" << Qt::endl;
    stream << "姿态 R/P/Y：" << rollDegrees() << " / " << pitchDegrees() << " / "
           << yawDegrees() << " deg" << Qt::endl;
    stream << "陀螺 R/P/Y：" << gyroRollRateDegreesPerSecond() << " / "
           << gyroPitchRateDegreesPerSecond() << " / " << gyroYawRateDegreesPerSecond()
           << " deg/s" << Qt::endl;
    stream << "GPS：" << (gpsAvailable() ? gpsFixType() : QStringLiteral("无数据"))
           << "，卫星 " << gpsSatellites() << Qt::endl;
    stream << "电池：";
    if (batteryAvailable()) {
        stream << batteryRemainingPercent() << "% / " << batteryVoltageVolts() << " V";
    } else {
        stream << "无数据";
    }
    stream << Qt::endl << Qt::endl;
    for (const QString &entry : std::as_const(m_diagnosticEntries)) {
        stream << entry << Qt::endl;
    }
    file.close();

    appendDiagnostic(QStringLiteral("诊断"), QStringLiteral("报告已导出到 %1").arg(filePath));
    useStatus(QStringLiteral("诊断报告已导出"));
    return QDir::toNativeSeparators(filePath);
}

void AppController::clearDiagnostics()
{
    m_diagnosticEntries.clear();
    emit diagnosticEntriesChanged();
    appendDiagnostic(QStringLiteral("应用"), QStringLiteral("诊断记录已清空"));
}

void AppController::useStatus(const QString &message)
{
    if (message.isEmpty() || m_statusMessage == message) return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void AppController::appendDiagnostic(const QString &source, const QString &message)
{
    if (message.isEmpty()) return;
    const QString entry = QStringLiteral("[%1] [%2] %3")
                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                                   source,
                                   message);
    m_diagnosticEntries.append(entry);
    m_sessionRecorder.appendEvent(entry);
    while (m_diagnosticEntries.size() > 300) {
        m_diagnosticEntries.removeFirst();
    }
    emit diagnosticEntriesChanged();
}

void AppController::updateIssueState()
{
    QString code;
    QString message;
    QString action;
    QString label;

    if (!m_docker.problemCode().isEmpty()) {
        code = m_docker.problemCode();
        message = m_docker.statusMessage();
        const bool containerProblem = code == QStringLiteral("container-conflict")
                                      || code == QStringLiteral("container-name-invalid");
        action = containerProblem ? QStringLiteral("environment")
                                  : QStringLiteral("refresh");
        label = containerProblem ? QStringLiteral("更换容器名称")
                                 : QStringLiteral("重新检测");
    } else if (m_connectionTimedOut) {
        code = QStringLiteral("connection-timeout");
        message = QStringLiteral("45 秒内没有收到 PX4 心跳，请重新启动仿真");
        action = QStringLiteral("restart");
        label = QStringLiteral("重新启动");
    } else if (m_healthTimedOut) {
        code = QStringLiteral("health-timeout");
        message = QStringLiteral("PX4 健康检查尚未稳定，请查看未通过项目");
        action = QStringLiteral("health");
        label = QStringLiteral("健康检查");
    } else if (m_commandIssue) {
        code = QStringLiteral("command-failed");
        message = m_commandIssueMessage;
        action = QStringLiteral("health");
        label = QStringLiteral("健康检查");
    }

    if (m_issueCode == code && m_issueMessage == message
        && m_recoveryAction == action && m_recoveryLabel == label) {
        return;
    }
    m_issueCode = code;
    m_issueMessage = message;
    m_recoveryAction = action;
    m_recoveryLabel = label;
    emit issueChanged();

    if (!code.isEmpty() && code != m_lastRecordedIssueCode) {
        m_sessionRecorder.markIssue(message);
        m_lastRecordedIssueCode = code;
    } else if (code.isEmpty()) {
        m_lastRecordedIssueCode.clear();
    }
}
QVariantMap AppController::currentIssueExplanation() const
{
    return Px4EventTranslator::explain(
        m_issueCode, m_issueMessage,
        {{QStringLiteral("gyrometerOk"), gyrometerOk()},
         {QStringLiteral("accelerometerOk"), accelerometerOk()},
         {QStringLiteral("magnetometerOk"), magnetometerOk()},
         {QStringLiteral("localPositionOk"), localPositionOk()},
         {QStringLiteral("globalPositionOk"), globalPositionOk()},
         {QStringLiteral("homePositionOk"), homePositionOk()},
         {QStringLiteral("armable"), armable()}});
}
