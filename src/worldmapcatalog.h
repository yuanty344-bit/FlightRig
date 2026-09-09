#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

class WorldMapCatalog final
{
public:
    // Scene coordinates: east, up, south. Only the registered building geometry
    // is tested; this is not a sensor-derived or dynamic obstacle test.
    [[nodiscard]] static QVariantMap raycastBuilding(const QString &worldId,
        const QVector3D &origin, const QVector3D &direction, double maximumDistance = 5000.0);
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
