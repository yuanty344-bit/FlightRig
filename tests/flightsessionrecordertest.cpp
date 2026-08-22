#include "flightsessionrecorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporaryRoot;
    if (!temporaryRoot.isValid()) return 1;

    const QString sessionsPath = QDir(temporaryRoot.path()).filePath(QStringLiteral("Sessions"));
    FlightSessionRecorder recorder(nullptr, sessionsPath);
    if (!recorder.startSession(QStringLiteral("gz_x500"), QStringLiteral("x500"), 5.0,
                               QStringLiteral("xatu_weiyang"), QStringLiteral("测试城市"),
                               QStringLiteral("test"), QStringLiteral("test/image"))) return 2;

    const QString sessionId = recorder.activeSessionId();
    const QString sessionPath = recorder.folderPathForSession(sessionId);
    if (sessionId.isEmpty() || !QFileInfo::exists(sessionPath)) return 3;
    if (recorder.deleteSession(sessionId)) return 4;

    recorder.setTestPlan({
        {QStringLiteral("id"), QStringLiteral("template-free-flight")},
        {QStringLiteral("name"), QStringLiteral("自由飞行")},
        {QStringLiteral("description"), QStringLiteral("测试方案快照")},
        {QStringLiteral("type"), QStringLiteral("free-flight")},
        {QStringLiteral("typeLabel"), QStringLiteral("自由飞行")},
        {QStringLiteral("endAction"), QStringLiteral("hold")},
        {QStringLiteral("acceptancePreset"), QStringLiteral("standard")},
        {QStringLiteral("builtIn"), true},
    });
    recorder.markHealthReady();
    recorder.markTakeoffRequested();
    recorder.updateFlightState(true, true);
    recorder.updateFlightState(false, false);

    recorder.finishSession(QStringLiteral("测试完成"));
    if (recorder.completedSessions().size() != 1) return 5;
    const QVariantMap completed = recorder.sessionDetails(sessionId);
    if (completed.value(QStringLiteral("formatVersion")).toInt() != 4) return 6;
    if (completed.value(QStringLiteral("testPlanName")).toString()
        != QStringLiteral("自由飞行")) return 7;
    if (completed.value(QStringLiteral("testResult")).toString()
        != QStringLiteral("passed")) return 8;
    if (!recorder.updateSessionDetails(sessionId, QStringLiteral("  校园巡检  "),
                                       QStringLiteral("  北区路线稳定。  "))) return 9;
    const QVariantMap editedDetails = recorder.sessionDetails(sessionId);
    if (editedDetails.value(QStringLiteral("displayName")).toString()
        != QStringLiteral("校园巡检")) return 10;
    if (editedDetails.value(QStringLiteral("notes")).toString()
        != QStringLiteral("北区路线稳定。")) return 11;

    FlightSessionRecorder reloaded(nullptr, sessionsPath);
    const QVariantMap reloadedDetails = reloaded.sessionDetails(sessionId);
    if (reloadedDetails.value(QStringLiteral("displayName")).toString()
        != QStringLiteral("校园巡检")) return 12;
    if (reloadedDetails.value(QStringLiteral("notes")).toString()
        != QStringLiteral("北区路线稳定。")) return 13;
    if (reloadedDetails.value(QStringLiteral("testResult")).toString()
        != QStringLiteral("passed")) return 14;
    if (!reloaded.deleteSession(sessionId)) return 15;
    if (QFileInfo::exists(sessionPath)) return 16;
    if (!reloaded.completedSessions().isEmpty()) return 17;
    if (reloaded.deleteSession(QStringLiteral("../Sessions"))) return 18;
    if (!QFileInfo::exists(sessionsPath)) return 19;
    if (reloaded.trashedSessions().size() != 1) return 20;
    if (!reloaded.restoreSession(sessionId)) return 21;
    if (!QFileInfo::exists(reloaded.folderPathForSession(sessionId))) return 22;
    if (!reloaded.deleteSession(sessionId)) return 23;
    if (reloaded.clearTrash() != 1) return 24;
    if (!reloaded.trashedSessions().isEmpty()) return 25;

    const QString reviewSessionsPath = QDir(temporaryRoot.path()).filePath(
        QStringLiteral("ReviewSessions"));
    FlightSessionRecorder reviewRecorder(nullptr, reviewSessionsPath);
    if (!reviewRecorder.startSession(QStringLiteral("gz_x500"), QStringLiteral("x500"), 20.0,
                                     QStringLiteral("xatu_weiyang"), QStringLiteral("测试城市"),
                                     QStringLiteral("test"), QStringLiteral("test/image"))) return 26;
    const QString reviewSessionId = reviewRecorder.activeSessionId();
    reviewRecorder.setTestPlan({
        {QStringLiteral("id"), QStringLiteral("template-city-inspection")},
        {QStringLiteral("name"), QStringLiteral("城市安全巡检")},
        {QStringLiteral("type"), QStringLiteral("city-inspection")},
        {QStringLiteral("typeLabel"), QStringLiteral("城市巡检")},
        {QStringLiteral("endAction"), QStringLiteral("land")},
        {QStringLiteral("acceptancePreset"), QStringLiteral("strict")},
        {QStringLiteral("builtIn"), true},
    });
    reviewRecorder.markHealthReady();
    reviewRecorder.markTakeoffRequested();
    reviewRecorder.updateFlightState(true, true);
    reviewRecorder.updateMissionMetrics(4.2, 2, 2, {});
    reviewRecorder.updateFlightState(false, false);
    reviewRecorder.finishSession(QStringLiteral("严格方案测试完成"));
    const QVariantMap reviewDetails = reviewRecorder.sessionDetails(reviewSessionId);
    if (reviewDetails.value(QStringLiteral("testResult")).toString()
        != QStringLiteral("failed")) return 27;
    if (!reviewDetails.value(QStringLiteral("testResultReason")).toString()
             .contains(QStringLiteral("3.0 米"))) return 28;
    return 0;
}
