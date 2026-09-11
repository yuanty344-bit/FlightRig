#include "app/appcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector2D>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>

namespace {
enum class Phase {
    WaitDocker,
    WaitVehicle,
    WaitReady,
    WaitAltitude,
    WaitKeyboardActive,
    MoveAway,
    WaitKeyboardStopped,
    WaitMissionStart,
    FlyMission,
    WaitStopped
};

bool testContainerExists(const QString &containerName)
{
    const QString dockerPath = QStandardPaths::findExecutable(QStringLiteral("docker"));
    if (dockerPath.isEmpty()) return true;

    QProcess inspect;
    inspect.start(dockerPath,
                  {QStringLiteral("inspect"), containerName});
    if (!inspect.waitForStarted(3000) || !inspect.waitForFinished(5000)) return true;
    return inspect.exitStatus() == QProcess::NormalExit && inspect.exitCode() == 0;
}

double horizontalDistance(const QVector3D &a, const QVector3D &b)
{
    return std::hypot(static_cast<double>(a.x() - b.x()),
                      static_cast<double>(a.z() - b.z()));
}

double pointToSegmentDistance(const QVector2D &point,
                              const QVector2D &start,
                              const QVector2D &end)
{
    const QVector2D segment = end - start;
    const float lengthSquared = QVector2D::dotProduct(segment, segment);
    if (lengthSquared <= 1.0e-6f) return (point - start).length();

    const float t = std::clamp(QVector2D::dotProduct(point - start, segment)
                                   / lengthSquared,
                               0.0f, 1.0f);
    return (point - (start + segment * t)).length();
}

QVariantMap waypoint(double north, double east)
{
    return {
        {QStringLiteral("northMeters"), north},
        {QStringLiteral("eastMeters"), east},
        {QStringLiteral("altitudeMeters"), 5.0},
        {QStringLiteral("speedMetersPerSecond"), 3.0},
        {QStringLiteral("yawDegrees"), 0.0}
    };
}
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        const QByteArray utf8 = message.toUtf8();
        std::fprintf(stderr, "%s\n", utf8.constData());
        std::fflush(stderr);
    });
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("FlightBenchSITLMissionTest"));
    QCoreApplication::setOrganizationName(QStringLiteral("FlightBench"));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temporaryData(
        QDir::tempPath() + QStringLiteral("/FlightBench-SITLMission-XXXXXX"));
    if (!temporaryData.isValid()) {
        qCritical("MISSION_TEST_FAILED: unable to create isolated data directory");
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
    const QString containerName = QStringLiteral("flightbench-test-mission-%1")
                                      .arg(QCoreApplication::applicationPid());

    AppController controller;
    controller.setContainerName(containerName);
    controller.setSelectedModelId(modelId);
    controller.setSelectedWorldId(worldId);
    const QVariantMap savedPlan = controller.saveCurrentTestPlan({
        {QStringLiteral("name"), QStringLiteral("SITL 航点自动验收")},
        {QStringLiteral("description"), QStringLiteral("自动测试专用，数据与用户目录隔离")},
        {QStringLiteral("type"), QStringLiteral("free-flight")},
        {QStringLiteral("modelId"), modelId},
        {QStringLiteral("worldId"), worldId},
        {QStringLiteral("takeoffAltitudeMeters"), 5.0},
        {QStringLiteral("endAction"), QStringLiteral("hold")},
        {QStringLiteral("acceptancePreset"), QStringLiteral("standard")}});
    if (!savedPlan.value(QStringLiteral("success")).toBool()) {
        qCritical("MISSION_TEST_FAILED: unable to create isolated test plan: %s",
                  qPrintable(savedPlan.value(QStringLiteral("message")).toString()));
        return 3;
    }

    Phase phase = Phase::WaitDocker;
    QElapsedTimer phaseClock;
    phaseClock.start();
    QElapsedTimer reportClock;
    reportClock.start();
    bool startRequested = false;
    bool failed = false;
    QString failureReason;
    QVector3D sceneOrigin;
    QVector3D movedPosition;
    double preMissionDisplacement = 0.0;
    double maximumCrossTrackError = 0.0;
    qsizetype nextTarget = 0;

    const QVector<QVector3D> fixedTargets {
        QVector3D(0.0f, 5.0f, 0.0f),
        QVector3D(8.0f, 5.0f, 0.0f),
        QVector3D(8.0f, 5.0f, -8.0f),
        QVector3D(0.0f, 5.0f, -8.0f),
        QVector3D(0.0f, 0.0f, 0.0f)
    };
    QVector<QVector2D> plannedPolyline;

    auto beginCleanup = [&](const QString &reason) {
        if (failed) return;
        failed = true;
        failureReason = reason;
        qCritical("MISSION_TEST_FAILED: %s; container=%s; status=%s; guidance=%s; mission=%s",
                  qPrintable(reason), qPrintable(containerName),
                  qPrintable(controller.statusMessage()), qPrintable(controller.testRunGuidance()),
                  qPrintable(controller.missionStatusMessage()));
        if (!controller.simulationRunning()) {
            application.exit(2);
            return;
        }
        if (controller.inAir() || controller.armed()) {
            controller.landAndStopSimulation();
        } else {
            controller.stopSimulation();
        }
        phase = Phase::WaitStopped;
        phaseClock.restart();
    };

    auto updateRouteEvidence = [&] {
        const QVector3D position = controller.vehicleScenePosition();
        while (nextTarget < fixedTargets.size()) {
            const double horizontal = horizontalDistance(position, fixedTargets.at(nextTarget));
            const bool altitudeMatches = nextTarget + 1 == fixedTargets.size()
                                             ? controller.altitudeMeters() < 1.0
                                             : std::abs(controller.altitudeMeters() - 5.0) < 2.0;
            if (horizontal > 1.8 || !altitudeMatches) break;
            qWarning("MISSION_TEST_WAYPOINT: reached fixed scene waypoint %lld at east=%.2f north=%.2f altitude=%.2f",
                     static_cast<long long>(nextTarget + 1),
                     position.x(), -position.z(), controller.altitudeMeters());
            ++nextTarget;
        }

        if (plannedPolyline.size() >= 2 && (controller.inAir() || controller.missionRunning())) {
            const QVector2D point(position.x(), position.z());
            double closest = std::numeric_limits<double>::max();
            for (qsizetype index = 1; index < plannedPolyline.size(); ++index) {
                closest = std::min(closest,
                                   pointToSegmentDistance(point,
                                                          plannedPolyline.at(index - 1),
                                                          plannedPolyline.at(index)));
            }
            maximumCrossTrackError = std::max(maximumCrossTrackError, closest);
        }
    };

    QTimer pollTimer;
    pollTimer.setInterval(250);
    QObject::connect(&pollTimer, &QTimer::timeout, &application, [&] {
        if (reportClock.elapsed() >= 5000) {
            reportClock.restart();
            const QVector3D position = controller.vehicleScenePosition();
            qWarning("MISSION_TEST_STATE: phase=%d docker=%d connected=%d ready=%d armed=%d in_air=%d mission=%d/%d busy=%d east=%.2f north=%.2f altitude=%.2f",
                     static_cast<int>(phase), controller.simulationRunning(),
                     controller.vehicleConnected(), controller.vehicleReady(),
                     controller.armed(), controller.inAir(), controller.missionRunning(),
                     controller.missionUploaded(), controller.busy(), position.x(),
                     -position.z(), controller.altitudeMeters());
        }

        switch (phase) {
        case Phase::WaitDocker:
            if (controller.dockerAvailable()) {
                if (!controller.busy()) {
                    if (controller.simulationRunning() && !startRequested) {
                        beginCleanup(QStringLiteral("the isolated SITL container was already running"));
                        break;
                    }
                    if (!startRequested) {
                        qWarning("MISSION_TEST_STEP: starting PX4 SITL container");
                        if (!controller.startSimulation()) {
                            beginCleanup(QStringLiteral("preflight rejected the SITL start request"));
                            break;
                        }
                        startRequested = true;
                        phaseClock.restart();
                    } else if (controller.simulationRunning()) {
                        phase = Phase::WaitVehicle;
                        phaseClock.restart();
                    }
                }
                if (startRequested && !controller.simulationRunning()
                    && !controller.busy() && phaseClock.elapsed() > 1000) {
                    beginCleanup(QStringLiteral("Docker rejected or ended the SITL start request"));
                } else if (startRequested && phaseClock.elapsed() > 45000) {
                    beginCleanup(QStringLiteral("timed out waiting for the SITL container to start"));
                }
            } else if (phaseClock.elapsed() > 30000) {
                beginCleanup(QStringLiteral("Docker Desktop did not become ready"));
            }
            break;

        case Phase::WaitVehicle:
            if (controller.vehicleConnected()) {
                phase = Phase::WaitReady;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 70000) {
                beginCleanup(QStringLiteral("timed out waiting for PX4 heartbeat"));
            }
            break;

        case Phase::WaitReady:
            if (controller.vehicleReady() && controller.telemetryAvailable()
                && controller.localPositionAvailable()
                && controller.globalPositionAvailable() && !controller.busy()) {
                sceneOrigin = controller.vehicleScenePosition();
                qWarning("MISSION_TEST_STEP: telemetry ready; taking off before moving away from the fixed scene origin");
                controller.takeoff();
                phase = Phase::WaitAltitude;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 90000) {
                beginCleanup(QStringLiteral("PX4 health or position data did not become ready"));
            }
            break;

        case Phase::WaitAltitude:
            if (controller.inAir() && controller.altitudeMeters() >= 4.0 && !controller.busy()) {
                controller.setControlSpeedMetersPerSecond(1.0);
                controller.startKeyboardControl();
                phase = Phase::WaitKeyboardActive;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 90000) {
                beginCleanup(QStringLiteral("takeoff did not reach the expected altitude"));
            }
            break;

        case Phase::WaitKeyboardActive:
            if (controller.keyboardControlActive() && !controller.busy()) {
                qWarning("MISSION_TEST_STEP: moving away so mission upload cannot accidentally use the current position as its origin");
                controller.setKeyboardControlAxes(1.0, 0.0, 0.0, 0.0);
                phase = Phase::MoveAway;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 15000) {
                beginCleanup(QStringLiteral("keyboard control did not start"));
            }
            break;

        case Phase::MoveAway:
            controller.setKeyboardControlAxes(1.0, 0.0, 0.0, 0.0);
            preMissionDisplacement = horizontalDistance(controller.vehicleScenePosition(), sceneOrigin);
            if (phaseClock.elapsed() % 1000 < 250) {
                const QVector3D current = controller.vehicleScenePosition();
                std::fprintf(stderr,
                             "MISSION_TEST_MOVE: origin=(%.2f,%.2f) current=(%.2f,%.2f) distance=%.2f\n",
                             sceneOrigin.x(), sceneOrigin.z(), current.x(), current.z(),
                             preMissionDisplacement);
                std::fflush(stderr);
            }
            if (preMissionDisplacement >= 4.0) {
                controller.releaseKeyboardControl();
                controller.stopKeyboardControl();
                phase = Phase::WaitKeyboardStopped;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 10000) {
                beginCleanup(QStringLiteral("vehicle did not move far enough to expose an origin-shift regression"));
            }
            break;

        case Phase::WaitKeyboardStopped:
            if (!controller.keyboardControlActive() && !controller.busy()) {
                movedPosition = controller.vehicleScenePosition();
                preMissionDisplacement = horizontalDistance(movedPosition, sceneOrigin);
                plannedPolyline = {QVector2D(movedPosition.x(), movedPosition.z())};
                for (const QVector3D &target : fixedTargets) {
                    plannedPolyline.append(QVector2D(target.x(), target.z()));
                }

                const QVariantList mission {
                    waypoint(0.0, 0.0), waypoint(0.0, 8.0),
                    waypoint(8.0, 8.0), waypoint(8.0, 0.0),
                    waypoint(0.0, 0.0)
                };
                qWarning("MISSION_TEST_STEP: uploading fixed-origin mission from displaced position east=%.2f north=%.2f",
                         movedPosition.x(), -movedPosition.z());
                controller.uploadAndStartMission(mission, QStringLiteral("land"));
                phase = Phase::WaitMissionStart;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 15000) {
                beginCleanup(QStringLiteral("keyboard control did not stop cleanly"));
            }
            break;

        case Phase::WaitMissionStart:
            updateRouteEvidence();
            if (controller.missionRunning()) {
                qWarning("MISSION_TEST_STEP: PX4 confirmed Mission mode");
                phase = Phase::FlyMission;
                phaseClock.restart();
            } else if (!controller.missionBusy() && phaseClock.elapsed() > 10000) {
                beginCleanup(QStringLiteral("mission did not enter PX4 Mission mode"));
            } else if (phaseClock.elapsed() > 30000) {
                beginCleanup(QStringLiteral("mission start confirmation timed out"));
            }
            break;

        case Phase::FlyMission:
            updateRouteEvidence();
            if (!controller.inAir() && !controller.armed()
                && controller.altitudeMeters() < 0.5) {
                if (nextTarget != fixedTargets.size()) {
                    beginCleanup(QStringLiteral("mission landed without reaching every fixed scene waypoint"));
                    break;
                }
                if (maximumCrossTrackError > 3.0) {
                    beginCleanup(QStringLiteral("mission exceeded the allowed cross-track error"));
                    break;
                }
                qWarning("MISSION_TEST_STEP: route verified; displacement before upload=%.2f m, maximum cross-track error=%.2f m",
                         preMissionDisplacement, maximumCrossTrackError);
                controller.stopSimulation();
                phase = Phase::WaitStopped;
                phaseClock.restart();
            } else if (phaseClock.elapsed() > 150000) {
                beginCleanup(QStringLiteral("mission did not complete and land"));
            }
            break;

        case Phase::WaitStopped:
            if (!controller.simulationRunning() && !controller.busy()
                && !testContainerExists(containerName)) {
                if (failed) {
                    qCritical("MISSION_TEST_CLEANUP: %s", qPrintable(failureReason));
                    application.exit(2);
                } else {
                    qWarning("MISSION_TEST_OK: fixed-origin route remained correct after uploading from a displaced vehicle position");
                    application.exit(0);
                }
            } else if (phaseClock.elapsed() > 90000) {
                qCritical("MISSION_TEST_FAILED: cleanup timed out; original=%s",
                          qPrintable(failureReason));
                application.exit(2);
            }
            break;
        }
    });
    pollTimer.start();

    QTimer::singleShot(420000, &application, [&application] {
        qCritical("MISSION_TEST_FAILED: global timeout");
        application.exit(2);
    });
    return application.exec();
}
