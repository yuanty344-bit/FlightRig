#include "app/appcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>

namespace {
enum class Phase {
    WaitDocker,
    WaitVehicle,
    WaitStableHealth,
    WaitAltitude,
    WaitKeyboardControl,
    MoveKeyboardControl,
    WaitPreciseTurn,
    WaitKeyboardControlStopped,
    WaitLanding,
    WaitStopped
};

bool testContainerExists(const QString &containerName)
{
    const QString dockerPath = QStandardPaths::findExecutable(QStringLiteral("docker"));
    if (dockerPath.isEmpty()) {
        return true;
    }

    QProcess inspect;
    inspect.start(dockerPath,
                  {QStringLiteral("inspect"), containerName});
    if (!inspect.waitForStarted(3000) || !inspect.waitForFinished(5000)) {
        return true;
    }
    return inspect.exitStatus() == QProcess::NormalExit && inspect.exitCode() == 0;
}

double normalizedHeading(double value)
{
    double result = std::fmod(value, 360.0);
    if (result < 0.0) result += 360.0;
    return result;
}

double headingError(double target, double current)
{
    double error = std::fmod(target - current + 540.0, 360.0) - 180.0;
    if (error <= -180.0) error += 360.0;
    return std::abs(error);
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("FlightBenchSITLFlightTest"));
    QCoreApplication::setOrganizationName(QStringLiteral("FlightBench"));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temporaryData(
        QDir::tempPath() + QStringLiteral("/FlightBench-SITLFlight-XXXXXX"));
    if (!temporaryData.isValid()) {
        qCritical("FLIGHT_TEST_FAILED: unable to create isolated data directory");
        return 3;
    }
    qputenv("FLIGHTBENCH_DATA_ROOT", QFile::encodeName(temporaryData.path()));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       temporaryData.path());

    const QStringList arguments = application.arguments();
    const qsizetype modelIndex = arguments.indexOf(QStringLiteral("--model"));
    const QString modelId = modelIndex >= 0 && modelIndex + 1 < arguments.size()
                                ? arguments.at(modelIndex + 1)
                                : QStringLiteral("gz_x500");
    const qsizetype worldIndex = arguments.indexOf(QStringLiteral("--world"));
    const QString worldId = worldIndex >= 0 && worldIndex + 1 < arguments.size()
                                ? arguments.at(worldIndex + 1)
                                : QStringLiteral("default");
    const bool connectionOnly = arguments.contains(QStringLiteral("--connection-only"));
    const QString containerName = QStringLiteral("flightbench-test-flight-%1")
                                      .arg(QCoreApplication::applicationPid());

    AppController controller;
    controller.setContainerName(containerName);
    controller.setSelectedModelId(modelId);
    controller.setSelectedWorldId(worldId);
    const QVariantMap savedPlan = controller.saveCurrentTestPlan({
        {QStringLiteral("name"), QStringLiteral("SITL 飞行自动验收")},
        {QStringLiteral("description"), QStringLiteral("自动测试专用，数据与用户目录隔离")},
        {QStringLiteral("type"), QStringLiteral("free-flight")},
        {QStringLiteral("modelId"), modelId},
        {QStringLiteral("worldId"), worldId},
        {QStringLiteral("takeoffAltitudeMeters"), 5.0},
        {QStringLiteral("endAction"), QStringLiteral("hold")},
        {QStringLiteral("acceptancePreset"), QStringLiteral("standard")}});
    if (!savedPlan.value(QStringLiteral("success")).toBool()) {
        qCritical("FLIGHT_TEST_FAILED: unable to create isolated test plan: %s",
                  qPrintable(savedPlan.value(QStringLiteral("message")).toString()));
        return 3;
    }

    Phase phase = Phase::WaitDocker;
    QElapsedTimer phaseClock;
    phaseClock.start();
    QElapsedTimer stateReportClock;
    stateReportClock.start();
    bool startRequested = false;
    bool sawVerticalMotion = false;
    bool sawKeyboardMotion = false;
    bool sawPreciseTurn = false;
    QVector3D keyboardStartPosition;
    double preciseTargetHeading = 0.0;
    bool reportedCleanupWait = false;
    int lastReportedAltitude = -1;

    auto fail = [&application, &controller, &containerName](const char *reason) {
        qCritical("FLIGHT_TEST_FAILED: %s; container=%s; status=%s; guidance=%s",
                  reason, qPrintable(containerName), qPrintable(controller.statusMessage()),
                  qPrintable(controller.testRunGuidance()));
        application.exit(2);
    };

    QObject::connect(&controller, &AppController::statusMessageChanged, &application, [&controller] {
        qWarning("FLIGHT_TEST_STATUS: %s", qPrintable(controller.statusMessage()));
    });

    QTimer pollTimer;
    pollTimer.setInterval(250);
    QObject::connect(&pollTimer, &QTimer::timeout, &application, [&] {
        if (stateReportClock.elapsed() >= 5000) {
            stateReportClock.restart();
            qWarning("FLIGHT_TEST_STATE: phase=%d docker=%d sitl=%d connected=%d ready=%d telemetry=%d imu=%d gps=%d battery=%d armed=%d in_air=%d busy=%d altitude=%.2f",
                     static_cast<int>(phase),
                     controller.dockerAvailable(), controller.simulationRunning(),
                     controller.vehicleConnected(), controller.vehicleReady(),
                     controller.telemetryAvailable(), controller.imuAvailable(),
                     controller.gpsAvailable(), controller.batteryAvailable(),
                     controller.armed(), controller.inAir(), controller.busy(),
                     controller.altitudeMeters());
        }
        switch (phase) {
        case Phase::WaitDocker:
            if (controller.dockerAvailable()) {
                if (!controller.busy()) {
                    if (controller.simulationRunning() && !startRequested) {
                        fail("the isolated SITL container was already running before the test");
                        break;
                    }
                    if (!startRequested) {
                        qWarning("FLIGHT_TEST_STEP: starting PX4 SITL container");
                        if (!controller.startSimulation()) {
                            fail("preflight rejected the SITL start request");
                            break;
                        }
                        startRequested = true;
                        phaseClock.restart();
                    } else if (controller.simulationRunning()) {
                        qWarning("FLIGHT_TEST_STEP: PX4 SITL container is running");
                        phase = Phase::WaitVehicle;
                        phaseClock.restart();
                    }
                }
                if (startRequested && !controller.simulationRunning()
                    && !controller.busy() && phaseClock.elapsed() > 1000) {
                    fail("Docker rejected or ended the SITL start request");
                } else if (startRequested && phaseClock.elapsed() > 45000) {
                    fail("timed out waiting for the SITL container to start");
                }
            } else if (phaseClock.elapsed() > 30000) {
                fail("Docker Desktop did not become ready");
            }
            break;

        case Phase::WaitVehicle:
            if (controller.vehicleConnected()) {
                qWarning("FLIGHT_TEST_STEP: MAVSDK connected to PX4");
                phase = Phase::WaitStableHealth;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 70000) {
                fail("timed out waiting for the PX4 heartbeat");
            }
            break;

        case Phase::WaitStableHealth:
            if (!controller.vehicleConnected()) {
                fail("PX4 connection was lost before takeoff");
                break;
            }
            if (controller.vehicleReady() && controller.telemetryAvailable()
                && controller.imuAvailable() && controller.gpsAvailable()
                && controller.batteryAvailable() && controller.localPositionAvailable()) {
                if (!std::isfinite(controller.headingDegrees())
                    || controller.headingDegrees() < 0.0 || controller.headingDegrees() >= 360.0
                    || !std::isfinite(controller.rollDegrees())
                    || !std::isfinite(controller.pitchDegrees())
                    || !std::isfinite(controller.yawDegrees())
                    || !std::isfinite(controller.gyroRollRateDegreesPerSecond())
                    || !std::isfinite(controller.gyroPitchRateDegreesPerSecond())
                    || !std::isfinite(controller.gyroYawRateDegreesPerSecond())
                    || controller.gpsSatellites() <= 0
                    || controller.batteryRemainingPercent() < 0.0
                    || controller.batteryRemainingPercent() > 100.0
                    || controller.batteryVoltageVolts() <= 0.0
                    || !std::isfinite(controller.vehicleSceneRotation().length())
                    || std::abs(controller.vehicleSceneRotation().length() - 1.0f) > 0.01f) {
                    fail("initial telemetry values or units were invalid");
                    break;
                }
                qWarning("FLIGHT_TEST_TELEMETRY_READY: heading=%.1f deg attitude=%.1f/%.1f/%.1f deg gyro=%.1f/%.1f/%.1f deg/s gps=%s/%d battery=%.1f%%/%.2fV",
                         controller.headingDegrees(),
                         controller.rollDegrees(), controller.pitchDegrees(), controller.yawDegrees(),
                         controller.gyroRollRateDegreesPerSecond(),
                         controller.gyroPitchRateDegreesPerSecond(),
                         controller.gyroYawRateDegreesPerSecond(),
                         qPrintable(controller.gpsFixType()), controller.gpsSatellites(),
                         controller.batteryRemainingPercent(), controller.batteryVoltageVolts());
                if (connectionOnly) {
                    qWarning("FLIGHT_TEST_STEP: model %s connected with healthy telemetry; stopping PX4 SITL container",
                             qPrintable(controller.selectedModelId()));
                    controller.stopSimulation();
                    phase = Phase::WaitStopped;
                    phaseClock.restart();
                    break;
                }
                qWarning("FLIGHT_TEST_STEP: health and all telemetry feeds are ready; requesting takeoff");
                controller.takeoff();
                phase = Phase::WaitAltitude;
                phaseClock.restart();
            }
            if (phaseClock.elapsed() > 90000) {
                fail("PX4 health checks did not remain ready");
            }
            break;

        case Phase::WaitAltitude: {
            if (controller.statusMessage().contains(QStringLiteral("失败"))) {
                fail("PX4 rejected the takeoff sequence");
                break;
            }
            const int altitudeMeter = static_cast<int>(controller.altitudeMeters());
            if (std::abs(controller.verticalSpeedMetersPerSecond()) > 0.20) {
                sawVerticalMotion = true;
            }
            if (altitudeMeter > lastReportedAltitude) {
                lastReportedAltitude = altitudeMeter;
                qWarning("FLIGHT_TEST_TELEMETRY: altitude=%.2f m hspeed=%.2f m/s vspeed=%.2f m/s heading=%.1f deg mode=%s",
                         controller.altitudeMeters(),
                         controller.speedMetersPerSecond(),
                         controller.verticalSpeedMetersPerSecond(),
                         controller.headingDegrees(),
                         qPrintable(controller.flightMode()));
            }
            if (controller.inAir() && controller.altitudeMeters() >= 4.0 && !controller.busy()) {
                if (controller.vehicleScenePosition().y() < 3.5f) {
                    fail("3D scene height did not follow the PX4 local position");
                    break;
                }
                qWarning("FLIGHT_TEST_STEP: takeoff verified; starting keyboard offboard control");
                controller.setControlSpeedMetersPerSecond(1.0);
                controller.startKeyboardControl();
                phase = Phase::WaitKeyboardControl;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 90000) {
                fail("vehicle did not reach the expected takeoff altitude");
            }
            break;
        }

        case Phase::WaitKeyboardControl:
            if (controller.statusMessage().contains(QStringLiteral("失败"))
                || controller.statusMessage().contains(QStringLiteral("无法启用"))) {
                fail("PX4 rejected keyboard offboard control");
                break;
            }
            if (controller.keyboardControlActive() && !controller.busy()) {
                qWarning("FLIGHT_TEST_STEP: keyboard control active; commanding forward velocity");
                keyboardStartPosition = controller.vehicleScenePosition();
                controller.setKeyboardControlAxes(1.0, 0.0, 0.0, 0.0);
                phase = Phase::MoveKeyboardControl;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 15000) {
                fail("keyboard offboard control did not become active");
            }
            break;

        case Phase::MoveKeyboardControl:
            controller.setKeyboardControlAxes(1.0, 0.0, 0.0, 0.0);
            if (controller.speedMetersPerSecond() > 0.25) sawKeyboardMotion = true;
            if (phaseClock.elapsed() >= 1800) {
                controller.releaseKeyboardControl();
                preciseTargetHeading = normalizedHeading(controller.headingDegrees() + 45.0);
                controller.turnKeyboardControl(45.0);
                phase = Phase::WaitPreciseTurn;
                phaseClock.restart();
            }
            break;

        case Phase::WaitPreciseTurn:
            controller.heartbeatKeyboardControl();
            if (controller.preciseTurnActive()) sawPreciseTurn = true;
            if (sawPreciseTurn && !controller.preciseTurnActive()
                && controller.statusMessage().contains(QStringLiteral("精确转向完成"))) {
                const double error = headingError(preciseTargetHeading,
                                                  controller.headingDegrees());
                if (error > 4.0) {
                    fail("precise 45 degree turn finished outside tolerance");
                    break;
                }
                qWarning("FLIGHT_TEST_STEP: precise 45 degree turn completed with %.2f deg error",
                         error);
                controller.stopKeyboardControl();
                phase = Phase::WaitKeyboardControlStopped;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 20000) {
                fail("precise 45 degree turn did not complete");
            }
            break;

        case Phase::WaitKeyboardControlStopped:
            if (!controller.keyboardControlActive() && !controller.busy()) {
                if (!sawKeyboardMotion) {
                    fail("keyboard forward command produced no horizontal motion");
                    break;
                }
                const QVector3D sceneMotion = controller.vehicleScenePosition()
                                              - keyboardStartPosition;
                const double horizontalSceneMotion = std::hypot(sceneMotion.x(), sceneMotion.z());
                if (horizontalSceneMotion < 0.25
                    || controller.flightPathPointCount() < 3
                    || controller.flightPathLengthMeters() < 0.25) {
                    fail("3D position or trajectory did not follow keyboard flight motion");
                    break;
                }
                qWarning("FLIGHT_TEST_STEP: verifying direct stop is blocked while airborne");
                controller.stopSimulation();
                if (!controller.simulationRunning()
                    || !controller.statusMessage().contains(QStringLiteral("降落并停止"))) {
                    fail("direct simulation stop was not blocked while airborne");
                    break;
                }
                qWarning("FLIGHT_TEST_STEP: direct stop blocked; requesting safe land and stop");
                controller.landAndStopSimulation();
                phase = Phase::WaitLanding;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 15000) {
                fail("keyboard offboard control did not stop cleanly");
            }
            break;

        case Phase::WaitLanding:
            if (std::abs(controller.verticalSpeedMetersPerSecond()) > 0.20) {
                sawVerticalMotion = true;
            }
            if (controller.statusMessage().contains(QStringLiteral("降落失败"))) {
                fail("PX4 rejected the landing command");
                break;
            }
            if (!controller.inAir() && !controller.armed() && controller.altitudeMeters() < 0.5) {
                if (!sawVerticalMotion) {
                    fail("vertical speed never reflected takeoff or landing motion");
                    break;
                }
                qWarning("FLIGHT_TEST_STEP: landing verified; waiting for automatic container stop");
                phase = Phase::WaitStopped;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 120000) {
                fail("vehicle did not finish landing and disarming");
            }
            break;

        case Phase::WaitStopped:
            if (!controller.simulationRunning() && !controller.busy()) {
                if (!testContainerExists(containerName)) {
                    const QVariantList sessions = controller.flightSessions();
                    if (sessions.isEmpty()) {
                        fail("flight session summary was not created");
                        break;
                    }
                    const QVariantMap session = sessions.first().toMap();
                    QFile csvFile(session.value(QStringLiteral("csvPath")).toString());
                    const bool csvHeaderReady = csvFile.open(QIODevice::ReadOnly | QIODevice::Text)
                                                && csvFile.readLine().contains("keyboard_control_active")
                                                && csvFile.seek(0)
                                                && csvFile.readLine().contains("scene_x_east_m")
                                                && csvFile.readLine().size() > 0;
                    const QVariantMap details = controller.sessionDetails(
                        session.value(QStringLiteral("id")).toString());
                    const bool controlEventReady = connectionOnly
                                                       || details.value(QStringLiteral("events"))
                                                              .toStringList().join(QLatin1Char('\n'))
                                                              .contains(QStringLiteral("键盘控制已启用"))
                                                              && details.value(QStringLiteral("events"))
                                                                     .toStringList().join(QLatin1Char('\n'))
                                                                     .contains(QStringLiteral("精确转向完成"));
                    const bool filesReady = QFileInfo(session.value(QStringLiteral("csvPath")).toString()).size() > 300
                                            && QFileInfo(session.value(QStringLiteral("eventsPath")).toString()).size() > 0
                                            && QFileInfo(QFileInfo(session.value(QStringLiteral("folderPath")).toString()).absoluteFilePath()
                                                             + QStringLiteral("/session.json")).size() > 0;
                    if (session.value(QStringLiteral("modelId")).toString() != controller.selectedModelId()
                        || session.value(QStringLiteral("healthResult")).toString() != QStringLiteral("已通过")
                        || session.value(QStringLiteral("status")).toString() != QStringLiteral("完成")
                        || session.value(QStringLiteral("sampleCount")).toLongLong() < 5
                        || !csvHeaderReady
                        || !controlEventReady
                        || !filesReady) {
                        fail("flight session metadata or data files were incomplete");
                        break;
                    }
                    if (!connectionOnly
                        && (session.value(QStringLiteral("takeoffResult")).toString() != QStringLiteral("成功")
                            || session.value(QStringLiteral("landingResult")).toString() != QStringLiteral("成功")
                            || session.value(QStringLiteral("maxAltitude")).toDouble() < 4.0
                            || session.value(QStringLiteral("maxVerticalSpeed")).toDouble() < 0.2)) {
                        fail("flight session did not preserve the verified flight result");
                        break;
                    }
                    if (connectionOnly) {
                        qWarning("FLIGHT_TEST_OK: model %s connect, health, telemetry and cleanup passed",
                                 qPrintable(controller.selectedModelId()));
                    } else {
                        qWarning("FLIGHT_TEST_OK: connect, telemetry, takeoff, landing and container cleanup all passed");
                    }
                    application.exit(0);
                } else if (!reportedCleanupWait) {
                    reportedCleanupWait = true;
                    qWarning("FLIGHT_TEST_STEP: controller stopped; waiting for Docker to remove the temporary container");
                }
            }
            if (phaseClock.elapsed() > 30000) {
                fail("PX4 SITL container still exists after stop completed");
            }
            break;
        }
    });
    pollTimer.start();

    QTimer::singleShot(330000, &application, [&application] {
        qCritical("FLIGHT_TEST_FAILED: global timeout");
        application.exit(2);
    });
    return application.exec();
}
