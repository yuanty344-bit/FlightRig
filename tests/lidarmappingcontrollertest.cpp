#include "mapping/lidarmappingcontroller.h"

#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QTextStream>
#include <QTemporaryDir>
#include <QVariantMap>

#include <cmath>
#include <numbers>

namespace {
QVariantList landmarkScan(double eastMeters, double northMeters, double headingDegrees)
{
    QVariantList scan;
    const double heading = headingDegrees * std::numbers::pi / 180.0;
    const double sinHeading = std::sin(heading);
    const double cosHeading = std::cos(heading);
    const auto appendLandmark = [&](double east, double north) {
        const double deltaEast = east - eastMeters;
        const double deltaNorth = north - northMeters;
        const double right = deltaEast * cosHeading - deltaNorth * sinHeading;
        const double forward = deltaEast * sinHeading + deltaNorth * cosHeading;
        const double range = std::hypot(right, forward);
        if (range < 0.35 || range > 30.0) return;
        scan.append(QVariantMap{
            {QStringLiteral("x"), right},
            {QStringLiteral("y"), -forward},
            {QStringLiteral("distance"), range},
            {QStringLiteral("hit"), true},
        });
    };
    for (int step = -20; step <= 20; ++step) {
        const double offset = step * 0.5;
        appendLandmark(-10.0, offset);
        appendLandmark(10.0, offset);
        appendLandmark(offset, -10.0);
        appendLandmark(offset, 10.0);
    }
    return scan;
}
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    LidarMappingController mapping;
    const QVariantList scan{
        QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), -5.0},
                    {QStringLiteral("distance"), 5.0}},
        QVariantMap{{QStringLiteral("x"), 3.0}, {QStringLiteral("y"), -4.0},
                    {QStringLiteral("distance"), 5.0}},
        QVariantMap{{QStringLiteral("x"), -3.0}, {QStringLiteral("y"), -4.0},
                    {QStringLiteral("distance"), 5.0}},
    };
    mapping.ingestScan(scan, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 1000, true);
    mapping.ingestScan(scan, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 1600, true);
    mapping.ingestScan(scan, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 2200, true);
    if (!mapping.available() || mapping.processedScanCount() != 3) {
        QTextStream(stderr) << "initial available=" << mapping.available()
                            << " processed=" << mapping.processedScanCount()
                            << " occupied=" << mapping.occupiedCellCount()
                            << " explored=" << mapping.exploredAreaSquareMeters()
                            << Qt::endl;
        return 1;
    }
    if (mapping.occupiedCellCount() < 3 || mapping.exploredAreaSquareMeters() <= 0.0) return 2;

    // Moving east must expand the same world map instead of replacing the
    // first scan with a vehicle-centred picture.
    mapping.ingestScan(scan, 3.0, 0.0, 90.0, 0.0, 0.0, 5.0, 2800, true);
    if (mapping.processedScanCount() != 4 || mapping.widthMeters() < 5.0) return 3;

    // A tilted scan is rejected because a body-mounted planar LiDAR can see
    // the ground while the vehicle accelerates.
    mapping.ingestScan(scan, 4.0, 0.0, 90.0, 0.0, 12.0, 5.0, 3400, true);
    if (mapping.processedScanCount() != 4 || mapping.tiltRejectedScanCount() != 1) return 8;

    // Build a room-shaped landmark scan, then inject eastward odometry drift
    // without changing the physical scan. Correlative matching should recover
    // part of that error instead of duplicating the walls.
    LidarMappingController slam;
    QVariantList wallScan;
    for (int index = -12; index <= 12; ++index) {
        const double right = index * 0.25;
        wallScan.append(QVariantMap{
            {QStringLiteral("x"), right},
            {QStringLiteral("y"), -5.0},
            {QStringLiteral("distance"), std::hypot(right, 5.0)},
            {QStringLiteral("hit"), true},
        });
    }
    const QVariantList roomScan = landmarkScan(0.0, 0.0, 0.0);
    for (int index = 0; index < 18; ++index) {
        const double rawEast = index >= 13 ? 0.5 : 0.0;
        slam.ingestScan(roomScan, rawEast, 0.0, 0.0, 0.0, 0.0, 5.0,
                        1000 + index * 600, true);
    }
    if (slam.processedScanCount() < 7 || slam.occupiedCellCount() < 100
        || slam.scanMatchedCount() < 1
        || slam.localizationConfidence() <= 0.0
        || slam.correctionDistanceMeters() < 0.05
        || slam.correctionDistanceMeters() > 2.0) {
        QTextStream(stderr) << "slam processed=" << slam.processedScanCount()
                            << " occupied=" << slam.occupiedCellCount()
                            << " matched=" << slam.scanMatchedCount()
                            << " confidence=" << slam.localizationConfidence()
                            << " correction=" << slam.correctionDistanceMeters()
                            << Qt::endl;
        return 9;
    }
    const QVariantMap slamSummary = slam.summary();
    if (slamSummary.value(QStringLiteral("formatVersion")).toInt() != 4
        || slamSummary.value(QStringLiteral("mappingAlgorithm")).toString()
               != QStringLiteral("pose-graph-slam")) return 10;

    // Moving keyframes must form an odometry-constrained pose graph, not just
    // paint scans directly into one irreversible global raster.
    LidarMappingController poseGraph;
    for (int index = 0; index < 4; ++index) {
        poseGraph.ingestScan(wallScan, index * 1.6, 0.0, 0.0,
                             0.0, 0.0, 5.0, 1000 + index * 600, true);
    }
    if (poseGraph.keyframeCount() < 3 || poseGraph.poseGraphConstraintCount() < 2) {
        QTextStream(stderr) << "keyframes=" << poseGraph.keyframeCount()
                            << " constraints=" << poseGraph.poseGraphConstraintCount()
                            << " processed=" << poseGraph.processedScanCount() << Qt::endl;
        return 12;
    }

    // Fly two laps through the same square environment. Just before the
    // second visit to the west side, inject a two-metre odometry jump. The
    // repeated scan must create a non-sequential loop constraint, optimize the
    // graph and rebuild the occupancy map from corrected keyframe poses.
    LidarMappingController loopGraph;
    for (int index = 0; index < 40; ++index) {
        const double angleDegrees = index * 18.0;
        const double angle = angleDegrees * std::numbers::pi / 180.0;
        const double trueEast = 5.0 * std::cos(angle);
        const double trueNorth = 5.0 * std::sin(angle);
        const double heading = std::fmod(720.0 - angleDegrees, 360.0);
        const double odometryJump = index >= 29 ? 1.25 : 0.0;
        loopGraph.ingestScan(landmarkScan(trueEast, trueNorth, heading),
                             trueEast + odometryJump, trueNorth, heading,
                             0.0, 0.0, 5.0, 1000 + index * 600, true);
    }
    if (loopGraph.loopClosureCount() < 1 || loopGraph.optimizationCount() < 1
        || loopGraph.poseGraphConstraintCount() <= loopGraph.keyframeCount() - 1)
        return 13;

    // A maximum-range beam confirms free space but must not create a false
    // circular wall at the sensor ceiling.
    LidarMappingController freeSpace;
    const QVariantList maximumRangeScan{
        QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), -30.0},
                    {QStringLiteral("distance"), 30.0}, {QStringLiteral("hit"), false}},
    };
    freeSpace.ingestScan(maximumRangeScan, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 1000, true);
    freeSpace.ingestScan(maximumRangeScan, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 1600, true);
    freeSpace.ingestScan(maximumRangeScan, 0.0, 0.0, 0.0, 0.0, 0.0, 5.0, 2200, true);
    if (freeSpace.occupiedCellCount() != 0 || !freeSpace.available()) return 11;

    QTemporaryDir temporary;
    if (!temporary.isValid()) return 4;
    const QVariantMap saved = mapping.saveToDirectory(temporary.path());
    if (!saved.value(QStringLiteral("saved")).toBool()) {
        QTextStream(stderr) << saved.value(QStringLiteral("error")).toString() << Qt::endl;
        return 5;
    }
    for (const QString &file : {QStringLiteral("lidar-map.png"),
                                QStringLiteral("lidar-map.pgm"),
                                QStringLiteral("lidar-map.yaml"),
                                QStringLiteral("lidar-map.json"),
                                QStringLiteral("lidar-pose-graph.json")}) {
        const QFileInfo info(temporary.filePath(file));
        if (!info.isFile() || info.size() <= 0) return 6;
    }
    // Presentation/export must not mutate the SLAM state, and repeated exports
    // of an unchanged map must preserve the exact public file formats.
    const QVariantMap beforeExport = mapping.summary();
    QTemporaryDir repeated;
    if (!repeated.isValid()
        || !mapping.saveToDirectory(repeated.path()).value(QStringLiteral("saved")).toBool()) return 14;
    for (const QString &name : {QStringLiteral("lidar-map.pgm"),
                                QStringLiteral("lidar-map.yaml"),
                                QStringLiteral("lidar-pose-graph.json")}) {
        QFile first(temporary.filePath(name));
        QFile second(repeated.filePath(name));
        if (!first.open(QIODevice::ReadOnly) || !second.open(QIODevice::ReadOnly)
            || first.readAll() != second.readAll()) return 15;
    }
    const QImage original(temporary.filePath(QStringLiteral("lidar-map.png")));
    if (original.isNull()
        || original != QImage(repeated.filePath(QStringLiteral("lidar-map.png")))) return 16;
    QFile graphFile(repeated.filePath(QStringLiteral("lidar-pose-graph.json")));
    if (!graphFile.open(QIODevice::ReadOnly)) return 17;
    const QJsonObject graph = QJsonDocument::fromJson(graphFile.readAll()).object();
    if (graph.value(QStringLiteral("formatVersion")).toInt() != 1
        || graph.value(QStringLiteral("nodeCount")).toInt() != mapping.keyframeCount()
        || graph.value(QStringLiteral("constraintCount")).toInt() != mapping.poseGraphConstraintCount()
        || beforeExport.value(QStringLiteral("processedScanCount"))
            != mapping.summary().value(QStringLiteral("processedScanCount"))) return 18;
    mapping.clear();
    return mapping.available() || mapping.processedScanCount() != 0 ? 7 : 0;
}
