#include "dockercontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

DockerController::DockerController(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::finished, this, &DockerController::handleFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &DockerController::handleError);
    connect(&m_inspectProcess, &QProcess::finished,
            this, &DockerController::handleInspectFinished);
    connect(&m_inspectProcess, &QProcess::errorOccurred,
            this, &DockerController::handleInspectError);
    m_monitorTimer.setInterval(3000);
    connect(&m_monitorTimer, &QTimer::timeout, this, [this] {
        if (m_dockerAvailable && m_simulationRunning && !m_busy) {
            inspectContainer();
        }
    });
    m_monitorTimer.start();
    QTimer::singleShot(0, this, &DockerController::probe);
}

DockerController::~DockerController()
{
    shutdownSynchronously();
}

void DockerController::probe()
{
    if (m_busy) {
        return;
    }

    m_dockerPath = QStandardPaths::findExecutable(QStringLiteral("docker"));
    if (m_dockerPath.isEmpty()) {
        setDockerAvailable(false);
        setImageAvailable(false);
        setProblemCode(QStringLiteral("docker-missing"));
        setStatusMessage(QStringLiteral("未找到 docker.exe，请启动或安装 Docker Desktop"));
        return;
    }

    setStatusMessage(QStringLiteral("正在连接 Docker Desktop…"));
    run(Operation::Probe,
        {QStringLiteral("version"), QStringLiteral("--format"), QStringLiteral("{{.Server.Version}}")});
}

void DockerController::startSimulation(const QString &modelId,
                                       const QString &worldId,
                                       const QString &worldAssetsDirectory,
                                       const QString &containerName)
{
    if (m_busy || m_simulationRunning) {
        return;
    }
    if (!m_dockerAvailable) {
        probe();
        return;
    }
    if (!m_imageAvailable) {
        setProblemCode(QStringLiteral("image-missing"));
        setStatusMessage(QStringLiteral("未找到 PX4 仿真镜像，请检查镜像后重新检测"));
        return;
    }

    setStatusMessage(QStringLiteral("正在启动 PX4 + Gazebo 仿真容器…"));
    static const QRegularExpression validModelId(QStringLiteral("^[A-Za-z0-9_-]+$"));
    static const QRegularExpression validWorldId(QStringLiteral("^[A-Za-z0-9_-]+$"));
    static const QRegularExpression validContainerName(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,62}$"));
    if (!validModelId.match(modelId).hasMatch()) {
        setProblemCode(QStringLiteral("model-invalid"));
        setStatusMessage(QStringLiteral("选择的仿真机型无效"));
        return;
    }
    if (!validWorldId.match(worldId).hasMatch()) {
        setProblemCode(QStringLiteral("world-invalid"));
        setStatusMessage(QStringLiteral("选择的仿真世界无效"));
        return;
    }
    if (!validContainerName.match(containerName).hasMatch()) {
        setProblemCode(QStringLiteral("container-name-invalid"));
        setStatusMessage(QStringLiteral("临时容器名称无效"));
        return;
    }

    QStringList arguments{
        QStringLiteral("run"), QStringLiteral("--rm"), QStringLiteral("-d"),
        QStringLiteral("--name"), containerName,
        QStringLiteral("-e"), QStringLiteral("HEADLESS=1"),
        QStringLiteral("-e"), QStringLiteral("PX4_SIM_MODEL=%1").arg(modelId),
        QStringLiteral("-e"), QStringLiteral("PX4_GZ_WORLD=%1").arg(worldId),
    };

    if (worldId != QStringLiteral("default")) {
        const QDir assets(worldAssetsDirectory);
        const QString worldFile = assets.filePath(
            QStringLiteral("worlds/%1.sdf").arg(worldId));
        if (!assets.exists() || !QFileInfo::exists(worldFile)) {
            setProblemCode(QStringLiteral("world-assets-missing"));
            setStatusMessage(QStringLiteral("找不到 %1 世界资源，请重新构建软件")
                                 .arg(worldId));
            return;
        }
        const QString mountSource = QDir::toNativeSeparators(assets.absolutePath());
        arguments.append({
            QStringLiteral("--mount"),
            QStringLiteral("type=bind,source=%1,target=/opt/flightbench-simulation,readonly")
                .arg(mountSource),
            QStringLiteral("-e"),
            QStringLiteral("PX4_GZ_WORLDS=/opt/flightbench-simulation/worlds"),
            QStringLiteral("-e"),
            QStringLiteral("GZ_SIM_RESOURCE_PATH=/opt/flightbench-simulation/models:"
                           "/opt/px4-gazebo/share/gz/models:"
                           "/opt/px4-gazebo/share/gz/worlds"),
        });
    }
    arguments.append(QString::fromLatin1(ImageName));

    m_containerName = containerName;
    setStatusMessage(QStringLiteral("正在启动 %1 · %2…").arg(modelId, worldId));
    ++m_stateGeneration;
    run(Operation::Start, arguments);
}

void DockerController::stopSimulation()
{
    if (m_busy || !m_simulationRunning) {
        return;
    }

    setStatusMessage(QStringLiteral("正在安全停止仿真容器…"));
    ++m_stateGeneration;
    run(Operation::Stop,
         {QStringLiteral("stop"), QStringLiteral("-t"), QStringLiteral("10"),
         m_containerName});
}

void DockerController::run(Operation operation, const QStringList &arguments)
{
    m_operation = operation;
    setBusy(true);
    m_process.setProgram(m_dockerPath);
    m_process.setArguments(arguments);
    m_process.start();
}

void DockerController::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const Operation completedOperation = m_operation;
    m_operation = Operation::None;
    setBusy(false);

    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
    const QString standardOutput = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
    const QString standardError = QString::fromUtf8(m_process.readAllStandardError()).trimmed();

    switch (completedOperation) {
    case Operation::Probe:
        setDockerAvailable(success);
        if (!success) {
            setImageAvailable(false);
            setProblemCode(QStringLiteral("docker-offline"));
            setStatusMessage(QStringLiteral("Docker Desktop 尚未就绪：%1")
                                 .arg(standardError.isEmpty() ? QStringLiteral("无法连接引擎") : standardError));
            return;
        }
        setProblemCode({});
        setStatusMessage(QStringLiteral("Docker Desktop 已连接，正在检查 PX4 镜像…"));
        run(Operation::CheckImage,
            {QStringLiteral("image"), QStringLiteral("inspect"),
             QString::fromLatin1(ImageName), QStringLiteral("--format"),
             QStringLiteral("{{.Id}}")});
        break;
    case Operation::CheckImage:
        setImageAvailable(success);
        if (success) {
            setProblemCode({});
            setStatusMessage(QStringLiteral("Docker Desktop 与 PX4 仿真镜像均已就绪"));
        } else {
            setProblemCode(QStringLiteral("image-missing"));
            setStatusMessage(QStringLiteral("未找到 px4io/px4-sitl-gazebo:latest 镜像"));
        }
        inspectContainer();
        break;
    case Operation::Start:
        if (success) {
            setProblemCode({});
            m_startedByUs = true;
            setSimulationRunning(true);
            setStatusMessage(QStringLiteral("PX4 + Gazebo 已启动，正在等待飞行器心跳…"));
        } else {
            const QString lowerError = standardError.toLower();
            if (lowerError.contains(QStringLiteral("conflict"))
                || lowerError.contains(QStringLiteral("already in use"))) {
                setProblemCode(QStringLiteral("container-conflict"));
                setStatusMessage(QStringLiteral("容器 %1 已存在，请在 Docker Desktop 中删除后重试")
                                     .arg(m_containerName));
            } else if (lowerError.contains(QStringLiteral("no such image"))) {
                setImageAvailable(false);
                setProblemCode(QStringLiteral("image-missing"));
                setStatusMessage(QStringLiteral("PX4 仿真镜像不存在，请重新检查本机镜像"));
            } else {
                setProblemCode(QStringLiteral("start-failed"));
                setStatusMessage(QStringLiteral("仿真启动失败：%1")
                                     .arg(standardError.isEmpty() ? QStringLiteral("未知错误") : standardError));
            }
        }
        break;
    case Operation::Stop:
        if (success) {
            setProblemCode({});
            m_startedByUs = false;
            setSimulationRunning(false);
            setStatusMessage(QStringLiteral("仿真容器已停止"));
        } else {
            setProblemCode(QStringLiteral("stop-failed"));
            setStatusMessage(QStringLiteral("停止仿真失败：%1")
                                 .arg(standardError.isEmpty() ? QStringLiteral("未知错误") : standardError));
        }
        break;
    case Operation::None:
        break;
    }
}

void DockerController::handleError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        setBusy(false);
        m_operation = Operation::None;
        setDockerAvailable(false);
        setImageAvailable(false);
        setProblemCode(QStringLiteral("docker-offline"));
        setStatusMessage(QStringLiteral("无法启动 docker.exe：%1").arg(m_process.errorString()));
    }
}

void DockerController::inspectContainer()
{
    if (!m_dockerAvailable || m_dockerPath.isEmpty() || m_busy
        || m_inspectProcess.state() != QProcess::NotRunning) {
        return;
    }

    m_activeInspectGeneration = m_stateGeneration;
    m_inspectProcess.setProgram(m_dockerPath);
    m_inspectProcess.setArguments({QStringLiteral("inspect"), QStringLiteral("--format"),
                                   QStringLiteral("{{.State.Running}}"),
                                   m_containerName});
    m_inspectProcess.start();
}

void DockerController::handleInspectFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const QString standardOutput = QString::fromUtf8(m_inspectProcess.readAllStandardOutput()).trimmed();
    m_inspectProcess.readAllStandardError();

    // A start or stop command began after this snapshot was requested. Its result is
    // authoritative, so an older inspect result must never overwrite the new state.
    if (m_activeInspectGeneration != m_stateGeneration) {
        return;
    }

    const bool wasRunning = m_simulationRunning;
    const bool isRunning = exitStatus == QProcess::NormalExit && exitCode == 0
                           && standardOutput.compare(QStringLiteral("true"),
                                                     Qt::CaseInsensitive) == 0;
    setSimulationRunning(isRunning);
    if (isRunning && !wasRunning) {
        setStatusMessage(QStringLiteral("检测到正在运行的 FlightBench 仿真容器"));
    } else if (!isRunning && wasRunning) {
        m_startedByUs = false;
        setStatusMessage(QStringLiteral("检测到仿真容器已经停止"));
    }
}

void DockerController::handleInspectError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        setDockerAvailable(false);
        setImageAvailable(false);
        setProblemCode(QStringLiteral("docker-offline"));
        setStatusMessage(QStringLiteral("无法启动 docker.exe：%1")
                             .arg(m_inspectProcess.errorString()));
    }
}

void DockerController::shutdownSynchronously()
{
    m_monitorTimer.stop();
    if (m_inspectProcess.state() != QProcess::NotRunning
        && !m_inspectProcess.waitForFinished(1000)) {
        m_inspectProcess.kill();
        m_inspectProcess.waitForFinished(1000);
    }

    const bool startMayHaveCreatedContainer = m_operation == Operation::Start;
    if (m_process.state() != QProcess::NotRunning
        && !m_process.waitForFinished(5000)) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }

    if ((!m_startedByUs && !startMayHaveCreatedContainer) || m_dockerPath.isEmpty()) {
        return;
    }
    QProcess::execute(m_dockerPath,
                      {QStringLiteral("stop"), QStringLiteral("-t"), QStringLiteral("10"),
                       m_containerName});
    m_startedByUs = false;
}

void DockerController::setDockerAvailable(bool value)
{
    if (m_dockerAvailable == value) return;
    m_dockerAvailable = value;
    emit dockerAvailableChanged();
}

void DockerController::setImageAvailable(bool value)
{
    if (m_imageAvailable == value) return;
    m_imageAvailable = value;
    emit imageAvailableChanged();
}

void DockerController::setSimulationRunning(bool value)
{
    if (m_simulationRunning == value) return;
    m_simulationRunning = value;
    emit simulationRunningChanged();
}

void DockerController::setBusy(bool value)
{
    if (m_busy == value) return;
    m_busy = value;
    emit busyChanged();
}

void DockerController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void DockerController::setProblemCode(const QString &code)
{
    if (m_problemCode == code) return;
    m_problemCode = code;
    emit problemCodeChanged();
}
