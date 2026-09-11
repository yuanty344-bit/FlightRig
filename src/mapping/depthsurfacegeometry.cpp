#include "mapping/depthsurfacegeometry.h"
#include "mapping/depthvoxelcoordinates.h"
#include <algorithm>
#include <limits>
#include <utility>
using DepthVoxelCoordinates::packedKey;
using DepthVoxelCoordinates::unpackedKey;
namespace {
struct MeshVertex {
    QVector3D position;
    QVector3D normal;
};

void appendFace(QVector<MeshVertex> &vertices, const QVector3D &a, const QVector3D &b,
                const QVector3D &c, const QVector3D &d, const QVector3D &normal)
{
    vertices << MeshVertex{a, normal} << MeshVertex{b, normal} << MeshVertex{c, normal}
             << MeshVertex{a, normal} << MeshVertex{c, normal} << MeshVertex{d, normal};
}

void appendCube(QVector<MeshVertex> &vertices, const QVector3D &center, float half)
{
    const QVector3D p000 = center + QVector3D(-half, -half, -half);
    const QVector3D p001 = center + QVector3D(-half, -half, half);
    const QVector3D p010 = center + QVector3D(-half, half, -half);
    const QVector3D p011 = center + QVector3D(-half, half, half);
    const QVector3D p100 = center + QVector3D(half, -half, -half);
    const QVector3D p101 = center + QVector3D(half, -half, half);
    const QVector3D p110 = center + QVector3D(half, half, -half);
    const QVector3D p111 = center + QVector3D(half, half, half);
    appendFace(vertices, p000, p100, p110, p010, QVector3D(0, 0, -1));
    appendFace(vertices, p101, p001, p011, p111, QVector3D(0, 0, 1));
    appendFace(vertices, p001, p000, p010, p011, QVector3D(-1, 0, 0));
    appendFace(vertices, p100, p101, p111, p110, QVector3D(1, 0, 0));
    appendFace(vertices, p010, p110, p111, p011, QVector3D(0, 1, 0));
    appendFace(vertices, p001, p101, p100, p000, QVector3D(0, -1, 0));
}

}

DepthSurfaceGeometry::DepthSurfaceGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    setStride(6 * static_cast<int>(sizeof(float)));
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float),
                 QQuick3DGeometry::Attribute::F32Type);
    setBounds(QVector3D(-1, -1, -1), QVector3D(1, 1, 1));
}

void DepthSurfaceGeometry::setSurfaceCells(const QHash<quint64, QVector3D> &centers,
                                           float sizeMeters)
{
    QVector<MeshVertex> vertices;
    vertices.reserve(centers.size() * 18);
    const float half = std::max(0.02f, sizeMeters * 0.5f);
    QVector3D minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max());
    QVector3D maximum(-minimum.x(), -minimum.y(), -minimum.z());
    for (auto it = centers.cbegin(); it != centers.cend(); ++it) {
        int x = 0, y = 0, z = 0;
        unpackedKey(it.key(), &x, &y, &z);
        const QVector3D center = it.value();
        const QVector3D p000 = center + QVector3D(-half, -half, -half);
        const QVector3D p001 = center + QVector3D(-half, -half, half);
        const QVector3D p010 = center + QVector3D(-half, half, -half);
        const QVector3D p011 = center + QVector3D(-half, half, half);
        const QVector3D p100 = center + QVector3D(half, -half, -half);
        const QVector3D p101 = center + QVector3D(half, -half, half);
        const QVector3D p110 = center + QVector3D(half, half, -half);
        const QVector3D p111 = center + QVector3D(half, half, half);
        if (!centers.contains(packedKey(x, y, z - 1)))
            appendFace(vertices, p000, p100, p110, p010, QVector3D(0, 0, -1));
        if (!centers.contains(packedKey(x, y, z + 1)))
            appendFace(vertices, p101, p001, p011, p111, QVector3D(0, 0, 1));
        if (!centers.contains(packedKey(x - 1, y, z)))
            appendFace(vertices, p001, p000, p010, p011, QVector3D(-1, 0, 0));
        if (!centers.contains(packedKey(x + 1, y, z)))
            appendFace(vertices, p100, p101, p111, p110, QVector3D(1, 0, 0));
        if (!centers.contains(packedKey(x, y + 1, z)))
            appendFace(vertices, p010, p110, p111, p011, QVector3D(0, 1, 0));
        if (!centers.contains(packedKey(x, y - 1, z)))
            appendFace(vertices, p001, p101, p100, p000, QVector3D(0, -1, 0));
        minimum.setX(std::min(minimum.x(), center.x() - half));
        minimum.setY(std::min(minimum.y(), center.y() - half));
        minimum.setZ(std::min(minimum.z(), center.z() - half));
        maximum.setX(std::max(maximum.x(), center.x() + half));
        maximum.setY(std::max(maximum.y(), center.y() + half));
        maximum.setZ(std::max(maximum.z(), center.z() + half));
    }
    QByteArray data(vertices.size() * 6 * static_cast<qsizetype>(sizeof(float)), Qt::Uninitialized);
    float *destination = reinterpret_cast<float *>(data.data());
    for (const MeshVertex &vertex : std::as_const(vertices)) {
        *destination++ = vertex.position.x(); *destination++ = vertex.position.y();
        *destination++ = vertex.position.z(); *destination++ = vertex.normal.x();
        *destination++ = vertex.normal.y(); *destination++ = vertex.normal.z();
    }
    setVertexData(data);
    m_triangleCount = vertices.size() / 3;
    if (!centers.isEmpty()) setBounds(minimum, maximum);
    update();
}

void DepthSurfaceGeometry::setVoxelCenters(const QVector<QVector3D> &centers, float sizeMeters)
{
    QVector<MeshVertex> vertices;
    vertices.reserve(centers.size() * 36);
    const float half = std::max(0.02f, sizeMeters * 0.5f);
    for (const QVector3D &center : centers) appendCube(vertices, center, half);
    QByteArray data(vertices.size() * 6 * static_cast<qsizetype>(sizeof(float)), Qt::Uninitialized);
    float *destination = reinterpret_cast<float *>(data.data());
    for (const MeshVertex &vertex : std::as_const(vertices)) {
        *destination++ = vertex.position.x(); *destination++ = vertex.position.y();
        *destination++ = vertex.position.z(); *destination++ = vertex.normal.x();
        *destination++ = vertex.normal.y(); *destination++ = vertex.normal.z();
    }
    setVertexData(data); m_triangleCount = vertices.size() / 3; update();
}

void DepthSurfaceGeometry::clearGeometry()
{
    setVertexData(QByteArray()); m_triangleCount = 0;
    setBounds(QVector3D(-1, -1, -1), QVector3D(1, 1, 1)); update();
}
