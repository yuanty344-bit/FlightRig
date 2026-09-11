#include "mapping/flightpathgeometry.h"

#include <algorithm>

namespace {
constexpr int MaxPathPoints = 5000;
constexpr float MinimumPointDistanceMeters = 0.08f;
constexpr qint64 MaximumSampleIntervalMs = 250;

void appendTriangle(QVector<QVector3D> &vertices,
                    const QVector3D &a,
                    const QVector3D &b,
                    const QVector3D &c)
{
    vertices.append(a);
    vertices.append(b);
    vertices.append(c);
}

void appendQuad(QVector<QVector3D> &vertices,
                const QVector3D &a,
                const QVector3D &b,
                const QVector3D &c,
                const QVector3D &d)
{
    appendTriangle(vertices, a, b, c);
    appendTriangle(vertices, a, c, d);
}
}

FlightPathGeometry::FlightPathGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    // A real triangle tube is used instead of LineStrip. D3D/OpenGL drivers are
    // free to clamp 3D line width to one pixel, which made the old live path
    // effectively invisible on some Windows machines.
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    setStride(3 * static_cast<int>(sizeof(float)));
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0,
                 QQuick3DGeometry::Attribute::F32Type);
    setBounds(QVector3D(-1.0f, -1.0f, -1.0f), QVector3D(1.0f, 1.0f, 1.0f));
}

void FlightPathGeometry::appendPoint(const QVector3D &point)
{
    if (!qIsFinite(point.x()) || !qIsFinite(point.y()) || !qIsFinite(point.z())) return;

    if (!m_points.isEmpty()) {
        const bool movedEnough = m_points.constLast().distanceToPoint(point)
                                 >= MinimumPointDistanceMeters;
        const bool waitedEnough = m_sampleClock.isValid()
                                  && m_sampleClock.elapsed() >= MaximumSampleIntervalMs;
        if (!movedEnough && !waitedEnough) return;
    }

    if (m_points.size() >= MaxPathPoints) {
        m_points.remove(0, std::min<qsizetype>(250, m_points.size()));
    }
    m_points.append(point);
    m_sampleClock.restart();
    rebuildGeometry();
    emit pointCountChanged();
}

void FlightPathGeometry::setPoints(const QVector<QVector3D> &points)
{
    QVector<QVector3D> validPoints;
    validPoints.reserve(std::min<qsizetype>(points.size(), MaxPathPoints));
    const qsizetype first = std::max<qsizetype>(0, points.size() - MaxPathPoints);
    for (qsizetype index = first; index < points.size(); ++index) {
        const QVector3D &point = points.at(index);
        if (qIsFinite(point.x()) && qIsFinite(point.y()) && qIsFinite(point.z()))
            validPoints.append(point);
    }
    m_points = std::move(validPoints);
    m_sampleClock.invalidate();
    rebuildGeometry();
    emit pointCountChanged();
}

void FlightPathGeometry::clearPath()
{
    if (m_points.isEmpty()) return;
    m_points.clear();
    m_sampleClock.invalidate();
    rebuildGeometry();
    emit pointCountChanged();
}

void FlightPathGeometry::setWidthMeters(double value)
{
    const double boundedValue = qBound(0.02, value, 0.12);
    if (qFuzzyCompare(m_widthMeters, boundedValue)) return;
    m_widthMeters = boundedValue;
    rebuildGeometry();
    emit displaySettingsChanged();
}

void FlightPathGeometry::setMaxVisibleLengthMeters(int value)
{
    const int boundedValue = value <= 0 ? 0 : qBound(10, value, 1000);
    if (m_maxVisibleLengthMeters == boundedValue) return;
    m_maxVisibleLengthMeters = boundedValue;
    rebuildGeometry();
    emit displaySettingsChanged();
}

void FlightPathGeometry::setDashed(bool dashed)
{
    if (m_dashed == dashed) return;
    m_dashed = dashed;
    rebuildGeometry();
}

void FlightPathGeometry::rebuildGeometry()
{
    m_lengthMeters = 0.0;
    for (qsizetype index = 1; index < m_points.size(); ++index) {
        m_lengthMeters += m_points.at(index - 1).distanceToPoint(m_points.at(index));
    }

    QVector<QVector3D> visiblePoints;
    if (m_maxVisibleLengthMeters <= 0 || m_lengthMeters <= m_maxVisibleLengthMeters) {
        visiblePoints = m_points;
    } else if (!m_points.isEmpty()) {
        double remainingMeters = m_maxVisibleLengthMeters;
        visiblePoints.prepend(m_points.constLast());
        for (qsizetype index = m_points.size() - 1; index > 0; --index) {
            const QVector3D end = m_points.at(index);
            const QVector3D start = m_points.at(index - 1);
            const double segmentMeters = start.distanceToPoint(end);
            if (segmentMeters <= remainingMeters) {
                visiblePoints.prepend(start);
                remainingMeters -= segmentMeters;
                continue;
            }
            if (segmentMeters > 0.000001) {
                const float fraction = static_cast<float>(remainingMeters / segmentMeters);
                visiblePoints.prepend(end + (start - end) * fraction);
            }
            break;
        }
    }

    const float pathHalfWidthMeters = static_cast<float>(m_widthMeters * 0.5);
    QVector<QVector3D> vertices;
    vertices.reserve(std::max<qsizetype>(0, visiblePoints.size() - 1) * 24);

    const auto appendTube = [&vertices, pathHalfWidthMeters](const QVector3D &start,
                                                             const QVector3D &end) {
        const QVector3D delta = end - start;
        if (delta.lengthSquared() < 0.000001f) return;

        const QVector3D direction = delta.normalized();
        const QVector3D reference = std::abs(QVector3D::dotProduct(direction,
                                                                   QVector3D(0, 1, 0))) < 0.92f
                                        ? QVector3D(0, 1, 0)
                                        : QVector3D(1, 0, 0);
        const QVector3D side = QVector3D::crossProduct(direction, reference).normalized()
                               * pathHalfWidthMeters;
        const QVector3D secondSide = QVector3D::crossProduct(direction, side).normalized()
                                     * pathHalfWidthMeters;

        const QVector3D start00 = start - side - secondSide;
        const QVector3D start01 = start - side + secondSide;
        const QVector3D start10 = start + side - secondSide;
        const QVector3D start11 = start + side + secondSide;
        const QVector3D end00 = end - side - secondSide;
        const QVector3D end01 = end - side + secondSide;
        const QVector3D end10 = end + side - secondSide;
        const QVector3D end11 = end + side + secondSide;

        appendQuad(vertices, start00, end00, end01, start01);
        appendQuad(vertices, start10, start11, end11, end10);
        appendQuad(vertices, start00, start10, end10, end00);
        appendQuad(vertices, start01, end01, end11, start11);
    };

    for (qsizetype index = 1; index < visiblePoints.size(); ++index) {
        const QVector3D start = visiblePoints.at(index - 1);
        const QVector3D end = visiblePoints.at(index);
        if (!m_dashed) {
            appendTube(start, end);
            continue;
        }
        const QVector3D delta = end - start;
        const float length = delta.length();
        if (length < 0.001f) continue;
        constexpr float dashLength = 0.55f;
        constexpr float gapLength = 0.32f;
        for (float offset = 0; offset < length; offset += dashLength + gapLength) {
            const float dashEnd = std::min(length, offset + dashLength);
            appendTube(start + delta * (offset / length),
                       start + delta * (dashEnd / length));
        }
    }

    QByteArray vertexData;
    vertexData.resize(vertices.size() * 3 * static_cast<int>(sizeof(float)));
    float *destination = reinterpret_cast<float *>(vertexData.data());
    for (qsizetype index = 0; index < vertices.size(); ++index) {
        const QVector3D &vertex = vertices.at(index);
        destination[index * 3] = vertex.x();
        destination[index * 3 + 1] = vertex.y();
        destination[index * 3 + 2] = vertex.z();
    }

    QVector3D minimum(0.0f, 0.0f, 0.0f);
    QVector3D maximum(0.0f, 0.0f, 0.0f);
    for (qsizetype index = 0; index < visiblePoints.size(); ++index) {
        const QVector3D &point = visiblePoints.at(index);
        if (index == 0) {
            minimum = point;
            maximum = point;
        } else {
            minimum.setX(std::min(minimum.x(), point.x()));
            minimum.setY(std::min(minimum.y(), point.y()));
            minimum.setZ(std::min(minimum.z(), point.z()));
            maximum.setX(std::max(maximum.x(), point.x()));
            maximum.setY(std::max(maximum.y(), point.y()));
            maximum.setZ(std::max(maximum.z(), point.z()));
        }
    }

    const float boundsPadding = 0.5f + pathHalfWidthMeters;
    setVertexData(vertexData);
    setBounds(minimum - QVector3D(boundsPadding, boundsPadding, boundsPadding),
              maximum + QVector3D(boundsPadding, boundsPadding, boundsPadding));
    // setVertexData() changes the resource, while update() schedules the scene
    // synchronization and a new frame. Without this call, geometry built before
    // the View3D exists is visible, but live flight updates remain on the CPU.
    update();
}
