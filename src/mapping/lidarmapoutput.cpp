#include "mapping/lidarmapoutput.h"
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>
#include <utility>


using namespace LidarMapTypes;
namespace {
void removeIsolatedRasterCells(QImage *image,
                               QRgb obstacleColor,
                               QRgb freeColor,
                               QRgb unknownColor)
{
    if (!image || image->format() != QImage::Format_RGB32
        || image->width() < 3 || image->height() < 3) {
        return;
    }
    const QImage source = image->copy();
    for (int y = 1; y < source.height() - 1; ++y) {
        const QRgb *sourceLine = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *targetLine = reinterpret_cast<QRgb *>(image->scanLine(y));
        for (int x = 1; x < source.width() - 1; ++x) {
            const QRgb color = sourceLine[x];
            if (color != obstacleColor && color != freeColor) continue;
            int support = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const QRgb *neighborLine = reinterpret_cast<const QRgb *>(
                    source.constScanLine(y + dy));
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx != 0 || dy != 0) && neighborLine[x + dx] == color) ++support;
                }
            }
            // Keep walls and filled free regions, but remove single-cell
            // speckles produced by one marginal ground return or rasterized
            // ray. This is presentation/export cleanup only; SLAM evidence is
            // retained internally for later observations.
            if (support < 2) targetLine[x] = unknownColor;
        }
    }
}
void unpackCellKey(quint64 key, int *x, int *y)
{
    *x = static_cast<qint32>(key >> 32);
    *y = static_cast<qint32>(key & 0xffffffffU);
}

}

QString LidarMapOutput::render(const LidarMapSnapshot &map)
{
    constexpr int ImageWidth = 1000;
    constexpr int ImageHeight = 700;
    constexpr double PaddingMeters = 4.0;
    const double minEast = map.minX * map.resolution - PaddingMeters;
    const double maxEast = (map.maxX + 1) * map.resolution + PaddingMeters;
    const double minNorth = map.minY * map.resolution - PaddingMeters;
    const double maxNorth = (map.maxY + 1) * map.resolution + PaddingMeters;
    const double scale = std::min(ImageWidth / std::max(1.0, maxEast - minEast),
                                  ImageHeight / std::max(1.0, maxNorth - minNorth));
    const auto screenPoint = [=](double east, double north) {
        return QPointF((east - minEast) * scale,
                       ImageHeight - (north - minNorth) * scale);
    };

    QImage image(ImageWidth, ImageHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QStringLiteral("#e9edf2")));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Build a one-pixel-per-cell raster first, then scale it once. This avoids
    // tens of thousands of QPainter fill calls on every display refresh.
    const int mapWidthCells = map.maxX - map.minX + 1;
    const int mapHeightCells = map.maxY - map.minY + 1;
    constexpr int MaximumRasterSide = 4096;
    const double rasterReduction = std::max(
        1.0, std::max(mapWidthCells, mapHeightCells) / static_cast<double>(MaximumRasterSide));
    const int rasterWidth = std::max(1, static_cast<int>(std::ceil(mapWidthCells / rasterReduction)));
    const int rasterHeight = std::max(1, static_cast<int>(std::ceil(mapHeightCells / rasterReduction)));
    const QRgb unknownColor = QColor(QStringLiteral("#e9edf2")).rgb();
    const QRgb freeColor = QColor(QStringLiteral("#f2f4f7")).rgb();
    const QRgb obstacleColor = QColor(QStringLiteral("#23374d")).rgb();
    QImage occupancyRaster(rasterWidth, rasterHeight, QImage::Format_RGB32);
    occupancyRaster.fill(unknownColor);
    for (auto it = map.cells.cbegin(); it != map.cells.cend(); ++it) {
        int x = 0;
        int y = 0;
        unpackCellKey(it.key(), &x, &y);
        const int px = std::clamp(static_cast<int>((x - map.minX) / rasterReduction),
                                  0, rasterWidth - 1);
        const int py = std::clamp(static_cast<int>((map.maxY - y) / rasterReduction),
                                  0, rasterHeight - 1);
        QRgb *pixel = reinterpret_cast<QRgb *>(occupancyRaster.scanLine(py)) + px;
        if (it.value() >= map.occupiedThreshold) *pixel = obstacleColor;
        else if (it.value() <= map.freeThreshold && *pixel != obstacleColor) *pixel = freeColor;
    }
    removeIsolatedRasterCells(&occupancyRaster, obstacleColor, freeColor, unknownColor);
    const QPointF mapTopLeft = screenPoint(map.minX * map.resolution,
                                            (map.maxY + 1) * map.resolution);
    const QPointF mapBottomRight = screenPoint((map.maxX + 1) * map.resolution,
                                                map.minY * map.resolution);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRectF(mapTopLeft, mapBottomRight), occupancyRaster);

    // A restrained metric grid makes orientation and scale easier to read.
    const double gridStepMeters = 10.0;
    painter.setPen(QPen(QColor(148, 163, 184, 115), 1.0));
    const double firstGridEast = std::ceil(minEast / gridStepMeters) * gridStepMeters;
    const double firstGridNorth = std::ceil(minNorth / gridStepMeters) * gridStepMeters;
    for (double east = firstGridEast; east <= maxEast; east += gridStepMeters)
        painter.drawLine(screenPoint(east, maxNorth), screenPoint(east, minNorth));
    for (double north = firstGridNorth; north <= maxNorth; north += gridStepMeters)
        painter.drawLine(screenPoint(minEast, north), screenPoint(maxEast, north));
    if (map.track.size() > 1) {
        QPainterPath path(screenPoint(map.track.constFirst().x(), map.track.constFirst().y()));
        for (qsizetype i = 1; i < map.track.size(); ++i)
            path.lineTo(screenPoint(map.track.at(i).x(), map.track.at(i).y()));
        painter.setPen(QPen(QColor(QStringLiteral("#2474e5")), 2.5,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }
    painter.setPen(QPen(Qt::white, 1.0));
    painter.setBrush(QColor(QStringLiteral("#2474e5")));
    for (const Keyframe &keyframe : std::as_const(map.keyframes))
        painter.drawEllipse(screenPoint(keyframe.pose.position.x(),
                                        keyframe.pose.position.y()), 3.0, 3.0);
    painter.setPen(QPen(QColor(QStringLiteral("#8b5cf6")), 1.5, Qt::DashLine));
    for (const PoseConstraint &constraint : std::as_const(map.constraints)) {
        if (!constraint.loopClosure || constraint.fromIndex < 0 || constraint.toIndex < 0
            || constraint.fromIndex >= map.keyframes.size()
            || constraint.toIndex >= map.keyframes.size()) {
            continue;
        }
        const QPointF from = map.keyframes.at(constraint.fromIndex).pose.position;
        const QPointF to = map.keyframes.at(constraint.toIndex).pose.position;
        painter.drawLine(screenPoint(from.x(), from.y()), screenPoint(to.x(), to.y()));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#f59e0b")));
    for (const QPointF &hit : std::as_const(map.hits))
        painter.drawEllipse(screenPoint(hit.x(), hit.y()), 2.3, 2.3);

    const QPointF drone = screenPoint(map.pose.x(), map.pose.y());
    painter.save();
    painter.translate(drone);
    painter.rotate(map.heading);
    QPainterPath arrow;
    arrow.moveTo(0.0, -11.0);
    arrow.lineTo(7.0, 8.0);
    arrow.lineTo(0.0, 4.0);
    arrow.lineTo(-7.0, 8.0);
    arrow.closeSubpath();
    painter.setBrush(QColor(QStringLiteral("#1268e8")));
    painter.setPen(QPen(Qt::white, 2.0));
    painter.drawPath(arrow);
    painter.restore();

    // North arrow and scale bar remain visible at every map extent.
    painter.setPen(QPen(QColor(QStringLiteral("#334155")), 2.0));
    painter.setBrush(QColor(QStringLiteral("#334155")));
    painter.drawLine(QPointF(34, 54), QPointF(34, 24));
    QPainterPath northArrow;
    northArrow.moveTo(34, 17);
    northArrow.lineTo(28, 28);
    northArrow.lineTo(40, 28);
    northArrow.closeSubpath();
    painter.drawPath(northArrow);
    painter.drawText(QPointF(28, 70), QStringLiteral("N"));
    const double scaleBarMeters = scale >= 10.0 ? 10.0 : 20.0;
    const double scaleBarPixels = scaleBarMeters * scale;
    const QPointF scaleStart(28, ImageHeight - 30);
    painter.drawLine(scaleStart, QPointF(scaleStart.x() + scaleBarPixels, scaleStart.y()));
    painter.drawLine(QPointF(scaleStart.x(), scaleStart.y() - 5),
                     QPointF(scaleStart.x(), scaleStart.y() + 5));
    painter.drawLine(QPointF(scaleStart.x() + scaleBarPixels, scaleStart.y() - 5),
                     QPointF(scaleStart.x() + scaleBarPixels, scaleStart.y() + 5));
    painter.drawText(QPointF(scaleStart.x(), scaleStart.y() - 8),
                     QStringLiteral("%1 m").arg(scaleBarMeters, 0, 'f', 0));
    painter.end();

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return QStringLiteral("data:image/png;base64,%1")
                       .arg(QString::fromLatin1(bytes.toBase64()));
}

bool LidarMapOutput::writePortableMap(const LidarMapSnapshot &map, const QString &directoryPath,
                                              QString *pngPath,
                                              QString *errorMessage)
{
    if (!map.available || !map.boundsValid) {
        if (errorMessage) *errorMessage = QStringLiteral("当前还没有可保存的累计地图");
        return false;
    }
    if (!QDir().mkpath(directoryPath)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建地图保存目录");
        return false;
    }
    const int width = map.maxX - map.minX + 1;
    const int height = map.maxY - map.minY + 1;
    if (width <= 0 || height <= 0 || static_cast<qint64>(width) * height > 100000000LL) {
        if (errorMessage) *errorMessage = QStringLiteral("地图范围过大，已阻止生成超大文件");
        return false;
    }
    const QRgb unknownGray = qRgb(205, 205, 205);
    const QRgb freeGray = qRgb(254, 254, 254);
    const QRgb occupiedGray = qRgb(0, 0, 0);
    QImage occupancy(width, height, QImage::Format_RGB32);
    occupancy.fill(unknownGray);
    for (auto it = map.cells.cbegin(); it != map.cells.cend(); ++it) {
        int x = 0;
        int y = 0;
        unpackCellKey(it.key(), &x, &y);
        const int px = x - map.minX;
        const int py = map.maxY - y;
        QRgb *pixel = reinterpret_cast<QRgb *>(occupancy.scanLine(py)) + px;
        if (it.value() >= map.occupiedThreshold)
            *pixel = occupiedGray;
        else if (it.value() <= map.freeThreshold && *pixel != occupiedGray)
            *pixel = freeGray;
    }
    removeIsolatedRasterCells(&occupancy, occupiedGray, freeGray, unknownGray);

    // The PNG is the human-facing preview, while PGM remains a standards-
    // compatible occupancy map. A softer free-space color keeps valid scan
    // coverage from visually overwhelming building outlines.
    QImage preview(width, height, QImage::Format_RGB32);
    for (int row = 0; row < height; ++row) {
        const QRgb *sourceLine = reinterpret_cast<const QRgb *>(occupancy.constScanLine(row));
        QRgb *previewLine = reinterpret_cast<QRgb *>(preview.scanLine(row));
        for (int column = 0; column < width; ++column) {
            previewLine[column] = sourceLine[column] == occupiedGray
                ? qRgb(35, 55, 77)
                : sourceLine[column] == freeGray
                    ? qRgb(242, 244, 247)
                    : qRgb(233, 237, 242);
        }
    }
    const QString actualPngPath = QDir(directoryPath).filePath(QStringLiteral("lidar-map.png"));
    QImageWriter pngWriter(actualPngPath, QByteArrayLiteral("png"));
    if (!pngWriter.write(preview)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入地图 PNG：%1（%2 × %3）")
                                .arg(pngWriter.errorString())
                                .arg(width)
                                .arg(height);
        }
        return false;
    }
    const QString pgmPath = QDir(directoryPath).filePath(QStringLiteral("lidar-map.pgm"));
    QSaveFile pgm(pgmPath);
    if (!pgm.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法写入地图 PGM");
        return false;
    }
    const QByteArray header = QByteArray("P5\n") + QByteArray::number(width) + ' '
                              + QByteArray::number(height) + "\n255\n";
    if (pgm.write(header) != header.size()) return false;
    for (int row = 0; row < height; ++row) {
        QByteArray scanLine(width, static_cast<char>(205));
        const QRgb *sourceLine = reinterpret_cast<const QRgb *>(occupancy.constScanLine(row));
        for (int column = 0; column < width; ++column) {
            scanLine[column] = sourceLine[column] == occupiedGray
                ? static_cast<char>(0)
                : sourceLine[column] == freeGray
                    ? static_cast<char>(254)
                    : static_cast<char>(205);
        }
        if (pgm.write(scanLine) != scanLine.size()) return false;
    }
    if (!pgm.commit()) return false;

    const double originEast = map.minX * map.resolution;
    const double originNorth = map.minY * map.resolution;
    QSaveFile yaml(QDir(directoryPath).filePath(QStringLiteral("lidar-map.yaml")));
    const QByteArray yamlContents = QStringLiteral(
        "image: lidar-map.pgm\nresolution: %1\norigin: [%2, %3, 0.0]\nnegate: 0\noccupied_thresh: 0.65\nfree_thresh: 0.25\n")
        .arg(map.resolution, 0, 'f', 3)
        .arg(originEast, 0, 'f', 3)
        .arg(originNorth, 0, 'f', 3).toUtf8();
    if (!yaml.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || yaml.write(yamlContents) != yamlContents.size() || !yaml.commit()) return false;

    QJsonObject metadata = QJsonObject::fromVariantMap(map.summary);
    metadata.insert(QStringLiteral("originEastMeters"), originEast);
    metadata.insert(QStringLiteral("originNorthMeters"), originNorth);
    metadata.insert(QStringLiteral("widthCells"), width);
    metadata.insert(QStringLiteral("heightCells"), height);
    QSaveFile json(QDir(directoryPath).filePath(QStringLiteral("lidar-map.json")));
    const QByteArray jsonContents = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    if (!json.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || json.write(jsonContents) != jsonContents.size() || !json.commit()) return false;
    if (!writePoseGraph(map, directoryPath, errorMessage)) return false;
    if (pngPath) *pngPath = actualPngPath;
    return true;
}

bool LidarMapOutput::writePoseGraph(const LidarMapSnapshot &map, const QString &directoryPath,
                                             QString *errorMessage)
{
    QJsonArray nodes;
    for (qsizetype index = 0; index < map.keyframes.size(); ++index) {
        const Keyframe &keyframe = map.keyframes.at(index);
        QJsonArray scan;
        for (const LocalScanPoint &point : keyframe.scan) {
            scan.append(QJsonArray{point.rightMeters, point.forwardMeters,
                                   point.rangeMeters, point.obstacleHit});
        }
        nodes.append(QJsonObject{
            {QStringLiteral("id"), static_cast<int>(index)},
            {QStringLiteral("timestampMs"), keyframe.timestampMs},
            {QStringLiteral("eastMeters"), keyframe.pose.position.x()},
            {QStringLiteral("northMeters"), keyframe.pose.position.y()},
            {QStringLiteral("headingDegrees"), keyframe.pose.headingDegrees},
            {QStringLiteral("rawEastMeters"), keyframe.rawPose.position.x()},
            {QStringLiteral("rawNorthMeters"), keyframe.rawPose.position.y()},
            {QStringLiteral("rawHeadingDegrees"), keyframe.rawPose.headingDegrees},
            {QStringLiteral("scan"), scan},
        });
    }
    QJsonArray constraints;
    for (const PoseConstraint &constraint : map.constraints) {
        constraints.append(QJsonObject{
            {QStringLiteral("from"), constraint.fromIndex},
            {QStringLiteral("to"), constraint.toIndex},
            {QStringLiteral("rightMeters"), constraint.relativePosition.x()},
            {QStringLiteral("forwardMeters"), constraint.relativePosition.y()},
            {QStringLiteral("headingDegrees"), constraint.relativeHeadingDegrees},
            {QStringLiteral("translationWeight"), constraint.translationWeight},
            {QStringLiteral("headingWeight"), constraint.headingWeight},
            {QStringLiteral("type"), constraint.loopClosure
                 ? QStringLiteral("loop-closure") : QStringLiteral("odometry")},
        });
    }
    const QJsonObject graph{
        {QStringLiteral("format"), QStringLiteral("FlightBenchLidarPoseGraph")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("coordinateFrame"), QStringLiteral("local-east-north")},
        {QStringLiteral("nodeCount"), nodes.size()},
        {QStringLiteral("constraintCount"), constraints.size()},
        {QStringLiteral("loopClosureCount"), map.loopClosures},
        {QStringLiteral("optimizationCount"), map.optimizations},
        {QStringLiteral("nodes"), nodes},
        {QStringLiteral("constraints"), constraints},
    };
    QSaveFile file(QDir(directoryPath).filePath(QStringLiteral("lidar-pose-graph.json")));
    const QByteArray contents = QJsonDocument(graph).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(contents) != contents.size() || !file.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("无法写入 SLAM 位姿图");
        return false;
    }
    return true;
}
