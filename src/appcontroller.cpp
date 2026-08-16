#include "appcontroller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
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
    // Keep world metadata in one place. Adding a future white-model city only
    // requires another definition plus its packaged simulation assets.
    static const QList<SimulationWorldDefinition> definitions{
        {QStringLiteral("default"), QStringLiteral("默认场地"),
         QStringLiteral("开阔地面，适合基础飞行与功能验证"), {}},
        {QStringLiteral("xatu_weiyang"), QStringLiteral("西安工业大学未央校区"),
         QStringLiteral("校园白模，包含建筑、道路、绿地和水面"),
         QStringLiteral("xatu_weiyang/meshes")},
    };
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
    return preset == QStringLiteral("orange")
           || preset == QStringLiteral("blue")
           || preset == QStringLiteral("teal")
           || preset == QStringLiteral("violet");
}
}

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    connect(&m_sessionRecorder, &FlightSessionRecorder::activeSessionChanged,
            this, &AppController::activeFlightSessionChanged);
    connect(&m_sessionRecorder, &FlightSessionRecorder::completedSessionsChanged,
            this, &AppController::flightSessionsChanged);
    connect(&m_routeLibrary, &RouteLibrary::routesChanged,
            this, &AppController::savedRoutesChanged);
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
            m_sessionRecorder.startSession(selectedModelId(), selectedModelName(), takeoffAltitude());
            m_connectionTimedOut = false;
            if (!vehicleConnected()) m_connectionTimeout.start();
        } else {
            setLandingAndStopping(false);
            m_sessionRecorder.finishSession(QStringLiteral("仿真已停止"));
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
    m_flightPathVisible = settings.value(QStringLiteral("flightPath/visible"), true).toBool();
    m_flightPathGeometry.setWidthMeters(
        settings.value(QStringLiteral("flightPath/widthMeters"), 0.06).toDouble());
    m_flightPathGeometry.setMaxVisibleLengthMeters(
        settings.value(QStringLiteral("flightPath/maxVisibleLengthMeters"), 100).toInt());
    const QString savedFlightPathColor =
        settings.value(QStringLiteral("flightPath/colorPreset"), QStringLiteral("orange")).toString();
    if (isFlightPathColorPreset(savedFlightPathColor)) {
        m_flightPathColorPreset = savedFlightPathColor;
    }
    const QString savedModelId = settings.value(QStringLiteral("simulation/modelId"),
                                                QStringLiteral("gz_x500")).toString();
    if (findSimulationModel(savedModelId)) m_selectedModelId = savedModelId;
    const QString savedWorldId = settings.value(QStringLiteral("simulation/worldId"),
                                                QStringLiteral("default")).toString();
    if (findSimulationWorld(savedWorldId)) m_selectedWorldId = savedWorldId;
    const QString savedContainerName = settings.value(
        QStringLiteral("simulation/containerName"), QStringLiteral("flightbench-px4")).toString();
    if (isValidContainerName(savedContainerName)) m_containerName = savedContainerName;
    appendDiagnostic(QStringLiteral("应用"), QStringLiteral("FlightBench 已启动，正在检查本机环境"));

    m_sessionSampleTimer.setInterval(200);
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
    m_sessionSampleTimer.stop();
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

void AppController::startSimulation()
{
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
    m_sessionRecorder.setMissionPlan(routeId, routeName, missionSnapshot, endAction);
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
    m_flightPathGeometry.clearPath();
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
    if (qFuzzyCompare(previousValue, m_flightPathGeometry.widthMeters())) return;
    QSettings().setValue(QStringLiteral("flightPath/widthMeters"),
                         m_flightPathGeometry.widthMeters());
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathColorPreset(const QString &preset)
{
    if (!isFlightPathColorPreset(preset) || m_flightPathColorPreset == preset) return;
    m_flightPathColorPreset = preset;
    QSettings().setValue(QStringLiteral("flightPath/colorPreset"), preset);
    emit flightPathDisplaySettingsChanged();
}

void AppController::setFlightPathMaxVisibleLengthMeters(int value)
{
    const int previousValue = m_flightPathGeometry.maxVisibleLengthMeters();
    m_flightPathGeometry.setMaxVisibleLengthMeters(value);
    if (previousValue == m_flightPathGeometry.maxVisibleLengthMeters()) return;
    QSettings().setValue(QStringLiteral("flightPath/maxVisibleLengthMeters"),
                         m_flightPathGeometry.maxVisibleLengthMeters());
    emit flightPathDisplaySettingsChanged();
}

void AppController::resetFlightPathDisplaySettings()
{
    setFlightPathVisible(true);
    setFlightPathWidthMeters(0.06);
    setFlightPathColorPreset(QStringLiteral("orange"));
    setFlightPathMaxVisibleLengthMeters(100);
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
        world.insert(QStringLiteral("hasPreview"), !definition.previewModelDirectory.isEmpty());
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
    return m_sessionRecorder.sessionDetails(sessionId);
}

QString AppController::exportSessionCsv(const QString &sessionId)
{
    const QString sourcePath = m_sessionRecorder.csvPathForSession(sessionId);
    if (sourcePath.isEmpty()) return {};

    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString exportDirectory = QDir(documents).filePath(QStringLiteral("FlightBench/Exports"));
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

QString AppController::exportDiagnosticReport()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString directoryPath = QDir(documents).filePath(QStringLiteral("FlightBench"));
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
        action = QStringLiteral("refresh");
        label = QStringLiteral("重新检测");
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
