#pragma once

#include <QList>
#include <QString>

namespace SimulationCatalog {
struct SimulationModelDefinition
{
    QString id;
    QString name;
    QString description;
    bool recommended;
    bool visible;
    QString sensorType;
    QString sensorLabel;
};

struct SimulationWorldDefinition
{
    QString id;
    QString name;
    QString description;
    QString previewModelDirectory;
    QString sceneType;
};

const QList<SimulationModelDefinition> &simulationModelDefinitions();
const SimulationModelDefinition *findSimulationModel(const QString &id);
const QList<SimulationWorldDefinition> &simulationWorldDefinitions();
const SimulationWorldDefinition *findSimulationWorld(const QString &id);
} // namespace SimulationCatalog
