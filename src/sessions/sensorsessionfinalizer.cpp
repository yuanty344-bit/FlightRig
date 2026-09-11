#include "sessions/sensorsessionfinalizer.h"
#include "sensors/sensorservicecontroller.h"
#include "sessions/flightsessionrecorder.h"
#include "mapping/lidarmappingcontroller.h"
#include "mapping/depthmappingcontroller.h"
#include "inspection/visualsurveycontroller.h"
#include "inspection/gimbalinspectioncontroller.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

void SensorSessionFinalizer::finalize(const QString &sensorType, SensorServiceController &sensorService,
    FlightSessionRecorder &sessionRecorder, LidarMappingController &lidarMapping,
    DepthMappingController &depthMapping, VisualSurveyController &visualSurvey,
    GimbalInspectionController &gimbalInspection)
{
    gimbalInspection.pause();
    QVariantMap sensorSummary = sensorService.finishSessionRecording();
    if (sensorSummary.isEmpty() || !sessionRecorder.active()) return;
    const bool recording = sensorSummary.value(QStringLiteral("usage")).toString()
                           == QStringLiteral("record");
    const QString sessionFolder = sessionRecorder.folderPathForSession(
        sessionRecorder.activeSessionId());
    bool rewriteManifest = false;
    if (recording && sensorType == QStringLiteral("lidar-2d")
        && lidarMapping.available()) {
        const QVariantMap mapSummary = lidarMapping.saveToDirectory(
            QDir(sessionFolder).filePath(QStringLiteral("sensor")));
        const bool mapSaved = mapSummary.value(QStringLiteral("saved")).toBool();
        const bool slamMapAvailable = mapSaved
            && mapSummary.value(QStringLiteral("formatVersion")).toInt() >= 3
            && mapSummary.value(QStringLiteral("keyframeCount")).toInt() >= 2
            && mapSummary.value(QStringLiteral("poseGraphConstraintCount")).toInt() >= 1;
        sensorSummary.insert(QStringLiteral("lidarMapAvailable"), mapSaved);
        sensorSummary.insert(QStringLiteral("slamMapAvailable"), slamMapAvailable);
        sensorSummary.insert(QStringLiteral("lidarMap"), mapSummary);
        sensorSummary.insert(QStringLiteral("sizeBytes"),
            sensorSummary.value(QStringLiteral("sizeBytes")).toLongLong()
                + mapSummary.value(QStringLiteral("sizeBytes")).toLongLong());
        rewriteManifest = true;
    }
    if (recording && sensorType == QStringLiteral("depth-camera")
        && depthMapping.available()) {
        const QVariantMap mapSummary = depthMapping.saveToDirectory(
            QDir(sessionFolder).filePath(QStringLiteral("sensor/depth-map")));
        const bool mapSaved = mapSummary.value(QStringLiteral("saved")).toBool();
        sensorSummary.insert(QStringLiteral("depthMapAvailable"), mapSaved);
        sensorSummary.insert(QStringLiteral("depthObstacleMap"), mapSummary);
        sensorSummary.insert(QStringLiteral("sizeBytes"),
            sensorSummary.value(QStringLiteral("sizeBytes")).toLongLong()
                + mapSummary.value(QStringLiteral("sizeBytes")).toLongLong());
        rewriteManifest = true;
    }
    if (recording && sensorType == QStringLiteral("mono-camera")) {
        visualSurvey.stop();
        const QVariantMap surveySummary = visualSurvey.finishSession();
        const bool surveyAvailable = surveySummary.value(QStringLiteral("available")).toBool();
        sensorSummary.insert(QStringLiteral("visualSurveyAvailable"), surveyAvailable);
        sensorSummary.insert(QStringLiteral("visualSurvey"), surveySummary);
        if (surveyAvailable) {
            sensorSummary.insert(QStringLiteral("sizeBytes"),
                sensorSummary.value(QStringLiteral("sizeBytes")).toLongLong()
                    + surveySummary.value(QStringLiteral("sizeBytes")).toLongLong());
            rewriteManifest = true;
        }
    }
    if (recording && sensorType == QStringLiteral("gimbal-camera")
        && gimbalInspection.hasTarget()) {
        const auto inspection = gimbalInspection.finishSession();
        sensorSummary.insert(QStringLiteral("gimbalInspection"), inspection);
        sensorSummary.insert(QStringLiteral("sizeBytes"),
            sensorSummary.value(QStringLiteral("sizeBytes")).toLongLong()
                + inspection.value(QStringLiteral("sizeBytes")).toLongLong());
        rewriteManifest = true;
    }
    if (rewriteManifest) {
        QSaveFile manifest(QDir(sessionFolder).filePath(QStringLiteral("sensor/manifest.json")));
        const QByteArray contents = QJsonDocument(
            QJsonObject::fromVariantMap(sensorSummary)).toJson(QJsonDocument::Indented);
        if (!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || manifest.write(contents) != contents.size() || !manifest.commit()) {
            sensorSummary.insert(QStringLiteral("manifestWriteFailed"), true);
        }
    }
    if (!sensorSummary.isEmpty()) sessionRecorder.setSensorSummary(sensorSummary);
}
