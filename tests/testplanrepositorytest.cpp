#include "testplanrepository.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <array>

namespace {
bool require(bool condition, const QString &message)
{
    if (condition) return true;
    qCritical().noquote() << message;
    return false;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FlightBenchTests"));
    QCoreApplication::setApplicationName(QStringLiteral("TestPlanRepository"));
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir testDirectory;
    if (!require(testDirectory.isValid(), QStringLiteral("无法创建测试目录"))) return 1;
    TestPlanRepository repository(nullptr, testDirectory.path());
    const QVariantList initialPlans = repository.plans();
    if (!require(initialPlans.isEmpty(), QStringLiteral("空目录不应自动创建测试模板"))) return 1;

    const QVariantMap created = repository.savePlan(
        {}, QStringLiteral("自动化测试方案"), QStringLiteral("验证方案持久化"),
        QStringLiteral("mission"), QStringLiteral("gz_x500"), QStringLiteral("default"),
        6.5, QStringLiteral("12345678-1234-1234-1234-123456789012"),
        QStringLiteral("land"), QStringLiteral("custom"),
        {{QStringLiteral("controlSettings"),
          QVariantMap{{QStringLiteral("horizontalSpeedMetersPerSecond"), 4.8},
                      {QStringLiteral("verticalSpeedMetersPerSecond"), 2.8},
                      {QStringLiteral("yawRateDegreesPerSecond"), 45.0}}},
         {QStringLiteral("acceptanceRules"),
          QVariantMap{{QStringLiteral("maxHorizontalDeviationMeters"), 2.5},
                      {QStringLiteral("minimumObstacleClearanceMeters"), 6.0},
                      {QStringLiteral("maximumTelemetryGapSeconds"), 0.6},
                      {QStringLiteral("maximumAltitudeMeters"), 80.0},
                      {QStringLiteral("maximumDurationSeconds"), 600}}},
         {QStringLiteral("sensorSettings"),
          QVariantMap{{QStringLiteral("usage"), QStringLiteral("record")},
                      {QStringLiteral("performance"), QStringLiteral("auto")},
                      {QStringLiteral("acceptanceRequired"), true},
                      {QStringLiteral("minimumFrameRateHz"), 6.0},
                      {QStringLiteral("maximumDataGapSeconds"), 2.5},
                      {QStringLiteral("minimumObstacleDistanceMeters"), 1.5},
                      {QStringLiteral("requireCameraFrame"), true},
                      {QStringLiteral("requireSlamMap"), true}}}});
    if (!require(created.value(QStringLiteral("success")).toBool(),
                 created.value(QStringLiteral("message")).toString())) return 1;
    const QString createdId = created.value(QStringLiteral("id")).toString();
    if (!require(!repository.plan(createdId).isEmpty(),
                 QStringLiteral("新建方案未出现在仓库中"))) return 1;
    const QVariantMap createdPlan = repository.plan(createdId);
    const QVariantMap createdSensorSettings = createdPlan.value(
        QStringLiteral("sensorSettings")).toMap();
    if (!require(createdPlan.value(QStringLiteral("version")).toInt() == 5
                     && createdPlan.value(QStringLiteral("acceptancePreset")).toString()
                            == QStringLiteral("custom")
                     && createdPlan.value(QStringLiteral("acceptanceRules")).toMap()
                            .value(QStringLiteral("maximumAltitudeMeters")).toDouble() == 80.0
                     && createdPlan.value(QStringLiteral("controlSettings")).toMap()
                            .value(QStringLiteral("yawRateDegreesPerSecond")).toDouble() == 45.0
                     && createdPlan.value(QStringLiteral("controlSettings")).toMap()
                            .value(QStringLiteral("horizontalSpeedMetersPerSecond")).toDouble() == 4.8
                     && createdPlan.value(QStringLiteral("controlSettings")).toMap()
                            .value(QStringLiteral("verticalSpeedMetersPerSecond")).toDouble() == 2.8
                     && createdSensorSettings.value(QStringLiteral("usage")).toString()
                            == QStringLiteral("record")
                     && createdSensorSettings.value(QStringLiteral("performance")).toString()
                            == QStringLiteral("auto")
                     && createdSensorSettings.value(QStringLiteral("acceptanceRequired")).toBool()
                     && !createdSensorSettings.value(QStringLiteral("requireSlamMap")).toBool(),
                 QStringLiteral("高级方案参数没有完整保存"))) return 1;

    const QVariantMap standardPlan = repository.savePlan(
        {}, QStringLiteral("标准预设不可被覆盖"), {}, QStringLiteral("free-flight"),
        QStringLiteral("gz_x500"), QStringLiteral("default"), 5.0, {},
        QStringLiteral("hold"), QStringLiteral("standard"),
        {{QStringLiteral("acceptanceRules"),
          QVariantMap{{QStringLiteral("maxHorizontalDeviationMeters"), 0.5},
                      {QStringLiteral("minimumObstacleClearanceMeters"), 50.0}}}});
    if (!require(standardPlan.value(QStringLiteral("success")).toBool(),
                 QStringLiteral("标准预设方案保存失败"))) return 1;
    const QString standardId = standardPlan.value(QStringLiteral("id")).toString();
    const QVariantMap standardRules = repository.plan(standardId)
                                          .value(QStringLiteral("acceptanceRules")).toMap();
    if (!require(standardRules.value(QStringLiteral("maxHorizontalDeviationMeters")).toDouble() == 8.0
                     && standardRules.value(QStringLiteral("minimumObstacleClearanceMeters")).toDouble() == 3.0,
                 QStringLiteral("标准预设被外部自定义数值覆盖"))) return 1;

    struct SensorProfileDefault {
        const char *mode;
        double minimumRateHz;
    };
    const std::array<SensorProfileDefault, 4> sensorProfiles{{
        {"eco", 3.0}, {"standard", 6.0}, {"smooth", 9.0}, {"auto", 5.0},
    }};
    for (const SensorProfileDefault &profile : sensorProfiles) {
        const QVariantMap saved = repository.savePlan(
            {}, QStringLiteral("传感器档位 %1").arg(QString::fromLatin1(profile.mode)), {},
            QStringLiteral("free-flight"), QStringLiteral("gz_x500_mono_cam"),
            QStringLiteral("default"), 5.0, {}, QStringLiteral("hold"),
            QStringLiteral("standard"),
            {{QStringLiteral("sensorSettings"),
              QVariantMap{{QStringLiteral("usage"), QStringLiteral("preview")},
                          {QStringLiteral("performance"), QString::fromLatin1(profile.mode)}}}});
        if (!require(saved.value(QStringLiteral("success")).toBool(),
                     QStringLiteral("无法保存传感器性能档位测试方案"))) return 1;
        const QString id = saved.value(QStringLiteral("id")).toString();
        const QVariantMap settings = repository.plan(id).value(
            QStringLiteral("sensorSettings")).toMap();
        if (!require(settings.value(QStringLiteral("minimumFrameRateHz")).toDouble()
                         == profile.minimumRateHz,
                     QStringLiteral("传感器性能档位的默认验收频率不正确"))) return 1;
        if (!require(repository.deletePlan(id).value(QStringLiteral("success")).toBool(),
                     QStringLiteral("无法清理传感器性能档位测试方案"))) return 1;
    }

    const QVariantMap duplicated = repository.duplicatePlan(createdId);
    if (!require(duplicated.value(QStringLiteral("success")).toBool(),
                 duplicated.value(QStringLiteral("message")).toString())) return 1;
    const QString duplicatedId = duplicated.value(QStringLiteral("id")).toString();

    const QVariantMap renamed = repository.renamePlan(
        duplicatedId, QStringLiteral("自动化测试方案副本"));
    if (!require(renamed.value(QStringLiteral("success")).toBool(),
                 renamed.value(QStringLiteral("message")).toString())) return 1;
    if (!require(repository.plan(duplicatedId).value(QStringLiteral("name")).toString()
                     == QStringLiteral("自动化测试方案副本"),
                 QStringLiteral("方案重命名没有保存"))) return 1;

    if (!require(repository.deletePlan(createdId).value(QStringLiteral("success")).toBool(),
                 QStringLiteral("原始测试方案删除失败"))) return 1;
    if (!require(repository.deletePlan(duplicatedId).value(QStringLiteral("success")).toBool(),
                 QStringLiteral("复制测试方案删除失败"))) return 1;
    if (!require(repository.deletePlan(standardId).value(QStringLiteral("success")).toBool(),
                 QStringLiteral("标准预设测试方案删除失败"))) return 1;

    const QString legacyId = QStringLiteral("legacy-city-plan");
    QFile legacyFile(testDirectory.filePath(legacyId + QStringLiteral(".fbplan.json")));
    const QVariantMap legacyPlan{
        {QStringLiteral("format"), QStringLiteral("FlightBenchTestPlan")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("id"), legacyId},
        {QStringLiteral("name"), QStringLiteral("旧城市方案")},
        {QStringLiteral("type"), QStringLiteral("city-inspection")},
        {QStringLiteral("modelId"), QStringLiteral("gz_x500")},
        {QStringLiteral("worldId"), QStringLiteral("default")},
        {QStringLiteral("takeoffAltitudeMeters"), 5.0},
        {QStringLiteral("routeId"), QStringLiteral("12345678-1234-1234-1234-123456789012")},
        {QStringLiteral("endAction"), QStringLiteral("land")},
        {QStringLiteral("acceptancePreset"), QStringLiteral("standard")},
        {QStringLiteral("sensorSettings"),
         QVariantMap{{QStringLiteral("usage"), QStringLiteral("record")},
                     {QStringLiteral("performance"), QStringLiteral("standard")},
                     {QStringLiteral("minimumFrameRateHz"), 7.0}}},
    };
    if (!require(legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 QStringLiteral("无法创建旧方案测试文件"))) return 1;
    const QByteArray legacyData = QJsonDocument(QJsonObject::fromVariantMap(legacyPlan)).toJson();
    if (!require(legacyFile.write(legacyData) == legacyData.size(),
                 QStringLiteral("旧方案测试文件写入失败"))) return 1;
    legacyFile.close();
    repository.refresh();
    const QVariantMap migrated = repository.plan(legacyId);
    if (!require(migrated.value(QStringLiteral("type")).toString() == QStringLiteral("mission")
                     && migrated.value(QStringLiteral("acceptancePreset")).toString()
                            == QStringLiteral("strict")
                     && migrated.value(QStringLiteral("requiresRoute")).toBool()
                     && migrated.value(QStringLiteral("sensorSettings")).toMap()
                            .value(QStringLiteral("minimumFrameRateHz")).toDouble() == 6.0,
                 QStringLiteral("旧城市巡检方案没有迁移为严格航点任务"))) return 1;
    if (!require(repository.deletePlan(legacyId).value(QStringLiteral("success")).toBool(),
                 QStringLiteral("迁移后的旧方案删除失败"))) return 1;
    if (!require(repository.plans().isEmpty(), QStringLiteral("删除后方案仓库应为空"))) return 1;

    return 0;
}
