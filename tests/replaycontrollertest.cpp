#include "replaycontroller.h"

#include <QCoreApplication>
#include <QFile>
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
    // Deliberately reorder the columns: replay must use header names, not fixed indexes.
    const QString csv = QStringLiteral(
        "scene_z_south_m,elapsed_ms,pitch_deg,local_position_available,scene_x_east_m,heading_deg,scene_y_up_m,roll_deg,yaw_deg,altitude_m,horizontal_speed_m_s,vertical_speed_m_s\n"
        "0,0,0,1,0,10,0,0,10,0,0,0\n"
        "-2,1000,10,1,10,30,4,20,30,4,6,-1\n");
    if (!writeText(csvPath, csv)
        || !writeText(eventsPath, QStringLiteral(
            "[12:00:00] [仿真] 开始\n"
            "[12:00:00] [连接] 已发现飞行器\n"
            "[12:00:00] [健康] 检查通过\n"
            "[12:00:01] [任务] 完成\n")))
        return 2;
    if (!ReplayController::containsPositionSamples(csvPath)) return 3;

    ReplayController controller;
    if (!controller.start(csvPath, eventsPath, QStringLiteral("2026-08-21T12:00:00"))) return 4;
    controller.pause();
    if (controller.durationMs() != 1000 || controller.events().size() != 4) return 5;
    controller.seek(500);
    const QVector3D position = controller.position();
    if (!closeEnough(position.x(), 5.0) || !closeEnough(position.y(), 2.0)
        || !closeEnough(position.z(), -1.0) || !closeEnough(controller.altitudeMeters(), 2.0)
        || !closeEnough(controller.horizontalSpeedMetersPerSecond(), 3.0))
        return 6;
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
    return 0;
}
