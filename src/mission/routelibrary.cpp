#include "mission/routelibrary.h"
#include "infrastructure/storagepaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace {
constexpr int RouteFormatVersion = 1;

bool validRouteId(const QString &routeId)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9-]{8,64}$"));
    return expression.match(routeId).hasMatch();
}

QString normalizedRouteName(const QString &name)
{
    QString normalized = name.simplified();
    if (normalized.size() > 40) normalized.truncate(40);
    return normalized;
}
}

RouteLibrary::RouteLibrary(QObject *parent)
    : QObject(parent)
{
    m_directory = StoragePaths::directory(QStringLiteral("Routes"));
    QDir().mkpath(m_directory);
    refresh();
}

QVariantMap RouteLibrary::result(bool success,
                                 const QString &message,
                                 const QVariantMap &extra)
{
    QVariantMap response = extra;
    response.insert(QStringLiteral("success"), success);
    response.insert(QStringLiteral("message"), message);
    return response;
}

QString RouteLibrary::routePath(const QString &routeId) const
{
    if (!validRouteId(routeId)) return {};
    return QDir(m_directory).filePath(routeId + QStringLiteral(".fbroute.json"));
}

QVariantMap RouteLibrary::readRoute(const QString &routeId) const
{
    const QString path = routePath(routeId);
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return {};
    const QVariantMap route = document.object().toVariantMap();
    if (route.value(QStringLiteral("format")).toString() != QStringLiteral("FlightBenchRoute")
        || route.value(QStringLiteral("version")).toInt() != RouteFormatVersion
        || route.value(QStringLiteral("id")).toString() != routeId) {
        return {};
    }
    return route;
}

bool RouteLibrary::writeRoute(const QVariantMap &route, QString *errorMessage) const
{
    const QString path = routePath(route.value(QStringLiteral("id")).toString());
    if (path.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("航线编号无效");
        return false;
    }
    if (!QDir().mkpath(m_directory)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建航线目录");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法写入航线文件");
        return false;
    }
    file.write(QJsonDocument(QJsonObject::fromVariantMap(route))
                   .toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("航线文件保存失败");
        return false;
    }
    return true;
}

QString RouteLibrary::uniqueName(const QString &baseName,
                                 const QString &excludedRouteId) const
{
    QString candidate = normalizedRouteName(baseName);
    if (candidate.isEmpty()) candidate = QStringLiteral("未命名航线");
    const QString original = candidate;
    for (int suffix = 2;; ++suffix) {
        bool exists = false;
        for (const QVariant &entry : m_routes) {
            const QVariantMap route = entry.toMap();
            if (route.value(QStringLiteral("id")).toString() != excludedRouteId
                && route.value(QStringLiteral("name")).toString().compare(
                       candidate, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) return candidate;
        candidate = QStringLiteral("%1 %2").arg(original).arg(suffix);
        if (candidate.size() > 40) candidate.truncate(40);
    }
}

QVariantMap RouteLibrary::saveRoute(const QString &routeId,
                                    const QString &name,
                                    const QVariantList &waypoints,
                                    const QString &endAction,
                                    double anchorEastMeters,
                                    double anchorNorthMeters)
{
    const QString normalizedName = normalizedRouteName(name);
    if (normalizedName.isEmpty()) return result(false, QStringLiteral("请输入航线名称"));
    if (waypoints.size() < 2 || waypoints.size() > 20) {
        return result(false, QStringLiteral("航线必须包含 2 至 20 个航点"));
    }
    if (!std::isfinite(anchorEastMeters) || !std::isfinite(anchorNorthMeters)) {
        return result(false, QStringLiteral("航线起点坐标无效"));
    }
    if (endAction != QStringLiteral("rtl") && endAction != QStringLiteral("land")
        && endAction != QStringLiteral("hold")) {
        return result(false, QStringLiteral("任务结束动作无效"));
    }

    QString id = routeId;
    QVariantMap previous;
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    } else {
        previous = readRoute(id);
        if (previous.isEmpty()) return result(false, QStringLiteral("要覆盖的航线不存在"));
    }

    QVariantList relativeWaypoints;
    relativeWaypoints.reserve(waypoints.size());
    for (const QVariant &entry : waypoints) {
        const QVariantMap point = entry.toMap();
        bool eastOk = false;
        bool northOk = false;
        bool altitudeOk = false;
        bool speedOk = false;
        const double east = point.value(QStringLiteral("eastMeters")).toDouble(&eastOk);
        const double north = point.value(QStringLiteral("northMeters")).toDouble(&northOk);
        const double altitude = point.value(QStringLiteral("altitudeMeters")).toDouble(&altitudeOk);
        const double speed = point.value(QStringLiteral("speedMetersPerSecond")).toDouble(&speedOk);
        const double yaw = point.value(QStringLiteral("yawDegrees"), 0.0).toDouble();
        const double gimbalYaw = point.value(QStringLiteral("gimbalYawDegrees"), 0.0).toDouble();
        const double gimbalPitch = point.value(QStringLiteral("gimbalPitchDegrees"), -45.0).toDouble();
        QString waypointName = point.value(QStringLiteral("name")).toString().trimmed();
        if (waypointName.size() > 24) waypointName.truncate(24);
        if (!eastOk || !northOk || !altitudeOk || !speedOk
            || !std::isfinite(east) || !std::isfinite(north)
            || !std::isfinite(altitude) || !std::isfinite(speed)
            || !std::isfinite(yaw) || !std::isfinite(gimbalYaw)
            || !std::isfinite(gimbalPitch)) {
            return result(false, QStringLiteral("航点数据不完整"));
        }
        relativeWaypoints.append(QVariantMap{
            {QStringLiteral("name"), waypointName},
            {QStringLiteral("eastMeters"), east - anchorEastMeters},
            {QStringLiteral("northMeters"), north - anchorNorthMeters},
            {QStringLiteral("altitudeMeters"), altitude},
            {QStringLiteral("speedMetersPerSecond"), speed},
            {QStringLiteral("yawDegrees"), yaw},
            {QStringLiteral("gimbalYawDegrees"), std::clamp(gimbalYaw, -180.0, 180.0)},
            {QStringLiteral("gimbalPitchDegrees"), std::clamp(gimbalPitch, -135.0, 45.0)},
            {QStringLiteral("capturePhoto"),
             point.value(QStringLiteral("capturePhoto"), false).toBool()},
        });
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString savedName = uniqueName(normalizedName, id);
    QVariantMap route{
        {QStringLiteral("format"), QStringLiteral("FlightBenchRoute")},
        {QStringLiteral("version"), RouteFormatVersion},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), savedName},
        {QStringLiteral("createdAt"), previous.value(
             QStringLiteral("createdAt"), now.toString(Qt::ISODate))},
        {QStringLiteral("updatedAt"), now.toString(Qt::ISODate)},
        {QStringLiteral("endAction"), endAction},
        {QStringLiteral("waypoints"), relativeWaypoints},
    };
    QString errorMessage;
    if (!writeRoute(route, &errorMessage)) return result(false, errorMessage);
    refresh();
    return result(true, QStringLiteral("航线已保存"),
                  {{QStringLiteral("id"), id},
                   {QStringLiteral("name"), savedName}});
}

QVariantMap RouteLibrary::loadRoute(const QString &routeId,
                                    double targetEastMeters,
                                    double targetNorthMeters) const
{
    const QVariantMap route = readRoute(routeId);
    if (route.isEmpty()) return result(false, QStringLiteral("航线文件不存在或已损坏"));
    QVariantList positionedWaypoints;
    const QVariantList relativeWaypoints = route.value(QStringLiteral("waypoints")).toList();
    for (const QVariant &entry : relativeWaypoints) {
        QVariantMap point = entry.toMap();
        point.insert(QStringLiteral("eastMeters"),
                     point.value(QStringLiteral("eastMeters")).toDouble() + targetEastMeters);
        point.insert(QStringLiteral("northMeters"),
                     point.value(QStringLiteral("northMeters")).toDouble() + targetNorthMeters);
        positionedWaypoints.append(point);
    }
    return result(true, QStringLiteral("航线已加载"),
                  {{QStringLiteral("id"), routeId},
                   {QStringLiteral("name"), route.value(QStringLiteral("name"))},
                   {QStringLiteral("endAction"), route.value(QStringLiteral("endAction"))},
                   {QStringLiteral("waypoints"), positionedWaypoints}});
}

QVariantMap RouteLibrary::duplicateRoute(const QString &routeId)
{
    QVariantMap route = readRoute(routeId);
    if (route.isEmpty()) return result(false, QStringLiteral("找不到要复制的航线"));
    const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QDateTime now = QDateTime::currentDateTime();
    const QString name = uniqueName(route.value(QStringLiteral("name")).toString()
                                        + QStringLiteral(" 副本"));
    route.insert(QStringLiteral("id"), newId);
    route.insert(QStringLiteral("name"), name);
    route.insert(QStringLiteral("createdAt"), now.toString(Qt::ISODate));
    route.insert(QStringLiteral("updatedAt"), now.toString(Qt::ISODate));
    QString errorMessage;
    if (!writeRoute(route, &errorMessage)) return result(false, errorMessage);
    refresh();
    return result(true, QStringLiteral("航线副本已创建"),
                  {{QStringLiteral("id"), newId}, {QStringLiteral("name"), name}});
}

QVariantMap RouteLibrary::renameRoute(const QString &routeId, const QString &name)
{
    QVariantMap route = readRoute(routeId);
    if (route.isEmpty()) return result(false, QStringLiteral("找不到要重命名的航线"));
    const QString normalizedName = normalizedRouteName(name);
    if (normalizedName.isEmpty()) return result(false, QStringLiteral("请输入航线名称"));
    const QString savedName = uniqueName(normalizedName, routeId);
    route.insert(QStringLiteral("name"), savedName);
    route.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    QString errorMessage;
    if (!writeRoute(route, &errorMessage)) return result(false, errorMessage);
    refresh();
    return result(true, QStringLiteral("航线已重命名"),
                  {{QStringLiteral("id"), routeId},
                   {QStringLiteral("name"), savedName}});
}

QVariantMap RouteLibrary::deleteRoute(const QString &routeId)
{
    const QVariantMap route = readRoute(routeId);
    if (route.isEmpty()) return result(false, QStringLiteral("找不到要删除的航线"));
    const QString path = routePath(routeId);
    if (path.isEmpty() || !QFile::remove(path)) {
        return result(false, QStringLiteral("航线删除失败"));
    }
    refresh();
    return result(true, QStringLiteral("航线已删除"));
}

void RouteLibrary::refresh()
{
    QVariantList routes;
    const QDir directory(m_directory);
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.fbroute.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo &fileInfo : files) {
        const QString fileName = fileInfo.fileName();
        const QString suffix = QStringLiteral(".fbroute.json");
        const QString id = fileName.left(fileName.size() - suffix.size());
        const QVariantMap route = readRoute(id);
        if (route.isEmpty()) continue;
        const QVariantList waypoints = route.value(QStringLiteral("waypoints")).toList();
        double distance = 0.0;
        double lastEast = 0.0;
        double lastNorth = 0.0;
        double estimatedSeconds = 0.0;
        for (const QVariant &entry : waypoints) {
            const QVariantMap point = entry.toMap();
            const double east = point.value(QStringLiteral("eastMeters")).toDouble();
            const double north = point.value(QStringLiteral("northMeters")).toDouble();
            const double segment = std::hypot(east - lastEast, north - lastNorth);
            distance += segment;
            estimatedSeconds += segment
                                / std::max(1.0, point.value(
                                                   QStringLiteral("speedMetersPerSecond"), 3.0)
                                                   .toDouble());
            lastEast = east;
            lastNorth = north;
        }
        estimatedSeconds += waypoints.size() * 2.0;
        const QDateTime updated = QDateTime::fromString(
            route.value(QStringLiteral("updatedAt")).toString(), Qt::ISODate);
        routes.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), route.value(QStringLiteral("name"))},
            {QStringLiteral("waypointCount"), waypoints.size()},
            {QStringLiteral("distanceMeters"), qRound(distance)},
            {QStringLiteral("estimatedSeconds"), qRound(estimatedSeconds)},
            {QStringLiteral("endAction"), route.value(QStringLiteral("endAction"))},
            {QStringLiteral("updatedAt"), route.value(QStringLiteral("updatedAt"))},
            {QStringLiteral("updatedLabel"), updated.isValid()
                 ? updated.toString(QStringLiteral("MM-dd HH:mm")) : QString()},
        });
    }
    m_routes = routes;
    emit routesChanged();
}
