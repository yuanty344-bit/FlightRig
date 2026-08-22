#include "acceptanceevaluator.h"

#include <QVariantList>

#include <algorithm>
#include <cmath>

namespace {
QVariantMap check(const QString &id, const QString &label, const QString &status,
                  const QString &detail, double value = 0.0, double limit = 0.0,
                  const QString &unit = {})
{
    QVariantMap result{{QStringLiteral("id"), id},
                       {QStringLiteral("label"), label},
                       {QStringLiteral("status"), status},
                       {QStringLiteral("detail"), detail}};
    if (!unit.isEmpty()) {
        result.insert(QStringLiteral("value"), value);
        result.insert(QStringLiteral("limit"), limit);
        result.insert(QStringLiteral("unit"), unit);
    }
    return result;
}

QString number(double value, int precision = 1)
{
    return QString::number(value, 'f', precision);
}
}

QVariantMap AcceptanceEvaluator::evaluate(const QVariantMap &session)
{
    const QString planType = session.value(QStringLiteral("testPlanType")).toString();
    if (planType.isEmpty()) {
        return {{QStringLiteral("result"), QStringLiteral("not-rated")},
                {QStringLiteral("resultLabel"), QStringLiteral("未评估")},
                {QStringLiteral("reason"), QStringLiteral("旧会话没有保存测试方案与验收规则")},
                {QStringLiteral("checks"), QVariantList{}},
                {QStringLiteral("passedCount"), 0},
                {QStringLiteral("warningCount"), 0},
                {QStringLiteral("failedCount"), 0}};
    }

    const QString preset = session.value(
        QStringLiteral("testPlanAcceptancePreset"), QStringLiteral("standard")).toString();
    const bool strict = preset == QStringLiteral("strict");
    const bool custom = preset == QStringLiteral("custom");
    const QVariantMap rules = session.value(QStringLiteral("testPlanAcceptanceRules")).toMap();
    const auto rule = [&rules](const QString &key, double fallback) {
        bool ok = false;
        const double value = rules.value(key, fallback).toDouble(&ok);
        return ok && std::isfinite(value) ? value : fallback;
    };
    // A user can start a waypoint task from a free-flight plan. Evaluate what
    // actually happened instead of hiding mission checks behind the saved plan type.
    const bool missionPlan = planType != QStringLiteral("free-flight")
                             || session.value(QStringLiteral("missionTotalItems")).toInt() > 0;
    const double deviationLimit = rule(QStringLiteral("maxHorizontalDeviationMeters"),
                                       strict ? 3.0 : 8.0);
    const double telemetryGapLimit = rule(QStringLiteral("maximumTelemetryGapSeconds"),
                                          strict ? 0.8 : 1.0);
    const double clearanceLimit = rule(QStringLiteral("minimumObstacleClearanceMeters"),
                                       strict ? 5.0 : 3.0);
    const double altitudeLimit = rule(QStringLiteral("maximumAltitudeMeters"), 100.0);
    const int durationLimit = static_cast<int>(std::round(
        rule(QStringLiteral("maximumDurationSeconds"), strict ? 900.0 : 1200.0)));
    const QString ruleLabel = custom ? QStringLiteral("自定义")
                                     : strict ? QStringLiteral("严格")
                                              : QStringLiteral("标准");

    QVariantList checks;
    const bool healthPassed = session.value(QStringLiteral("healthResult")).toString()
                              == QStringLiteral("已通过");
    checks.append(check(QStringLiteral("health"), QStringLiteral("起飞前健康检查"),
                        healthPassed ? QStringLiteral("pass") : QStringLiteral("fail"),
                        healthPassed ? QStringLiteral("传感器、位置和解锁许可均已稳定")
                                     : QStringLiteral("健康检查没有稳定通过")));

    const bool takeoffPassed = session.value(QStringLiteral("takeoffResult")).toString()
                               == QStringLiteral("成功");
    checks.append(check(QStringLiteral("takeoff"), QStringLiteral("成功起飞"),
                        takeoffPassed ? QStringLiteral("pass") : QStringLiteral("fail"),
                        takeoffPassed ? QStringLiteral("已检测到飞行器离地")
                                      : QStringLiteral("没有完成起飞")));

    const int completedItems = session.value(QStringLiteral("missionCompletedItems")).toInt();
    const int totalItems = session.value(QStringLiteral("missionTotalItems")).toInt();
    if (missionPlan) {
        const bool missionPassed = totalItems > 0 && completedItems >= totalItems;
        checks.append(check(QStringLiteral("mission"), QStringLiteral("航线完成"),
                            missionPassed ? QStringLiteral("pass") : QStringLiteral("fail"),
                            totalItems <= 0
                                ? QStringLiteral("没有执行方案中的航线")
                                : QStringLiteral("已完成 %1 / %2 个航点")
                                      .arg(std::min(completedItems, totalItems)).arg(totalItems)));
    }

    const bool endedInAir = session.value(QStringLiteral("endedInAir")).toBool();
    const bool endedArmed = session.value(QStringLiteral("endedArmed")).toBool();
    const bool safelyStopped = !endedInAir && !endedArmed;
    checks.append(check(QStringLiteral("landing"), QStringLiteral("安全落地并锁定"),
                        safelyStopped ? QStringLiteral("pass") : QStringLiteral("fail"),
                        safelyStopped ? QStringLiteral("会话结束时飞行器已落地并锁定")
                                      : QStringLiteral("会话结束时飞行器仍在空中或已解锁")));

    const int issueCount = session.value(QStringLiteral("issueCount")).toInt();
    checks.append(check(QStringLiteral("events"), QStringLiteral("严重运行事件"),
                        issueCount == 0 ? QStringLiteral("pass") : QStringLiteral("warning"),
                        issueCount == 0 ? QStringLiteral("没有记录到严重事件")
                                        : QStringLiteral("记录到 %1 项需要复核的事件").arg(issueCount)));

    if (missionPlan) {
        const double deviation = session.value(QStringLiteral("missionMaxCrossTrackError")).toDouble();
        checks.append(check(QStringLiteral("deviation"), QStringLiteral("最大航线偏差"),
                            deviation <= deviationLimit ? QStringLiteral("pass")
                                                        : QStringLiteral("fail"),
                            QStringLiteral("%1 米，%2方案参考值为 %3 米")
                                .arg(number(deviation))
                                .arg(ruleLabel)
                                .arg(number(deviationLimit)),
                            deviation, deviationLimit, QStringLiteral("m")));
    }

    const double telemetryGap = session.value(QStringLiteral("maximumTelemetryGapSeconds")).toDouble();
    checks.append(check(QStringLiteral("telemetry"), QStringLiteral("遥测连续性"),
                        telemetryGap <= telemetryGapLimit ? QStringLiteral("pass")
                                                         : QStringLiteral("warning"),
                        QStringLiteral("最长中断 %1 秒，参考值 %2 秒")
                            .arg(number(telemetryGap)).arg(number(telemetryGapLimit)),
                        telemetryGap, telemetryGapLimit, QStringLiteral("s")));

    const bool clearanceAvailable = session.value(QStringLiteral("obstacleClearanceAvailable")).toBool();
    if (clearanceAvailable) {
        const double clearance = session.value(QStringLiteral("minimumObstacleClearanceMeters")).toDouble();
        const int infringements = session.value(QStringLiteral("obstacleInfringementCount")).toInt();
        const bool clearancePassed = infringements == 0 && clearance >= clearanceLimit;
        checks.append(check(QStringLiteral("clearance"), QStringLiteral("建筑安全净空"),
                            clearancePassed ? QStringLiteral("pass") : QStringLiteral("fail"),
                            infringements > 0
                                ? QStringLiteral("轨迹进入建筑安全范围 %1 次").arg(infringements)
                                : QStringLiteral("最小净空 %1 米，参考值 %2 米")
                                      .arg(number(clearance)).arg(number(clearanceLimit)),
                            clearance, clearanceLimit, QStringLiteral("m")));
    } else if (missionPlan && (strict || custom)) {
        checks.append(check(QStringLiteral("clearance"), QStringLiteral("建筑安全净空"),
                            QStringLiteral("warning"),
                            QStringLiteral("当前世界没有可用于运行后验收的建筑数据")));
    }

    const double maxAltitude = session.value(QStringLiteral("maxAltitude")).toDouble();
    checks.append(check(QStringLiteral("altitude"), QStringLiteral("最大高度"),
                        maxAltitude <= altitudeLimit ? QStringLiteral("pass")
                                                     : QStringLiteral("fail"),
                        QStringLiteral("最高 %1 米，方案限制 %2 米")
                            .arg(number(maxAltitude)).arg(number(altitudeLimit)),
                        maxAltitude, altitudeLimit, QStringLiteral("m")));

    const int duration = session.value(QStringLiteral("durationSeconds")).toInt();
    checks.append(check(QStringLiteral("duration"), QStringLiteral("任务用时"),
                        duration <= durationLimit ? QStringLiteral("pass")
                                                  : QStringLiteral("warning"),
                        QStringLiteral("用时 %1，方案参考上限 %2 分钟")
                            .arg(session.value(QStringLiteral("durationLabel")).toString())
                            .arg(durationLimit / 60),
                        duration, durationLimit, QStringLiteral("s")));

    const QString interruption = session.value(QStringLiteral("missionInterruptionReason")).toString();
    if (!interruption.isEmpty()) {
        checks.append(check(QStringLiteral("interruption"), QStringLiteral("任务中止原因"),
                            QStringLiteral("fail"), interruption));
    }

    int passedCount = 0;
    int warningCount = 0;
    int failedCount = 0;
    QString firstFailure;
    for (const QVariant &entry : checks) {
        const QVariantMap item = entry.toMap();
        const QString status = item.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("pass")) ++passedCount;
        else if (status == QStringLiteral("warning")) ++warningCount;
        else if (status == QStringLiteral("fail")) {
            ++failedCount;
            if (firstFailure.isEmpty()) firstFailure = item.value(QStringLiteral("detail")).toString();
        }
    }

    QString result = QStringLiteral("passed");
    QString label = QStringLiteral("通过");
    QString reason = QStringLiteral("方案要求的关键步骤和安全指标均已通过");
    const QString endReason = session.value(QStringLiteral("endReason")).toString();
    if (endReason.contains(QStringLiteral("用户中止"))) {
        result = QStringLiteral("aborted");
        label = QStringLiteral("用户中止");
        reason = endReason;
    } else if (failedCount > 0) {
        result = QStringLiteral("failed");
        label = QStringLiteral("未通过");
        reason = firstFailure;
    } else if (warningCount > 0) {
        result = QStringLiteral("warning");
        label = QStringLiteral("有警告");
        reason = QStringLiteral("测试已完成，但有 %1 项需要复核").arg(warningCount);
    }

    return {{QStringLiteral("result"), result},
            {QStringLiteral("resultLabel"), label},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("checks"), checks},
            {QStringLiteral("passedCount"), passedCount},
            {QStringLiteral("warningCount"), warningCount},
            {QStringLiteral("failedCount"), failedCount},
            {QStringLiteral("preset"), preset},
            {QStringLiteral("presetLabel"), custom ? QStringLiteral("自定义检查")
                                                    : strict ? QStringLiteral("严格检查")
                                                             : QStringLiteral("标准检查")}};
}
