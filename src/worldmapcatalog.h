#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class WorldMapCatalog final
{
public:
    [[nodiscard]] static QVariantMap metadata(const QString &worldId);
    [[nodiscard]] static QVariantMap analyzeRoute(const QString &worldId,
                                                  const QVariantList &waypoints,
                                                  double startEastMeters,
                                                  double startNorthMeters,
                                                  double startAltitudeMeters);
    [[nodiscard]] static QVariantMap analyzePosition(const QString &worldId,
                                                     double eastMeters,
                                                     double northMeters,
                                                     double altitudeMeters);
};
