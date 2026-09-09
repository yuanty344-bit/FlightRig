#include "dockercontroller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QRegularExpression>
#include <QSaveFile>
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
    // QProcess may emit finished while waitForFinished() runs or from its own
    // destructor. At that point another QProcess member can already be gone,
    // so normal operation handlers must not run during controller teardown.
    m_monitorTimer.stop();
    QObject::disconnect(&m_process, nullptr, this, nullptr);
    QObject::disconnect(&m_inspectProcess, nullptr, this, nullptr);
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
        setNetworkAvailable(false);
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
                                       const QString &containerName,
                                       const QString &sensorPerformanceMode)
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
    if (!m_networkAvailable) {
        setProblemCode(QStringLiteral("network-missing"));
        setStatusMessage(QStringLiteral("FlightBench 容器网络尚未就绪，请重新检测"));
        return;
    }
    if (!m_containerNameAvailable) {
        setProblemCode(QStringLiteral("container-conflict"));
        setStatusMessage(QStringLiteral("容器 %1 已存在但未运行，请删除或更换名称")
                             .arg(containerName));
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
        QStringLiteral("--network"), QString::fromLatin1(NetworkName),
        QStringLiteral("-e"), QStringLiteral("HEADLESS=1"),
        QStringLiteral("-e"), QStringLiteral("GZ_PARTITION=flightbench"),
        QStringLiteral("-e"), QStringLiteral("PX4_SIM_MODEL=%1").arg(modelId),
        QStringLiteral("-e"), QStringLiteral("PX4_GZ_WORLD=%1").arg(worldId),
    };

    const QDir assets(worldAssetsDirectory);
    if (modelId == QStringLiteral("gz_x500_lidar_2d")
        && !QFileInfo::exists(assets.filePath(
            QStringLiteral("models/x500_lidar_2d/model.sdf")))) {
        setProblemCode(QStringLiteral("model-assets-missing"));
        setStatusMessage(QStringLiteral("找不到 x500 LiDAR 360° 模型资源，请重新构建软件"));
        return;
    }
    if (modelId == QStringLiteral("gz_x500_mono_cam")
        && !QFileInfo::exists(assets.filePath(
            QStringLiteral("models/mono_cam/model.sdf")))) {
        setProblemCode(QStringLiteral("model-assets-missing"));
        setStatusMessage(QStringLiteral("找不到优化后的 Mono Camera 模型资源，请重新构建软件"));
        return;
    }
    if (modelId == QStringLiteral("gz_x500_depth")
        && !QFileInfo::exists(assets.filePath(
            QStringLiteral("models/OakD-Lite/model.sdf")))) {
        setProblemCode(QStringLiteral("model-assets-missing"));
        setStatusMessage(QStringLiteral("找不到优化后的 Depth Camera 模型资源，请重新构建软件"));
        return;
    }
    if (modelId == QStringLiteral("gz_x500_gimbal")
        && !QFileInfo::exists(assets.filePath(
            QStringLiteral("models/gimbal/model.sdf")))) {
        setProblemCode(QStringLiteral("model-assets-missing"));
        setStatusMessage(QStringLiteral("找不到优化后的 Gimbal 模型资源，请重新构建软件"));
        return;
    }
    if (worldId != QStringLiteral("default")) {
        const QString worldFile = assets.filePath(
            QStringLiteral("worlds/%1.sdf").arg(worldId));
        if (!assets.exists() || !QFileInfo::exists(worldFile)) {
            setProblemCode(QStringLiteral("world-assets-missing"));
            setStatusMessage(QStringLiteral("找不到 %1 世界资源，请重新构建软件")
                                 .arg(worldId));
            return;
        }
    }

    // Mount FlightBench model overrides for every world. The LiDAR override
    // expands the scanner to 360 degrees, while the camera overrides keep
    // sensor rendering aligned with the 10 FPS preview consumed by the app.
    // The nested stock gimbal is also resolved from this path so its original
    // 30 FPS camera and 250 Hz camera IMU do not starve PX4 after take-off.
    if (assets.exists()) {
        const QString mountSource = QDir::toNativeSeparators(assets.absolutePath());
        const QString sensorProfile = prepareSensorModelProfile(
            modelId, assets.absolutePath(), sensorPerformanceMode);
        arguments.append({
            QStringLiteral("--mount"),
            QStringLiteral("type=bind,source=%1,target=/opt/flightbench-simulation,readonly")
                .arg(mountSource),
            QStringLiteral("-e"),
            QStringLiteral("GZ_SIM_RESOURCE_PATH=%1/opt/flightbench-simulation/models:"
                           "/opt/px4-gazebo/share/gz/models:"
                           "/opt/px4-gazebo/share/gz/worlds")
                .arg(sensorProfile.isEmpty()
                         ? QString()
                         : QStringLiteral("/opt/flightbench-sensor-profile/models:")),
        });
        if (!sensorProfile.isEmpty()) {
            arguments.append({
                QStringLiteral("--mount"),
                QStringLiteral("type=bind,source=%1,target=/opt/flightbench-sensor-profile,readonly")
                    .arg(QDir::toNativeSeparators(sensorProfile)),
            });
        }
        // PX4_GZ_WORLDS replaces the image's built-in world directory. Only
        // point it at FlightBench assets for custom worlds; the default world
        // must continue to resolve from the image.
        if (worldId != QStringLiteral("default")) {
            arguments.append({
                QStringLiteral("-e"),
                QStringLiteral("PX4_GZ_WORLDS=/opt/flightbench-simulation/worlds"),
            });
        }
        if (modelId == QStringLiteral("gz_x500_lidar_2d")) {
            // The PX4 entrypoint resolves the selected top-level model through
            // PX4_GZ_MODELS before Gazebo processes nested model:// includes.
            // Point only the LiDAR variant at our 360-degree wrapper; its x500
            // base and visual mesh still resolve from the stock resource path.
            arguments.append({
                QStringLiteral("-e"),
                QStringLiteral("PX4_GZ_MODELS=/opt/flightbench-simulation/models"),
            });
        }
    }
    arguments.append(QString::fromLatin1(ImageName));

    m_containerName = containerName;
    setStatusMessage(QStringLiteral("正在启动 %1 · %2…").arg(modelId, worldId));
    ++m_stateGeneration;
    run(Operation::Start, arguments);
}

QString DockerController::prepareSensorModelProfile(const QString &modelId,
                                                    const QString &assetsDirectory,
                                                    const QString &performanceMode)
{
    QString payloadModel;
    if (modelId == QStringLiteral("gz_x500_lidar_2d")) payloadModel = QStringLiteral("lidar_2d_360");
    else if (modelId == QStringLiteral("gz_x500_mono_cam")) payloadModel = QStringLiteral("mono_cam");
    else if (modelId == QStringLiteral("gz_x500_depth")) payloadModel = QStringLiteral("OakD-Lite");
    else if (modelId == QStringLiteral("gz_x500_gimbal")) payloadModel = QStringLiteral("gimbal");
    else return {};

    const QString normalizedMode = performanceMode == QStringLiteral("eco")
                                   ? QStringLiteral("eco")
                                   : (performanceMode == QStringLiteral("smooth")
                                      || performanceMode == QStringLiteral("auto"))
                                     ? QStringLiteral("smooth")
                                     : QStringLiteral("standard");
    const int updateRate = normalizedMode == QStringLiteral("eco") ? 5
                           : normalizedMode == QStringLiteral("smooth") ? 15 : 10;
    const QString sourcePath = QDir(assetsDirectory).filePath(
        QStringLiteral("models/%1").arg(payloadModel));
    if (!QFileInfo(sourcePath).isDir()) return {};
    const QString profileRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                    .filePath(QStringLiteral("FlightBench/sensor-profile"));
    const QString targetPath = QDir(profileRoot).filePath(
        QStringLiteral("models/%1").arg(payloadModel));
    QDir targetDirectory(targetPath);
    if (targetDirectory.exists() && !targetDirectory.removeRecursively()) return {};
    if (!QDir().mkpath(targetPath)) return {};

    QDirIterator iterator(sourcePath, QDir::Files | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourceFile = iterator.next();
        const QString relative = QDir(sourcePath).relativeFilePath(sourceFile);
        const QString targetFile = QDir(targetPath).filePath(relative);
        if (!QDir().mkpath(QFileInfo(targetFile).absolutePath())
            || !QFile::copy(sourceFile, targetFile)) {
            return {};
        }
    }

    const QString modelFilePath = QDir(targetPath).filePath(QStringLiteral("model.sdf"));
    QFile modelFile(modelFilePath);
    if (!modelFile.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString contents = QString::fromUtf8(modelFile.readAll());
    modelFile.close();
    const QRegularExpression rateExpression(
        QStringLiteral("<update_rate>\\s*[0-9.]+\\s*</update_rate>"));
    const QString replacement = QStringLiteral("<update_rate>%1</update_rate>").arg(updateRate);
    if (payloadModel == QStringLiteral("gimbal")) {
        QRegularExpressionMatch lastMatch;
        auto matches = rateExpression.globalMatch(contents);
        while (matches.hasNext()) lastMatch = matches.next();
        if (!lastMatch.hasMatch()) return {};
        contents.replace(lastMatch.capturedStart(), lastMatch.capturedLength(), replacement);
    } else {
        contents.replace(rateExpression, replacement);
    }
    QSaveFile output(modelFilePath);
    const QByteArray encoded = contents.toUtf8();
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || output.write(encoded) != encoded.size() || !output.commit()) {
        return {};
    }
    return profileRoot;
}

void DockerController::setContainerName(const QString &containerName)
{
    if (containerName == m_containerName) return;
    m_containerName = containerName;
    ++m_stateGeneration;
    setContainerNameAvailable(true);
    if (m_dockerAvailable && !m_busy) inspectContainer();
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
            setNetworkAvailable(false);
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
        if (!success) {
            setNetworkAvailable(false);
            setProblemCode(QStringLiteral("image-missing"));
            setStatusMessage(QStringLiteral("未找到 px4io/px4-sitl-gazebo:latest 镜像"));
            inspectContainer();
            break;
        }
        setProblemCode({});
        setStatusMessage(QStringLiteral("PX4 镜像已就绪，正在检查容器网络…"));
        run(Operation::CheckNetwork,
            {QStringLiteral("network"), QStringLiteral("inspect"),
             QString::fromLatin1(NetworkName), QStringLiteral("--format"),
             QStringLiteral("{{.Id}}")});
        break;
    case Operation::CheckNetwork:
        if (success) {
            setNetworkAvailable(true);
            setProblemCode({});
            setStatusMessage(QStringLiteral("Docker Desktop、PX4 镜像与容器网络均已就绪"));
            inspectContainer();
        } else {
            setNetworkAvailable(false);
            setStatusMessage(QStringLiteral("正在创建 FlightBench 专用容器网络…"));
            run(Operation::CreateNetwork,
                {QStringLiteral("network"), QStringLiteral("create"),
                 QString::fromLatin1(NetworkName)});
        }
        break;
    case Operation::CreateNetwork:
        setNetworkAvailable(success);
        if (success) {
            setProblemCode({});
            setStatusMessage(QStringLiteral("FlightBench 运行环境已就绪"));
        } else {
            setProblemCode(QStringLiteral("network-failed"));
            setStatusMessage(QStringLiteral("无法创建 FlightBench 容器网络：%1")
                                 .arg(standardError.isEmpty()
                                          ? QStringLiteral("未知错误") : standardError));
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
        setNetworkAvailable(false);
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
        QTimer::singleShot(0, this, &DockerController::inspectContainer);
        return;
    }

    const bool containerExists = exitStatus == QProcess::NormalExit && exitCode == 0;
    const bool wasRunning = m_simulationRunning;
    const bool isRunning = containerExists
                           && standardOutput.compare(QStringLiteral("true"),
                                                     Qt::CaseInsensitive) == 0;
    setContainerNameAvailable(!containerExists || isRunning);
    setSimulationRunning(isRunning);
    if (isRunning && !wasRunning) {
        setProblemCode({});
        setStatusMessage(QStringLiteral("检测到正在运行的 FlightBench 仿真容器"));
    } else if (!isRunning && wasRunning) {
        m_startedByUs = false;
        setStatusMessage(QStringLiteral("检测到仿真容器已经停止"));
    } else if (containerExists && !isRunning) {
        setProblemCode(QStringLiteral("container-conflict"));
        setStatusMessage(QStringLiteral("容器 %1 已存在但未运行，请删除或更换名称")
                             .arg(m_containerName));
    } else if (m_problemCode == QStringLiteral("container-conflict")) {
        setProblemCode({});
        setStatusMessage(QStringLiteral("容器名称可用"));
    }
}

void DockerController::handleInspectError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        setDockerAvailable(false);
        setImageAvailable(false);
        setNetworkAvailable(false);
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

void DockerController::setNetworkAvailable(bool value)
{
    if (m_networkAvailable == value) return;
    m_networkAvailable = value;
    emit networkAvailableChanged();
}

void DockerController::setContainerNameAvailable(bool value)
{
    if (m_containerNameAvailable == value) return;
    m_containerNameAvailable = value;
    emit containerNameAvailableChanged();
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
