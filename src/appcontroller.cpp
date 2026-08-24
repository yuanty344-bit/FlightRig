#include "appcontroller.h"
#include "preflightevaluator.h"
#include "storagepaths.h"
#include "px4eventtranslator.h"
#include "testreportgenerator.h"
#include "worldmapcatalog.h"

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
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
struct SimulationModelDefinition
{
    QString id;
    QString name;
    QString description;
    bool recommended;
};

struct SimulationWorldDefinition
{
    QString id;
    QString name;
    QString description;
    QString previewModelDirectory;
    QString sceneType;
};

const QList<SimulationModelDefinition> &simulationModelDefinitions()
{
    static const QList<SimulationModelDefinition> definitions{
        {QStringLiteral("gz_x500"), QStringLiteral("x500"),
         QStringLiteral("标准四旋翼，适合基础飞行与功能验证"), true},
        {QStringLiteral("gz_x500_depth"), QStringLiteral("x500 Depth"),
         QStringLiteral("搭载深度相机，用于近距离感知实验"), false},
        {QStringLiteral("gz_x500_vision"), QStringLiteral("x500 Vision"),
         QStringLiteral("搭载视觉相机，用于图像与视觉实验"), false},
        {QStringLiteral("gz_x500_lidar_2d"), QStringLiteral("x500 LiDAR"),
         QStringLiteral("搭载二维激光雷达，用于测距与避障实验"), false},
        {QStringLiteral("gz_x500_gimbal"), QStringLiteral("x500 Gimbal"),
         QStringLiteral("搭载云台相机，用于载荷与视角实验"), false},
    };
    return definitions;
}

const SimulationModelDefinition *findSimulationModel(const QString &id)
{
    const auto &definitions = simulationModelDefinitions();
    for (const auto &definition : definitions) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

const QList<SimulationWorldDefinition> &simulationWorldDefinitions()
{
    static const QList<SimulationWorldDefinition> definitions = [] {
        QList<SimulationWorldDefinition> result{
            {QStringLiteral("default"), QStringLiteral("默认场地"),
             QStringLiteral("开阔地面，适合基础飞行与功能验证"), {},
             QStringLiteral("simple")},
            // Retain a built-in fallback so old build trees and tests still
            // recognize the original city if catalog.json has not been copied.
            {QStringLiteral("xatu_weiyang"), QStringLiteral("西安工业大学未央校区"),
             QStringLiteral("校园白模，包含建筑、道路、绿地和水面"),
             QStringLiteral("xatu_weiyang/meshes"), QStringLiteral("campus")},
        };

        const QString catalogPath = QDir(QCoreApplication::applicationDirPath())
                                        .filePath(QStringLiteral("simulation/worlds/catalog.json"));
        QFile catalogFile(catalogPath);
        if (!catalogFile.open(QIODevice::ReadOnly)) return result;
        const QJsonDocument document = QJsonDocument::fromJson(catalogFile.readAll());
        if (!document.isArray()) return result;

        static const QRegularExpression validId(QStringLiteral("^[a-z0-9][a-z0-9_]{1,47}$"));
        for (const QJsonValue &value : document.array()) {
            const QJsonObject entry = value.toObject();
            const QString id = entry.value(QStringLiteral("id")).toString();
            const QString name = entry.value(QStringLiteral("name")).toString();
            const QString description = entry.value(QStringLiteral("description")).toString();
            const QString sceneType = entry.value(QStringLiteral("sceneType")).toString(
                QStringLiteral("other"));
            QString previewDirectory =
                entry.value(QStringLiteral("previewModelDirectory")).toString();
            if (!validId.match(id).hasMatch() || id == QStringLiteral("default")
                || name.trimmed().isEmpty()) {
                continue;
            }
            if (previewDirectory.isEmpty()) previewDirectory = id + QStringLiteral("/meshes");

            const SimulationWorldDefinition definition{id, name, description,
                                                        previewDirectory, sceneType};
            auto existing = std::find_if(result.begin(), result.end(), [&id](const auto &item) {
                return item.id == id;
            });
            if (existing == result.end()) result.append(definition);
            else *existing = definition;
        }
        return result;
    }();
    return definitions;
}

const SimulationWorldDefinition *findSimulationWorld(const QString &id)
{
    const auto &definitions = simulationWorldDefinitions();
    for (const auto &definition : definitions) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

bool isValidContainerName(const QString &name)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,62}$"));
    return expression.match(name).hasMatch();
}

bool isFlightPathColorPreset(const QString &preset)
{
    const QString normalized = preset.trimmed().toLower();
    static const QRegularExpression customColor(
        QStringLiteral("^#[0-9a-f]{6}$"));
    return normalized == QStringLiteral("orange")
           || normalized == QStringLiteral("blue")
           || normalized == QStringLiteral("teal")
           || normalized == QStringLiteral("violet")
           || customColor.match(normalized).hasMatch();
}
}

AppController *createAppController(QObject *parent)
{
    return new AppController(parent);
}

AppController::AppController(QObject *parent)
    : QObject(parent)
{
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
    connect(&m_docker, &DockerController::simulationRunningChanged, this, [this] {
        m_vehicle.setSitlExpected(simulationRunning());
        if (simulationRunning()) {
            resetFlightScene();
            m_lastRecordedIssueCode.clear();
            if (m_sessionRecorder.startSession(
                    selectedModelId(), selectedModelName(), takeoffAltitude(),
                    selectedWorldId(), selectedWorldName(), applicationVersion(),
                    QStringLiteral("px4io/px4-sitl-gazebo:latest"),
                    qtRuntimeVersion(), mavsdkVersion())) {
                m_sessionRecorder.setTestPlan(selectedTestPlan());
            }
            m_connectionTimedOut = false;
            if (!vehicleConnected()) m_connectionTimeout.start();
        } else {
            setLandingAndStopping(false);
            const QString completedSessionId = m_sessionRecorder.activeSessionId();
            m_sessionRecorder.finishSession(
                m_sessionEndReason.isEmpty() ? QStringLiteral("仿真已停止")
                                             : m_sessionEndReason);
            if (m_autoGenerateReports && !completedSessionId.isEmpty()) {
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
            if (m_restartRequested) {
                m_restartRequested = false;
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
            if (m_landingAndStopping) {
                setLandingAndStopping(false);
                useStatus(QStringLiteral("降落并停止已取消：飞行器连接已断开"));
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
                if (!m_landingAndStopping) return;
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
    m_vehicle.setTakeoffAltitude(settings.value(QStringLiteral("flight/takeoffAltitude"), 5.0).toDouble());
    if (settings.contains(QStringLiteral("flight/controlSpeedMetersPerSecond"))) {
        m_vehicle.setControlSpeedMetersPerSecond(
            settings.value(QStringLiteral("flight/controlSpeedMetersPerSecond"), 1.0).toDouble());
    } else {
        // One-time migration from the former slow / standard / fast selector.
        static constexpr double legacySpeeds[]{0.5, 1.0, 2.0};
        const int legacyMode = std::clamp(
            settings.value(QStringLiteral("flight/controlSpeedMode"), 1).toInt(), 0, 2);
        m_vehicle.setControlSpeedMetersPerSecond(legacySpeeds[legacyMode]);
        settings.setValue(QStringLiteral("flight/controlSpeedMetersPerSecond"),
                          m_vehicle.controlSpeedMetersPerSecond());
        settings.remove(QStringLiteral("flight/controlSpeedMode"));
    }
    m_vehicle.setControlVerticalSpeedMetersPerSecond(
        settings.value(QStringLiteral("flight/controlVerticalSpeedMetersPerSecond"), 0.5).toDouble());
    m_vehicle.setControlYawRateDegreesPerSecond(
        settings.value(QStringLiteral("flight/controlYawRateDegreesPerSecond"), 30.0).toDouble());
    m_flightPathVisible = settings.value(QStringLiteral("flightPath/visible"), true).toBool();
    m_flightPathAutoClear = settings.value(
        QStringLiteral("flightPath/autoClear"), true).toBool();
    m_flightPathGeometry.setWidthMeters(
        settings.value(QStringLiteral("flightPath/widthMeters"), 0.06).toDouble());
    m_replay.pathGeometry()->setWidthMeters(m_flightPathGeometry.widthMeters());
    m_flightPathGeometry.setMaxVisibleLengthMeters(
        settings.value(QStringLiteral("flightPath/maxVisibleLengthMeters"), 100).toInt());
    m_replay.pathGeometry()->setMaxVisibleLengthMeters(
        m_flightPathGeometry.maxVisibleLengthMeters());
    const QString savedFlightPathColor =
        settings.value(QStringLiteral("flightPath/colorPreset"), QStringLiteral("orange")).toString();
    if (isFlightPathColorPreset(savedFlightPathColor)) {
        m_flightPathColorPreset = savedFlightPathColor.trimmed().toLower();
    }
    m_cameraRotationSensitivity = std::clamp(
        settings.value(QStringLiteral("view/rotationSensitivity"), 1.0).toDouble(), 0.5, 2.0);
    m_cameraZoomSensitivity = std::clamp(
        settings.value(QStringLiteral("view/zoomSensitivity"), 1.0).toDouble(), 0.5, 2.0);
    m_invertCameraVertical = settings.value(
        QStringLiteral("view/invertVertical"), false).toBool();
    m_groundMarkerVisible = settings.value(
        QStringLiteral("view/groundMarkerVisible"), true).toBool();
    const QString savedMarkerSize = settings.value(
        QStringLiteral("view/groundMarkerSize"), QStringLiteral("auto")).toString();
    if (QStringList{QStringLiteral("auto"), QStringLiteral("small"),
                    QStringLiteral("medium"), QStringLiteral("large")}.contains(savedMarkerSize))
        m_groundMarkerSizeMode = savedMarkerSize;
    const QString savedCameraMode = settings.value(
        QStringLiteral("view/defaultCameraMode"), QStringLiteral("follow-both")).toString();
    if (QStringList{QStringLiteral("follow"), QStringLiteral("follow-both"),
                    QStringLiteral("free")}.contains(savedCameraMode))
        m_defaultCameraMode = savedCameraMode;
    m_cityBuildingOpacity = std::clamp(
        settings.value(QStringLiteral("view/buildingOpacity"), 1.0).toDouble(), 0.25, 1.0);
    m_worldRoadsVisible = settings.value(QStringLiteral("view/roadsVisible"), true).toBool();
    m_worldGreenAreasVisible = settings.value(QStringLiteral("view/greenAreasVisible"), true).toBool();
    m_worldWaterVisible = settings.value(QStringLiteral("view/waterVisible"), true).toBool();
    m_defaultGridVisible = settings.value(QStringLiteral("view/defaultGridVisible"), true).toBool();
    m_preciseTurnDefaultDegrees = std::clamp(
        settings.value(QStringLiteral("interaction/preciseTurnDegrees"), 45.0).toDouble(),
        5.0, 180.0);
    const int savedRecordingHz = settings.value(
        QStringLiteral("recording/telemetryHz"), 5).toInt();
    m_telemetryRecordingHz = QStringList{QStringLiteral("2"), QStringLiteral("5"),
                                         QStringLiteral("10")}.contains(QString::number(savedRecordingHz))
                                 ? savedRecordingHz : 5;
    m_autoGenerateReports = settings.value(
        QStringLiteral("recording/autoGenerateReports"), false).toBool();
    const int savedRetentionDays = settings.value(
        QStringLiteral("recording/trashRetentionDays"), 30).toInt();
    m_trashRetentionDays = QList<int>{0, 7, 30, 90}.contains(savedRetentionDays)
                               ? savedRetentionDays : 30;
    const QString savedModelId = settings.value(QStringLiteral("simulation/modelId"),
                                                QStringLiteral("gz_x500")).toString();
    if (findSimulationModel(savedModelId)) m_selectedModelId = savedModelId;
    const QString savedWorldId = settings.value(QStringLiteral("simulation/worldId"),
                                                QStringLiteral("default")).toString();
    if (findSimulationWorld(savedWorldId)) m_selectedWorldId = savedWorldId;
    const QString savedContainerName = settings.value(
        QStringLiteral("simulation/containerName"), QStringLiteral("flightbench-px4")).toString();
    if (isValidContainerName(savedContainerName)) m_containerName = savedContainerName;
    m_docker.setContainerName(m_containerName);
    const QString savedTestPlanId = settings.value(
        QStringLiteral("testPlans/selectedId")).toString();
    if (!m_testPlanRepository.plan(savedTestPlanId).isEmpty()) {
        setSelectedTestPlanId(savedTestPlanId);
    }
    appendDiagnostic(QStringLiteral("应用"), QStringLiteral("FlightBench 已启动，正在检查本机环境"));

    m_sessionRecorder.purgeTrashOlderThan(m_trashRetentionDays);
    refreshSessionStorageUsage();
    m_sessionSampleTimer.setInterval(1000 / m_telemetryRecordingHz);
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
    // Member QObjects are destroyed after this destructor body, while value
    // members such as m_diagnosticEntries are destroyed before m_docker. A
    // finishing QProcess may emit status signals during DockerController's
    // destructor, so disconnect every internal sender before any AppController
    // state can disappear.
    QObject::disconnect(&m_docker, nullptr, this, nullptr);
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

void AppController::refresh()
{
    m_connectionTimedOut = false;
    m_healthTimedOut = false;
    m_commandIssue = false;
    m_commandIssueMessage.clear();
    updateIssueState();
    m_vehicle.start();
    m_docker.probe();
}

bool AppController::startSimulation()
{
    if (replayActive()) {
        useStatus(QStringLiteral("请先退出历史回放，再启动仿真"));
        return false;
    }
    if (!containerNameAvailable()) {
        useStatus(QStringLiteral("当前容器名称已被占用，请在运行环境中更换名称"));
        return false;
    }
    const QVariantMap preflight = preflightSummary();
    if (!preflight.value(QStringLiteral("canStart")).toBool()) {
        useStatus(preflight.value(QStringLiteral("summary")).toString());
        return false;
    }
    m_connectionTimedOut = false;
    m_healthTimedOut = false;
    m_commandIssue = false;
    m_commandIssueMessage.clear();
    updateIssueState();
    m_vehicle.start();
    const QString worldAssetsDirectory =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("simulation"));
    m_docker.startSimulation(m_selectedModelId,
                             m_selectedWorldId,
                             worldAssetsDirectory,
                             m_containerName);
    return true;
}

void AppController::stopSimulation()
{
    m_vehicle.releaseKeyboardControl();
    if (m_landingAndStopping) {
        useStatus(QStringLiteral("正在降落，落地并锁定后将自动停止仿真…"));
        return;
    }
    if (vehicleConnected() && (inAir() || armed())) {
        useStatus(QStringLiteral("飞行器仍在空中或已解锁，请使用“降落并停止”"));
        appendDiagnostic(QStringLiteral("安全"),
                         QStringLiteral("已阻止飞行中直接停止仿真"));
        return;
    }
    m_docker.stopSimulation();
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
    if (!simulationRunning()) {
        useStatus(QStringLiteral("仿真尚未运行"));
        return;
    }
    if (m_landingAndStopping) {
        useStatus(QStringLiteral("正在降落，落地并锁定后将自动停止仿真…"));
        return;
    }
    if (!vehicleConnected() || (!inAir() && !armed())) {
        stopSimulation();
        return;
    }
    if (m_vehicle.busy() || m_docker.busy()) {
        useStatus(QStringLiteral("当前指令尚未完成，请稍后再试"));
        return;
    }

    setLandingAndStopping(true);
    m_vehicle.releaseKeyboardControl();
    m_sessionRecorder.markLandingRequested();
    appendDiagnostic(QStringLiteral("安全"),
                     QStringLiteral("已请求降落，落地后自动停止仿真"));
    m_vehicle.land();
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
    if (m_flightPathAutoClear) m_flightPathGeometry.clearPath();
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
    if (armed() || inAir()) m_flightPathGeometry.appendPoint(position);
}

void AppController::resetMissionMonitor()
{
    m_missionSceneWaypoints.clear();
    m_missionWaypointSpeeds.clear();
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
            m_restartRequested = true;
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
    if (!m_landingAndStopping || !simulationRunning() || !vehicleConnected()
        || inAir() || armed()) {
        return;
    }

    QTimer::singleShot(600, this, [this] {
        if (!m_landingAndStopping || !simulationRunning() || !vehicleConnected()
            || inAir() || armed()) {
            return;
        }
        setLandingAndStopping(false);
        useStatus(QStringLiteral("落地与锁定已确认，正在停止仿真…"));
        appendDiagnostic(QStringLiteral("安全"),
                         QStringLiteral("落地与锁定已确认，允许停止仿真"));
        m_docker.stopSimulation();
    });
}

void AppController::setLandingAndStopping(bool value)
{
    if (m_landingAndStopping == value) return;
    m_landingAndStopping = value;
    emit landingAndStoppingChanged();
    emit busyChanged();
}

void AppController::setTakeoffAltitude(double value)
{
    m_vehicle.setTakeoffAltitude(value);
    QSettings settings;
    settings.setValue(QStringLiteral("flight/takeoffAltitude"), m_vehicle.takeoffAltitude());
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("默认起飞高度设为 %1 米")
                         .arg(QString::number(m_vehicle.takeoffAltitude(), 'f', 1)));
}

void AppController::setControlSpeedMetersPerSecond(double value)
{
    const double previousValue = m_vehicle.controlSpeedMetersPerSecond();
    m_vehicle.setControlSpeedMetersPerSecond(value);
    if (qFuzzyCompare(previousValue, m_vehicle.controlSpeedMetersPerSecond())) return;
    QSettings settings;
    settings.setValue(QStringLiteral("flight/controlSpeedMetersPerSecond"),
                      m_vehicle.controlSpeedMetersPerSecond());
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("键盘水平速度设为 %1 m/s")
                         .arg(QString::number(m_vehicle.controlSpeedMetersPerSecond(), 'f', 1)));
}

void AppController::setFlightPathVisible(bool visible)
{
    if (m_flightPathVisible == visible) return;
    m_flightPathVisible = visible;
    QSettings().setValue(QStringLiteral("flightPath/visible"), visible);
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathWidthMeters(double value)
{
    const double previousValue = m_flightPathGeometry.widthMeters();
    m_flightPathGeometry.setWidthMeters(value);
    m_replay.pathGeometry()->setWidthMeters(m_flightPathGeometry.widthMeters());
    if (qFuzzyCompare(previousValue, m_flightPathGeometry.widthMeters())) return;
    QSettings().setValue(QStringLiteral("flightPath/widthMeters"),
                         m_flightPathGeometry.widthMeters());
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathColorPreset(const QString &preset)
{
    const QString normalized = preset.trimmed().toLower();
    if (!isFlightPathColorPreset(normalized) || m_flightPathColorPreset == normalized) return;
    m_flightPathColorPreset = normalized;
    QSettings().setValue(QStringLiteral("flightPath/colorPreset"), normalized);
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathMaxVisibleLengthMeters(int value)
{
    const int previousValue = m_flightPathGeometry.maxVisibleLengthMeters();
    m_flightPathGeometry.setMaxVisibleLengthMeters(value);
    m_replay.pathGeometry()->setMaxVisibleLengthMeters(
        m_flightPathGeometry.maxVisibleLengthMeters());
    if (previousValue == m_flightPathGeometry.maxVisibleLengthMeters()) return;
    QSettings().setValue(QStringLiteral("flightPath/maxVisibleLengthMeters"),
                         m_flightPathGeometry.maxVisibleLengthMeters());
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathAutoClear(bool value)
{
    if (m_flightPathAutoClear == value) return;
    m_flightPathAutoClear = value;
    QSettings().setValue(QStringLiteral("flightPath/autoClear"), value);
    emit flightPathDisplaySettingsChanged();
}

void AppController::setCameraRotationSensitivity(double value)
{
    const double normalized = std::round(std::clamp(value, 0.5, 2.0) * 10.0) / 10.0;
    if (qFuzzyCompare(m_cameraRotationSensitivity, normalized)) return;
    m_cameraRotationSensitivity = normalized;
    QSettings().setValue(QStringLiteral("view/rotationSensitivity"), normalized);
    emit viewPreferencesChanged();
}

void AppController::setCameraZoomSensitivity(double value)
{
    const double normalized = std::round(std::clamp(value, 0.5, 2.0) * 10.0) / 10.0;
    if (qFuzzyCompare(m_cameraZoomSensitivity, normalized)) return;
    m_cameraZoomSensitivity = normalized;
    QSettings().setValue(QStringLiteral("view/zoomSensitivity"), normalized);
    emit viewPreferencesChanged();
}

void AppController::setInvertCameraVertical(bool value)
{
    if (m_invertCameraVertical == value) return;
    m_invertCameraVertical = value;
    QSettings().setValue(QStringLiteral("view/invertVertical"), value);
    emit viewPreferencesChanged();
}

void AppController::setGroundMarkerVisible(bool value)
{
    if (m_groundMarkerVisible == value) return;
    m_groundMarkerVisible = value;
    QSettings().setValue(QStringLiteral("view/groundMarkerVisible"), value);
    emit viewPreferencesChanged();
}

void AppController::setGroundMarkerSizeMode(const QString &value)
{
    if (!QStringList{QStringLiteral("auto"), QStringLiteral("small"),
                     QStringLiteral("medium"), QStringLiteral("large")}.contains(value)
        || m_groundMarkerSizeMode == value) return;
    m_groundMarkerSizeMode = value;
    QSettings().setValue(QStringLiteral("view/groundMarkerSize"), value);
    emit viewPreferencesChanged();
}

void AppController::setDefaultCameraMode(const QString &value)
{
    if (!QStringList{QStringLiteral("follow"), QStringLiteral("follow-both"),
                     QStringLiteral("free")}.contains(value)
        || m_defaultCameraMode == value) return;
    m_defaultCameraMode = value;
    QSettings().setValue(QStringLiteral("view/defaultCameraMode"), value);
    emit viewPreferencesChanged();
}

void AppController::setCityBuildingOpacity(double value)
{
    const double normalized = std::round(std::clamp(value, 0.25, 1.0) * 20.0) / 20.0;
    if (qFuzzyCompare(m_cityBuildingOpacity, normalized)) return;
    m_cityBuildingOpacity = normalized;
    QSettings().setValue(QStringLiteral("view/buildingOpacity"), normalized);
    emit viewPreferencesChanged();
}

void AppController::setWorldRoadsVisible(bool value)
{
    if (m_worldRoadsVisible == value) return;
    m_worldRoadsVisible = value;
    QSettings().setValue(QStringLiteral("view/roadsVisible"), value);
    emit viewPreferencesChanged();
}

void AppController::setWorldGreenAreasVisible(bool value)
{
    if (m_worldGreenAreasVisible == value) return;
    m_worldGreenAreasVisible = value;
    QSettings().setValue(QStringLiteral("view/greenAreasVisible"), value);
    emit viewPreferencesChanged();
}

void AppController::setWorldWaterVisible(bool value)
{
    if (m_worldWaterVisible == value) return;
    m_worldWaterVisible = value;
    QSettings().setValue(QStringLiteral("view/waterVisible"), value);
    emit viewPreferencesChanged();
}

void AppController::setDefaultGridVisible(bool value)
{
    if (m_defaultGridVisible == value) return;
    m_defaultGridVisible = value;
    QSettings().setValue(QStringLiteral("view/defaultGridVisible"), value);
    emit viewPreferencesChanged();
}

void AppController::setPreciseTurnDefaultDegrees(double value)
{
    const double normalized = std::round(std::clamp(value, 5.0, 180.0));
    if (qFuzzyCompare(m_preciseTurnDefaultDegrees, normalized)) return;
    m_preciseTurnDefaultDegrees = normalized;
    QSettings().setValue(QStringLiteral("interaction/preciseTurnDegrees"), normalized);
    emit interactionPreferencesChanged();
}

void AppController::setTelemetryRecordingHz(int value)
{
    if (!QList<int>{2, 5, 10}.contains(value) || m_telemetryRecordingHz == value) return;
    m_telemetryRecordingHz = value;
    m_sessionSampleTimer.setInterval(1000 / value);
    QSettings().setValue(QStringLiteral("recording/telemetryHz"), value);
    emit recordingPreferencesChanged();
}

void AppController::setAutoGenerateReports(bool value)
{
    if (m_autoGenerateReports == value) return;
    m_autoGenerateReports = value;
    QSettings().setValue(QStringLiteral("recording/autoGenerateReports"), value);
    emit recordingPreferencesChanged();
}

void AppController::setTrashRetentionDays(int value)
{
    if (!QList<int>{0, 7, 30, 90}.contains(value) || m_trashRetentionDays == value) return;
    m_trashRetentionDays = value;
    QSettings().setValue(QStringLiteral("recording/trashRetentionDays"), value);
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
        QVariantMap model;
        model.insert(QStringLiteral("id"), definition.id);
        model.insert(QStringLiteral("name"), definition.name);
        model.insert(QStringLiteral("description"), definition.description);
        model.insert(QStringLiteral("recommended"), definition.recommended);
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

void AppController::setControlVerticalSpeedMetersPerSecond(double value)
{
    const double previousValue = m_vehicle.controlVerticalSpeedMetersPerSecond();
    m_vehicle.setControlVerticalSpeedMetersPerSecond(value);
    if (qFuzzyCompare(previousValue, m_vehicle.controlVerticalSpeedMetersPerSecond())) return;
    QSettings().setValue(QStringLiteral("flight/controlVerticalSpeedMetersPerSecond"),
                         m_vehicle.controlVerticalSpeedMetersPerSecond());
    appendDiagnostic(QStringLiteral("设置"),
                     QStringLiteral("键盘垂直速度设为 %1 m/s")
                         .arg(QString::number(m_vehicle.controlVerticalSpeedMetersPerSecond(), 'f', 1)));
}

void AppController::setControlYawRateDegreesPerSecond(double value)
{
    const double previousValue = m_vehicle.controlYawRateDegreesPerSecond();
    m_vehicle.setControlYawRateDegreesPerSecond(value);
    if (qFuzzyCompare(previousValue, m_vehicle.controlYawRateDegreesPerSecond())) return;
    QSettings().setValue(QStringLiteral("flight/controlYawRateDegreesPerSecond"),
                         m_vehicle.controlYawRateDegreesPerSecond());
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
    const bool environmentAvailable = dockerAvailable() && imageAvailable();

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
                                               : QStringLiteral("镜像未找到");
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
    if (!dockerAvailable() || !imageAvailable()) return QStringLiteral("environment");
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
    if (stage == QStringLiteral("environment"))
        return !dockerAvailable() ? QStringLiteral("请先启动 Docker Desktop")
                                  : QStringLiteral("请安装已支持的 PX4 SITL 镜像");
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
    return QCoreApplication::applicationVersion();
}

QString AppController::qtRuntimeVersion() const
{
    return QString::fromLatin1(qVersion());
}

QString AppController::mavsdkVersion() const
{
#ifdef FLIGHTBENCH_MAVSDK_VERSION
    return QStringLiteral(FLIGHTBENCH_MAVSDK_VERSION);
#else
    return QStringLiteral("3.17.1");
#endif
}

QString AppController::buildDescription() const
{
#ifdef _MSC_VER
#ifdef NDEBUG
    return QStringLiteral("MSVC %1 · Release · 64 位").arg(_MSC_VER);
#else
    return QStringLiteral("MSVC %1 · Debug · 64 位").arg(_MSC_VER);
#endif
#else
    return QStringLiteral("C++ · 64 位");
#endif
}

QString AppController::platformDescription() const
{
    return QStringLiteral("%1 · %2")
        .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
}

QVariantMap AppController::sessionDetails(const QString &sessionId) const
{
    QVariantMap details = m_sessionRecorder.sessionDetails(sessionId);
    if (!details.isEmpty()) {
        details.insert(QStringLiteral("replayAvailable"),
                       ReplayController::containsPositionSamples(
                           details.value(QStringLiteral("csvPath")).toString()));
        if (!details.contains(QStringLiteral("formatVersion")))
            details.insert(QStringLiteral("formatVersion"), 1);
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
    QSettings settings;
    QVariantMap document;
    document.insert(QStringLiteral("format"), QStringLiteral("FlightBenchSettings"));
    document.insert(QStringLiteral("formatVersion"), 1);
    document.insert(QStringLiteral("exportedAt"),
                    QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    document.insert(QStringLiteral("appearance"), QVariantMap{
        {QStringLiteral("mode"), settings.value(QStringLiteral("Appearance/mode"),
                                                 QStringLiteral("light"))},
        {QStringLiteral("textScalePercent"),
         settings.value(QStringLiteral("Appearance/textScalePercent"), 100)},
        {QStringLiteral("reducedMotion"),
         settings.value(QStringLiteral("Appearance/reducedMotion"), false)}});
    document.insert(QStringLiteral("interaction"), QVariantMap{
        {QStringLiteral("preciseTurnDegrees"), m_preciseTurnDefaultDegrees},
        {QStringLiteral("cameraRotationSensitivity"), m_cameraRotationSensitivity},
        {QStringLiteral("cameraZoomSensitivity"), m_cameraZoomSensitivity},
        {QStringLiteral("invertCameraVertical"), m_invertCameraVertical}});
    document.insert(QStringLiteral("view"), QVariantMap{
        {QStringLiteral("groundMarkerVisible"), m_groundMarkerVisible},
        {QStringLiteral("groundMarkerSize"), m_groundMarkerSizeMode},
        {QStringLiteral("defaultCameraMode"), m_defaultCameraMode},
        {QStringLiteral("buildingOpacity"), m_cityBuildingOpacity},
        {QStringLiteral("roadsVisible"), m_worldRoadsVisible},
        {QStringLiteral("greenAreasVisible"), m_worldGreenAreasVisible},
        {QStringLiteral("waterVisible"), m_worldWaterVisible},
        {QStringLiteral("defaultGridVisible"), m_defaultGridVisible}});
    document.insert(QStringLiteral("flightPath"), QVariantMap{
        {QStringLiteral("visible"), m_flightPathVisible},
        {QStringLiteral("widthMeters"), flightPathWidthMeters()},
        {QStringLiteral("colorPreset"), m_flightPathColorPreset},
        {QStringLiteral("maxVisibleLengthMeters"), flightPathMaxVisibleLengthMeters()},
        {QStringLiteral("autoClear"), m_flightPathAutoClear}});
    document.insert(QStringLiteral("recording"), QVariantMap{
        {QStringLiteral("telemetryHz"), m_telemetryRecordingHz},
        {QStringLiteral("autoGenerateReports"), m_autoGenerateReports},
        {QStringLiteral("trashRetentionDays"), m_trashRetentionDays}});
    const QString exportDirectory = StoragePaths::directory(QStringLiteral("Exports"));
    if (!QDir().mkpath(exportDirectory)) {
        useStatus(QStringLiteral("无法创建设置导出目录"));
        return {};
    }
    const QString path = QDir(exportDirectory).filePath(QStringLiteral("flightbench-settings.json"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject::fromVariantMap(document))
                          .toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        useStatus(QStringLiteral("设置导出失败"));
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
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QVariantMap document = json.object().toVariantMap();
    if (parseError.error != QJsonParseError::NoError
        || document.value(QStringLiteral("format")).toString()
               != QStringLiteral("FlightBenchSettings")
        || document.value(QStringLiteral("formatVersion")).toInt() != 1) {
        result.insert(QStringLiteral("message"), QStringLiteral("这不是可用的 FlightBench 设置文件"));
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
    useStatus(m_trashRetentionDays > 0
                  ? QStringLiteral("会话已移到回收站，将保留 %1 天")
                        .arg(m_trashRetentionDays)
                  : QStringLiteral("会话已移到回收站，可随时恢复"));
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
    QSettings().remove(QStringLiteral("replay/worldOverrides/%1").arg(sessionId));
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
    QSettings().setValue(QStringLiteral("replay/worldOverrides/%1").arg(m_replaySessionId),
                         m_replayWorldId);
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
    const QVariantMap details = m_sessionRecorder.sessionDetails(sessionId);
    if (details.isEmpty()) {
        useStatus(QStringLiteral("没有找到这个飞行会话"));
        return false;
    }
    const QString csvPath = details.value(QStringLiteral("csvPath")).toString();
    if (!m_replay.start(csvPath,
                        details.value(QStringLiteral("eventsPath")).toString(),
                        details.value(QStringLiteral("startedAt")).toString())) {
        useStatus(m_replay.errorMessage().isEmpty()
                      ? QStringLiteral("该会话缺少可回放的位置数据")
                      : m_replay.errorMessage());
        return false;
    }
    m_replaySessionId = sessionId;
    m_replaySessionName = details.value(QStringLiteral("displayName")).toString();
    if (m_replaySessionName.isEmpty())
        m_replaySessionName = details.value(QStringLiteral("missionRouteName")).toString();
    if (m_replaySessionName.isEmpty())
        m_replaySessionName = details.value(QStringLiteral("startedLabel"), QStringLiteral("历史会话")).toString();
    m_replayModelId = details.value(QStringLiteral("modelId"), QStringLiteral("gz_x500")).toString();
    m_replayModelName = details.value(QStringLiteral("modelName"), QStringLiteral("x500")).toString();
    const QString recordedWorldId = details.value(QStringLiteral("worldId")).toString();
    m_replayWorldRecorded = !recordedWorldId.isEmpty();
    m_replayWorldId = recordedWorldId;
    if (m_replayWorldId.isEmpty()) {
        m_replayWorldId = QSettings().value(
            QStringLiteral("replay/worldOverrides/%1").arg(sessionId), m_selectedWorldId).toString();
    }
    const auto *world = findSimulationWorld(m_replayWorldId);
    if (!world) {
        m_replayWorldId = QStringLiteral("default");
        m_replayWorldRecorded = false;
        world = findSimulationWorld(m_replayWorldId);
    }
    m_replayWorldName = m_replayWorldRecorded
                            ? details.value(QStringLiteral("worldName"),
                                            world ? world->name : QStringLiteral("默认场地")).toString()
                            : (world ? world->name : QStringLiteral("默认场地"));
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
        QVector3D fallbackStart = m_replay.position();
        if (!details.contains(QStringLiteral("missionStartEastMeters"))) {
            for (const QVariant &eventValue : m_replay.events()) {
                const QVariantMap event = eventValue.toMap();
                if (!event.value(QStringLiteral("label")).toString()
                         .contains(QStringLiteral("执行航线快照"))) continue;
                m_replay.seek(event.value(QStringLiteral("timeMs")).toLongLong());
                fallbackStart = m_replay.position();
                m_replay.seek(0);
                break;
            }
        }
        const QVector3D missionStart(
            static_cast<float>(details.value(QStringLiteral("missionStartEastMeters"), fallbackStart.x()).toDouble()),
            static_cast<float>(details.value(QStringLiteral("missionStartUpMeters"), fallbackStart.y()).toDouble()),
            static_cast<float>(details.value(QStringLiteral("missionStartSouthMeters"), fallbackStart.z()).toDouble()));
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
    m_replaySessionId.clear();
    m_replaySessionName.clear();
    m_replayModelId.clear();
    m_replayModelName.clear();
    m_replayWorldId.clear();
    m_replayWorldName.clear();
    m_replayWorldPreviewDirectory = {};
    m_replayWorldRecorded = false;
    m_replayRouteAnalysis.clear();
    m_replayPlannedPathGeometry.clearPath();
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
         {QStringLiteral("controlSettings"), draft.value(QStringLiteral("controlSettings"))}});
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
         {QStringLiteral("controlSettings"), plan.value(QStringLiteral("controlSettings"))}});
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
        const QString message = QStringLiteral("旧会话缺少当前可用的机型或仿真场景，无法直接复用");
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
         {QStringLiteral("controlSettings"), session.value(QStringLiteral("testPlanControlSettings"))}});
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
