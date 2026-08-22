#include "worldmapcatalog.h"

#include <QCoreApplication>
#include <QDebug>

#include <cmath>

namespace {
bool expect(bool condition, const char *message)
{
    if (condition) return true;
    qCritical().noquote() << message;
    return false;
}

QVariantMap waypoint(double east, double north, double altitude)
{
    return {{QStringLiteral("eastMeters"), east},
            {QStringLiteral("northMeters"), north},
            {QStringLiteral("altitudeMeters"), altitude}};
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;

    const QVariantMap defaultMap = WorldMapCatalog::metadata(QStringLiteral("default"));
    passed &= expect(!defaultMap.value(QStringLiteral("available")).toBool(),
                     "The default world must remain a grid-only map");

    const QVariantMap campusMap = WorldMapCatalog::metadata(QStringLiteral("xatu_weiyang"));
    passed &= expect(campusMap.value(QStringLiteral("available")).toBool(),
                     "The campus map resource was not loaded");
    passed &= expect(campusMap.value(QStringLiteral("obstacleTriangleCount")).toInt() == 391,
                     "Unexpected campus roof triangle count");
    const double campusMinEast = campusMap.value(QStringLiteral("minEastMeters")).toDouble();
    const double campusMaxEast = campusMap.value(QStringLiteral("maxEastMeters")).toDouble();
    passed &= expect(campusMinEast < -350.0 && campusMaxEast > 350.0
                         && campusMaxEast - campusMinEast >= 750.0,
                     "Campus map east extent is incomplete");

    const QVariantMap mutasiMap = WorldMapCatalog::metadata(QStringLiteral("xian_mutasi"));
    passed &= expect(mutasiMap.value(QStringLiteral("available")).toBool(),
                     "The Xi'an Mutasi city map resource was not loaded");
    passed &= expect(mutasiMap.value(QStringLiteral("obstacleTriangleCount")).toInt() == 197,
                     "Unexpected Xi'an Mutasi roof triangle count");
    passed &= expect(mutasiMap.value(QStringLiteral("maxEastMeters")).toDouble()
                         - mutasiMap.value(QStringLiteral("minEastMeters")).toDouble() >= 1000.0,
                     "Xi'an Mutasi map extent is incomplete");

    const QVariantMap risky = WorldMapCatalog::analyzeRoute(
        QStringLiteral("xatu_weiyang"),
        {waypoint(-220.0, -220.0, 5.0)},
        -220.0, -220.0, 5.0);
    passed &= expect(risky.value(QStringLiteral("available")).toBool(),
                     "Campus safety analysis is unavailable");
    passed &= expect(!risky.value(QStringLiteral("safe")).toBool(),
                     "A low waypoint inside a 12 m building was not rejected");
    passed &= expect(risky.value(QStringLiteral("requiredAltitudeMeters")).toDouble() >= 15.0,
                     "Building safety margin was not applied");

    const QVariantMap clear = WorldMapCatalog::analyzeRoute(
        QStringLiteral("xatu_weiyang"),
        {waypoint(10.0, 10.0, 60.0)},
        0.0, 0.0, 60.0);
    passed &= expect(clear.value(QStringLiteral("safe")).toBool(),
                     "A high route across the campus should be clear");

    return passed ? 0 : 1;
}
