#include "sessions/sessionarchive.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>

namespace {
constexpr int CurrentSessionFormatVersion = 5;

bool isCurrentSession(const QVariantMap &metadata)
{
    if (metadata.value(QStringLiteral("format")).toString()
            != QStringLiteral("FlightBenchSession")
        || metadata.value(QStringLiteral("formatVersion")).toInt()
               != CurrentSessionFormatVersion)
        return false;
    const QStringList requiredTextFields{
        QStringLiteral("modelId"), QStringLiteral("modelName"),
        QStringLiteral("worldId"), QStringLiteral("worldName"),
        QStringLiteral("startedAt"), QStringLiteral("csvPath"),
        QStringLiteral("eventsPath")};
    for (const QString &field : requiredTextFields) {
        if (metadata.value(field).toString().isEmpty()) return false;
    }
    const QVariantMap sensor = metadata.value(QStringLiteral("sensorRecording")).toMap();
    if (sensor.isEmpty()) return true;
    if (sensor.value(QStringLiteral("format")).toString()
            != QStringLiteral("FlightBenchSensorSession")
        || sensor.value(QStringLiteral("formatVersion")).toInt() != 2)
        return false;
    const QVariantMap lidar = sensor.value(QStringLiteral("lidarMap")).toMap();
    if (!lidar.isEmpty() && lidar.value(QStringLiteral("formatVersion")).toInt() != 4)
        return false;
    const QVariantMap depth = sensor.value(QStringLiteral("depthObstacleMap")).toMap();
    if (!depth.isEmpty() && depth.value(QStringLiteral("formatVersion")).toInt() != 2)
        return false;
    const QVariantMap survey = sensor.value(QStringLiteral("visualSurvey")).toMap();
    if (!survey.isEmpty()
        && (survey.value(QStringLiteral("formatVersion")).toInt() != 2
            || survey.value(QStringLiteral("qualityMetricVersion")).toInt() != 2))
        return false;
    const QVariantMap inspection = sensor.value(QStringLiteral("gimbalInspection")).toMap();
    return inspection.isEmpty()
           || inspection.value(QStringLiteral("formatVersion")).toInt() == 1;
}
}

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
        if (!isCurrentSession(metadata)
            || metadata.value(QStringLiteral("id")).toString().isEmpty()) continue;
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
        if (!isCurrentSession(metadata)
            || metadata.value(QStringLiteral("id")).toString().isEmpty()) continue;
        if (!saveMetadata(metadata, folder.absoluteFilePath())) continue;
        sessions.append(metadata);
    }
    return sessions;
}
