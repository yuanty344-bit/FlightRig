#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class RouteLibrary final : public QObject
{
    Q_OBJECT

public:
    explicit RouteLibrary(QObject *parent = nullptr);

    [[nodiscard]] QVariantList routes() const { return m_routes; }
    [[nodiscard]] QString directory() const { return m_directory; }

    QVariantMap saveRoute(const QString &routeId,
                          const QString &name,
                          const QVariantList &waypoints,
                          const QString &endAction,
                          double anchorEastMeters,
                          double anchorNorthMeters);
    [[nodiscard]] QVariantMap loadRoute(const QString &routeId,
                                        double targetEastMeters,
                                        double targetNorthMeters) const;
    QVariantMap duplicateRoute(const QString &routeId);
    QVariantMap renameRoute(const QString &routeId, const QString &name);
    QVariantMap deleteRoute(const QString &routeId);
    void refresh();

signals:
    void routesChanged();

private:
    [[nodiscard]] QString routePath(const QString &routeId) const;
    [[nodiscard]] QVariantMap readRoute(const QString &routeId) const;
    [[nodiscard]] QString uniqueName(const QString &baseName,
                                     const QString &excludedRouteId = {}) const;
    [[nodiscard]] static QVariantMap result(bool success,
                                            const QString &message,
                                            const QVariantMap &extra = {});
    bool writeRoute(const QVariantMap &route, QString *errorMessage = nullptr) const;

    QString m_directory;
    QVariantList m_routes;
};
