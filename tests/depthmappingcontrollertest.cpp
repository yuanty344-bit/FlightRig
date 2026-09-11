#include "mapping/depthmappingcontroller.h"
#include "mapping/depthmapreader.h"
#include <QBuffer>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

namespace {
QByteArray constantDepthGrid(int width, int height, quint16 millimetres)
{
    QByteArray result(width * height * 2, Qt::Uninitialized);
    for (int index = 0; index < width * height; ++index) {
        result[index * 2] = static_cast<char>(millimetres & 0xff);
        result[index * 2 + 1] = static_cast<char>((millimetres >> 8) & 0xff);
    }
    return result;
}

bool require(bool condition, const char *message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QBuffer legacy;
    legacy.setData("east_m,up_m,south_m\n-0.01,2,3\ninvalid,2,3\n");
    legacy.open(QIODevice::ReadOnly);
    int parsed = 0;
    bool legacyValid = true;
    QString parseError;
    const bool read = DepthMapReader::readPoints(legacy, 0.25,
        [&](int x, int y, int z, const DepthVoxel &voxel) {
            ++parsed;
            legacyValid = legacyValid && x == -1 && y == 8 && z == 12
                && voxel.observations == 4 && voxel.weight == 6.0f
                && voxel.occupancy == 2.0f && voxel.freeObservations == 0;
        }, &parseError);
    if (!require(read && parsed == 1 && legacyValid,
                 "legacy defaults and negative coordinate decoding changed")) return 1;
    DepthMappingController controller;
    const QByteArray depth = constantDepthGrid(64, 36, 6000);
    const QVector3D pose(0.0f, 5.0f, 0.0f);

    controller.ingestDepthFrame(depth, 64, 36, pose, 0, 0, 0, 1000, true);
    if (!require(!controller.available(), "one frame must not confirm permanent obstacles"))
        return 1;
    controller.ingestDepthFrame(depth, 64, 36, pose, 0, 0, 0, 1700, true);
    if (!require(controller.available(), "two supported frames should confirm obstacles")
        || !require(controller.confirmedVoxelCount() > 10,
                    "confirmed map should contain multiple surface voxels")
        || !require(controller.corridorClearanceMeters() > 5.5
                        && controller.corridorClearanceMeters() < 6.5,
                    "forward corridor should preserve metric depth")
        || !require(controller.mapCenter().z() < -6.0f
                        && controller.mapCenter().z() > -6.3f,
                    "map must include the real forward camera mounting offset")) {
        return 1;
    }

    const int processed = controller.processedFrameCount();
    controller.ingestDepthFrame(depth, 64, 36, pose, 0, 30, 0, 2500, true);
    if (!require(controller.processedFrameCount() == processed,
                 "strong pitch must be rejected before map fusion")
        || !require(controller.skippedFrameCount() > 0,
                    "rejected frames must remain visible in diagnostics")) {
        return 1;
    }

    QByteArray invalidDepth(64 * 36 * 2, '\0');
    controller.ingestDepthFrame(invalidDepth, 64, 36, pose, 0, 0, 0, 3300, true);
    if (!require(controller.safetyLevel() == QStringLiteral("unknown"),
                 "an empty depth frame must not be reported as a clear corridor")) {
        return 1;
    }

    QTemporaryDir output;
    if (!require(output.isValid(), "temporary export directory unavailable")) return 1;
    const QString outputDirectory = output.path();
    const QVariantMap saved = controller.saveToDirectory(outputDirectory);
    if (!saved.value(QStringLiteral("saved")).toBool())
        std::cerr << saved.value(QStringLiteral("error")).toString().toStdString() << '\n';
    if (!require(saved.value(QStringLiteral("saved")).toBool(),
                 "confirmed depth map should export")
        || !require(QFileInfo::exists(QDir(outputDirectory).filePath(
                        QStringLiteral("depth-obstacles.ply"))),
                    "PLY export missing")
        || !require(QFileInfo::exists(QDir(outputDirectory).filePath(
                        QStringLiteral("depth-obstacles.csv"))),
                    "CSV export missing")
        || !require(QFileInfo::exists(QDir(outputDirectory).filePath(
                        QStringLiteral("depth-obstacles.png"))),
                    "preview export missing")
        || !require(QFileInfo::exists(QDir(outputDirectory).filePath(
                        QStringLiteral("depth-surface.obj"))),
                    "triangle mesh export missing")
        || !require(saved.value(QStringLiteral("formatVersion")).toInt() == 2,
                    "DepthFusion map format must be version 2")
        || !require(saved.value(QStringLiteral("chunkCount")).toInt() > 0,
                    "chunked map must report occupied chunks")) {
        return 1;
    }

    const QVariantList route{
        QVariantMap{{QStringLiteral("eastMeters"), 0.0},
                    {QStringLiteral("northMeters"), 0.0},
                    {QStringLiteral("altitudeMeters"), 5.0}},
        QVariantMap{{QStringLiteral("eastMeters"), 0.0},
                    {QStringLiteral("northMeters"), 10.0},
                    {QStringLiteral("altitudeMeters"), 5.0}}};
    const QVariantMap routeAnalysis = controller.analyzeRoute(route, 1.0);
    if (!require(routeAnalysis.value(QStringLiteral("available")).toBool(),
                 "saved surface should support route analysis")
        || !require(routeAnalysis.value(QStringLiteral("collisionCount")).toInt() > 0,
                    "route crossing the mapped wall must be marked")) {
        return 1;
    }

    DepthMappingController loaded;
    if (!require(loaded.loadMap(outputDirectory), "saved map must load without replay")
        || !require(loaded.loadedFromDisk(), "loaded map must expose its historical state")
        || !require(loaded.confirmedVoxelCount() == controller.confirmedVoxelCount(),
                    "loaded surface count must match the saved map")) {
        return 1;
    }

    const QByteArray fartherDepth = constantDepthGrid(64, 36, 10000);
    controller.ingestDepthFrame(fartherDepth, 64, 36, pose, 0, 0, 0, 4100, true);
    controller.ingestDepthFrame(fartherDepth, 64, 36, pose, 0, 0, 0, 4800, true);
    controller.ingestDepthFrame(fartherDepth, 64, 36, pose, 0, 0, 0, 5500, true);
    if (!require(controller.clearedVoxelCount() > 0,
                 "free-space rays must remove stale ghost obstacles")) {
        return 1;
    }
    return 0;
}
