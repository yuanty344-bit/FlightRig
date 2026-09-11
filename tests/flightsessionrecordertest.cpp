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
    const QString sensorDirectory = QDir(sessionPath).filePath(QStringLiteral("sensor"));
    if (!QDir().mkpath(sensorDirectory)) return 40;
    QFile timeline(QDir(sensorDirectory).filePath(QStringLiteral("timeline.ndjson")));
    if (!timeline.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || timeline.write("{\"type\":\"camera\",\"stream\":\"rgb\",\"elapsedMs\":10}\n") <= 0)
        return 41;
    timeline.close();
    QSaveFile manifest(QDir(sensorDirectory).filePath(QStringLiteral("manifest.json")));
    const QByteArray manifestData = QJsonDocument(QJsonObject{
        {QStringLiteral("available"), true},
        {QStringLiteral("recorded"), true},
        {QStringLiteral("kind"), QStringLiteral("mono-camera")},
        {QStringLiteral("rgbFrameCount"), 1},
        {QStringLiteral("statusLabel"), QStringLiteral("已记录并可同步回放")},
    }).toJson(QJsonDocument::Indented);
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || manifest.write(manifestData) != manifestData.size() || !manifest.commit()) return 42;
    const QString metadataPath = QDir(sessionPath).filePath(QStringLiteral("session.json"));
    QFile metadataInput(metadataPath);
    if (!metadataInput.open(QIODevice::ReadOnly)) return 43;
    QJsonObject metadataObject = QJsonDocument::fromJson(metadataInput.readAll()).object();
    metadataInput.close();
    metadataObject.insert(QStringLiteral("sensorRecording"), QJsonObject{});
    QSaveFile metadataOutput(metadataPath);
    const QByteArray metadataData = QJsonDocument(metadataObject).toJson(QJsonDocument::Indented);
    if (!metadataOutput.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || metadataOutput.write(metadataData) != metadataData.size()
        || !metadataOutput.commit()) return 44;
    FlightSessionRecorder recovered(nullptr, sessionsPath);
    const QVariantMap recoveredSensor = recovered.sessionDetails(sessionId)
                                            .value(QStringLiteral("sensorRecording")).toMap();
    if (!recoveredSensor.value(QStringLiteral("replayable")).toBool()
        || !recoveredSensor.value(QStringLiteral("recordingComplete")).toBool()) return 45;
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
