#include "flight/px4eventtranslator.h"

namespace {
QVariantMap explanation(const QString &title, const QString &what,
                        const QString &impact, const QString &action,
                        const QString &technical)
{
    return {{QStringLiteral("title"), title}, {QStringLiteral("what"), what},
            {QStringLiteral("impact"), impact}, {QStringLiteral("action"), action},
            {QStringLiteral("technical"), technical}};
}
}

QVariantMap Px4EventTranslator::explain(const QString &issueCode,
                                        const QString &technicalMessage,
                                        const QVariantMap &health)
{
    if (issueCode.isEmpty()) return {};
    if (issueCode == QStringLiteral("health-timeout")) {
        if (!health.value(QStringLiteral("magnetometerOk")).toBool())
            return explanation(QStringLiteral("航向参考尚未建立"),
                               QStringLiteral("PX4 还没有获得稳定的磁航向参考。"),
                               QStringLiteral("在此之前无法安全解锁起飞。"),
                               QStringLiteral("等待传感器稳定；长时间无变化时重新启动本次仿真。"), technicalMessage);
        if (!health.value(QStringLiteral("localPositionOk")).toBool())
            return explanation(QStringLiteral("本地位置尚不可用"),
                               QStringLiteral("PX4 还没有建立无人机相对起飞点的位置。"),
                               QStringLiteral("起飞、航点任务和键盘控制会被阻止。"),
                               QStringLiteral("保持仿真运行并等待定位完成，必要时重新启动仿真。"), technicalMessage);
        if (!health.value(QStringLiteral("globalPositionOk")).toBool())
            return explanation(QStringLiteral("全局位置尚不可用"),
                               QStringLiteral("GPS 或全局坐标估计还没有准备好。"),
                               QStringLiteral("返航和依赖全局坐标的任务不可用。"),
                               QStringLiteral("等待 GPS 收敛，并检查场景是否正常运行。"), technicalMessage);
        if (!health.value(QStringLiteral("homePositionOk")).toBool())
            return explanation(QStringLiteral("Home 点尚未建立"),
                               QStringLiteral("PX4 还没有记录安全返航参考点。"),
                               QStringLiteral("返航与部分起飞检查不能通过。"),
                               QStringLiteral("等待位置稳定，PX4 会自动建立 Home 点。"), technicalMessage);
        return explanation(QStringLiteral("PX4 尚未允许解锁"),
                           QStringLiteral("传感器或飞行状态仍有一项起飞条件未满足。"),
                           QStringLiteral("软件不会绕过 PX4 的安全限制。"),
                           QStringLiteral("展开技术详情查看等待项，或重新启动本次仿真。"), technicalMessage);
    }
    if (issueCode.contains(QStringLiteral("timeout")) || issueCode.contains(QStringLiteral("connection")))
        return explanation(QStringLiteral("飞行器连接中断"),
                           QStringLiteral("FlightBench 没有继续收到 PX4 心跳。"),
                           QStringLiteral("所有飞行操作均已停止发送。"),
                           QStringLiteral("检查容器是否仍在运行，然后执行重新检测或重启仿真。"), technicalMessage);
    if (issueCode.contains(QStringLiteral("container")) || issueCode.contains(QStringLiteral("docker")))
        return explanation(QStringLiteral("仿真环境不可用"),
                           QStringLiteral("Docker 或 PX4 临时容器没有正常运行。"),
                           QStringLiteral("无法连接仿真飞行器。"),
                           QStringLiteral("打开 Docker Desktop，确认引擎启动后重新检测。"), technicalMessage);
    return explanation(QStringLiteral("当前操作需要处理"), technicalMessage,
                       QStringLiteral("可能影响当前测试继续执行。"),
                       QStringLiteral("按页面给出的恢复操作处理；技术原文可在诊断中查看。"), technicalMessage);
}
