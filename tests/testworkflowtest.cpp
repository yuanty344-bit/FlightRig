#include "testing/acceptanceevaluator.h"
#include "sessions/testreportgenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const QString csvPath = QDir(directory.path()).filePath(QStringLiteral("telemetry.csv"));
    QFile csv(csvPath);
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) return 2;
    csv.write("elapsed_ms,altitude_m,horizontal_speed_m_s,local_position_available,scene_x_east_m,scene_z_south_m\n"
              "0,0,0,1,0,0\n1000,5,2,1,2,-1\n2000,5,1,1,4,-2\n");
    csv.close();

    QVariantMap session{
        {QStringLiteral("id"), QStringLiteral("workflow-test")},
        {QStringLiteral("testPlanType"), QStringLiteral("mission")},
        {QStringLiteral("testPlanAcceptancePreset"), QStringLiteral("standard")},
        {QStringLiteral("testPlanAcceptanceRules"), QVariantMap{
            {QStringLiteral("maxHorizontalDeviationMeters"), 8.0},
            {QStringLiteral("minimumObstacleClearanceMeters"), 3.0},
            {QStringLiteral("maximumTelemetryGapSeconds"), 1.0},
            {QStringLiteral("maximumAltitudeMeters"), 100.0},
            {QStringLiteral("maximumDurationSeconds"), 1200}}},
        {QStringLiteral("testPlanControlSettings"), QVariantMap{
            {QStringLiteral("horizontalSpeedMetersPerSecond"), 1.0},
            {QStringLiteral("verticalSpeedMetersPerSecond"), 0.5},
            {QStringLiteral("yawRateDegreesPerSecond"), 30.0}}},
        {QStringLiteral("testPlanSensorSettings"), QVariantMap{
            {QStringLiteral("usage"), QStringLiteral("record")},
            {QStringLiteral("acceptanceRequired"), true},
            {QStringLiteral("minimumFrameRateHz"), 8.0},
            {QStringLiteral("maximumDataGapSeconds"), 2.0},
            {QStringLiteral("minimumObstacleDistanceMeters"), 1.0},
            {QStringLiteral("requireCameraFrame"), true},
            {QStringLiteral("requireVisualSurvey"), true},
            {QStringLiteral("minimumVisualKeyframes"), 5},
            {QStringLiteral("minimumVisualCoveragePercent"), 60.0}}},
        {QStringLiteral("sensorRecording"), QVariantMap{
            {QStringLiteral("available"), true},
            {QStringLiteral("recorded"), true},
            {QStringLiteral("replayable"), true},
            {QStringLiteral("recordingComplete"), true},
            {QStringLiteral("usage"), QStringLiteral("record")},
            {QStringLiteral("kind"), QStringLiteral("mono-camera")},
            {QStringLiteral("performanceMode"), QStringLiteral("standard")},
            {QStringLiteral("targetFramesPerSecond"), 10},
            {QStringLiteral("dataAvailabilityPercent"), 96.0},
            {QStringLiteral("averageFrameRateHz"), 9.8},
            // One transient instantaneous sample may be low; the rate rule
            // evaluates sustained average and the gap rule handles dropouts.
            {QStringLiteral("minimumFrameRateHz"), 1.0},
            {QStringLiteral("maximumDataGapSeconds"), 0.4},
            {QStringLiteral("averageLatencyMilliseconds"), 80.0},
            {QStringLiteral("latencyMeasured"), true},
            {QStringLiteral("latencyMethod"), QStringLiteral("monotonic-sync-v1")},
            {QStringLiteral("latencySampleCount"), 100},
            {QStringLiteral("rgbFrameCount"), 100},
            {QStringLiteral("visualSurveyAvailable"), true},
            {QStringLiteral("visualSurvey"), QVariantMap{
                {QStringLiteral("available"), true},
                {QStringLiteral("keyframeCount"), 8},
                 {QStringLiteral("goodKeyframeCount"), 7},
                 {QStringLiteral("warningKeyframeCount"), 1},
                 {QStringLiteral("coveragePercent"), 20.0},
                 {QStringLiteral("spatialCoveragePercent"), 87.5},
                 {QStringLiteral("qualityPassPercent"), 87.5}}},
            {QStringLiteral("statusLabel"), QStringLiteral("已记录并可同步回放")}}},
        {QStringLiteral("testPlanName"), QStringLiteral("航线验证")},
        {QStringLiteral("healthResult"), QStringLiteral("已通过")},
        {QStringLiteral("takeoffResult"), QStringLiteral("成功")},
        {QStringLiteral("missionCompletedItems"), 2},
        {QStringLiteral("missionTotalItems"), 2},
        {QStringLiteral("endedInAir"), false},
        {QStringLiteral("endedArmed"), false},
        {QStringLiteral("durationSeconds"), 120},
        {QStringLiteral("durationLabel"), QStringLiteral("02:00")},
        {QStringLiteral("maxAltitude"), 5.0},
        {QStringLiteral("maxHorizontalSpeed"), 2.0},
        {QStringLiteral("missionMaxCrossTrackError"), 1.0},
        {QStringLiteral("maximumTelemetryGapSeconds"), 0.2},
        {QStringLiteral("csvPath"), csvPath},
        {QStringLiteral("modelName"), QStringLiteral("x500")},
        {QStringLiteral("worldName"), QStringLiteral("默认场地")},
        {QStringLiteral("displayName"), QStringLiteral("报告 <转义> 检查")},
        {QStringLiteral("missionRouteName"), QStringLiteral("测试航线")},
        {QStringLiteral("missionTotalItems"), 2},
        {QStringLiteral("missionCompletedItems"), 2},
        {QStringLiteral("missionStartElapsedMs"), 0},
        {QStringLiteral("missionEndElapsedMs"), 2000},
        {QStringLiteral("missionWaypoints"), QVariantList{
            QVariantMap{{QStringLiteral("eastMeters"), 2.0},
                        {QStringLiteral("northMeters"), 1.0}},
            QVariantMap{{QStringLiteral("eastMeters"), 4.0},
                        {QStringLiteral("northMeters"), 2.0}}}},
    };
    const QVariantMap evaluation = AcceptanceEvaluator::evaluate(session);
    if (evaluation.value(QStringLiteral("result")).toString() != QStringLiteral("passed")) return 3;
    bool sensorCheckFound = false;
    for (const QVariant &entry : evaluation.value(QStringLiteral("checks")).toList()) {
        if (entry.toMap().value(QStringLiteral("id")).toString()
            == QStringLiteral("sensor-data")) sensorCheckFound = true;
    }
    if (!sensorCheckFound) return 8;

    QVariantMap incompleteSurveySession = session;
    QVariantMap incompleteSensor = session.value(QStringLiteral("sensorRecording")).toMap();
    QVariantMap incompleteSurvey = incompleteSensor.value(QStringLiteral("visualSurvey")).toMap();
    incompleteSurvey.insert(QStringLiteral("keyframeCount"), 2);
    incompleteSensor.insert(QStringLiteral("visualSurvey"), incompleteSurvey);
    incompleteSurveySession.insert(QStringLiteral("sensorRecording"), incompleteSensor);
    if (AcceptanceEvaluator::evaluate(incompleteSurveySession)
            .value(QStringLiteral("result")).toString() != QStringLiteral("failed")) return 17;

    // The UI and test-plan editor use 0.1 m precision. A calculated 2.977 m
    // clearance is displayed as 3.0 m and must not fail against a visible
    // 3.0 m limit when the trajectory never entered the obstacle polygon.
    QVariantMap roundedClearanceSession = session;
    roundedClearanceSession.insert(QStringLiteral("obstacleClearanceAvailable"), true);
    roundedClearanceSession.insert(QStringLiteral("minimumObstacleClearanceMeters"),
                                   2.977254971);
    roundedClearanceSession.insert(QStringLiteral("obstacleInfringementCount"), 0);
    if (AcceptanceEvaluator::evaluate(roundedClearanceSession)
            .value(QStringLiteral("result")).toString() != QStringLiteral("passed")) return 15;
    roundedClearanceSession.insert(QStringLiteral("minimumObstacleClearanceMeters"), 2.94);
    if (AcceptanceEvaluator::evaluate(roundedClearanceSession)
            .value(QStringLiteral("result")).toString() != QStringLiteral("failed")) return 16;

    QVariantMap observeOnlySession = session;
    QVariantMap observeSettings = session.value(
        QStringLiteral("testPlanSensorSettings")).toMap();
    observeSettings.insert(QStringLiteral("acceptanceRequired"), false);
    observeOnlySession.insert(QStringLiteral("testPlanSensorSettings"), observeSettings);
    observeOnlySession.insert(QStringLiteral("sensorRecording"), QVariantMap{});
    const QVariantMap observeEvaluation = AcceptanceEvaluator::evaluate(observeOnlySession);
    if (observeEvaluation.value(QStringLiteral("result")).toString()
            != QStringLiteral("passed")) return 13;
    for (const QVariant &entry : observeEvaluation.value(QStringLiteral("checks")).toList()) {
        if (entry.toMap().value(QStringLiteral("id")).toString()
                .startsWith(QStringLiteral("sensor-"))) return 13;
    }

    QVariantMap disabledSensorSession = observeOnlySession;
    observeSettings.insert(QStringLiteral("usage"), QStringLiteral("off"));
    observeSettings.insert(QStringLiteral("acceptanceRequired"), true);
    disabledSensorSession.insert(QStringLiteral("testPlanSensorSettings"), observeSettings);
    if (AcceptanceEvaluator::evaluate(disabledSensorSession)
            .value(QStringLiteral("result")).toString()
            != QStringLiteral("passed")) return 14;
    QVariantMap brokenDepthSession = session;
    QVariantMap brokenDepth = session.value(QStringLiteral("sensorRecording")).toMap();
    brokenDepth.insert(QStringLiteral("kind"), QStringLiteral("depth-camera"));
    brokenDepth.insert(QStringLiteral("depthFrameCount"), 0);
    brokenDepth.insert(QStringLiteral("depthValidRatio"), 0.0);
    brokenDepthSession.insert(QStringLiteral("sensorRecording"), brokenDepth);
    if (AcceptanceEvaluator::evaluate(brokenDepthSession)
            .value(QStringLiteral("result")).toString() != QStringLiteral("failed")) return 9;

    QVariantMap unmeasuredObstacleSession = session;
    QVariantMap unmeasuredObstacle = session.value(QStringLiteral("sensorRecording")).toMap();
    unmeasuredObstacle.insert(QStringLiteral("kind"), QStringLiteral("depth-camera"));
    unmeasuredObstacle.insert(QStringLiteral("depthFrameCount"), 100);
    unmeasuredObstacle.insert(QStringLiteral("depthValidRatio"), 0.8);
    unmeasuredObstacle.insert(QStringLiteral("nearestObstacleMeters"), 0.0);
    unmeasuredObstacle.insert(QStringLiteral("obstacleDistanceMeasured"), false);
    unmeasuredObstacleSession.insert(QStringLiteral("sensorRecording"), unmeasuredObstacle);
    if (AcceptanceEvaluator::evaluate(unmeasuredObstacleSession)
            .value(QStringLiteral("result")).toString() != QStringLiteral("failed")) return 12;

    QVariantMap partialRecordingSession = session;
    QVariantMap partialRecording = session.value(QStringLiteral("sensorRecording")).toMap();
    partialRecording.insert(QStringLiteral("recordingComplete"), false);
    partialRecording.insert(QStringLiteral("statusLabel"), QStringLiteral("写入失败"));
    partialRecordingSession.insert(QStringLiteral("sensorRecording"), partialRecording);
    if (AcceptanceEvaluator::evaluate(partialRecordingSession)
            .value(QStringLiteral("result")).toString() != QStringLiteral("failed")) return 10;
    QVariantMap customSession = session;
    customSession.insert(QStringLiteral("testPlanAcceptancePreset"), QStringLiteral("custom"));
    QVariantMap customRules = session.value(QStringLiteral("testPlanAcceptanceRules")).toMap();
    customRules.insert(QStringLiteral("maxHorizontalDeviationMeters"), 0.5);
    customSession.insert(QStringLiteral("testPlanAcceptanceRules"), customRules);
    const QVariantMap customEvaluation = AcceptanceEvaluator::evaluate(customSession);
    if (customEvaluation.value(QStringLiteral("result")).toString() != QStringLiteral("failed")
        || customEvaluation.value(QStringLiteral("presetLabel")).toString()
               != QStringLiteral("自定义检查")) return 4;
    session.insert(QStringLiteral("testResult"), evaluation.value(QStringLiteral("result")));
    session.insert(QStringLiteral("testResultLabel"), evaluation.value(QStringLiteral("resultLabel")));
    session.insert(QStringLiteral("testResultReason"), evaluation.value(QStringLiteral("reason")));
    session.insert(QStringLiteral("acceptanceChecks"), evaluation.value(QStringLiteral("checks")));
    QString error;
    const QString report = TestReportGenerator::generate(session, directory.path(), &error);
    if (report.isEmpty() || !QFileInfo::exists(report)) return 5;
    QFile html(report);
    if (!html.open(QIODevice::ReadOnly)) return 6;
    const QByteArray contents = html.readAll();
    if (!contents.contains("FlightRig")
        || !contents.contains("报告 &lt;转义&gt; 检查")
        || !contents.contains("width:100%")
        || contents.contains("100%%")
        || !contents.contains("等比例显示")
        || !contents.contains("起飞结果")
        || !contents.contains("落地结果")
        || !contents.contains("键盘控制速度")
        || !contents.contains("验收阈值")
        || !contents.contains("飞行记录编号")
        || !contents.contains("96.0%")
        || !contents.contains("9.8 Hz")
        || !contents.contains("视觉巡检影像地图")
        || !contents.contains("8 张关键帧")
        || !contents.contains("平均传输延迟（估计）")
        || !contents.contains("80 ms")
        || !contents.contains("单调时钟往返校准")) return 7;

    QVariantMap manualPreviewSession = session;
    QVariantMap manualPreview = session.value(QStringLiteral("sensorRecording")).toMap();
    manualPreview.insert(QStringLiteral("usage"), QStringLiteral("preview"));
    manualPreview.insert(QStringLiteral("latencyMeasured"), false);
    manualPreviewSession.insert(QStringLiteral("sensorRecording"), manualPreview);
    const QString previewReport = TestReportGenerator::generate(
        manualPreviewSession, directory.filePath(QStringLiteral("preview-report")), &error);
    QFile previewHtml(previewReport);
    if (previewReport.isEmpty() || !previewHtml.open(QIODevice::ReadOnly)) return 11;
    const QByteArray previewContents = previewHtml.readAll();
    if (!previewContents.contains("未测量")
        || !previewContents.contains("仅预览")) return 11;
    return 0;
}
