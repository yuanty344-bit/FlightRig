#pragma once
#include "mapping/depthvoxel.h"
#include <QHash>
#include <QVector3D>
#include <QString>
namespace DepthMapExport {
bool writePointCloud(const QHash<quint64, QVector3D> &centers,
    const QHash<quint64, DepthVoxel> &voxels, const QString &directoryPath,
    QString *plyPath, QString *csvPath, QString *errorMessage);
bool writeMesh(const QHash<quint64, QVector3D> &centers, float resolution,
    const QString &directoryPath, QString *meshPath, int *triangleCount, QString *errorMessage);
bool writeTopDownPreview(const QHash<quint64, QVector3D> &centers,
    bool boundsValid, double width, double depth, const QVector3D &center,
    const QString &directoryPath, QString *previewPath, QString *errorMessage);
}
