#include "testing/preflightevaluator.h"

#include "mapping/worldmapcatalog.h"

#include <QPointF>
#include <QVariantList>

#include <algorithm>
#include <cmath>

namespace {
QVariantMap item(const QString &id, const QString &label, const QString &status,
                 const QString &detail, const QString &action = {})
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("label"), label},
            {QStringLiteral("status"), status},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("action"), action}};
}

double routeDistance(const QVariantList &waypoints)
{
    QPointF previous(0.0, 0.0);
    double distance = 0.0;
    for (const QVariant &entry : waypoints) {
        const QVariantMap waypoint = entry.toMap();
        const QPointF current(waypoint.value(QStringLiteral("eastMeters")).toDouble(),
                              waypoint.value(QStringLiteral("northMeters")).toDouble());
        distance += std::hypot(current.x() - previous.x(), current.y() - previous.y());
        previous = current;
    }
    return distance;
}
}

QVariantMap PreflightEvaluator::evaluate(const QVariantMap &plan,
                                         const QVariantMap &route,
                                         const QVariantMap &worldMap,
                                         bool dockerAvailable,
                                         bool imageAvailable,
                                         bool modelAvailable,
                                         bool worldAvailable,
                                         bool previewAvailable,
                                         bool containerNameValid)
{
    QVariantList checks;
    const bool requiresRoute = plan.value(QStringLiteral("requiresRoute")).toBool();
    const bool planComplete = !plan.isEmpty() && plan.value(QStringLiteral("complete")).toBool();
    checks.append(item(QStringLiteral("plan"), QStringLiteral("测试方案"),
                       planComplete ? QStringLiteral("pass") : QStringLiteral("block"),
                       planComplete ? QStringLiteral("机型、世界和方案参数完整")
                                    : requiresRoute ? QStringLiteral("当前方案还没有选择可用航线")
                                                    : QStringLiteral("当前方案不完整"),
                       QStringLiteral("plan")));
    checks.append(item(QStringLiteral("docker"), QStringLiteral("Docker Desktop"),
                       dockerAvailable ? QStringLiteral("pass") : QStringLiteral("block"),
                       dockerAvailable ? QStringLiteral("Docker 引擎可以使用")
                                       : QStringLiteral("请先启动 Docker Desktop"),
                       QStringLiteral("environment")));
    checks.append(item(QStringLiteral("image"), QStringLiteral("PX4 SITL 镜像"),
                       imageAvailable ? QStringLiteral("pass") : QStringLiteral("block"),
                       imageAvailable ? QStringLiteral("已找到 px4io/px4-sitl-gazebo")
                                      : QStringLiteral("本机没有找到所需仿真镜像"),
                       QStringLiteral("environment")));
    checks.append(item(QStringLiteral("container"), QStringLiteral("临时容器名称"),
                       containerNameValid ? QStringLiteral("pass") : QStringLiteral("block"),
                       containerNameValid ? QStringLiteral("名称格式可以用于本次临时容器")
                                          : QStringLiteral("容器名称格式无效"),
                       QStringLiteral("environment")));
    checks.append(item(QStringLiteral("model"), QStringLiteral("飞行器资源"),
                       modelAvailable ? QStringLiteral("pass") : QStringLiteral("block"),
                       modelAvailable ? QStringLiteral("方案机型已登记并可供启动")
                                      : QStringLiteral("方案使用的机型当前不可用"),
                       QStringLiteral("plan")));
    checks.append(item(QStringLiteral("world"), QStringLiteral("仿真世界"),
                       worldAvailable ? QStringLiteral("pass") : QStringLiteral("block"),
                       worldAvailable ? QStringLiteral("场景资源已登记")
                                      : QStringLiteral("方案使用的仿真世界当前不可用"),
                       QStringLiteral("environment")));

    const QString worldId = plan.value(QStringLiteral("worldId")).toString();
    const QVariantMap acceptanceRules = plan.value(QStringLiteral("acceptanceRules")).toMap();
    const double maximumAltitude = std::clamp(
        acceptanceRules.value(QStringLiteral("maximumAltitudeMeters"), 100.0).toDouble(),
        1.0, 100.0);
    const int maximumDurationSeconds = std::clamp(
        acceptanceRules.value(QStringLiteral("maximumDurationSeconds"), 1200).toInt(),
        60, 7200);
    const double takeoffAltitude = plan.value(
        QStringLiteral("takeoffAltitudeMeters"), 5.0).toDouble();
    checks.append(item(QStringLiteral("takeoff-altitude"), QStringLiteral("起飞高度"),
                       takeoffAltitude <= maximumAltitude ? QStringLiteral("pass")
                                                          : QStringLiteral("block"),
                       takeoffAltitude <= maximumAltitude
                           ? QStringLiteral("%1 米，不超过方案的 %2 米限制")
                                 .arg(QString::number(takeoffAltitude, 'f', 1),
                                      QString::number(maximumAltitude, 'f', 1))
                           : QStringLiteral("起飞高度 %1 米超过方案最大高度 %2 米")
                                 .arg(QString::number(takeoffAltitude, 'f', 1),
                                      QString::number(maximumAltitude, 'f', 1)),
                       QStringLiteral("plan")));
    const bool cityWorld = worldId != QStringLiteral("default");
    const bool mapAvailable = worldMap.value(QStringLiteral("available")).toBool();
    checks.append(item(QStringLiteral("map"), QStringLiteral("二维地图与建筑数据"),
                       !cityWorld || mapAvailable ? QStringLiteral("pass")
                                                  : QStringLiteral("warning"),
                       !cityWorld ? QStringLiteral("默认场地不需要建筑地图")
                       : mapAvailable ? QStringLiteral("任务地图和建筑障碍数据可用")
                                      : QStringLiteral("可以启动，但无法进行建筑碰撞预检查")));
    checks.append(item(QStringLiteral("preview"), QStringLiteral("三维场景模型"),
                       !cityWorld || previewAvailable ? QStringLiteral("pass")
                                                      : QStringLiteral("warning"),
                       !cityWorld ? QStringLiteral("默认场地使用开阔地面")
                       : previewAvailable ? QStringLiteral("Qt 三维白模目录可用")
                                          : QStringLiteral("Gazebo 可启动，但软件内三维白模可能缺失")));
    if (cityWorld && mapAvailable) {
        const double spawnClearance = worldMap.value(
            QStringLiteral("recommendedTakeoffClearanceMeters")).toDouble();
        checks.append(item(QStringLiteral("spawn"), QStringLiteral("起飞点安全"),
                           spawnClearance >= 5.0 ? QStringLiteral("pass")
                                                : QStringLiteral("warning"),
                           QStringLiteral("世界原点周围最近建筑约 %1 米")
                               .arg(QString::number(spawnClearance, 'f', 1))));
    }

    const QVariantList waypoints = route.value(QStringLiteral("waypoints")).toList();
    if (requiresRoute) {
        checks.append(item(QStringLiteral("waypoints"), QStringLiteral("航点数量"),
                           waypoints.isEmpty() ? QStringLiteral("block") : QStringLiteral("pass"),
                           waypoints.isEmpty() ? QStringLiteral("请选择至少包含一个航点的航线")
                                               : QStringLiteral("共 %1 个航点").arg(waypoints.size()),
                           QStringLiteral("route")));

        bool coordinateValid = !waypoints.isEmpty();
        for (const QVariant &entry : waypoints) {
            const QVariantMap point = entry.toMap();
            bool eastOk = false;
            bool northOk = false;
            bool altitudeOk = false;
            const double east = point.value(QStringLiteral("eastMeters")).toDouble(&eastOk);
            const double north = point.value(QStringLiteral("northMeters")).toDouble(&northOk);
            const double altitude = point.value(QStringLiteral("altitudeMeters")).toDouble(&altitudeOk);
            coordinateValid = coordinateValid && eastOk && northOk && altitudeOk
                              && std::isfinite(east) && std::isfinite(north)
                              && std::isfinite(altitude) && altitude >= 1.0
                              && altitude <= maximumAltitude;
        }
        checks.append(item(QStringLiteral("coordinates"), QStringLiteral("航点坐标与高度"),
                           coordinateValid ? QStringLiteral("pass") : QStringLiteral("block"),
                           coordinateValid
                               ? QStringLiteral("东/北坐标有效，高度均不超过 %1 米")
                                     .arg(QString::number(maximumAltitude, 'f', 1))
                               : QStringLiteral("存在无效坐标或超过方案最大高度的航点"),
                           QStringLiteral("route")));

        const double distance = routeDistance(waypoints);
        double averageSpeed = 0.0;
        int speedSamples = 0;
        for (const QVariant &entry : waypoints) {
            const double speed = entry.toMap().value(QStringLiteral("speedMetersPerSecond"), 2.0).toDouble();
            if (speed > 0.0) { averageSpeed += speed; ++speedSamples; }
        }
        averageSpeed = speedSamples > 0 ? averageSpeed / speedSamples : 2.0;
        const int seconds = static_cast<int>(std::ceil(distance / std::max(0.5, averageSpeed)));
        const bool withinDuration = seconds <= maximumDurationSeconds;
        checks.append(item(QStringLiteral("estimate"), QStringLiteral("预计航程"),
                           withinDuration ? QStringLiteral("pass")
                                          : QStringLiteral("warning"),
                           withinDuration
                               ? QStringLiteral("约 %1 米 · %2 分 %3 秒")
                                     .arg(QString::number(distance, 'f', 0))
                                     .arg(seconds / 60).arg(seconds % 60)
                               : QStringLiteral("预计 %1 分 %2 秒，超过方案的 %3 分钟限制")
                                     .arg(seconds / 60).arg(seconds % 60)
                                     .arg(maximumDurationSeconds / 60),
                           withinDuration ? QString() : QStringLiteral("plan")));

        if (coordinateValid && mapAvailable) {
            const QVariantMap safety = WorldMapCatalog::analyzeRoute(
                worldId, waypoints, 0.0, 0.0,
                plan.value(QStringLiteral("takeoffAltitudeMeters"), 5.0).toDouble());
            const bool safe = safety.value(QStringLiteral("safe"), true).toBool();
            checks.append(item(QStringLiteral("safety"), QStringLiteral("建筑碰撞预检查"),
                               safe ? QStringLiteral("pass") : QStringLiteral("block"),
                               safety.value(QStringLiteral("message")).toString(),
                               safe ? QString() : QStringLiteral("auto-fix")));
        }
    }

    checks.append(item(QStringLiteral("mavsdk"), QStringLiteral("MAVSDK 监听地址"),
                       QStringLiteral("pass"), QStringLiteral("UDP 127.0.0.1:14540")));

    int blockCount = 0;
    int warningCount = 0;
    for (const QVariant &entry : checks) {
        const QString status = entry.toMap().value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("block")) ++blockCount;
        else if (status == QStringLiteral("warning")) ++warningCount;
    }
    const QString summary = blockCount > 0
                                ? QStringLiteral("发现 %1 个必须处理的问题").arg(blockCount)
                            : warningCount > 0
                                ? QStringLiteral("可以开始，另有 %1 项提醒").arg(warningCount)
                                : QStringLiteral("全部检查完成，可以启动仿真");
    return {{QStringLiteral("checks"), checks},
            {QStringLiteral("blockCount"), blockCount},
            {QStringLiteral("warningCount"), warningCount},
            {QStringLiteral("canStart"), blockCount == 0},
            {QStringLiteral("summary"), summary}};
}
