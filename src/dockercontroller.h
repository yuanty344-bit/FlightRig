#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class DockerController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dockerAvailable READ dockerAvailable NOTIFY dockerAvailableChanged)
    Q_PROPERTY(bool imageAvailable READ imageAvailable NOTIFY imageAvailableChanged)
    Q_PROPERTY(bool simulationRunning READ simulationRunning NOTIFY simulationRunningChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString problemCode READ problemCode NOTIFY problemCodeChanged)

public:
    explicit DockerController(QObject *parent = nullptr);
    ~DockerController() override;

    [[nodiscard]] bool dockerAvailable() const { return m_dockerAvailable; }
    [[nodiscard]] bool imageAvailable() const { return m_imageAvailable; }
    [[nodiscard]] bool simulationRunning() const { return m_simulationRunning; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString statusMessage() const { return m_statusMessage; }
    [[nodiscard]] QString problemCode() const { return m_problemCode; }

    Q_INVOKABLE void probe();
    void startSimulation(const QString &modelId);
    Q_INVOKABLE void stopSimulation();

signals:
    void dockerAvailableChanged();
    void imageAvailableChanged();
    void simulationRunningChanged();
    void busyChanged();
    void statusMessageChanged();
    void problemCodeChanged();

private:
    enum class Operation { None, Probe, CheckImage, Start, Stop };

    void run(Operation operation, const QStringList &arguments);
    void inspectContainer();
    void setDockerAvailable(bool value);
    void setImageAvailable(bool value);
    void setSimulationRunning(bool value);
    void setBusy(bool value);
    void setStatusMessage(const QString &message);
    void setProblemCode(const QString &code);
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleError(QProcess::ProcessError error);
    void handleInspectFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleInspectError(QProcess::ProcessError error);
    void shutdownSynchronously();

    static constexpr auto ContainerName = "flightbench-px4";
    static constexpr auto ImageName = "px4io/px4-sitl-gazebo:latest";

    QProcess m_process;
    QProcess m_inspectProcess;
    QTimer m_monitorTimer;
    QString m_dockerPath;
    Operation m_operation{Operation::None};
    quint64 m_stateGeneration{0};
    quint64 m_activeInspectGeneration{0};
    bool m_dockerAvailable{false};
    bool m_imageAvailable{false};
    bool m_simulationRunning{false};
    bool m_busy{false};
    bool m_startedByUs{false};
    QString m_statusMessage{QStringLiteral("正在检查 Docker Desktop…")};
    QString m_problemCode;
};
