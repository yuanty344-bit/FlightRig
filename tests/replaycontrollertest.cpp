#include "sessions/replaycontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

namespace {
bool closeEnough(double left, double right) { return std::abs(left - right) < 0.02; }

bool writeText(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(text.toUtf8());
    return true;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QString csvPath = directory.filePath(QStringLiteral("telemetry.csv"));
    const QString eventsPath = directory.filePath(QStringLiteral("events.log"));
    const QString sensorPath = directory.filePath(QStringLiteral("sensor"));
    if (!QDir().mkpath(sensorPath)) return 2;
    // Deliberately reorder the columns: replay must use header names, not fixed indexes.
    const QString csv = QStringLiteral(
        "scene_z_south_m,elapsed_ms,pitch_deg,local_position_available,scene_x_east_m,heading_deg,scene_y_up_m,roll_deg,yaw_deg,altitude_m,horizontal_speed_m_s,vertical_speed_m_s\n"
        "0,0,0,1,0,10,0,0,10,0,0,0\n"
        "-2,1000,10,1,10,30,4,20,30,4,6,-1\n"
        "-2,5000,10,1,10,30,4,20,30,4,0,0\n");
    if (!QDir(sensorPath).mkpath(QStringLiteral("rgb"))) return 2;
    const QString rgbPath = QDir(sensorPath).filePath(QStringLiteral("rgb/frame.jpg"));
    if (!writeText(csvPath, csv)
        || !writeText(rgbPath, QStringLiteral("test-jpeg"))
        || !writeText(eventsPath, QStringLiteral(
            "[12:00:00] [仿真] 开始\n"
            "[12:00:00] [连接] 已发现飞行器\n"
            "[12:00:00] [健康] 检查通过\n"
            "[12:00:01] [任务] 完成\n"))
        || !writeText(QDir(sensorPath).filePath(QStringLiteral("timeline.ndjson")),
            QStringLiteral("{\"type\":\"scan\",\"elapsedMs\":500,\"hz\":10,"
                           "\"nearestDistance\":2.5,\"nearestAngle\":45,"
                           "\"fieldOfView\":360,\"points\":[{\"x\":1,\"y\":2}]}\n"
                           "{\"type\":\"camera\",\"stream\":\"rgb\",\"elapsedMs\":500,"
                           "\"hz\":10,\"latencyMethod\":\"monotonic-sync-v1\",\"latencyMilliseconds\":12,\"file\":\"rgb/frame.jpg\"}\n"
                           "{\"type\":\"gimbal-state\",\"elapsedMs\":400,\"actual\":false,"
                           "\"targetYaw\":180,\"targetPitch\":-20}\n"
                           "{\"type\":\"gimbal-state\",\"elapsedMs\":500,\"actual\":true,"
                           "\"yaw\":-180,\"pitch\":-20,\"roll\":0}\n")))
        return 2;
    if (!ReplayController::containsPositionSamples(csvPath)) return 3;

    ReplayController controller;
    if (!controller.start(csvPath, eventsPath, QStringLiteral("2026-08-21T12:00:00"),
                          directory.path())) return 4;
    controller.pause();
    if (controller.durationMs() != 5000 || controller.events().size() != 4) return 5;
    controller.seek(500);
    const QVector3D position = controller.position();
    if (!closeEnough(position.x(), 5.0) || !closeEnough(position.y(), 2.0)
        || !closeEnough(position.z(), -1.0) || !closeEnough(controller.altitudeMeters(), 2.0)
        || !closeEnough(controller.horizontalSpeedMetersPerSecond(), 3.0))
        return 6;
    if (!controller.sensorAvailable() || controller.lidarPoints().size() != 1
        || !closeEnough(controller.lidarNearestDistanceMeters(), 2.5)
        || !closeEnough(controller.lidarFieldOfViewDegrees(), 360.0)) return 13;
    if (controller.rgbFrameDataUrl().isEmpty() || controller.rgbFrameHz() != 10.0
        || controller.rgbLatencyMilliseconds() != 12
        || !controller.gimbalFeedbackAvailable()
        || !closeEnough(controller.gimbalTargetYawDegrees(), 180.0)
        || !closeEnough(controller.gimbalYawDegrees(), -180.0)
        || controller.gimbalMoving()) return 14;
    QString savedPath;
    if (!controller.saveCurrentCameraFrame(QStringLiteral("rgb"),
                                           directory.filePath(QStringLiteral("captures")),
                                           &savedPath)
        || !QFileInfo(savedPath).isFile()) return 15;
    controller.seek(2500);
    if (!controller.rgbStreamInterrupted() || !controller.rgbFrameDataUrl().isEmpty()
        || !controller.lidarPoints().isEmpty()) return 16;
    controller.seek(500);
    if (controller.pathGeometry()->pointCount() < 2) return 7;
    controller.seekEvent(2);
    controller.seekPreviousEvent();
    if (controller.timeMs() != 0 || controller.currentEventIndex() != 1) return 8;
    controller.seekPreviousEvent();
    if (controller.timeMs() != 0 || controller.currentEventIndex() != 0) return 9;
    controller.seekNextEvent();
    if (controller.timeMs() != 0 || controller.currentEventIndex() != 1) return 10;
    controller.seekNextEvent();
    if (controller.timeMs() != 0 || controller.currentEventIndex() != 2) return 11;
    controller.seekNextEvent();
    if (controller.timeMs() != 1000 || controller.currentEventIndex() != 3) return 12;
    controller.stop();
    // Historical wall-clock differences must not reappear as trusted latency.
    const QString legacyTimeline = QDir(sensorPath).filePath(QStringLiteral("timeline.ndjson"));
    if (!writeText(legacyTimeline, QStringLiteral(
        "{\"type\":\"camera\",\"stream\":\"rgb\",\"elapsedMs\":500,"
        "\"hz\":10,\"latencyMilliseconds\":308,\"file\":\"rgb/frame.jpg\"}\n"))) return 17;
    if (!controller.start(csvPath, eventsPath, QStringLiteral("2026-08-21T12:00:00"),
                          directory.path())) return 18;
    controller.pause();
    controller.seek(500);
    if (controller.rgbFrameDataUrl().isEmpty() || controller.rgbLatencyMilliseconds() != -1) return 19;
    controller.seek(0);
    if (controller.rgbLatencyMilliseconds() != -1) return 20;
    controller.stop();
    return 0;
}
