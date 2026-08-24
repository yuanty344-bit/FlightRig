#include "testplanrepository.h"
#include "storagepaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace {
constexpr int TestPlanFormatVersion = 2;

bool validPlanId(const QString &planId)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9-]{8,64}$"));
    return expression.match(planId).hasMatch();
}

bool validType(const QString &type)
{
    return type == QStringLiteral("free-flight")
           || type == QStringLiteral("mission");
}

bool validEndAction(const QString &endAction)
{
    return endAction == QStringLiteral("hold")
           || endAction == QStringLiteral("land")
           || endAction == QStringLiteral("rtl");
}

QString normalizedName(const QString &name)
{
    QString normalized = name.simplified();
    if (normalized.size() > 40) normalized.truncate(40);
    return normalized;
}

QString normalizedDescription(const QString &description)
{
    QString normalized = description.simplified();
    if (normalized.size() > 120) normalized.truncate(120);
    return normalized;
}

double boundedNumber(const QVariantMap &values, const QString &key,
                     double fallback, double minimum, double maximum,
                     int decimals = 1)
{
    bool ok = false;
    const double candidate = values.value(key, fallback).toDouble(&ok);
    const double bounded = std::clamp(ok && std::isfinite(candidate) ? candidate : fallback,
                                      minimum, maximum);
    const double scale = std::pow(10.0, decimals);
    return std::round(bounded * scale) / scale;
}

QVariantMap defaultAcceptanceRules(const QString &preset)
{
    const bool strict = preset == QStringLiteral("strict");
    return {{QStringLiteral("maxHorizontalDeviationMeters"), strict ? 3.0 : 8.0},
            {QStringLiteral("minimumObstacleClearanceMeters"), strict ? 5.0 : 3.0},
            {QStringLiteral("maximumTelemetryGapSeconds"), strict ? 0.8 : 1.0},
            {QStringLiteral("maximumAltitudeMeters"), 100.0},
            {QStringLiteral("maximumDurationSeconds"), strict ? 900 : 1200}};
}

QVariantMap normalizedAcceptanceRules(const QString &preset, const QVariantMap &values)
{
    const QVariantMap defaults = defaultAcceptanceRules(preset);
    return {
        {QStringLiteral("maxHorizontalDeviationMeters"),
         boundedNumber(values, QStringLiteral("maxHorizontalDeviationMeters"),
                       defaults.value(QStringLiteral("maxHorizontalDeviationMeters")).toDouble(),
                       0.5, 50.0)},
        {QStringLiteral("minimumObstacleClearanceMeters"),
         boundedNumber(values, QStringLiteral("minimumObstacleClearanceMeters"),
                       defaults.value(QStringLiteral("minimumObstacleClearanceMeters")).toDouble(),
                       0.5, 50.0)},
        {QStringLiteral("maximumTelemetryGapSeconds"),
         boundedNumber(values, QStringLiteral("maximumTelemetryGapSeconds"),
                       defaults.value(QStringLiteral("maximumTelemetryGapSeconds")).toDouble(),
                       0.1, 10.0)},
        {QStringLiteral("maximumAltitudeMeters"),
         boundedNumber(values, QStringLiteral("maximumAltitudeMeters"),
                       defaults.value(QStringLiteral("maximumAltitudeMeters")).toDouble(),
                       1.0, 100.0)},
        {QStringLiteral("maximumDurationSeconds"),
         static_cast<int>(boundedNumber(values, QStringLiteral("maximumDurationSeconds"),
                                        defaults.value(QStringLiteral("maximumDurationSeconds")).toDouble(),
                                        60.0, 7200.0, 0))},
    };
}

QVariantMap normalizedControlSettings(const QVariantMap &values)
{
    return {
        {QStringLiteral("horizontalSpeedMetersPerSecond"),
         boundedNumber(values, QStringLiteral("horizontalSpeedMetersPerSecond"), 1.0, 0.5, 2.0)},
        {QStringLiteral("verticalSpeedMetersPerSecond"),
         boundedNumber(values, QStringLiteral("verticalSpeedMetersPerSecond"), 0.5, 0.2, 1.5)},
        {QStringLiteral("yawRateDegreesPerSecond"),
         boundedNumber(values, QStringLiteral("yawRateDegreesPerSecond"), 30.0, 5.0, 90.0, 0)},
    };
}

QVariantMap normalizedStoredPlan(QVariantMap plan)
{
    QString type = plan.value(QStringLiteral("type"), QStringLiteral("free-flight")).toString();
    QString preset = plan.value(QStringLiteral("acceptancePreset"), QStringLiteral("standard")).toString();
    if (type == QStringLiteral("city-inspection")) {
        type = QStringLiteral("mission");
        preset = QStringLiteral("strict");
        plan.insert(QStringLiteral("migratedFromType"), QStringLiteral("city-inspection"));
    }
    if (!validType(type)) type = QStringLiteral("free-flight");
    if (preset != QStringLiteral("standard") && preset != QStringLiteral("strict")
        && preset != QStringLiteral("custom")) {
        preset = QStringLiteral("standard");
    }
    plan.insert(QStringLiteral("version"), TestPlanFormatVersion);
    plan.insert(QStringLiteral("type"), type);
    if (type == QStringLiteral("free-flight")) {
        plan.insert(QStringLiteral("routeId"), QString());
        plan.insert(QStringLiteral("endAction"), QStringLiteral("hold"));
    }
    plan.insert(QStringLiteral("acceptancePreset"), preset);
    const QVariantMap storedRules = preset == QStringLiteral("custom")
                                        ? plan.value(QStringLiteral("acceptanceRules")).toMap()
                                        : QVariantMap{};
    plan.insert(QStringLiteral("acceptanceRules"), normalizedAcceptanceRules(
                    preset, storedRules));
    plan.insert(QStringLiteral("controlSettings"), normalizedControlSettings(
                    plan.value(QStringLiteral("controlSettings")).toMap()));
    plan.insert(QStringLiteral("requiresRoute"), type == QStringLiteral("mission"));
    return plan;
}
}

TestPlanRepository::TestPlanRepository(QObject *parent, const QString &directory)
    : QObject(parent)
{
    if (directory.isEmpty()) {
        m_directory = StoragePaths::directory(QStringLiteral("Plans"));
    } else {
        m_directory = QDir::cleanPath(directory);
    }
    QDir().mkpath(m_directory);
    refresh();
}

QVariantMap TestPlanRepository::result(bool success,
                                       const QString &message,
                                       const QVariantMap &extra)
{
    QVariantMap response = extra;
    response.insert(QStringLiteral("success"), success);
    response.insert(QStringLiteral("message"), message);
    return response;
}

QString TestPlanRepository::planPath(const QString &planId) const
{
    if (!validPlanId(planId) || planId.startsWith(QStringLiteral("template-"))) return {};
    return QDir(m_directory).filePath(planId + QStringLiteral(".fbplan.json"));
}

QVariantMap TestPlanRepository::readUserPlan(const QString &planId) const
{
    const QString path = planPath(planId);
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return {};
    QVariantMap storedPlan = document.object().toVariantMap();
    const int version = storedPlan.value(QStringLiteral("version")).toInt();
    const QString storedType = storedPlan.value(QStringLiteral("type")).toString();
    if (storedPlan.value(QStringLiteral("format")).toString() != QStringLiteral("FlightBenchTestPlan")
        || (version != 1 && version != TestPlanFormatVersion)
        || storedPlan.value(QStringLiteral("id")).toString() != planId
        || (!validType(storedType) && storedType != QStringLiteral("city-inspection"))
        || !validEndAction(storedPlan.value(QStringLiteral("endAction")).toString())) {
        return {};
    }
    storedPlan = normalizedStoredPlan(storedPlan);
    storedPlan.insert(QStringLiteral("builtIn"), false);
    return storedPlan;
}

QVariantMap TestPlanRepository::plan(const QString &planId) const
{
    for (const QVariant &entry : m_plans) {
        const QVariantMap candidate = entry.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == planId) return candidate;
    }
    return {};
}

bool TestPlanRepository::writePlan(const QVariantMap &plan, QString *errorMessage) const
{
    const QString path = planPath(plan.value(QStringLiteral("id")).toString());
    if (path.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("测试方案编号无效");
        return false;
    }
    if (!QDir().mkpath(m_directory)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建测试方案目录");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法写入测试方案文件");
        return false;
    }
    const QByteArray contents = QJsonDocument(QJsonObject::fromVariantMap(plan))
                                    .toJson(QJsonDocument::Indented);
    if (file.write(contents) != contents.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("测试方案内容写入不完整");
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("测试方案保存失败");
        return false;
    }
    return true;
}

QString TestPlanRepository::uniqueName(const QString &baseName,
                                       const QString &excludedPlanId) const
{
    QString candidate = normalizedName(baseName);
    if (candidate.isEmpty()) candidate = QStringLiteral("未命名方案");
    const QString original = candidate;
    for (int suffix = 2;; ++suffix) {
        bool exists = false;
        for (const QVariant &entry : m_plans) {
            const QVariantMap existing = entry.toMap();
            if (existing.value(QStringLiteral("id")).toString() != excludedPlanId
                && existing.value(QStringLiteral("name")).toString().compare(
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

QVariantMap TestPlanRepository::savePlan(const QString &planId,
                                         const QString &name,
                                         const QString &description,
                                         const QString &type,
                                         const QString &modelId,
                                         const QString &worldId,
                                         double takeoffAltitudeMeters,
                                         const QString &routeId,
                                         const QString &endAction,
                                         const QString &acceptancePreset,
                                         const QVariantMap &advancedSettings)
{
    const QString cleanName = normalizedName(name);
    if (cleanName.isEmpty()) return result(false, QStringLiteral("请输入方案名称"));
    const bool legacyCityType = type == QStringLiteral("city-inspection");
    const QString normalizedType = legacyCityType ? QStringLiteral("mission") : type;
    const QString normalizedRouteId = normalizedType == QStringLiteral("mission")
                                          ? routeId : QString();
    const QString normalizedEndAction = normalizedType == QStringLiteral("mission")
                                            ? endAction : QStringLiteral("hold");
    if (!validType(normalizedType)) return result(false, QStringLiteral("执行方式无效"));
    if (modelId.trimmed().isEmpty() || worldId.trimmed().isEmpty()) {
        return result(false, QStringLiteral("请选择飞行器和仿真世界"));
    }
    if (!std::isfinite(takeoffAltitudeMeters)
        || takeoffAltitudeMeters < 1.0 || takeoffAltitudeMeters > 100.0) {
        return result(false, QStringLiteral("起飞高度必须在 1.0 至 100.0 米之间"));
    }
    if (!normalizedRouteId.isEmpty() && !validPlanId(normalizedRouteId)) {
        return result(false, QStringLiteral("航线编号无效"));
    }
    if (!validEndAction(normalizedEndAction)) return result(false, QStringLiteral("结束动作无效"));

    QString id = planId;
    QVariantMap previous;
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    } else {
        previous = readUserPlan(id);
        if (previous.isEmpty()) return result(false, QStringLiteral("要更新的测试方案不存在"));
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString preset = legacyCityType ? QStringLiteral("strict")
                           : acceptancePreset == QStringLiteral("strict")
                               ? QStringLiteral("strict")
                           : acceptancePreset == QStringLiteral("custom")
                               ? QStringLiteral("custom")
                               : QStringLiteral("standard");
    QVariantMap storedPlan{
        {QStringLiteral("format"), QStringLiteral("FlightBenchTestPlan")},
        {QStringLiteral("version"), TestPlanFormatVersion},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), uniqueName(cleanName, id)},
        {QStringLiteral("description"), normalizedDescription(description)},
        {QStringLiteral("type"), normalizedType},
        {QStringLiteral("modelId"), modelId},
        {QStringLiteral("worldId"), worldId},
        {QStringLiteral("takeoffAltitudeMeters"), std::round(takeoffAltitudeMeters * 10.0) / 10.0},
        {QStringLiteral("routeId"), normalizedRouteId},
        {QStringLiteral("endAction"), normalizedEndAction},
        {QStringLiteral("acceptancePreset"), preset},
        {QStringLiteral("acceptanceRules"), normalizedAcceptanceRules(
             preset, preset == QStringLiteral("custom")
                         ? advancedSettings.value(QStringLiteral("acceptanceRules")).toMap()
                         : QVariantMap{})},
        {QStringLiteral("controlSettings"), normalizedControlSettings(
             advancedSettings.value(QStringLiteral("controlSettings")).toMap())},
        {QStringLiteral("createdAt"), previous.value(
             QStringLiteral("createdAt"), now.toString(Qt::ISODate))},
        {QStringLiteral("updatedAt"), now.toString(Qt::ISODate)},
    };
    QString errorMessage;
    if (!writePlan(storedPlan, &errorMessage)) return result(false, errorMessage);
    refresh();
    return result(true, planId.isEmpty() ? QStringLiteral("测试方案已创建")
                                         : QStringLiteral("测试方案已更新"),
                  {{QStringLiteral("id"), id},
                   {QStringLiteral("name"), storedPlan.value(QStringLiteral("name"))}});
}

QVariantMap TestPlanRepository::duplicatePlan(const QString &planId)
{
    QVariantMap source = plan(planId);
    if (source.isEmpty()) return result(false, QStringLiteral("找不到要复制的测试方案"));
    return savePlan({}, source.value(QStringLiteral("name")).toString() + QStringLiteral(" 副本"),
                    source.value(QStringLiteral("description")).toString(),
                    source.value(QStringLiteral("type")).toString(),
                    source.value(QStringLiteral("modelId")).toString(),
                    source.value(QStringLiteral("worldId")).toString(),
                    source.value(QStringLiteral("takeoffAltitudeMeters"), 5.0).toDouble(),
                    source.value(QStringLiteral("routeId")).toString(),
                    source.value(QStringLiteral("endAction"), QStringLiteral("hold")).toString(),
                    source.value(QStringLiteral("acceptancePreset"), QStringLiteral("standard")).toString(),
                    {{QStringLiteral("acceptanceRules"), source.value(QStringLiteral("acceptanceRules"))},
                     {QStringLiteral("controlSettings"), source.value(QStringLiteral("controlSettings"))}});
}

QVariantMap TestPlanRepository::renamePlan(const QString &planId, const QString &name)
{
    QVariantMap storedPlan = readUserPlan(planId);
    if (storedPlan.isEmpty()) return result(false, QStringLiteral("找不到要重命名的测试方案"));
    const QString cleanName = normalizedName(name);
    if (cleanName.isEmpty()) return result(false, QStringLiteral("请输入方案名称"));
    const QString savedName = uniqueName(cleanName, planId);
    storedPlan.insert(QStringLiteral("name"), savedName);
    storedPlan.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    storedPlan.remove(QStringLiteral("builtIn"));
    storedPlan.remove(QStringLiteral("requiresRoute"));
    QString errorMessage;
    if (!writePlan(storedPlan, &errorMessage)) return result(false, errorMessage);
    refresh();
    return result(true, QStringLiteral("测试方案已重命名"),
                  {{QStringLiteral("id"), planId}, {QStringLiteral("name"), savedName}});
}

QVariantMap TestPlanRepository::deletePlan(const QString &planId)
{
    if (readUserPlan(planId).isEmpty()) return result(false, QStringLiteral("找不到要删除的测试方案"));
    const QString path = planPath(planId);
    if (path.isEmpty() || !QFile::remove(path)) return result(false, QStringLiteral("测试方案删除失败"));
    refresh();
    return result(true, QStringLiteral("测试方案已删除"));
}

void TestPlanRepository::refresh()
{
    QVariantList refreshedPlans;
    const QDir directory(m_directory);
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.fbplan.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo &fileInfo : files) {
        const QString suffix = QStringLiteral(".fbplan.json");
        const QString id = fileInfo.fileName().left(fileInfo.fileName().size() - suffix.size());
        const QVariantMap storedPlan = readUserPlan(id);
        if (!storedPlan.isEmpty()) refreshedPlans.append(storedPlan);
    }
    m_plans = refreshedPlans;
    emit plansChanged();
}
