#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class RouteDeviationAnalyzer final
{
public:
    [[nodiscard]] static QVariantMap analyze(const QString &csvPath,
                                             const QVariantList &relativeWaypoints,
                                             double startEastMeters,
                                             double startUpMeters,
                                             double startSouthMeters,
                                             qint64 missionStartElapsedMs = -1,
                                             qint64 missionEndElapsedMs = -1);
};
