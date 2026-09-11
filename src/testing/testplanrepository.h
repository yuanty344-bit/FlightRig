#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class TestPlanRepository final : public QObject
{
    Q_OBJECT

public:
    explicit TestPlanRepository(QObject *parent = nullptr,
                                const QString &directory = {});

    [[nodiscard]] QVariantList plans() const { return m_plans; }
    [[nodiscard]] QVariantMap plan(const QString &planId) const;
    [[nodiscard]] QString directory() const { return m_directory; }

    QVariantMap savePlan(const QString &planId,
                         const QString &name,
                         const QString &description,
                         const QString &type,
                         const QString &modelId,
                         const QString &worldId,
                         double takeoffAltitudeMeters,
                         const QString &routeId,
                         const QString &endAction,
                         const QString &acceptancePreset = {},
                         const QVariantMap &advancedSettings = {});
    QVariantMap duplicatePlan(const QString &planId);
    QVariantMap renamePlan(const QString &planId, const QString &name);
    QVariantMap deletePlan(const QString &planId);
    void refresh();

signals:
    void plansChanged();

private:
    [[nodiscard]] QString planPath(const QString &planId) const;
    [[nodiscard]] QVariantMap readUserPlan(const QString &planId) const;
    [[nodiscard]] QString uniqueName(const QString &baseName,
                                     const QString &excludedPlanId = {}) const;
    [[nodiscard]] static QVariantMap result(bool success,
                                            const QString &message,
                                            const QVariantMap &extra = {});
    bool writePlan(const QVariantMap &plan, QString *errorMessage = nullptr) const;

    QString m_directory;
    QVariantList m_plans;
};
