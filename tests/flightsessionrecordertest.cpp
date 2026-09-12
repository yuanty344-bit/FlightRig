#include "sessions/flightsessionrecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporaryRoot;
    if (!temporaryRoot.isValid()) return 1;

    const QString sessionsPath = QDir(temporaryRoot.path()).filePath(QStringLiteral("Sessions"));
    FlightSessionRecorder recorder(nullptr, sessionsPath);
    QString lastRecorderError;
    QObject::connect(&recorder, &FlightSessionRecorder::recorderError,
                     [&lastRecorderError](const QString &message) {
                         lastRecorderError = message;
                         qCritical().noquote() << "Session recorder error:" << message;
                     });
    if (!recorder.startSession(QStringLiteral("gz_x500"), QStringLiteral("x500"), 5.0,
                               QStringLiteral("xatu_weiyang"), QStringLiteral("测试城市"),
                               QStringLiteral("test"), QStringLiteral("test/image"))) {
        const QByteArray diagnostic = QStringLiteral("Could not start the test session in %1: %2\n")
                                          .arg(sessionsPath, lastRecorderError)
                                          .toLocal8Bit();
        std::fwrite(diagnostic.constData(), 1, diagnostic.size(), stderr);
        std::fflush(stderr);
        return 2;
    }

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
    if (completed.value(QStringLiteral("formatVersion")).toInt() != 5) return 6;
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

    const QString reportPath = QDir(temporaryRoot.path()).filePath(
        QStringLiteral("report.html"));
    QFile report(reportPath);
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || report.write("<!doctype html><title>FlightBench</title>") <= 0) return 12;
    report.close();
    if (!recorder.setSessionReport(sessionId, reportPath)) return 13;
    const QVariantMap reportDetails = recorder.sessionDetails(sessionId);
    if (QDir::fromNativeSeparators(reportDetails.value(QStringLiteral("reportPath")).toString())
        != QDir::fromNativeSeparators(QFileInfo(reportPath).absoluteFilePath())) return 14;
    if (reportDetails.value(QStringLiteral("reportFileName")).toString()
        != QStringLiteral("report.html")) return 15;
    if (reportDetails.value(QStringLiteral("reportFormatVersion")).toInt() != 1) return 16;

    FlightSessionRecorder reloaded(nullptr, sessionsPath);
    const QVariantMap reloadedDetails = reloaded.sessionDetails(sessionId);
    if (reloadedDetails.value(QStringLiteral("displayName")).toString()
        != QStringLiteral("校园巡检")) return 17;
    if (reloadedDetails.value(QStringLiteral("notes")).toString()
        != QStringLiteral("北区路线稳定。")) return 18;
    if (reloadedDetails.value(QStringLiteral("testResult")).toString()
        != QStringLiteral("passed")) return 19;
    if (QDir::fromNativeSeparators(
            reloadedDetails.value(QStringLiteral("reportPath")).toString())
        != QDir::fromNativeSeparators(QFileInfo(reportPath).absoluteFilePath())) return 20;
    const QString sensorPath = QDir(sessionPath).filePath(QStringLiteral("sensor"));
    if (!QDir().mkpath(sensorPath)) return 43;
    QFile sensorFile(QDir(sensorPath).filePath(QStringLiteral("sample.bin")));
    if (!sensorFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || sensorFile.write("sensor") != 6) return 44;
    sensorFile.close();
    if (!reloaded.deleteSessionSensorData(sessionId)) return 45;
    if (QFileInfo::exists(sensorPath)) return 46;
    FlightSessionRecorder sensorDataDeletedReload(nullptr, sessionsPath);
    const QVariantMap sensorDataDeletedDetails = sensorDataDeletedReload.sessionDetails(sessionId);
    const QVariantMap deletedSensorSummary = sensorDataDeletedDetails
                                                 .value(QStringLiteral("sensorRecording"))
                                                 .toMap();
    if (sensorDataDeletedDetails.isEmpty()
        || deletedSensorSummary.value(QStringLiteral("format")).toString()
               != QStringLiteral("FlightBenchSensorSession")
        || deletedSensorSummary.value(QStringLiteral("formatVersion")).toInt() != 2
        || !deletedSensorSummary.value(QStringLiteral("deleted")).toBool()) return 47;
    const QString obsoletePath = QDir(sessionsPath).filePath(QStringLiteral("obsolete-session"));
    if (!QDir().mkpath(obsoletePath)) return 40;
    QSaveFile obsoleteFile(QDir(obsoletePath).filePath(QStringLiteral("session.json")));
    const QByteArray obsoleteData = QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("FlightBenchSession")},
        {QStringLiteral("formatVersion"), 4},
        {QStringLiteral("id"), QStringLiteral("obsolete-session")},
    }).toJson(QJsonDocument::Indented);
    if (!obsoleteFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || obsoleteFile.write(obsoleteData) != obsoleteData.size()
        || !obsoleteFile.commit()) return 41;
    FlightSessionRecorder currentOnly(nullptr, sessionsPath);
    if (currentOnly.completedSessions().size() != 1
        || !currentOnly.sessionDetails(QStringLiteral("obsolete-session")).isEmpty()) return 42;
    if (!reloaded.deleteSession(sessionId)) return 21;
    if (QFileInfo::exists(sessionPath)) return 22;
    if (!reloaded.completedSessions().isEmpty()) return 23;
    if (reloaded.deleteSession(QStringLiteral("../Sessions"))) return 24;
    if (!QFileInfo::exists(sessionsPath)) return 25;
    if (reloaded.trashedSessions().size() != 1) return 26;
    if (!reloaded.restoreSession(sessionId)) return 27;
    if (!QFileInfo::exists(reloaded.folderPathForSession(sessionId))) return 28;
    if (!reloaded.deleteSession(sessionId)) return 29;

    const QString trashMetadataPath = QDir(QFileInfo(sessionsPath).absolutePath()).filePath(
        QStringLiteral("Trash/%1/session.json").arg(sessionId));
    QFile trashMetadataFile(trashMetadataPath);
    if (!trashMetadataFile.open(QIODevice::ReadOnly)) return 30;
    QJsonObject trashMetadata = QJsonDocument::fromJson(trashMetadataFile.readAll()).object();
    trashMetadataFile.close();
    trashMetadata.insert(QStringLiteral("trashedAt"),
                         QDateTime::currentDateTime().addDays(-60).toString(Qt::ISODate));
    QSaveFile oldTrashMetadata(trashMetadataPath);
    const QByteArray oldTrashContents = QJsonDocument(trashMetadata).toJson(
        QJsonDocument::Indented);
    if (!oldTrashMetadata.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || oldTrashMetadata.write(oldTrashContents) != oldTrashContents.size()
        || !oldTrashMetadata.commit()) return 31;

    FlightSessionRecorder retentionRecorder(nullptr, sessionsPath);
    if (retentionRecorder.trashedSessions().size() != 1) return 32;
    if (retentionRecorder.purgeTrashOlderThan(0) != 0) return 33;
    if (!QFileInfo::exists(QFileInfo(trashMetadataPath).absolutePath())) return 34;
    if (retentionRecorder.purgeTrashOlderThan(30) != 1) return 35;
    if (!retentionRecorder.trashedSessions().isEmpty()) return 36;

    const QString reviewSessionsPath = QDir(temporaryRoot.path()).filePath(
        QStringLiteral("ReviewSessions"));
    FlightSessionRecorder reviewRecorder(nullptr, reviewSessionsPath);
    if (!reviewRecorder.startSession(QStringLiteral("gz_x500"), QStringLiteral("x500"), 20.0,
                                     QStringLiteral("xatu_weiyang"), QStringLiteral("测试城市"),
                                     QStringLiteral("test"), QStringLiteral("test/image"))) return 37;
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
        != QStringLiteral("failed")) return 38;
    if (!reviewDetails.value(QStringLiteral("testResultReason")).toString()
             .contains(QStringLiteral("3.0 米"))) return 39;
    return 0;
}
