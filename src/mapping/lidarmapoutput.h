#pragma once
#include "mapping/lidarmaptypes.h"
#include <QHash>
#include <QVariantMap>

// Borrowed read-only view: no duplicated occupancy grid or pose graph.
struct LidarMapSnapshot {
    const QHash<quint64, qint8> &cells;
    const QVector<QPointF> &track;
    const QVector<QPointF> &hits;
    const QVector<LidarMapTypes::Keyframe> &keyframes;
    const QVector<LidarMapTypes::PoseConstraint> &constraints;
    QPointF pose;
    double heading;
    int minX, maxX, minY, maxY;
    bool boundsValid, available;
    int loopClosures, optimizations;
    double resolution;
    int occupiedThreshold, freeThreshold;
    QVariantMap summary;
};
namespace LidarMapOutput {
QString render(const LidarMapSnapshot &map);
bool writePortableMap(const LidarMapSnapshot &map, const QString &directoryPath,
    QString *pngPath, QString *errorMessage);
bool writePoseGraph(const LidarMapSnapshot &map, const QString &directoryPath,
    QString *errorMessage);
}
