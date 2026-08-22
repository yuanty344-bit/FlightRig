#include "acceptanceevaluator.h"
#include "testreportgenerator.h"

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
    if (!contents.contains("FlightBench")
        || !contents.contains("报告 &lt;转义&gt; 检查")
        || !contents.contains("width:100%")
        || contents.contains("100%%")
        || !contents.contains("等比例显示")
        || !contents.contains("起飞结果")
        || !contents.contains("落地结果")
        || !contents.contains("键盘控制速度")
        || !contents.contains("验收阈值")
        || !contents.contains("会话编号")) return 7;
    return 0;
}
