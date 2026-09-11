#pragma once
#include <QHash>
#include <QQuick3DGeometry>
#include <QVector3D>

class DepthSurfaceGeometry final : public QQuick3DGeometry
{
    Q_OBJECT
public:
    explicit DepthSurfaceGeometry(QQuick3DObject *parent = nullptr);
    void setSurfaceCells(const QHash<quint64, QVector3D> &centers, float sizeMeters);
    void setVoxelCenters(const QVector<QVector3D> &centers, float sizeMeters);
    void clearGeometry();
    [[nodiscard]] int triangleCount() const { return m_triangleCount; }
private:
    int m_triangleCount{0};
};
