#include "infrastructure/simulationcatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace SimulationCatalog {
const QList<SimulationModelDefinition> &simulationModelDefinitions()
{
    static const QList<SimulationModelDefinition> definitions{
        {QStringLiteral("gz_x500"), QStringLiteral("x500"),
         QStringLiteral("标准四旋翼，适合基础飞行与功能验证"), true, true,
         QStringLiteral("none"), QStringLiteral("标准四旋翼")},
        {QStringLiteral("gz_x500_depth"), QStringLiteral("x500 Depth"),
         QStringLiteral("搭载 RGB 与深度相机，用于近距离感知实验"), false, true,
         QStringLiteral("depth-camera"), QStringLiteral("RGB + 深度相机")},
        {QStringLiteral("gz_x500_mono_cam"), QStringLiteral("x500 Mono Camera"),
         QStringLiteral("搭载单目相机，用于图像采集与视觉实验"), false, true,
         QStringLiteral("mono-camera"), QStringLiteral("单目相机")},
        {QStringLiteral("gz_x500_lidar_2d"), QStringLiteral("x500 LiDAR"),
         QStringLiteral("搭载二维激光雷达，可使用 ROS 2 实时查看扫描数据"), false, true,
         QStringLiteral("lidar-2d"), QStringLiteral("二维激光雷达")},
        {QStringLiteral("gz_x500_gimbal"), QStringLiteral("x500 Gimbal"),
         QStringLiteral("搭载云台相机与相机 IMU，用于载荷与视角实验"), false, true,
         QStringLiteral("gimbal-camera"), QStringLiteral("云台相机")},
    };
    return definitions;
}

const SimulationModelDefinition *findSimulationModel(const QString &id)
{
    const auto &definitions = simulationModelDefinitions();
    for (const auto &definition : definitions) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

const QList<SimulationWorldDefinition> &simulationWorldDefinitions()
{
    static const QList<SimulationWorldDefinition> definitions = [] {
        QList<SimulationWorldDefinition> result{
            {QStringLiteral("default"), QStringLiteral("默认场地"),
             QStringLiteral("开阔地面，适合基础飞行与功能验证"), {},
             QStringLiteral("simple")},
            // Retain a built-in fallback so old build trees and tests still
            // recognize the original city if catalog.json has not been copied.
            {QStringLiteral("xatu_weiyang"), QStringLiteral("西安工业大学未央校区"),
             QStringLiteral("校园白模，包含建筑、道路、绿地和水面"),
             QStringLiteral("xatu_weiyang/meshes"), QStringLiteral("campus")},
        };

        const QString catalogPath = QDir(QCoreApplication::applicationDirPath())
                                        .filePath(QStringLiteral("simulation/worlds/catalog.json"));
        QFile catalogFile(catalogPath);
        if (!catalogFile.open(QIODevice::ReadOnly)) return result;
        const QJsonDocument document = QJsonDocument::fromJson(catalogFile.readAll());
        if (!document.isArray()) return result;

        static const QRegularExpression validId(QStringLiteral("^[a-z0-9][a-z0-9_]{1,47}$"));
        for (const QJsonValue &value : document.array()) {
            const QJsonObject entry = value.toObject();
            const QString id = entry.value(QStringLiteral("id")).toString();
            const QString name = entry.value(QStringLiteral("name")).toString();
            const QString description = entry.value(QStringLiteral("description")).toString();
            const QString sceneType = entry.value(QStringLiteral("sceneType")).toString(
                QStringLiteral("other"));
            QString previewDirectory =
                entry.value(QStringLiteral("previewModelDirectory")).toString();
            if (!validId.match(id).hasMatch() || id == QStringLiteral("default")
                || name.trimmed().isEmpty()) {
                continue;
            }
            if (previewDirectory.isEmpty()) previewDirectory = id + QStringLiteral("/meshes");

            const SimulationWorldDefinition definition{id, name, description,
                                                        previewDirectory, sceneType};
            auto existing = std::find_if(result.begin(), result.end(), [&id](const auto &item) {
                return item.id == id;
            });
            if (existing == result.end()) result.append(definition);
            else *existing = definition;
        }
        return result;
    }();
    return definitions;
}

const SimulationWorldDefinition *findSimulationWorld(const QString &id)
{
    const auto &definitions = simulationWorldDefinitions();
    for (const auto &definition : definitions) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

} // namespace SimulationCatalog
