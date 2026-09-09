#include "worldmapcatalog.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr double kGeometryEpsilon = 1e-7;

struct ObstacleTriangle
{
    QPointF a;
    QPointF b;
    QPointF c;
    double heightMeters{0.0};
};

struct WorldMapData
{
    bool available{false};
    QString worldId;
    double minEastMeters{0.0};
    double maxEastMeters{0.0};
    double minNorthMeters{0.0};
    double maxNorthMeters{0.0};
    double safetyMarginMeters{3.0};
    QPointF recommendedTakeoffPoint;
    double recommendedClearanceMeters{0.0};
    QVector<ObstacleTriangle> obstacles;
};

double cross(const QPointF &a, const QPointF &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

bool pointInTriangle(const QPointF &point, const ObstacleTriangle &triangle)
{
    const double d1 = cross(triangle.b - triangle.a, point - triangle.a);
    const double d2 = cross(triangle.c - triangle.b, point - triangle.b);
    const double d3 = cross(triangle.a - triangle.c, point - triangle.c);
    const bool hasNegative = d1 < -kGeometryEpsilon || d2 < -kGeometryEpsilon
                             || d3 < -kGeometryEpsilon;
    const bool hasPositive = d1 > kGeometryEpsilon || d2 > kGeometryEpsilon
                             || d3 > kGeometryEpsilon;
    return !(hasNegative && hasPositive);
}

double pointToSegmentDistance(const QPointF &point,
                              const QPointF &start,
                              const QPointF &end)
{
    const QPointF segment = end - start;
    const double lengthSquared = QPointF::dotProduct(segment, segment);
    if (lengthSquared <= kGeometryEpsilon)
        return std::hypot(point.x() - start.x(), point.y() - start.y());
    const double projection = std::clamp(
        QPointF::dotProduct(point - start, segment) / lengthSquared, 0.0, 1.0);
    const QPointF nearest = start + segment * projection;
    return std::hypot(point.x() - nearest.x(), point.y() - nearest.y());
}

double horizontalDistanceToTriangle(const QPointF &point,
                                    const ObstacleTriangle &triangle)
{
    if (pointInTriangle(point, triangle)) return 0.0;
    return std::min({pointToSegmentDistance(point, triangle.a, triangle.b),
                     pointToSegmentDistance(point, triangle.b, triangle.c),
                     pointToSegmentDistance(point, triangle.c, triangle.a)});
}

double nearestObstacleDistance(const QPointF &point,
                               const QVector<ObstacleTriangle> &obstacles)
{
    double minimum = std::numeric_limits<double>::max();
    for (const ObstacleTriangle &obstacle : obstacles)
        minimum = std::min(minimum, horizontalDistanceToTriangle(point, obstacle));
    return std::isfinite(minimum) ? minimum : 0.0;
}

bool segmentIntersectionParameter(const QPointF &start,
                                  const QPointF &end,
                                  const QPointF &edgeStart,
                                  const QPointF &edgeEnd,
                                  double *parameter)
{
    const QPointF route = end - start;
    const QPointF edge = edgeEnd - edgeStart;
    const double denominator = cross(route, edge);
    if (std::abs(denominator) <= kGeometryEpsilon) return false;

    const QPointF delta = edgeStart - start;
    const double routeParameter = cross(delta, edge) / denominator;
    const double edgeParameter = cross(delta, route) / denominator;
    if (routeParameter < -kGeometryEpsilon || routeParameter > 1.0 + kGeometryEpsilon
        || edgeParameter < -kGeometryEpsilon || edgeParameter > 1.0 + kGeometryEpsilon) {
        return false;
    }
    *parameter = std::clamp(routeParameter, 0.0, 1.0);
    return true;
}

bool routeIntervalInsideTriangle(const QPointF &start,
                                 const QPointF &end,
                                 const ObstacleTriangle &triangle,
                                 double *minimumParameter,
                                 double *maximumParameter)
{
    QVector<double> parameters;
    if (pointInTriangle(start, triangle)) parameters.append(0.0);
    if (pointInTriangle(end, triangle)) parameters.append(1.0);

    const std::array<std::pair<QPointF, QPointF>, 3> edges{
        std::pair{triangle.a, triangle.b},
        std::pair{triangle.b, triangle.c},
        std::pair{triangle.c, triangle.a},
    };
    for (const auto &[edgeStart, edgeEnd] : edges) {
        double parameter = 0.0;
        if (segmentIntersectionParameter(start, end, edgeStart, edgeEnd, &parameter))
            parameters.append(parameter);
    }
    if (parameters.isEmpty()) return false;

    const auto [minimum, maximum] = std::minmax_element(parameters.cbegin(), parameters.cend());
    *minimumParameter = *minimum;
    *maximumParameter = *maximum;
    return true;
}

WorldMapData loadMap(const QString &worldId)
{
    WorldMapData data;
    const QString resourcePath = QStringLiteral(":/worldmaps/%1.json").arg(worldId);
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) return data;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return data;

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString()
            != QStringLiteral("FlightBenchWorldMap")) {
        return data;
    }
    const QJsonObject bounds = root.value(QStringLiteral("bounds")).toObject();
    const QJsonArray triangles = root.value(QStringLiteral("buildingRoofTriangles")).toArray();
    if (bounds.isEmpty() || triangles.isEmpty()) return data;

    data.worldId = root.value(QStringLiteral("worldId")).toString(worldId);
    data.minEastMeters = bounds.value(QStringLiteral("minEast")).toDouble();
    data.maxEastMeters = bounds.value(QStringLiteral("maxEast")).toDouble();
    data.minNorthMeters = bounds.value(QStringLiteral("minNorth")).toDouble();
    data.maxNorthMeters = bounds.value(QStringLiteral("maxNorth")).toDouble();
    data.safetyMarginMeters = root.value(QStringLiteral("safetyMarginMeters")).toDouble(3.0);
    if (data.maxEastMeters <= data.minEastMeters
        || data.maxNorthMeters <= data.minNorthMeters) {
        return {};
    }

    data.obstacles.reserve(triangles.size());
    for (const QJsonValue &value : triangles) {
        const QJsonArray triangle = value.toArray();
        if (triangle.size() != 7) continue;
        data.obstacles.append({QPointF(triangle.at(0).toDouble(), triangle.at(1).toDouble()),
                               QPointF(triangle.at(2).toDouble(), triangle.at(3).toDouble()),
                               QPointF(triangle.at(4).toDouble(), triangle.at(5).toDouble()),
                               triangle.at(6).toDouble()});
    }
    data.available = !data.obstacles.isEmpty();
    if (data.available) {
        // All generated FlightBench worlds use their local origin as the PX4
        // spawn point. Report the real spawn clearance instead of suggesting a
        // point that Gazebo would not actually use.
        data.recommendedTakeoffPoint = QPointF(0.0, 0.0);
        data.recommendedClearanceMeters = nearestObstacleDistance(
            data.recommendedTakeoffPoint, data.obstacles);
    }
    return data;
}

const WorldMapData &mapForWorld(const QString &worldId)
{
    static QHash<QString, WorldMapData> maps;
    auto iterator = maps.find(worldId);
    if (iterator == maps.end()) iterator = maps.insert(worldId, loadMap(worldId));
    return iterator.value();
}

QVariantList sortedValues(const QSet<int> &values)
{
    QList<int> sorted = values.values();
    std::sort(sorted.begin(), sorted.end());
    QVariantList result;
    result.reserve(sorted.size());
    for (const int value : sorted) result.append(value);
    return result;
}
}

QVariantMap WorldMapCatalog::metadata(const QString &worldId)
{
    const WorldMapData &map = mapForWorld(worldId);
    if (!map.available) return {{QStringLiteral("available"), false}};
    return {
        {QStringLiteral("available"), true},
        {QStringLiteral("worldId"), map.worldId},
        {QStringLiteral("lightSource"),
         QUrl(QStringLiteral("qrc:/worldmaps/%1-light.svg").arg(map.worldId))},
        {QStringLiteral("darkSource"),
         QUrl(QStringLiteral("qrc:/worldmaps/%1-dark.svg").arg(map.worldId))},
        {QStringLiteral("minEastMeters"), map.minEastMeters},
        {QStringLiteral("maxEastMeters"), map.maxEastMeters},
        {QStringLiteral("minNorthMeters"), map.minNorthMeters},
        {QStringLiteral("maxNorthMeters"), map.maxNorthMeters},
        {QStringLiteral("safetyMarginMeters"), map.safetyMarginMeters},
        {QStringLiteral("obstacleTriangleCount"), map.obstacles.size()},
        {QStringLiteral("recommendedTakeoffEastMeters"), map.recommendedTakeoffPoint.x()},
        {QStringLiteral("recommendedTakeoffNorthMeters"), map.recommendedTakeoffPoint.y()},
        {QStringLiteral("recommendedTakeoffClearanceMeters"), map.recommendedClearanceMeters},
        {QStringLiteral("recommendedTakeoffName"), QStringLiteral("世界原点起飞区")},
    };
}

QVariantMap WorldMapCatalog::raycastBuilding(const QString &worldId,
    const QVector3D &origin, const QVector3D &direction, double maximumDistance)
{
    const auto &map = mapForWorld(worldId);
    QVariantMap result{{QStringLiteral("available"), map.available},
                       {QStringLiteral("hit"), false}};
    const QVector3D ray = direction.normalized();
    if (!map.available || ray.lengthSquared() < 0.5f || !std::isfinite(maximumDistance)
        || maximumDistance <= 0.0 || !std::isfinite(origin.lengthSquared())
        || !std::isfinite(ray.lengthSquared())) return result;
    double nearest = maximumDistance;
    QVector3D nearestNormal;
    for (const auto &triangle : map.obstacles) {
        double enter = 0.0, leave = nearest;
        QVector3D normal;
        bool intersects = true;
        const auto plane = [&](const QVector3D &n, double bound) {
            if (!intersects) return;
            const double offset = QVector3D::dotProduct(n, origin) - bound;
            const double slope = QVector3D::dotProduct(n, ray);
            if (std::abs(slope) < 1e-8) {
                if (offset > 1e-5) intersects = false;
                return;
            }
            const double t = -offset / slope;
            if (slope < 0.0 && t > enter) { enter = t; normal = n.normalized(); }
            else if (slope > 0.0) leave = std::min(leave, t);
            if (enter > leave + 1e-6) intersects = false;
        };
        const std::array<QPointF, 3> p{triangle.a, triangle.b, triangle.c};
        const double sign = cross(p[1] - p[0], p[2] - p[0]) >= 0.0 ? 1.0 : -1.0;
        for (int i = 0; i < 3; ++i) {
            const QPointF edge = p[(i + 1) % 3] - p[i];
            const double east = sign * edge.y(), north = -sign * edge.x();
            plane(QVector3D(float(east), 0, float(-north)),
                  east * p[i].x() + north * p[i].y());
        }
        plane(QVector3D(0, 1, 0), triangle.heightMeters);
        plane(QVector3D(0, -1, 0), 0.0);
        // Origins inside a building are unsafe too. Return a zero-distance hit
        // for visibility checks, but callers must reject it as a picked target.
        if (intersects && leave >= 0.0 && enter <= nearest) {
            nearest = enter;
            nearestNormal = normal;
            result[QStringLiteral("hit")] = true;
        }
    }
    if (result.value(QStringLiteral("hit")).toBool()) {
        result[QStringLiteral("distance")] = nearest;
        result[QStringLiteral("position")] = QVariant::fromValue(origin + ray * float(nearest));
        result[QStringLiteral("normal")] = QVariant::fromValue(nearestNormal);
    }
    return result;
}

QVariantMap WorldMapCatalog::analyzeRoute(const QString &worldId,
                                           const QVariantList &waypoints,
                                           double startEastMeters,
                                           double startNorthMeters,
                                           double startAltitudeMeters)
{
    const WorldMapData &map = mapForWorld(worldId);
    if (!map.available) {
        return {{QStringLiteral("available"), false},
                {QStringLiteral("safe"), true},
                {QStringLiteral("message"), QStringLiteral("当前场景没有建筑障碍数据")}};
    }

    struct RoutePoint
    {
        QPointF position;
        double altitudeMeters{0.0};
    };
    QVector<RoutePoint> route;
    route.reserve(waypoints.size() + 1);
    route.append({QPointF(startEastMeters, startNorthMeters),
                  std::max(0.0, startAltitudeMeters)});
    for (const QVariant &entry : waypoints) {
        const QVariantMap waypoint = entry.toMap();
        bool eastValid = false;
        bool northValid = false;
        bool altitudeValid = false;
        const double east = waypoint.value(QStringLiteral("eastMeters")).toDouble(&eastValid);
        const double north = waypoint.value(QStringLiteral("northMeters")).toDouble(&northValid);
        const double altitude = waypoint.value(QStringLiteral("altitudeMeters")).toDouble(&altitudeValid);
        if (!eastValid || !northValid || !altitudeValid) continue;
        route.append({QPointF(east, north), std::max(0.0, altitude)});
    }

    QSet<int> riskyWaypoints;
    QSet<int> riskySegments;
    double requiredAltitudeMeters = 0.0;
    for (int waypointIndex = 1; waypointIndex < route.size(); ++waypointIndex) {
        const RoutePoint &waypoint = route.at(waypointIndex);
        for (const ObstacleTriangle &obstacle : map.obstacles) {
            const double safeAltitude = obstacle.heightMeters + map.safetyMarginMeters;
            if (waypoint.altitudeMeters + kGeometryEpsilon < safeAltitude
                && pointInTriangle(waypoint.position, obstacle)) {
                riskyWaypoints.insert(waypointIndex - 1);
                requiredAltitudeMeters = std::max(requiredAltitudeMeters, safeAltitude);
            }
        }
    }

    for (int segmentIndex = 0; segmentIndex + 1 < route.size(); ++segmentIndex) {
        const RoutePoint &start = route.at(segmentIndex);
        const RoutePoint &end = route.at(segmentIndex + 1);
        for (const ObstacleTriangle &obstacle : map.obstacles) {
            double minimumParameter = 0.0;
            double maximumParameter = 0.0;
            if (!routeIntervalInsideTriangle(start.position, end.position, obstacle,
                                             &minimumParameter, &maximumParameter)) {
                continue;
            }
            const double altitudeDelta = end.altitudeMeters - start.altitudeMeters;
            const double firstAltitude = start.altitudeMeters + altitudeDelta * minimumParameter;
            const double lastAltitude = start.altitudeMeters + altitudeDelta * maximumParameter;
            const double safeAltitude = obstacle.heightMeters + map.safetyMarginMeters;
            if (std::min(firstAltitude, lastAltitude) + kGeometryEpsilon < safeAltitude) {
                riskySegments.insert(segmentIndex);
                requiredAltitudeMeters = std::max(requiredAltitudeMeters, safeAltitude);
            }
        }
    }

    const bool safe = riskyWaypoints.isEmpty() && riskySegments.isEmpty();
    const int affectedCount = std::max(riskyWaypoints.size(), riskySegments.size());
    const QString message = safe
        ? QStringLiteral("航线未发现低空穿越建筑")
        : QStringLiteral("发现 %1 处建筑风险，相关航段建议高于 %2 米")
              .arg(affectedCount)
              .arg(QString::number(requiredAltitudeMeters, 'f', 1));
    return {
        {QStringLiteral("available"), true},
        {QStringLiteral("safe"), safe},
        {QStringLiteral("message"), message},
        {QStringLiteral("riskyWaypointIndices"), sortedValues(riskyWaypoints)},
        {QStringLiteral("riskySegmentIndices"), sortedValues(riskySegments)},
        {QStringLiteral("requiredAltitudeMeters"), requiredAltitudeMeters},
        {QStringLiteral("safetyMarginMeters"), map.safetyMarginMeters},
    };
}

QVariantMap WorldMapCatalog::analyzePosition(const QString &worldId,
                                             double eastMeters,
                                             double northMeters,
                                             double altitudeMeters)
{
    const WorldMapData &map = mapForWorld(worldId);
    if (!map.available) return {{QStringLiteral("available"), false}};

    const QPointF position(eastMeters, northMeters);
    double horizontalClearance = std::numeric_limits<double>::max();
    double verticalClearance = std::numeric_limits<double>::max();
    bool insideBuilding = false;
    bool infringed = false;
    for (const ObstacleTriangle &obstacle : map.obstacles) {
        const double horizontal = horizontalDistanceToTriangle(position, obstacle);
        horizontalClearance = std::min(horizontalClearance, horizontal);
        if (horizontal <= kGeometryEpsilon) {
            insideBuilding = true;
            const double vertical = altitudeMeters - obstacle.heightMeters;
            verticalClearance = std::min(verticalClearance, vertical);
            if (vertical < map.safetyMarginMeters) infringed = true;
        }
    }

    const double effectiveClearance = insideBuilding
                                          ? std::max(0.0, verticalClearance)
                                          : horizontalClearance;
    return {{QStringLiteral("available"), true},
            {QStringLiteral("insideBuildingFootprint"), insideBuilding},
            {QStringLiteral("infringed"), infringed},
            {QStringLiteral("horizontalClearanceMeters"), horizontalClearance},
            {QStringLiteral("verticalClearanceMeters"),
             insideBuilding ? verticalClearance : 0.0},
            {QStringLiteral("effectiveClearanceMeters"), effectiveClearance},
            {QStringLiteral("safetyMarginMeters"), map.safetyMarginMeters}};
}
