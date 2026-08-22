#include "routedeviationanalyzer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include <cstdio>
#include <cmath>

namespace {
bool closeTo(double actual, double expected, double tolerance = 0.02)
{
    return std::abs(actual - expected) <= tolerance;
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
    QTemporaryFile csv(QDir::current().filePath(
        QStringLiteral("route-analysis-fixture-XXXXXX.csv")));
    if (!csv.open()) {
        const QByteArray error = csv.errorString().toLocal8Bit();
        std::fprintf(stderr, "Telemetry fixture creation failed: %s\n", error.constData());
        return 2;
    }
    const QString csvPath = csv.fileName();
    csv.write("elapsed_ms,local_position_available,scene_x_east_m,scene_y_up_m,scene_z_south_m\n");
    csv.write("0,1,0,0,20\n");
    csv.write("1000,1,0,0,0\n");
    csv.write("1500,1,2,1,1\n");
    csv.write("2000,1,4,2,1\n");
    csv.write("2500,1,6,3,3\n");
    csv.write("3000,1,8,4,1\n");
    csv.write("3500,1,10,5,0\n");
    csv.write("4000,1,20,5,20\n");
    csv.close();

    const QVariantList route{waypoint(10.0, 0.0, 5.0)};
    const QVariantMap exact = RouteDeviationAnalyzer::analyze(csvPath, route,
                                                               0.0, 0.0, 0.0, 1000, 3500);
    if (!exact.value(QStringLiteral("available")).toBool()) return 3;
    if (exact.value(QStringLiteral("estimated")).toBool()) return 4;
    if (exact.value(QStringLiteral("sampleCount")).toInt() != 6) return 5;
    if (!closeTo(exact.value(QStringLiteral("meanHorizontalDeviationMeters")).toDouble(), 1.0)) return 6;
    if (!closeTo(exact.value(QStringLiteral("p95HorizontalDeviationMeters")).toDouble(), 3.0)) return 7;
    if (!closeTo(exact.value(QStringLiteral("maxHorizontalDeviationMeters")).toDouble(), 3.0)) return 8;
    if (!closeTo(exact.value(QStringLiteral("withinTolerancePercent")).toDouble(), 80.0)) return 9;
    if (exact.value(QStringLiteral("worstTimeMs")).toLongLong() != 2500) return 10;
    if (!closeTo(exact.value(QStringLiteral("plannedDistanceMeters")).toDouble(),
                 std::sqrt(125.0))) return 11;

    const QVariantMap estimated = RouteDeviationAnalyzer::analyze(csvPath, route,
                                                                   0.0, 0.0, 0.0);
    if (!estimated.value(QStringLiteral("available")).toBool()) return 12;
    if (!estimated.value(QStringLiteral("estimated")).toBool()) return 13;
    if (RouteDeviationAnalyzer::analyze(csvPath, {}, 0.0, 0.0, 0.0)
            .value(QStringLiteral("available")).toBool()) return 14;
    return 0;
}
