#include "mission/routelibrary.h"

#include <QCoreApplication>
#include <QDateTime>

#include <cmath>

namespace {
QVariantMap waypoint(const QString &name, double east, double north,
                     double altitude, double speed)
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("eastMeters"), east},
            {QStringLiteral("northMeters"), north},
            {QStringLiteral("altitudeMeters"), altitude},
            {QStringLiteral("speedMetersPerSecond"), speed},
            {QStringLiteral("yawDegrees"), 45.0}};
}

bool closeEnough(double left, double right)
{
    return std::abs(left - right) < 0.001;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    RouteLibrary library;
    const QString testName = QStringLiteral("RouteLibraryTest-%1")
                                 .arg(QDateTime::currentMSecsSinceEpoch());
    const QVariantList points{
        waypoint(QStringLiteral("入口"), 110.0, -45.0, 6.0, 3.0),
        waypoint(QStringLiteral("巡检点"), 125.0, -30.0, 8.0, 4.0),
    };

    const QVariantMap saved = library.saveRoute({}, testName, points,
                                                QStringLiteral("rtl"), 100.0, -50.0);
    if (!saved.value(QStringLiteral("success")).toBool()) return 1;
    const QString id = saved.value(QStringLiteral("id")).toString();

    const QVariantMap loaded = library.loadRoute(id, 10.0, 20.0);
    if (!loaded.value(QStringLiteral("success")).toBool()) return 2;
    const QVariantList loadedPoints = loaded.value(QStringLiteral("waypoints")).toList();
    if (loadedPoints.size() != 2) return 3;
    const QVariantMap first = loadedPoints.constFirst().toMap();
    if (!closeEnough(first.value(QStringLiteral("eastMeters")).toDouble(), 20.0)
        || !closeEnough(first.value(QStringLiteral("northMeters")).toDouble(), 25.0)
        || first.value(QStringLiteral("name")).toString() != QStringLiteral("入口")) {
        return 4;
    }

    const QVariantMap duplicated = library.duplicateRoute(id);
    if (!duplicated.value(QStringLiteral("success")).toBool()) return 5;
    const QString duplicateId = duplicated.value(QStringLiteral("id")).toString();
    const QVariantMap renamed = library.renameRoute(duplicateId, testName + QStringLiteral("-renamed"));
    if (!renamed.value(QStringLiteral("success")).toBool()) return 6;

    const bool originalDeleted = library.deleteRoute(id).value(QStringLiteral("success")).toBool();
    const bool duplicateDeleted = library.deleteRoute(duplicateId).value(QStringLiteral("success")).toBool();
    return originalDeleted && duplicateDeleted ? 0 : 7;
}
