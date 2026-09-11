#include "sessions/sessionarchive.h"
#include "testing/acceptanceevaluator.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

bool SessionArchive::saveMetadata(QVariantMap &metadata,
                                                    const QString &folderPath)
{
    metadata.insert(QStringLiteral("folderPath"), QDir::toNativeSeparators(folderPath));
    metadata.insert(QStringLiteral("csvPath"),
                    QDir::toNativeSeparators(QDir(folderPath).filePath(QStringLiteral("telemetry.csv"))));
    metadata.insert(QStringLiteral("eventsPath"),
                    QDir::toNativeSeparators(QDir(folderPath).filePath(QStringLiteral("events.log"))));
    QSaveFile file(QDir(folderPath).filePath(QStringLiteral("session.json")));
    const QByteArray contents = QJsonDocument(QJsonObject::fromVariantMap(metadata))
                                    .toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(contents) == contents.size() && file.commit();
}

QVariantList SessionArchive::loadCompleted(const QString &directory)
{
    QVariantList sessions;
    QDir root(directory);
    const QFileInfoList folders = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                      QDir::Time);
    for (const QFileInfo &folder : folders) {
        QFile metadataFile(QDir(folder.absoluteFilePath()).filePath(QStringLiteral("session.json")));
        if (!metadataFile.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll());
        metadataFile.close();
        if (!document.isObject()) continue;
        QVariantMap metadata = document.object().toVariantMap();
        if (metadata.value(QStringLiteral("id")).toString().isEmpty()) continue;
        bool recoveredSensorSummary = false;
        if (metadata.value(QStringLiteral("sensorRecording")).toMap().isEmpty()) {
            const QString sensorDirectory = QDir(folder.absoluteFilePath())
                                                .filePath(QStringLiteral("sensor"));
            QFile manifestFile(QDir(sensorDirectory).filePath(QStringLiteral("manifest.json")));
            if (manifestFile.open(QIODevice::ReadOnly)) {
                const QJsonDocument manifest = QJsonDocument::fromJson(manifestFile.readAll());
                manifestFile.close();
                if (manifest.isObject()) {
                    QVariantMap summary = manifest.object().toVariantMap();
                    const QFileInfo timeline(QDir(sensorDirectory)
                                                 .filePath(QStringLiteral("timeline.ndjson")));
                    const bool replayable = timeline.isFile() && timeline.size() > 0;
                    summary.insert(QStringLiteral("recorded"), replayable);
                    summary.insert(QStringLiteral("replayable"), replayable);
                    if (!summary.contains(QStringLiteral("recordingComplete")))
                        summary.insert(QStringLiteral("recordingComplete"), replayable);
                    metadata.insert(QStringLiteral("sensorRecording"), summary);
                    recoveredSensorSummary = true;
                }
            }
        }
        // Sessions created before route reuse was introduced do not contain this key.
        // Normalizing it here keeps old local data compatible with the current QML model.
        if (!metadata.contains(QStringLiteral("missionRouteName")))
            metadata.insert(QStringLiteral("missionRouteName"), QString());
        if (!metadata.contains(QStringLiteral("displayName")))
            metadata.insert(QStringLiteral("displayName"), QString());
        if (!metadata.contains(QStringLiteral("notes")))
            metadata.insert(QStringLiteral("notes"), QString());
        if (!metadata.contains(QStringLiteral("reportPath")))
            metadata.insert(QStringLiteral("reportPath"), QString());
        if (!metadata.contains(QStringLiteral("reportGeneratedAt")))
            metadata.insert(QStringLiteral("reportGeneratedAt"), QString());
        if (!metadata.contains(QStringLiteral("testPlanName")))
            metadata.insert(QStringLiteral("testPlanName"), QString());
        if (!metadata.contains(QStringLiteral("testPlanType")))
            metadata.insert(QStringLiteral("testPlanType"), QString());
        if (recoveredSensorSummary
            && !metadata.value(QStringLiteral("testPlanType")).toString().isEmpty()) {
            const QVariantMap evaluation = AcceptanceEvaluator::evaluate(metadata);
            metadata.insert(QStringLiteral("testResult"), evaluation.value(QStringLiteral("result")));
            metadata.insert(QStringLiteral("testResultLabel"), evaluation.value(QStringLiteral("resultLabel")));
            metadata.insert(QStringLiteral("testResultReason"), evaluation.value(QStringLiteral("reason")));
            metadata.insert(QStringLiteral("acceptanceChecks"), evaluation.value(QStringLiteral("checks")));
            metadata.insert(QStringLiteral("acceptancePassedCount"), evaluation.value(QStringLiteral("passedCount")));
            metadata.insert(QStringLiteral("acceptanceWarningCount"), evaluation.value(QStringLiteral("warningCount")));
            metadata.insert(QStringLiteral("acceptanceFailedCount"), evaluation.value(QStringLiteral("failedCount")));
            metadata.insert(QStringLiteral("reportNeedsRegeneration"), true);
            if (!saveMetadata(metadata, folder.absoluteFilePath()))
                metadata.insert(QStringLiteral("metadataRepairPending"), true);
        }
        if (!metadata.contains(QStringLiteral("testResult"))) {
            metadata.insert(QStringLiteral("testResult"), QStringLiteral("not-rated"));
            metadata.insert(QStringLiteral("testResultLabel"), QStringLiteral("未评估"));
            metadata.insert(QStringLiteral("testResultReason"),
                            QStringLiteral("旧会话没有测试方案结果"));
        }
        if (metadata.value(QStringLiteral("status")).toString() == QStringLiteral("记录中")) {
            metadata.insert(QStringLiteral("status"), QStringLiteral("未完成"));
            metadata.insert(QStringLiteral("endReason"), QStringLiteral("上次会话未正常结束"));
        }
        sessions.append(metadata);
        if (sessions.size() >= 50) break;
    }
    return sessions;
}

QVariantList SessionArchive::loadTrashed(const QString &directory)
{
    QVariantList sessions;
    const QFileInfoList folders = QDir(directory).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo &folder : folders) {
        QFile file(QDir(folder.absoluteFilePath()).filePath(QStringLiteral("session.json")));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!document.isObject()) continue;
        QVariantMap metadata = document.object().toVariantMap();
        if (!saveMetadata(metadata, folder.absoluteFilePath())) continue;
        sessions.append(metadata);
    }
    return sessions;
}
