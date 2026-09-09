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
        // Clearance is displayed and configured to 0.1 m while polygon distance
        // calculations retain full floating-point precision. Compare at the
        // same precision so 2.977 m is not shown as 3.0 m and then failed
        // against a visible 3.0 m limit.
        const double displayedClearance = std::round(clearance * 10.0) / 10.0;
        const double displayedLimit = std::round(clearanceLimit * 10.0) / 10.0;
        const bool clearancePassed = infringements == 0
                                     && displayedClearance >= displayedLimit;
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

    const QVariantMap sensorSettings = session.value(
        QStringLiteral("testPlanSensorSettings")).toMap();
    const QString sensorUsage = sensorSettings.value(
        QStringLiteral("usage"), QStringLiteral("off")).toString();
    const bool sensorAcceptanceRequired = sensorUsage != QStringLiteral("off")
        && sensorSettings.value(QStringLiteral("acceptanceRequired"), false).toBool();
    if (sensorAcceptanceRequired) {
        const QVariantMap sensor = session.value(QStringLiteral("sensorRecording")).toMap();
        const bool sensorAvailable = sensor.value(QStringLiteral("available")).toBool();
        checks.append(check(QStringLiteral("sensor-data"), QStringLiteral("传感器数据可用"),
                            sensorAvailable ? QStringLiteral("pass") : QStringLiteral("fail"),
                            sensorAvailable
                                ? QStringLiteral("已收到本次方案要求的传感器数据")
                                : QStringLiteral("本次会话没有收到有效传感器数据")));
        if (sensorUsage == QStringLiteral("record")) {
            const bool recordingComplete = sensor.value(
                QStringLiteral("recordingComplete")).toBool();
            checks.append(check(QStringLiteral("sensor-recording-complete"),
                                QStringLiteral("传感器记录完整"),
                                recordingComplete ? QStringLiteral("pass")
                                                  : QStringLiteral("fail"),
                                recordingComplete
                                    ? QStringLiteral("数据与时间索引均已完整保存")
                                    : sensor.value(QStringLiteral("statusLabel"),
                                                   QStringLiteral("传感器记录未完整保存")).toString()));
        }

        const double minimumRate = sensorSettings.value(
            QStringLiteral("minimumFrameRateHz"), 5.0).toDouble();
        // A single transient instantaneous sample is too noisy for a whole
        // session verdict. The configured minimum is the minimum acceptable
        // sustained (active-period average) rate; continuity is evaluated
        // independently by the maximum-gap rule below.
        const double actualAverageRate = sensor.value(
            QStringLiteral("averageFrameRateHz")).toDouble();
        checks.append(check(QStringLiteral("sensor-rate"), QStringLiteral("传感器平均频率"),
                            sensorAvailable && actualAverageRate >= minimumRate
                                ? QStringLiteral("pass") : QStringLiteral("fail"),
                            sensorAvailable
                                ? QStringLiteral("平均 %1 Hz，方案要求不少于 %2 Hz")
                                      .arg(number(actualAverageRate)).arg(number(minimumRate))
                                : QStringLiteral("没有数据，无法计算平均频率"),
                            actualAverageRate, minimumRate, QStringLiteral("Hz")));

        const double maximumGap = sensorSettings.value(
            QStringLiteral("maximumDataGapSeconds"), 2.0).toDouble();
        const double actualGap = sensor.value(
            QStringLiteral("maximumDataGapSeconds")).toDouble();
        checks.append(check(QStringLiteral("sensor-gap"), QStringLiteral("传感器连续性"),
                            sensorAvailable && actualGap <= maximumGap
                                ? QStringLiteral("pass") : QStringLiteral("fail"),
                            sensorAvailable
                                ? QStringLiteral("最长中断 %1 秒，方案上限 %2 秒")
                                      .arg(number(actualGap)).arg(number(maximumGap))
                                : QStringLiteral("没有数据，无法判断连续性"),
                            actualGap, maximumGap, QStringLiteral("s")));

        const QString sensorKind = sensor.value(QStringLiteral("kind")).toString();
        if (sensorKind == QStringLiteral("depth-camera")) {
            const double validRatio = sensor.value(QStringLiteral("depthValidRatio")).toDouble();
            checks.append(check(QStringLiteral("sensor-depth-valid"),
                                QStringLiteral("深度数据有效"),
                                sensorAvailable && validRatio >= 0.05
                                    ? QStringLiteral("pass") : QStringLiteral("fail"),
                                sensorAvailable
                                    ? QStringLiteral("平均有效深度区域 %1%")
                                          .arg(number(validRatio * 100.0))
                                    : QStringLiteral("深度流缺失或没有有效距离数据"),
                                validRatio * 100.0, 5.0, QStringLiteral("%")));
        }
        if (sensorKind == QStringLiteral("lidar-2d")
            || sensorKind == QStringLiteral("depth-camera")) {
            const double obstacleLimit = sensorSettings.value(
                QStringLiteral("minimumObstacleDistanceMeters"), 1.0).toDouble();
            const double nearestObstacle = sensor.value(
                QStringLiteral("nearestObstacleMeters")).toDouble();
            const bool distanceMeasured = sensor.value(
                QStringLiteral("obstacleDistanceMeasured"), nearestObstacle > 0.0).toBool();
            const bool obstaclePassed = sensorAvailable
                && distanceMeasured && nearestObstacle >= obstacleLimit;
            checks.append(check(QStringLiteral("sensor-obstacle"),
                                QStringLiteral("传感器最近障碍"),
                                obstaclePassed ? QStringLiteral("pass") : QStringLiteral("fail"),
                                !sensorAvailable
                                    ? QStringLiteral("没有数据，无法判断障碍距离")
                                    : !distanceMeasured
                                      ? QStringLiteral("距离数据未测量或无效，不能判断障碍净空")
                                      : QStringLiteral("最近 %1 米，方案下限 %2 米")
                                            .arg(number(nearestObstacle)).arg(number(obstacleLimit)),
                                nearestObstacle, obstacleLimit, QStringLiteral("m")));
        }

        if (sensorSettings.value(QStringLiteral("requireCameraFrame"), false).toBool()
            && sensorKind != QStringLiteral("lidar-2d")) {
            const int cameraFrames = sensor.value(QStringLiteral("rgbFrameCount")).toInt();
            const bool mustBeSaved = sensorUsage == QStringLiteral("record");
            const bool saved = sensor.value(QStringLiteral("replayable"),
                                             sensor.value(QStringLiteral("recorded"))).toBool();
            const bool cameraPassed = cameraFrames > 0 && (!mustBeSaved || saved);
            checks.append(check(QStringLiteral("sensor-camera-frame"),
                                mustBeSaved ? QStringLiteral("相机画面已保存")
                                            : QStringLiteral("相机画面有效"),
                                cameraPassed ? QStringLiteral("pass")
                                             : QStringLiteral("fail"),
                                cameraFrames <= 0
                                    ? QStringLiteral("没有收到可用彩色画面")
                                    : mustBeSaved && !saved
                                      ? QStringLiteral("收到画面，但没有成功保存可回放记录")
                                      : QStringLiteral("收到 %1 帧有效彩色画面").arg(cameraFrames)));
        }
        if (sensorKind == QStringLiteral("mono-camera")
            && sensorSettings.value(QStringLiteral("requireVisualSurvey"), false).toBool()) {
            const QVariantMap survey = sensor.value(QStringLiteral("visualSurvey")).toMap();
            const int actualFrames = survey.value(QStringLiteral("keyframeCount")).toInt();
            const int requiredFrames = sensorSettings.value(
                QStringLiteral("minimumVisualKeyframes"), 5).toInt();
            checks.append(check(QStringLiteral("sensor-visual-keyframes"),
                                QStringLiteral("视觉巡检关键帧"),
                                actualFrames >= requiredFrames ? QStringLiteral("pass")
                                                                : QStringLiteral("fail"),
                                QStringLiteral("保存 %1 张，方案要求不少于 %2 张")
                                    .arg(actualFrames).arg(requiredFrames),
                                actualFrames, requiredFrames, QStringLiteral("张")));
            const double actualCoverage = survey.contains(
                QStringLiteral("spatialCoveragePercent"))
                ? survey.value(QStringLiteral("spatialCoveragePercent")).toDouble()
                : survey.value(QStringLiteral("coveragePercent")).toDouble();
            const double requiredCoverage = sensorSettings.value(
                QStringLiteral("minimumVisualCoveragePercent"), 60.0).toDouble();
            checks.append(check(QStringLiteral("sensor-visual-coverage"),
                                QStringLiteral("视觉巡检采集覆盖"),
                                actualCoverage >= requiredCoverage ? QStringLiteral("pass")
                                                                   : QStringLiteral("fail"),
                                QStringLiteral("候选位置/方向中成功保存 %1%，方案要求不少于 %2%")
                                    .arg(number(actualCoverage)).arg(number(requiredCoverage)),
                                actualCoverage, requiredCoverage, QStringLiteral("%")));
        }
        if (sensorSettings.value(QStringLiteral("requireSlamMap"), false).toBool()) {
            const bool slamAvailable = sensor.value(
                QStringLiteral("slamMapAvailable")).toBool();
            checks.append(check(QStringLiteral("sensor-slam"), QStringLiteral("SLAM 地图"),
                                slamAvailable ? QStringLiteral("pass")
                                              : QStringLiteral("fail"),
                                slamAvailable ? QStringLiteral("已生成 SLAM 地图")
                                              : QStringLiteral("本版本没有启用 SLAM 地图生成")));
        }
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
