#include "mission/routedeviationanalyzer.h"

#include <QFile>
#include <QHash>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double WithinToleranceMeters = 2.0;
constexpr double FinalArrivalRadiusMeters = 2.5;

struct PositionSample
{
    qint64 elapsedMs{0};
    QVector3D position;
};

struct ClosestPoint
{
    double horizontalDistance{0.0};
    double altitudeDistance{0.0};
    int segmentIndex{0};
    double projection{0.0};
};

QStringList splitCsvLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool quoted = false;
    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (quoted) {
            if (character == QLatin1Char('"')) {
                if (index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"')) {
                    current.append(QLatin1Char('"'));
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                current.append(character);
            }
        } else if (character == QLatin1Char('"')) {
            quoted = true;
        } else if (character == QLatin1Char(',')) {
            fields.append(current);
            current.clear();
        } else {
            current.append(character);
        }
    }
    fields.append(current);
    return fields;
}

bool parseFinite(const QStringList &fields, int index, double *value)
{
    if (index < 0 || index >= fields.size()) return false;
    bool ok = false;
    const double parsed = fields.at(index).toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

double horizontalDistance(const QVector3D &left, const QVector3D &right)
{
    return std::hypot(static_cast<double>(left.x() - right.x()),
                      static_cast<double>(left.z() - right.z()));
}

ClosestPoint closestPointOnRoute(const QVector3D &position,
                                 const QVector<QVector3D> &route)
{
    ClosestPoint best;
    best.horizontalDistance = std::numeric_limits<double>::infinity();
    for (qsizetype index = 0; index + 1 < route.size(); ++index) {
        const QVector3D &start = route.at(index);
        const QVector3D &end = route.at(index + 1);
        const double dx = static_cast<double>(end.x() - start.x());
        const double dz = static_cast<double>(end.z() - start.z());
        const double lengthSquared = dx * dx + dz * dz;
        const double projection = lengthSquared < 1.0e-8
                                      ? 0.0
                                      : std::clamp(((position.x() - start.x()) * dx
                                                    + (position.z() - start.z()) * dz)
                                                       / lengthSquared,
                                                   0.0, 1.0);
        const double plannedX = start.x() + dx * projection;
        const double plannedZ = start.z() + dz * projection;
        const double distance = std::hypot(position.x() - plannedX,
                                           position.z() - plannedZ);
        if (distance >= best.horizontalDistance) continue;
        const double plannedY = start.y() + (end.y() - start.y()) * projection;
        best.horizontalDistance = distance;
        best.altitudeDistance = std::abs(position.y() - plannedY);
        best.segmentIndex = static_cast<int>(index);
        best.projection = projection;
    }
    return best;
}

QVector<PositionSample> loadSamples(const QString &csvPath)
{
    QVector<PositionSample> samples;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return samples;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd()) return samples;

    const QStringList header = splitCsvLine(stream.readLine());
    QHash<QString, int> columns;
    for (int index = 0; index < header.size(); ++index)
        columns.insert(header.at(index).trimmed(), index);
    const int elapsedColumn = columns.value(QStringLiteral("elapsed_ms"), -1);
    const int availableColumn = columns.value(QStringLiteral("local_position_available"), -1);
    const int eastColumn = columns.value(QStringLiteral("scene_x_east_m"), -1);
    const int upColumn = columns.value(QStringLiteral("scene_y_up_m"), -1);
    const int southColumn = columns.value(QStringLiteral("scene_z_south_m"), -1);
    if (elapsedColumn < 0 || availableColumn < 0 || eastColumn < 0
        || upColumn < 0 || southColumn < 0) return samples;

    while (!stream.atEnd()) {
        const QStringList fields = splitCsvLine(stream.readLine());
        if (availableColumn >= fields.size()
            || fields.at(availableColumn).trimmed() != QLatin1String("1")) continue;
        bool elapsedOk = false;
        const qint64 elapsedMs = elapsedColumn < fields.size()
                                     ? fields.at(elapsedColumn).toLongLong(&elapsedOk) : -1;
        double east = 0.0, up = 0.0, south = 0.0;
        if (!elapsedOk || elapsedMs < 0 || !parseFinite(fields, eastColumn, &east)
            || !parseFinite(fields, upColumn, &up) || !parseFinite(fields, southColumn, &south)) {
            continue;
        }
        PositionSample sample{elapsedMs, QVector3D(static_cast<float>(east),
                                                   static_cast<float>(up),
                                                   static_cast<float>(south))};
        if (!samples.isEmpty() && sample.elapsedMs < samples.constLast().elapsedMs) continue;
        if (!samples.isEmpty() && sample.elapsedMs == samples.constLast().elapsedMs)
            samples.last() = sample;
        else
            samples.append(sample);
    }
    return samples;
}
}

QVariantMap RouteDeviationAnalyzer::analyze(const QString &csvPath,
                                            const QVariantList &relativeWaypoints,
                                            double startEastMeters,
                                            double startUpMeters,
                                            double startSouthMeters,
                                            qint64 missionStartElapsedMs,
                                            qint64 missionEndElapsedMs)
{
    QVariantMap result{{QStringLiteral("available"), false}};
    if (relativeWaypoints.isEmpty()) return result;

    QVector<QVector3D> route;
    route.reserve(relativeWaypoints.size() + 1);
    route.append(QVector3D(static_cast<float>(startEastMeters),
                           static_cast<float>(startUpMeters),
                           static_cast<float>(startSouthMeters)));
    for (const QVariant &value : relativeWaypoints) {
        const QVariantMap waypoint = value.toMap();
        route.append(QVector3D(
            static_cast<float>(startEastMeters
                               + waypoint.value(QStringLiteral("eastMeters")).toDouble()),
            static_cast<float>(waypoint.value(QStringLiteral("altitudeMeters"),
                                               startUpMeters).toDouble()),
            static_cast<float>(startSouthMeters
                               - waypoint.value(QStringLiteral("northMeters")).toDouble())));
    }
    if (route.size() < 2) return result;

    const QVector<PositionSample> allSamples = loadSamples(csvPath);
    if (allSamples.size() < 2) return result;

    qsizetype firstIndex = 0;
    const bool estimated = missionStartElapsedMs < 0;
    if (!estimated) {
        const auto first = std::lower_bound(allSamples.cbegin(), allSamples.cend(),
                                            missionStartElapsedMs,
                                            [](const PositionSample &sample, qint64 timeMs) {
                                                return sample.elapsedMs < timeMs;
                                            });
        firstIndex = std::distance(allSamples.cbegin(), first);
    } else {
        double nearestStartDistance = std::numeric_limits<double>::infinity();
        for (qsizetype index = 0; index < allSamples.size(); ++index) {
            const double distance = horizontalDistance(allSamples.at(index).position,
                                                       route.constFirst());
            if (distance < nearestStartDistance) {
                nearestStartDistance = distance;
                firstIndex = index;
            }
        }
    }
    if (firstIndex + 1 >= allSamples.size()) return result;

    qsizetype lastIndex = allSamples.size() - 1;
    if (missionEndElapsedMs >= allSamples.at(firstIndex).elapsedMs) {
        const auto end = std::upper_bound(allSamples.cbegin() + firstIndex,
                                          allSamples.cend(), missionEndElapsedMs,
                                          [](qint64 timeMs, const PositionSample &sample) {
                                              return timeMs < sample.elapsedMs;
                                          });
        if (end != allSamples.cbegin() + firstIndex)
            lastIndex = std::distance(allSamples.cbegin(), end) - 1;
    } else {
        // Old sessions do not have an exact mission-end timestamp. Advance
        // through segments in order so a loop whose final point is near the
        // start cannot be mistaken for an immediately completed mission.
        int activeSegment = 0;
        const int finalSegment = route.size() - 2;
        for (qsizetype index = firstIndex + 1; index < allSamples.size(); ++index) {
            const QVector<QVector3D> segment{route.at(activeSegment),
                                             route.at(activeSegment + 1)};
            const ClosestPoint closest = closestPointOnRoute(allSamples.at(index).position,
                                                               segment);
            const double distanceToTarget = horizontalDistance(
                allSamples.at(index).position, route.at(activeSegment + 1));
            if (activeSegment < finalSegment
                && (distanceToTarget <= FinalArrivalRadiusMeters
                    || (closest.projection >= 0.98
                        && closest.horizontalDistance <= 5.0))) {
                ++activeSegment;
            }
            if (activeSegment == finalSegment
                && horizontalDistance(allSamples.at(index).position, route.constLast())
                       <= FinalArrivalRadiusMeters) {
                lastIndex = index;
                break;
            }
        }
    }
    if (lastIndex <= firstIndex) return result;

    QVector<double> horizontalDeviations;
    horizontalDeviations.reserve(lastIndex - firstIndex + 1);
    double horizontalSum = 0.0;
    double altitudeSum = 0.0;
    double maximumHorizontal = 0.0;
    double maximumAltitude = 0.0;
    double actualDistance = 0.0;
    qint64 analyzedDurationMs = 0;
    qint64 withinToleranceDurationMs = 0;
    qint64 worstTimeMs = allSamples.at(firstIndex).elapsedMs;
    QVector3D worstPosition = allSamples.at(firstIndex).position;

    for (qsizetype index = firstIndex; index <= lastIndex; ++index) {
        const PositionSample &sample = allSamples.at(index);
        const ClosestPoint closest = closestPointOnRoute(sample.position, route);
        horizontalDeviations.append(closest.horizontalDistance);
        horizontalSum += closest.horizontalDistance;
        altitudeSum += closest.altitudeDistance;
        maximumAltitude = std::max(maximumAltitude, closest.altitudeDistance);
        if (index < lastIndex) {
            const qint64 intervalMs = std::max<qint64>(
                0, allSamples.at(index + 1).elapsedMs - sample.elapsedMs);
            analyzedDurationMs += intervalMs;
            if (closest.horizontalDistance <= WithinToleranceMeters)
                withinToleranceDurationMs += intervalMs;
        }
        if (closest.horizontalDistance > maximumHorizontal) {
            maximumHorizontal = closest.horizontalDistance;
            worstTimeMs = sample.elapsedMs;
            worstPosition = sample.position;
        }
        if (index > firstIndex)
            actualDistance += sample.position.distanceToPoint(allSamples.at(index - 1).position);
    }

    double plannedDistance = 0.0;
    for (qsizetype index = 1; index < route.size(); ++index)
        plannedDistance += route.at(index).distanceToPoint(route.at(index - 1));
    std::sort(horizontalDeviations.begin(), horizontalDeviations.end());
    const qsizetype p95Index = std::clamp<qsizetype>(
        static_cast<qsizetype>(std::ceil(horizontalDeviations.size() * 0.95)) - 1,
        0, horizontalDeviations.size() - 1);
    const double count = static_cast<double>(horizontalDeviations.size());

    result = {
        {QStringLiteral("available"), true},
        {QStringLiteral("estimated"), estimated},
        {QStringLiteral("sampleCount"), horizontalDeviations.size()},
        {QStringLiteral("startTimeMs"), allSamples.at(firstIndex).elapsedMs},
        {QStringLiteral("endTimeMs"), allSamples.at(lastIndex).elapsedMs},
        {QStringLiteral("meanHorizontalDeviationMeters"), horizontalSum / count},
        {QStringLiteral("p95HorizontalDeviationMeters"), horizontalDeviations.at(p95Index)},
        {QStringLiteral("maxHorizontalDeviationMeters"), maximumHorizontal},
        {QStringLiteral("meanAltitudeDeviationMeters"), altitudeSum / count},
        {QStringLiteral("maxAltitudeDeviationMeters"), maximumAltitude},
        {QStringLiteral("withinToleranceMeters"), WithinToleranceMeters},
        {QStringLiteral("analyzedDurationMs"), analyzedDurationMs},
        {QStringLiteral("withinTolerancePercent"),
         analyzedDurationMs > 0
             ? 100.0 * static_cast<double>(withinToleranceDurationMs)
                   / static_cast<double>(analyzedDurationMs)
             : (horizontalDeviations.constFirst() <= WithinToleranceMeters ? 100.0 : 0.0)},
        {QStringLiteral("plannedDistanceMeters"), plannedDistance},
        {QStringLiteral("actualDistanceMeters"), actualDistance},
        {QStringLiteral("worstTimeMs"), worstTimeMs},
        {QStringLiteral("worstEastMeters"), worstPosition.x()},
        {QStringLiteral("worstUpMeters"), worstPosition.y()},
        {QStringLiteral("worstSouthMeters"), worstPosition.z()},
    };
    return result;
}
