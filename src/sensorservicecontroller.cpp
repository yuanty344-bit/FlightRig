#include "sensorservicecontroller.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QHostAddress>
#include <QDateTime>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace {
QString gazeboModelInstance(const QString &modelId)
{
    QString instance = modelId;
    if (instance.startsWith(QStringLiteral("gz_"))) instance.remove(0, 3);
    return instance + QStringLiteral("_0");
}

QString sensorKindForModel(const QString &modelId)
{
    if (modelId == QStringLiteral("gz_x500_lidar_2d")) return QStringLiteral("lidar-2d");
    if (modelId == QStringLiteral("gz_x500_mono_cam")) return QStringLiteral("mono-camera");
    if (modelId == QStringLiteral("gz_x500_depth")) return QStringLiteral("depth-camera");
    if (modelId == QStringLiteral("gz_x500_gimbal")) return QStringLiteral("gimbal-camera");
    return {};
}

void writeFourCc(QDataStream &stream, const char value[5])
{
    stream.writeRawData(value, 4);
}

double nearestDepthInSafetyRegion(const QByteArray &millimetres, int width, int height)
{
    if (width <= 0 || height <= 0
        || millimetres.size() < static_cast<qsizetype>(width) * height * 2) return 0.0;

    // The lower edge of a forward camera sees the ground during take-off and
    // landing, and the extreme border can include the aircraft body. Neither
    // is a forward obstacle. Use the central safety region for the clearance
    // verdict while keeping the complete depth image and pointer measurements.
    const int left = std::clamp(static_cast<int>(std::round(width * 0.05)), 0, width - 1);
    const int right = std::clamp(static_cast<int>(std::round(width * 0.95)), left + 1, width);
    const int top = std::clamp(static_cast<int>(std::round(height * 0.05)), 0, height - 1);
    const int bottom = std::clamp(static_cast<int>(std::round(height * 0.75)), top + 1, height);
    quint16 nearestMillimetres = std::numeric_limits<quint16>::max();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const qsizetype offset = (static_cast<qsizetype>(y) * width + x) * 2;
            const auto low = static_cast<quint8>(millimetres.at(offset));
            const auto high = static_cast<quint8>(millimetres.at(offset + 1));
            const quint16 value = static_cast<quint16>(low | (high << 8));
            if (value > 0 && value < nearestMillimetres) nearestMillimetres = value;
        }
    }
    return nearestMillimetres == std::numeric_limits<quint16>::max()
        ? 0.0 : nearestMillimetres / 1000.0;
}
}

SensorServiceController::SensorServiceController(QObject *parent)
    : QObject(parent)
{
    m_gatewayClock.start();
    m_clockSyncTimer.setInterval(1000);
    connect(&m_clockSyncTimer, &QTimer::timeout, this,
            &SensorServiceController::requestClockSync);
    connect(&m_process, &QProcess::finished,
            this, &SensorServiceController::handleFinished);
    connect(&m_process, &QProcess::errorOccurred,
            this, &SensorServiceController::handleError);
    connect(&m_gateway, &QTcpSocket::readyRead,
            this, &SensorServiceController::readGatewayData);
    connect(&m_gateway, &QTcpSocket::connected, this, [this] {
        m_clockSync.reset();
        m_gatewayBuffer.clear();
        m_clockRequestSentMs = -1;
        m_clockSyncTimer.start();
        requestClockSync();
        setConnected(true);
        setProblemCode({});
        setStatusMessage(QStringLiteral("ROS 2 数据网关已连接，正在等待传感器数据…"));
        applyPerformanceMode(m_performanceMode, true);
        if (m_sensorKind == QStringLiteral("gimbal-camera")) {
            QTimer::singleShot(150, this, [this] {
                setGimbalAngles(m_gimbalTargetYawDegrees,
                                m_gimbalTargetPitchDegrees,
                                m_gimbalTargetRollDegrees);
            });
        }
    });
    connect(&m_gateway, &QTcpSocket::disconnected, this, [this] {
        m_clockSyncTimer.stop();
        m_clockSync.reset();
        m_rgbLatencyMilliseconds = m_depthLatencyMilliseconds = -1;
        emit cameraFrameChanged();
        emit depthFrameChanged();
        setConnected(false);
        if (m_running) {
            setStatusMessage(QStringLiteral("正在重新连接 ROS 2 数据服务…"));
            m_reconnectTimer.start();
        }
    });
    connect(&m_gateway, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::RemoteHostClosedError || !m_running) return;
        setConnected(false);
        setProblemCode(QStringLiteral("gateway-unavailable"));
        setStatusMessage(QStringLiteral("ROS 2 容器已启动，正在等待传感器数据…"));
        m_reconnectTimer.start();
    });

    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(1000);
    connect(&m_reconnectTimer, &QTimer::timeout,
            this, &SensorServiceController::connectGateway);

    m_monitorTimer.setInterval(3000);
    connect(&m_monitorTimer, &QTimer::timeout, this, [this] {
        if (!m_running || m_busy || m_dockerPath.isEmpty()) return;
        bool streamUnstable = !m_connected;
        if (m_connected && m_sensorKind == QStringLiteral("lidar-2d")
            && m_lastScanTimer.isValid() && m_lastScanTimer.elapsed() > 2000) {
            resetScan();
            streamUnstable = true;
            setStatusMessage(QStringLiteral("雷达扫描暂时中断，正在等待新数据…"));
        }
        const bool cameraKind = m_connected && m_sensorKind != QStringLiteral("lidar-2d");
        const bool nextRgbInterrupted = cameraKind && m_lastRgbFrameTimer.isValid()
                                        && m_lastRgbFrameTimer.elapsed() > 2000;
        const bool nextDepthInterrupted = cameraKind
                                          && m_sensorKind == QStringLiteral("depth-camera")
                                          && m_lastDepthFrameTimer.isValid()
                                          && m_lastDepthFrameTimer.elapsed() > 2000;
        if (nextRgbInterrupted != m_rgbStreamInterrupted
            || nextDepthInterrupted != m_depthStreamInterrupted) {
            if ((!m_rgbStreamInterrupted && nextRgbInterrupted)
                || (!m_depthStreamInterrupted && nextDepthInterrupted))
                ++m_sessionDropoutCount;
            m_rgbStreamInterrupted = nextRgbInterrupted;
            m_depthStreamInterrupted = nextDepthInterrupted;
            m_cameraStreamInterrupted = nextRgbInterrupted || nextDepthInterrupted;
            emit cameraStateChanged();
            if (m_cameraStreamInterrupted)
                setStatusMessage(QStringLiteral("相机数据流暂时中断，正在等待新数据…"));
        }
        streamUnstable = streamUnstable || nextRgbInterrupted || nextDepthInterrupted;
        if (cameraKind && std::max(m_rgbLatencyMilliseconds,
                                   m_depthLatencyMilliseconds) > 600) streamUnstable = true;
        updateAutomaticPerformanceMode(streamUnstable);
        run(Operation::Inspect,
            {QStringLiteral("inspect"), QStringLiteral("--format"),
             QStringLiteral("{{.State.Running}}"), QString::fromLatin1(ContainerName)});
    });
    QTimer::singleShot(0, this, &SensorServiceController::probe);
}

SensorServiceController::~SensorServiceController()
{
    m_clockSyncTimer.stop();
    m_monitorTimer.stop();
    m_reconnectTimer.stop();
    QObject::disconnect(&m_process, nullptr, this, nullptr);
    QObject::disconnect(&m_gateway, nullptr, this, nullptr);
    if (m_recording) finalizeRecording();
    if (m_sessionTracking) finishSessionRecording();
    shutdownSynchronously();
}

#ifdef FLIGHTBENCH_TESTING
void SensorServiceController::setSensorKindForTesting(const QString &kind)
{
    m_sensorKind = kind;
}

void SensorServiceController::injectGatewayMessageForTesting(const QJsonObject &message)
{
    processGatewayMessage(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void SensorServiceController::setMaximumRecordedBytesForTesting(qint64 bytes)
{
    m_sessionMaximumRecordedBytes = std::max<qint64>(1, bytes);
}

qint64 SensorServiceController::synchronizeClockForTesting(qint64 remoteOffsetMs)
{
    const qint64 now = m_gatewayClock.elapsed();
    m_clockSync.reset();
    m_clockSync.observe(now, now + remoteOffsetMs, now + remoteOffsetMs, now);
    return now + remoteOffsetMs;
}
#endif

void SensorServiceController::probe()
{
    if (m_busy || m_running) return;
    m_dockerPath = QStandardPaths::findExecutable(QStringLiteral("docker"));
    if (m_dockerPath.isEmpty()) {
        setImageAvailable(false);
        setProblemCode(QStringLiteral("docker-missing"));
        setStatusMessage(QStringLiteral("启动 Docker Desktop 后才能使用 ROS 2 数据服务"));
        return;
    }
    setStatusMessage(QStringLiteral("正在检查 ROS 2 数据服务镜像…"));
    run(Operation::ProbeImage,
        {QStringLiteral("image"), QStringLiteral("inspect"),
         QString::fromLatin1(ImageName), QStringLiteral("--format"),
         QStringLiteral("{{.Id}}")});
}

int SensorServiceController::targetFramesPerSecond() const
{
    if (m_effectivePerformanceMode == QStringLiteral("eco")) return 5;
    if (m_effectivePerformanceMode == QStringLiteral("smooth")) return 15;
    return 10;
}

void SensorServiceController::applyPerformanceMode(const QString &mode, bool notifyGateway)
{
    QString normalized = mode;
    if (normalized != QStringLiteral("eco") && normalized != QStringLiteral("standard")
        && normalized != QStringLiteral("smooth") && normalized != QStringLiteral("auto")) {
        normalized = QStringLiteral("standard");
    }
    QString effective = normalized;
    if (normalized == QStringLiteral("auto")) {
        const bool continuingAuto = m_performanceMode == QStringLiteral("auto")
                                    && (m_effectivePerformanceMode == QStringLiteral("eco")
                                        || m_effectivePerformanceMode == QStringLiteral("standard")
                                        || m_effectivePerformanceMode == QStringLiteral("smooth"));
        effective = continuingAuto ? m_effectivePerformanceMode : QStringLiteral("standard");
    }
    if (m_sessionTracking && m_sessionRecordingTimer.isValid())
        updateExpectedSessionSamples(m_sessionRecordingTimer.elapsed());
    const bool changed = m_performanceMode != normalized
                         || m_effectivePerformanceMode != effective;
    m_performanceMode = normalized;
    m_effectivePerformanceMode = effective;
    if (normalized != QStringLiteral("auto")) m_autoStableIntervals = 0;
    if (changed) emit performanceModeChanged();
    if (!notifyGateway || m_gateway.state() != QAbstractSocket::ConnectedState) return;
    const QJsonObject command{{QStringLiteral("type"), QStringLiteral("performance-command")},
                              {QStringLiteral("mode"), m_effectivePerformanceMode}};
    m_gateway.write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
}

void SensorServiceController::setEffectivePerformanceMode(const QString &mode,
                                                          bool notifyGateway)
{
    if (mode != QStringLiteral("eco") && mode != QStringLiteral("standard")
        && mode != QStringLiteral("smooth")) return;
    if (m_effectivePerformanceMode == mode) return;
    if (m_sessionTracking && m_sessionRecordingTimer.isValid())
        updateExpectedSessionSamples(m_sessionRecordingTimer.elapsed());
    m_effectivePerformanceMode = mode;
    emit performanceModeChanged();
    if (!notifyGateway || m_gateway.state() != QAbstractSocket::ConnectedState) return;
    const QJsonObject command{{QStringLiteral("type"), QStringLiteral("performance-command")},
                              {QStringLiteral("mode"), m_effectivePerformanceMode}};
    m_gateway.write(QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n');
}

void SensorServiceController::updateAutomaticPerformanceMode(bool streamUnstable)
{
    if (m_performanceMode != QStringLiteral("auto")) return;
    if (streamUnstable) {
        m_autoStableIntervals = 0;
        setEffectivePerformanceMode(QStringLiteral("eco"), true);
        return;
    }
    ++m_autoStableIntervals;
    // The monitor runs every three seconds. Recover conservatively: first return
    // to 10 FPS, and only use 15 FPS after roughly half a minute of stable data.
    const bool latencyKnown = m_sensorKind == QStringLiteral("lidar-2d")
        || (m_rgbLatencyMilliseconds >= 0
            && (m_sensorKind != QStringLiteral("depth-camera") || m_depthLatencyMilliseconds >= 0));
    if (m_autoStableIntervals >= 10
        && latencyKnown
        && m_clockSync.uncertainty(m_gatewayClock.elapsed()) >= 0
        && std::max(m_rgbLatencyMilliseconds, m_depthLatencyMilliseconds) < 250)
        setEffectivePerformanceMode(QStringLiteral("smooth"), true);
    else if (m_autoStableIntervals >= 3)
        setEffectivePerformanceMode(QStringLiteral("standard"), true);
}

void SensorServiceController::start(const QString &modelId, const QString &worldId,
                                    const QString &performanceMode)
{
    if (m_busy || m_running) return;
    const QString nextSensorKind = sensorKindForModel(modelId);
    if (nextSensorKind.isEmpty()) {
        setProblemCode(QStringLiteral("sensor-not-supported"));
        setStatusMessage(QStringLiteral("当前机型尚未接入 ROS 2 传感器数据"));
        return;
    }
    if (!m_imageAvailable) {
        setProblemCode(QStringLiteral("sensor-image-missing"));
        setStatusMessage(QStringLiteral("尚未生成 FlightBench ROS 2 数据服务镜像"));
        return;
    }
    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (!safeId.match(worldId).hasMatch()) {
        setProblemCode(QStringLiteral("world-invalid"));
        setStatusMessage(QStringLiteral("ROS 2 数据服务收到无效的世界编号"));
        return;
    }

    applyPerformanceMode(performanceMode, false);
    resetScan();
    resetCamera();
    resetGimbal();
    if (m_sensorKind != nextSensorKind) {
        m_sensorKind = nextSensorKind;
        emit sensorKindChanged();
    }
    setProblemCode({});
    setStatusMessage(QStringLiteral("正在启动 ROS 2 传感器数据服务…"));
    run(Operation::Start,
        {QStringLiteral("run"), QStringLiteral("--rm"), QStringLiteral("-d"),
         QStringLiteral("--name"), QString::fromLatin1(ContainerName),
         QStringLiteral("--network"), QString::fromLatin1(NetworkName),
         // PX4 flight control must win CPU scheduling under Docker Desktop
         // contention; the read-only sensor bridge can safely drop frames.
         QStringLiteral("--cpu-shares"), QStringLiteral("256"),
         QStringLiteral("-p"), QStringLiteral("127.0.0.1:8765:8765"),
         QStringLiteral("-e"), QStringLiteral("GZ_PARTITION=flightbench"),
         QStringLiteral("-e"), QStringLiteral("PX4_GZ_WORLD=%1").arg(worldId),
         QStringLiteral("-e"), QStringLiteral("PX4_MODEL_INSTANCE=%1")
                                      .arg(gazeboModelInstance(modelId)),
         QStringLiteral("-e"), QStringLiteral("SENSOR_KIND=%1").arg(m_sensorKind),
         QStringLiteral("-e"), QStringLiteral("SENSOR_PERFORMANCE_MODE=%1")
                                      .arg(m_effectivePerformanceMode),
         QStringLiteral("-e"), QStringLiteral("SENSOR_GATEWAY_PORT=8765"),
         QString::fromLatin1(ImageName)});
}

void SensorServiceController::stop()
{
    m_reconnectTimer.stop();
    if (m_sessionTracking) finishSessionRecording();
    if (m_busy || !m_running || m_dockerPath.isEmpty()) return;
    // Mark the service stopped before aborting the socket. Otherwise the
    // disconnected handler immediately schedules a reconnect while Docker is
    // still stopping the container.
    setRunning(false);
    setStatusMessage(QStringLiteral("正在停止 ROS 2 数据服务…"));
    run(Operation::Stop,
        {QStringLiteral("stop"), QStringLiteral("-t"), QStringLiteral("5"),
         QString::fromLatin1(ContainerName)});
}

void SensorServiceController::run(Operation operation, const QStringList &arguments)
{
    m_operation = operation;
    // The periodic read-only inspect must not make the whole application look
    // busy every three seconds or temporarily disable flight controls.
    if (operation != Operation::Inspect) setBusy(true);
    m_process.setProgram(m_dockerPath);
    m_process.setArguments(arguments);
    m_process.start();
}

void SensorServiceController::handleFinished(int exitCode,
                                             QProcess::ExitStatus exitStatus)
{
    const Operation completed = m_operation;
    m_operation = Operation::None;
    if (completed != Operation::Inspect) setBusy(false);
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
    const QString output = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
    const QString error = QString::fromUtf8(m_process.readAllStandardError()).trimmed();

    switch (completed) {
    case Operation::ProbeImage:
        setImageAvailable(success);
        setProblemCode(success ? QString() : QStringLiteral("sensor-image-missing"));
        setStatusMessage(success ? QStringLiteral("ROS 2 数据服务镜像已就绪")
                                 : QStringLiteral("未找到 flightbench/ros-sensor-bridge:0.12.0"));
        break;
    case Operation::Start:
        if (success) {
            m_startedByUs = true;
            setRunning(true);
            setProblemCode({});
            setStatusMessage(QStringLiteral("ROS 2 容器已启动，正在等待传感器话题…"));
            m_monitorTimer.start();
            m_reconnectTimer.start();
        } else {
            setProblemCode(QStringLiteral("sensor-start-failed"));
            setStatusMessage(QStringLiteral("ROS 2 数据服务启动失败：%1")
                                 .arg(error.isEmpty() ? QStringLiteral("未知错误") : error));
        }
        break;
    case Operation::Stop:
        if (success) {
            m_startedByUs = false;
            setRunning(false);
            resetScan();
            resetCamera();
            resetGimbal();
            setProblemCode({});
            setStatusMessage(QStringLiteral("ROS 2 数据服务已停止"));
        } else {
            setRunning(true);
            m_monitorTimer.start();
            m_reconnectTimer.start();
            setProblemCode(QStringLiteral("sensor-stop-failed"));
            setStatusMessage(QStringLiteral("ROS 2 数据服务停止失败：%1")
                                 .arg(error.isEmpty() ? QStringLiteral("未知错误") : error));
        }
        break;
    case Operation::Inspect:
        if (!success || output.compare(QStringLiteral("true"), Qt::CaseInsensitive) != 0) {
            m_startedByUs = false;
            setRunning(false);
            setConnected(false);
            resetScan();
            resetCamera();
            resetGimbal();
            setProblemCode(QStringLiteral("sensor-container-stopped"));
            setStatusMessage(QStringLiteral("ROS 2 数据服务容器已经停止"));
        }
        break;
    case Operation::None:
        break;
    }
}

void SensorServiceController::handleError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart) return;
    const Operation failed = m_operation;
    m_operation = Operation::None;
    if (failed != Operation::Inspect) setBusy(false);
    if (failed == Operation::Stop) {
        setRunning(true);
        m_monitorTimer.start();
        m_reconnectTimer.start();
    }
    setProblemCode(QStringLiteral("docker-offline"));
    setStatusMessage(QStringLiteral("无法执行 docker.exe：%1").arg(m_process.errorString()));
}

void SensorServiceController::connectGateway()
{
    if (!m_running || m_gateway.state() != QAbstractSocket::UnconnectedState) return;
    m_gateway.connectToHost(QHostAddress::LocalHost, GatewayPort);
}

void SensorServiceController::requestClockSync()
{
    if (m_gateway.state() != QAbstractSocket::ConnectedState) return;
    const qint64 now = m_gatewayClock.elapsed();
    if (m_clockRequestSentMs >= 0 && now - m_clockRequestSentMs < 2000) return;
    m_clockRequestSentMs = now;
    const QJsonObject request{{QStringLiteral("type"), QStringLiteral("clock-ping")},
                              {QStringLiteral("id"), ++m_clockRequestId}};
    m_gateway.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
}

void SensorServiceController::readGatewayData()
{
    m_gatewayBuffer.append(m_gateway.readAll());
    QList<QByteArray> messages;
    for (;;) {
        const qsizetype newline = m_gatewayBuffer.indexOf('\n');
        if (newline < 0) break;
        const QByteArray line = m_gatewayBuffer.left(newline).trimmed();
        m_gatewayBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) messages.append(line);
    }

    // If the UI thread was briefly busy, a single readyRead can contain many
    // already-obsolete frames. Processing every one would make latency grow
    // further. Keep fresh messages, but for each backlogged sensor channel keep
    // only its newest stale frame so the live view catches up immediately.
    const qint64 nowMs = m_gatewayClock.elapsed();
    QHash<QString, qsizetype> newestBackloggedMessage;
    QSet<qsizetype> superseded;
    for (qsizetype index = 0; index < messages.size(); ++index) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(messages.at(index), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) continue;
        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type != QStringLiteral("camera") && type != QStringLiteral("scan")) continue;
        const qint64 sentAt = object.value(QStringLiteral("sentAtMonotonicMs")).toInteger(-1);
        if (m_clockSync.latency(sentAt, nowMs) <= 250) continue;
        const QString channel = type + QLatin1Char(':')
                                + object.value(QStringLiteral("stream")).toString();
        const auto previous = newestBackloggedMessage.constFind(channel);
        if (previous != newestBackloggedMessage.cend()) superseded.insert(previous.value());
        newestBackloggedMessage.insert(channel, index);
    }
    if (m_sessionTracking) {
        m_sessionStaleFrameDiscardCount += static_cast<int>(superseded.size());
    }
    for (qsizetype index = 0; index < messages.size(); ++index) {
        if (!superseded.contains(index)) processGatewayMessage(messages.at(index));
    }
    constexpr qsizetype MaximumBufferBytes = 2 * 1024 * 1024;
    if (m_gatewayBuffer.size() > MaximumBufferBytes) {
        m_gatewayBuffer.clear();
        setProblemCode(QStringLiteral("gateway-message-too-large"));
        setStatusMessage(QStringLiteral("ROS 2 数据消息异常，已等待下一帧"));
    }
}

void SensorServiceController::processGatewayMessage(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return;
    QJsonObject message = document.object();
    const QString messageType = message.value(QStringLiteral("type")).toString();
    if (messageType == QStringLiteral("clock-pong")) {
        if (m_clockRequestSentMs >= 0
            && message.value(QStringLiteral("id")).toInteger(-1) == m_clockRequestId) {
            m_clockSync.observe(m_clockRequestSentMs,
                message.value(QStringLiteral("serverReceiveMonoMs")).toInteger(-1),
                message.value(QStringLiteral("serverSendMonoMs")).toInteger(-1),
                m_gatewayClock.elapsed());
            m_clockRequestSentMs = -1;
        }
        return;
    }
    if (messageType == QStringLiteral("camera") || messageType == QStringLiteral("scan")) {
        const qint64 receivedMonoMs = m_gatewayClock.elapsed();
        const int latency = m_clockSync.latency(
            message.value(QStringLiteral("sentAtMonotonicMs")).toInteger(-1), receivedMonoMs);
        // Calculate once on receipt, before image decoding or disk I/O. Never
        // subtract the remote epoch from the Windows epoch, even for old gateways.
        message.insert(QStringLiteral("gatewaySentAtEpochMs"),
                       message.value(QStringLiteral("sentAtEpochMs")));
        message.insert(QStringLiteral("sentAtEpochMs"),
                       QDateTime::currentMSecsSinceEpoch() - std::max(0, latency));
        message.insert(QStringLiteral("latencyMilliseconds"), latency);
        message.insert(QStringLiteral("latencyMethod"), QStringLiteral("monotonic-sync-v1"));
        message.insert(QStringLiteral("latencyUncertaintyMilliseconds"),
                       latency >= 0 ? m_clockSync.uncertainty(receivedMonoMs) : -1);
    }
    if (messageType == QStringLiteral("gimbal-state")) {
        const bool actual = message.value(QStringLiteral("actual")).toBool();
        const double yaw = std::clamp(message.value(QStringLiteral("yaw")).toDouble(),
                                      -180.0, 180.0);
        const double pitch = std::clamp(message.value(QStringLiteral("pitch")).toDouble(),
                                        -135.0, 45.0);
        const double roll = std::clamp(message.value(QStringLiteral("roll")).toDouble(),
                                       -45.0, 45.0);
        m_gimbalTargetYawDegrees = std::clamp(
            message.value(QStringLiteral("targetYaw")).toDouble(m_gimbalTargetYawDegrees),
            -180.0, 180.0);
        m_gimbalTargetPitchDegrees = std::clamp(
            message.value(QStringLiteral("targetPitch")).toDouble(m_gimbalTargetPitchDegrees),
            -135.0, 45.0);
        m_gimbalTargetRollDegrees = std::clamp(
            message.value(QStringLiteral("targetRoll")).toDouble(m_gimbalTargetRollDegrees),
            -45.0, 45.0);
        if (actual) {
            m_gimbalFeedbackClock.restart();
            m_gimbalYawDegrees = yaw;
            m_gimbalPitchDegrees = pitch;
            m_gimbalRollDegrees = roll;
            m_gimbalFeedbackAvailable = true;
        }
        emit gimbalStateChanged();
        recordSensorMessage(message);
        return;
    }
    if (messageType == QStringLiteral("camera")) {
        const QString stream = message.value(QStringLiteral("stream")).toString();
        const QString encodedJpeg = message.value(QStringLiteral("jpegBase64")).toString();
        const QByteArray jpeg = QByteArray::fromBase64(encodedJpeg.toLatin1());
        const int width = message.value(QStringLiteral("width")).toInt();
        const int height = message.value(QStringLiteral("height")).toInt();
        if (jpeg.isEmpty() || width <= 0 || height <= 0) return;

        const double frameHz = std::clamp(message.value(QStringLiteral("hz")).toDouble(), 0.0, 240.0);
        const qint64 sentAt = message.value(QStringLiteral("sentAtEpochMs")).toInteger();
        const int latencyMilliseconds = message.value(QStringLiteral("latencyMilliseconds")).toInt(-1);
        m_lastCameraFrameTimer.restart();

        if (stream == QStringLiteral("depth")) {
            const bool hadPreviousFrame = !m_depthJpeg.isEmpty();
            // Gateway values are instantaneous and naturally move on every
            // frame. Smooth only the live label; session acceptance continues
            // to use the original timestamps and is not affected.
            m_depthFrameHz = hadPreviousFrame
                ? m_depthFrameHz * 0.8 + frameHz * 0.2 : frameHz;
            m_depthLatencyMilliseconds = hadPreviousFrame && m_depthLatencyMilliseconds >= 0
                && latencyMilliseconds >= 0
                ? qRound(m_depthLatencyMilliseconds * 0.8
                         + latencyMilliseconds * 0.2)
                : latencyMilliseconds;
            m_lastDepthFrameTimer.restart();
            if (m_depthStreamInterrupted) {
                m_depthStreamInterrupted = false;
                m_cameraStreamInterrupted = m_rgbStreamInterrupted;
                emit cameraStateChanged();
            }
            m_depthJpeg = jpeg;
            m_depthFrameDataUrlCache = QStringLiteral("data:image/jpeg;base64,") + encodedJpeg;
            m_depthWidth = width;
            m_depthHeight = height;
            m_depthCenterDistanceMeters = std::max(
                0.0, message.value(QStringLiteral("centerDistance")).toDouble());
            m_depthNearestDistanceMeters = std::max(
                0.0, message.value(QStringLiteral("nearestDistance")).toDouble());
            m_depthValidRatio = std::clamp(
                message.value(QStringLiteral("validRatio")).toDouble(), 0.0, 1.0);
            m_depthGridWidth = message.value(QStringLiteral("gridWidth")).toInt();
            m_depthGridHeight = message.value(QStringLiteral("gridHeight")).toInt();
            m_depthMillimetres = QByteArray::fromBase64(
                message.value(QStringLiteral("depthMillimetresBase64")).toString().toLatin1());
            m_depthTimestampEpochMs = sentAt > 0
                ? sentAt : QDateTime::currentMSecsSinceEpoch();
            const double safetyRegionNearest = nearestDepthInSafetyRegion(
                m_depthMillimetres, m_depthGridWidth, m_depthGridHeight);
            if (safetyRegionNearest > 0.0)
                m_depthNearestDistanceMeters = safetyRegionNearest;
            if (m_recording && m_recordingStream == QStringLiteral("depth"))
                appendRecordingFrame(jpeg, width, height);
            QJsonObject recordedMessage = message;
            recordedMessage.insert(QStringLiteral("nearestDistance"),
                                   m_depthNearestDistanceMeters);
            recordSensorMessage(recordedMessage, jpeg);
            emit depthFrameChanged();
        } else {
            const bool hadPreviousFrame = !m_rgbJpeg.isEmpty();
            m_rgbFrameHz = hadPreviousFrame
                ? m_rgbFrameHz * 0.8 + frameHz * 0.2 : frameHz;
            m_rgbLatencyMilliseconds = hadPreviousFrame && m_rgbLatencyMilliseconds >= 0
                && latencyMilliseconds >= 0
                ? qRound(m_rgbLatencyMilliseconds * 0.8
                         + latencyMilliseconds * 0.2)
                : latencyMilliseconds;
            m_lastRgbFrameTimer.restart();
            ++m_rgbFrameSequence;
            if (m_rgbStreamInterrupted) {
                m_rgbStreamInterrupted = false;
                m_cameraStreamInterrupted = m_depthStreamInterrupted;
                emit cameraStateChanged();
            }
            m_rgbJpeg = jpeg;
            m_rgbFrameDataUrlCache = QStringLiteral("data:image/jpeg;base64,") + encodedJpeg;
            m_rgbWidth = width;
            m_rgbHeight = height;
            m_rgbTimestampEpochMs = sentAt > 0
                ? sentAt : QDateTime::currentMSecsSinceEpoch();
            if (m_recording && m_recordingStream == QStringLiteral("rgb"))
                appendRecordingFrame(jpeg, width, height);
            recordSensorMessage(message, jpeg);
            emit cameraFrameChanged();
        }
        setConnected(true);
        setProblemCode({});
        setStatusMessage(QStringLiteral("相机数据正常"));
        return;
    }
    if (messageType != QStringLiteral("scan")) return;

    const QJsonArray ranges = message.value(QStringLiteral("ranges")).toArray();
    const double angleMin = message.value(QStringLiteral("angleMin")).toDouble();
    const double angleIncrement = message.value(QStringLiteral("angleIncrement")).toDouble();
    const double rangeMin = message.value(QStringLiteral("rangeMin")).toDouble(0.1);
    const double rangeMax = message.value(QStringLiteral("rangeMax")).toDouble(30.0);
    if (ranges.isEmpty() || !std::isfinite(angleIncrement) || angleIncrement <= 0.0) return;

    const qsizetype step = std::max<qsizetype>(1, ranges.size() / 360);
    QVariantList points;
    points.reserve((ranges.size() + step - 1) / step);
    double nearest = std::numeric_limits<double>::infinity();
    double nearestAngle = 0.0;
    for (qsizetype index = 0; index < ranges.size(); index += step) {
        const double range = ranges.at(index).toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(range) || range < rangeMin || range > rangeMax) continue;
        const double angle = angleMin + static_cast<double>(index) * angleIncrement;
        QVariantMap point;
        // ROS LaserScan angles are positive counter-clockwise (left). Screen
        // coordinates grow to the right, so mirror the horizontal component.
        point.insert(QStringLiteral("x"), -std::sin(angle) * range);
        point.insert(QStringLiteral("y"), -std::cos(angle) * range);
        point.insert(QStringLiteral("distance"), range);
        // A value at the sensor ceiling describes observed free space, not a
        // physical wall. Preserve this distinction in recordings so SLAM does
        // not draw a false 30 m obstacle ring.
        point.insert(QStringLiteral("hit"),
                     range < rangeMax - std::max(0.08, rangeMax * 0.005));
        points.append(point);
        if (range < nearest) {
            nearest = range;
            nearestAngle = angle * 180.0 / std::numbers::pi;
        }
    }
    m_lidarPoints = points;
    m_lidarTimestampEpochMs = message.value(QStringLiteral("sentAtEpochMs"))
                                  .toInteger(QDateTime::currentMSecsSinceEpoch());
    m_lastScanTimer.restart();
    m_lidarNearestDistanceMeters = std::isfinite(nearest) ? nearest : 0.0;
    m_lidarNearestAngleDegrees = std::isfinite(nearest) ? nearestAngle : 0.0;
    m_lidarScanHz = std::clamp(message.value(QStringLiteral("hz")).toDouble(), 0.0, 1000.0);
    m_lidarFieldOfViewDegrees = ranges.size() > 1
        ? std::abs(angleIncrement) * static_cast<double>(ranges.size() - 1)
              * 180.0 / std::numbers::pi
        : 0.0;
    setConnected(true);
    setProblemCode({});
    setStatusMessage(QStringLiteral("二维雷达数据正常"));
    QJsonObject recordedMessage = message;
    recordedMessage.remove(QStringLiteral("ranges"));
    recordedMessage.insert(QStringLiteral("points"), QJsonArray::fromVariantList(points));
    recordedMessage.insert(QStringLiteral("nearestDistance"), m_lidarNearestDistanceMeters);
    recordedMessage.insert(QStringLiteral("nearestAngle"), m_lidarNearestAngleDegrees);
    recordedMessage.insert(QStringLiteral("fieldOfView"), m_lidarFieldOfViewDegrees);
    recordSensorMessage(recordedMessage);
    emit lidarScanChanged();
}

bool SensorServiceController::beginSessionRecording(const QString &sessionFolder,
                                                    const QString &usage,
                                                    const QString &performanceMode)
{
    if (m_sessionTracking) finishSessionRecording();
    applyPerformanceMode(performanceMode, false);
    if (usage != QStringLiteral("preview") && usage != QStringLiteral("record")) return true;
    const QString cleanFolder = QDir::cleanPath(sessionFolder);
    if (cleanFolder.isEmpty() || !QFileInfo(cleanFolder).isDir()) return false;
    m_sessionUsage = usage;
    m_sessionRecordingRequested = usage == QStringLiteral("record");
    m_sessionWriteFailed = false;
    m_sessionMaximumRecordedBytes = 2LL * 1024 * 1024 * 1024;
    if (usage == QStringLiteral("record")) {
        QStorageInfo storage(cleanFolder);
        constexpr qint64 ReservedFreeBytes = 256LL * 1024 * 1024;
        constexpr qint64 MinimumUsefulRecordingBytes = 128LL * 1024 * 1024;
        if (storage.isValid() && storage.isReady()) {
            m_sessionMaximumRecordedBytes = std::min<qint64>(
                m_sessionMaximumRecordedBytes,
                std::max<qint64>(0, storage.bytesAvailable() - ReservedFreeBytes));
        }
        if (m_sessionMaximumRecordedBytes < MinimumUsefulRecordingBytes) {
            setProblemCode(QStringLiteral("sensor-storage-low"));
            setStatusMessage(QStringLiteral("磁盘空间不足，已保留 256 MB 安全余量并改为仅预览"));
            m_sessionUsage = QStringLiteral("preview");
        }
    }
    if (m_sessionUsage == QStringLiteral("record")) {
        m_sessionSensorDirectory = QDir(cleanFolder).filePath(QStringLiteral("sensor"));
        QDir sensorDirectory(m_sessionSensorDirectory);
        if (!sensorDirectory.mkpath(QStringLiteral("rgb"))
            || !sensorDirectory.mkpath(QStringLiteral("depth"))) {
            setStatusMessage(QStringLiteral("无法创建会话传感器目录，已改为仅预览"));
            m_sessionUsage = QStringLiteral("preview");
            m_sessionSensorDirectory.clear();
        } else {
            m_sessionTimelineFile.setFileName(
                sensorDirectory.filePath(QStringLiteral("timeline.ndjson")));
            if (!m_sessionTimelineFile.open(
                    QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                setStatusMessage(QStringLiteral("无法创建传感器时间索引，已改为仅预览"));
                m_sessionUsage = QStringLiteral("preview");
                m_sessionSensorDirectory.clear();
            }
        }
    }
    m_sessionBytesWritten = 0;
    m_sessionFirstDataMs = -1;
    m_sessionLastDataMs = -1;
    m_lastRgbRecordMs = m_lastDepthRecordMs = m_lastScanRecordMs = -1;
    m_lastPrimarySampleMs = -1;
    m_lastRgbPrimarySampleMs = m_lastDepthPrimarySampleMs = m_lastLidarPrimarySampleMs = -1;
    m_sessionMaximumDataGapMs = 0;
    m_sessionRgbFrames = m_sessionDepthFrames = m_sessionScanCount = 0;
    m_sessionGimbalSamples = m_sessionDropoutCount = 0;
    m_sessionLatencyTotalMs = 0.0;
    m_sessionLatencySamples = 0;
    m_sessionMinimumFrameRateHz = 0.0;
    m_sessionRgbMinimumFrameRateHz = 0.0;
    m_sessionDepthMinimumFrameRateHz = 0.0;
    m_sessionLidarMinimumFrameRateHz = 0.0;
    m_sessionRgbMaximumDataGapMs = 0;
    m_sessionDepthMaximumDataGapMs = 0;
    m_sessionLidarMaximumDataGapMs = 0;
    m_sessionDepthValidRatioTotal = 0.0;
    m_sessionDepthValidRatioSamples = 0;
    m_sessionRecordedSampleCount = 0;
    m_sessionWriteFailureCount = 0;
    m_sessionStaleFrameDiscardCount = 0;
    m_sessionExpectedSamples = 0.0;
    m_sessionExpectedUpdatedAtMs = -1;
    m_sessionPrimaryDataStarted = false;
    m_sessionPrimaryFirstDataMs = -1;
    m_sessionNextStorageCheckBytes = 0;
    m_sessionNearestObstacleMeters = 0.0;
    m_sessionStorageLimitReached = false;
    m_sessionRecordingTimer.restart();
    m_sessionTracking = true;
    const bool recording = m_sessionUsage == QStringLiteral("record");
    if (m_sessionRecording != recording) {
        m_sessionRecording = recording;
        emit sessionRecordingChanged();
    }
    return true;
}

void SensorServiceController::updateExpectedSessionSamples(qint64 elapsedMs)
{
    // Container creation, bridge discovery and the first ROS subscription take
    // several seconds. They are startup time, not a data-stream interruption.
    if (!m_sessionTracking || !m_sessionPrimaryDataStarted) return;
    if (m_sessionExpectedUpdatedAtMs < 0) {
        m_sessionExpectedUpdatedAtMs = elapsedMs;
        return;
    }
    if (elapsedMs <= m_sessionExpectedUpdatedAtMs) return;
    const qint64 deltaMs = elapsedMs - m_sessionExpectedUpdatedAtMs;
    m_sessionExpectedSamples += deltaMs * targetFramesPerSecond() / 1000.0;
    m_sessionExpectedUpdatedAtMs = elapsedMs;
}

void SensorServiceController::stopSessionFileRecording(const QString &message,
                                                       bool storageLimit)
{
    if (!m_sessionRecording) return;
    m_sessionStorageLimitReached = m_sessionStorageLimitReached || storageLimit;
    m_sessionWriteFailed = m_sessionWriteFailed || !storageLimit;
    if (!storageLimit) ++m_sessionWriteFailureCount;
    if (m_sessionTimelineFile.isOpen()) {
        m_sessionTimelineFile.flush();
        m_sessionTimelineFile.close();
    }
    m_sessionRecording = false;
    emit sessionRecordingChanged();
    setProblemCode(storageLimit ? QStringLiteral("sensor-storage-limit")
                                : QStringLiteral("sensor-recording-write-failed"));
    setStatusMessage(message);
}

void SensorServiceController::recordSensorMessage(const QJsonObject &source,
                                                  const QByteArray &jpeg)
{
    if (!m_sessionTracking || !m_sessionRecordingTimer.isValid()) return;
    if (m_sessionRecording && m_sessionBytesWritten >= m_sessionMaximumRecordedBytes) {
        ++m_sessionDropoutCount;
        stopSessionFileRecording(
            QStringLiteral("传感器记录已达到本次安全容量上限，实时预览继续运行"), true);
    }
    if (m_sessionRecording && m_sessionBytesWritten >= m_sessionNextStorageCheckBytes) {
        constexpr qint64 MinimumFreeBytes = 192LL * 1024 * 1024;
        constexpr qint64 StorageCheckIntervalBytes = 16LL * 1024 * 1024;
        const QStorageInfo storage(m_sessionSensorDirectory);
        if (storage.isValid() && storage.isReady() && storage.bytesAvailable() < MinimumFreeBytes) {
            ++m_sessionDropoutCount;
            stopSessionFileRecording(
                QStringLiteral("磁盘剩余空间不足，已停止保存但继续实时预览"), true);
        }
        m_sessionNextStorageCheckBytes = m_sessionBytesWritten + StorageCheckIntervalBytes;
    }
    const qint64 elapsedMs = m_sessionRecordingTimer.elapsed();
    QJsonObject entry = source;
    entry.insert(QStringLiteral("elapsedMs"), elapsedMs);
    entry.remove(QStringLiteral("jpegBase64"));
    if (!m_sessionRecording)
        entry.remove(QStringLiteral("depthMillimetresBase64"));
    const QString type = entry.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("camera")) {
        const QString stream = entry.value(QStringLiteral("stream")).toString() == QStringLiteral("depth")
                                   ? QStringLiteral("depth") : QStringLiteral("rgb");
        qint64 &lastMs = stream == QStringLiteral("depth") ? m_lastDepthRecordMs : m_lastRgbRecordMs;
        const int minimumInterval = std::max(1, 1000 / targetFramesPerSecond());
        if (lastMs >= 0 && elapsedMs - lastMs < minimumInterval - 2) return;
        lastMs = elapsedMs;
        if (!m_sessionPrimaryDataStarted) {
            m_sessionPrimaryDataStarted = true;
            m_sessionPrimaryFirstDataMs = elapsedMs;
            m_sessionExpectedUpdatedAtMs = elapsedMs;
        } else {
            updateExpectedSessionSamples(elapsedMs);
        }
        int &counter = stream == QStringLiteral("depth") ? m_sessionDepthFrames : m_sessionRgbFrames;
        const QString relativePath = QStringLiteral("%1/%2-%3.jpg")
                                         .arg(stream)
                                         .arg(counter, 7, 10, QLatin1Char('0'))
                                         .arg(elapsedMs, 9, 10, QLatin1Char('0'));
        if (m_sessionRecording) {
            QFile frameFile(QDir(m_sessionSensorDirectory).filePath(relativePath));
            if (!frameFile.open(QIODevice::WriteOnly) || frameFile.write(jpeg) != jpeg.size()) {
                ++m_sessionDropoutCount;
                stopSessionFileRecording(
                    QStringLiteral("传感器文件写入失败，已停止保存但继续实时预览"), false);
                entry.remove(QStringLiteral("file"));
            } else {
                frameFile.close();
                entry.insert(QStringLiteral("file"), relativePath);
                m_sessionBytesWritten += jpeg.size();
            }
        }
        ++counter;
        const double frameRate = entry.value(QStringLiteral("hz")).toDouble();
        if (frameRate > 0.0 && (m_sessionMinimumFrameRateHz <= 0.0
                               || frameRate < m_sessionMinimumFrameRateHz))
            m_sessionMinimumFrameRateHz = frameRate;
        if (stream == QStringLiteral("depth")) {
            if (frameRate > 0.0 && (m_sessionDepthMinimumFrameRateHz <= 0.0
                                   || frameRate < m_sessionDepthMinimumFrameRateHz))
                m_sessionDepthMinimumFrameRateHz = frameRate;
            const double validRatio = std::clamp(
                entry.value(QStringLiteral("validRatio")).toDouble(), 0.0, 1.0);
            m_sessionDepthValidRatioTotal += validRatio;
            ++m_sessionDepthValidRatioSamples;
            const double nearest = entry.value(QStringLiteral("nearestDistance")).toDouble();
            if (nearest > 0.0 && (m_sessionNearestObstacleMeters <= 0.0
                                  || nearest < m_sessionNearestObstacleMeters))
                m_sessionNearestObstacleMeters = nearest;
        } else if (frameRate > 0.0 && (m_sessionRgbMinimumFrameRateHz <= 0.0
                                      || frameRate < m_sessionRgbMinimumFrameRateHz)) {
            m_sessionRgbMinimumFrameRateHz = frameRate;
        }
        const int latencyMs = entry.value(QStringLiteral("latencyMilliseconds")).toInt(-1);
        if (latencyMs >= 0) {
            m_sessionLatencyTotalMs += latencyMs;
            ++m_sessionLatencySamples;
        }
    } else if (type == QStringLiteral("scan")) {
        const int minimumInterval = std::max(1, 1000 / targetFramesPerSecond());
        if (m_lastScanRecordMs >= 0 && elapsedMs - m_lastScanRecordMs < minimumInterval - 2) return;
        m_lastScanRecordMs = elapsedMs;
        if (!m_sessionPrimaryDataStarted) {
            m_sessionPrimaryDataStarted = true;
            m_sessionPrimaryFirstDataMs = elapsedMs;
            m_sessionExpectedUpdatedAtMs = elapsedMs;
        } else {
            updateExpectedSessionSamples(elapsedMs);
        }
        ++m_sessionScanCount;
        const double frameRate = entry.value(QStringLiteral("hz")).toDouble();
        if (frameRate > 0.0 && (m_sessionMinimumFrameRateHz <= 0.0
                               || frameRate < m_sessionMinimumFrameRateHz))
            m_sessionMinimumFrameRateHz = frameRate;
        if (frameRate > 0.0 && (m_sessionLidarMinimumFrameRateHz <= 0.0
                               || frameRate < m_sessionLidarMinimumFrameRateHz))
            m_sessionLidarMinimumFrameRateHz = frameRate;
        const double nearest = entry.value(QStringLiteral("nearestDistance")).toDouble();
        if (nearest > 0.0 && (m_sessionNearestObstacleMeters <= 0.0
                              || nearest < m_sessionNearestObstacleMeters))
            m_sessionNearestObstacleMeters = nearest;
        const int latencyMs = entry.value(QStringLiteral("latencyMilliseconds")).toInt(-1);
        if (latencyMs >= 0) {
            m_sessionLatencyTotalMs += latencyMs;
            ++m_sessionLatencySamples;
        }
    } else if (type == QStringLiteral("gimbal-state")) {
        ++m_sessionGimbalSamples;
    }
    if (type == QStringLiteral("camera") || type == QStringLiteral("scan")) {
        if (m_lastPrimarySampleMs >= 0)
            m_sessionMaximumDataGapMs = std::max(
                m_sessionMaximumDataGapMs, elapsedMs - m_lastPrimarySampleMs);
        m_lastPrimarySampleMs = elapsedMs;
        qint64 *streamLastMs = nullptr;
        qint64 *streamMaximumGapMs = nullptr;
        if (type == QStringLiteral("scan")) {
            streamLastMs = &m_lastLidarPrimarySampleMs;
            streamMaximumGapMs = &m_sessionLidarMaximumDataGapMs;
        } else if (entry.value(QStringLiteral("stream")).toString() == QStringLiteral("depth")) {
            streamLastMs = &m_lastDepthPrimarySampleMs;
            streamMaximumGapMs = &m_sessionDepthMaximumDataGapMs;
        } else {
            streamLastMs = &m_lastRgbPrimarySampleMs;
            streamMaximumGapMs = &m_sessionRgbMaximumDataGapMs;
        }
        if (*streamLastMs >= 0)
            *streamMaximumGapMs = std::max(*streamMaximumGapMs, elapsedMs - *streamLastMs);
        *streamLastMs = elapsedMs;
    }
    if (m_sessionRecording) {
        const QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
        if (m_sessionTimelineFile.write(line) != line.size()) {
            ++m_sessionDropoutCount;
            stopSessionFileRecording(
                QStringLiteral("传感器时间索引写入失败，已停止保存但继续实时预览"), false);
        } else {
            m_sessionBytesWritten += line.size();
            ++m_sessionRecordedSampleCount;
        }
    }
    if (m_sessionFirstDataMs < 0) m_sessionFirstDataMs = elapsedMs;
    m_sessionLastDataMs = elapsedMs;
    if ((m_sessionRgbFrames + m_sessionDepthFrames + m_sessionScanCount
         + m_sessionGimbalSamples) % 10 == 0) {
        if (m_sessionTimelineFile.isOpen()) m_sessionTimelineFile.flush();
    }
}

bool SensorServiceController::writeSessionManifest(const QVariantMap &summary)
{
    if (m_sessionSensorDirectory.isEmpty()) return false;
    QSaveFile file(QDir(m_sessionSensorDirectory).filePath(QStringLiteral("manifest.json")));
    const QByteArray contents = QJsonDocument(QJsonObject::fromVariantMap(summary))
                                    .toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(contents) == contents.size() && file.commit();
}

QVariantMap SensorServiceController::finishSessionRecording()
{
    if (!m_sessionTracking) return {};
    const qint64 finishedAtMs = m_sessionRecordingTimer.isValid()
        ? m_sessionRecordingTimer.elapsed() : 0;
    updateExpectedSessionSamples(finishedAtMs);
    if (m_lastPrimarySampleMs >= 0 && finishedAtMs > m_lastPrimarySampleMs) {
        m_sessionMaximumDataGapMs = std::max(
            m_sessionMaximumDataGapMs, finishedAtMs - m_lastPrimarySampleMs);
    }
    const auto includeTailGap = [finishedAtMs](qint64 lastSampleMs, qint64 &maximumGapMs) {
        if (lastSampleMs >= 0)
            maximumGapMs = std::max(maximumGapMs, finishedAtMs - lastSampleMs);
    };
    includeTailGap(m_lastRgbPrimarySampleMs, m_sessionRgbMaximumDataGapMs);
    includeTailGap(m_lastDepthPrimarySampleMs, m_sessionDepthMaximumDataGapMs);
    includeTailGap(m_lastLidarPrimarySampleMs, m_sessionLidarMaximumDataGapMs);
    if (m_sessionTimelineFile.isOpen()) {
        m_sessionTimelineFile.flush();
        m_sessionTimelineFile.close();
    }
    const int totalSamples = m_sessionRgbFrames + m_sessionDepthFrames
                             + m_sessionScanCount + m_sessionGimbalSamples;
    const double durationSeconds = std::max<qint64>(0, finishedAtMs) / 1000.0;
    int primarySamples = m_sessionScanCount;
    if (m_sensorKind == QStringLiteral("depth-camera"))
        primarySamples = std::min(m_sessionRgbFrames, m_sessionDepthFrames);
    else if (m_sensorKind != QStringLiteral("lidar-2d"))
        primarySamples = m_sessionRgbFrames;
    const double expectedSamples = std::max(0.0, m_sessionExpectedSamples);
    const double activeDurationSeconds = m_sessionPrimaryFirstDataMs >= 0
        && m_lastPrimarySampleMs > m_sessionPrimaryFirstDataMs
        ? (m_lastPrimarySampleMs - m_sessionPrimaryFirstDataMs) / 1000.0 : 0.0;
    const double averageFrameRateHz = activeDurationSeconds > 0.0 && primarySamples > 1
        ? (primarySamples - 1) / activeDurationSeconds
        : static_cast<double>(primarySamples);
    const auto availability = [expectedSamples](int count) {
        return expectedSamples > 0.0
            ? std::clamp(count / expectedSamples * 100.0, 0.0, 100.0)
            : count > 0 ? 100.0 : 0.0;
    };
    const double rgbAvailabilityPercent = availability(m_sessionRgbFrames);
    const double depthAvailabilityPercent = availability(m_sessionDepthFrames);
    const double lidarAvailabilityPercent = availability(m_sessionScanCount);
    double availabilityPercent = lidarAvailabilityPercent;
    if (m_sensorKind == QStringLiteral("depth-camera"))
        availabilityPercent = std::min(rgbAvailabilityPercent, depthAvailabilityPercent);
    else if (m_sensorKind != QStringLiteral("lidar-2d"))
        availabilityPercent = rgbAvailabilityPercent;

    double primaryMinimumFrameRateHz = m_sessionLidarMinimumFrameRateHz;
    qint64 primaryMaximumGapMs = m_sessionLidarMaximumDataGapMs;
    if (m_sensorKind == QStringLiteral("depth-camera")) {
        primaryMinimumFrameRateHz = (m_sessionRgbMinimumFrameRateHz > 0.0
                                     && m_sessionDepthMinimumFrameRateHz > 0.0)
            ? std::min(m_sessionRgbMinimumFrameRateHz, m_sessionDepthMinimumFrameRateHz) : 0.0;
        primaryMaximumGapMs = std::max(m_sessionRgbMaximumDataGapMs,
                                       m_sessionDepthMaximumDataGapMs);
    } else if (m_sensorKind != QStringLiteral("lidar-2d")) {
        primaryMinimumFrameRateHz = m_sessionRgbMinimumFrameRateHz;
        primaryMaximumGapMs = m_sessionRgbMaximumDataGapMs;
    }
    const double depthValidRatio = m_sessionDepthValidRatioSamples > 0
        ? m_sessionDepthValidRatioTotal / m_sessionDepthValidRatioSamples : 0.0;
    const QString timelinePath = QDir(m_sessionSensorDirectory)
                                     .filePath(QStringLiteral("timeline.ndjson"));
    const bool replayable = m_sessionRecordedSampleCount > 0
                            && QFileInfo(timelinePath).isFile()
                            && QFileInfo(timelinePath).size() > 0;
    const bool requestedRecording = m_sessionRecordingRequested;
    bool recordingComplete = replayable && !m_sessionWriteFailed
                             && !m_sessionStorageLimitReached;
    const bool visualDataAvailable = m_sensorKind == QStringLiteral("lidar-2d")
        ? m_sessionScanCount > 0
        : m_sensorKind == QStringLiteral("depth-camera")
          ? m_sessionRgbFrames > 0 && m_sessionDepthFrames > 0 && depthValidRatio > 0.0
          : m_sessionRgbFrames > 0;
    QVariantMap summary{
        {QStringLiteral("format"), QStringLiteral("FlightBenchSensorSession")},
        {QStringLiteral("formatVersion"), 2},
        {QStringLiteral("available"), visualDataAvailable},
        {QStringLiteral("usage"), m_sessionUsage},
        {QStringLiteral("requestedUsage"), requestedRecording
             ? QStringLiteral("record") : m_sessionUsage},
        {QStringLiteral("recorded"), replayable},
        {QStringLiteral("replayable"), replayable},
        {QStringLiteral("recordingComplete"), recordingComplete},
        {QStringLiteral("kind"), m_sensorKind},
        {QStringLiteral("performanceMode"), m_performanceMode},
        {QStringLiteral("effectivePerformanceMode"), m_effectivePerformanceMode},
        {QStringLiteral("targetFramesPerSecond"), targetFramesPerSecond()},
        {QStringLiteral("expectedSampleCount"), expectedSamples},
        {QStringLiteral("startedOffsetMs"), std::max<qint64>(0, m_sessionFirstDataMs)},
        {QStringLiteral("primaryStartedOffsetMs"),
         std::max<qint64>(0, m_sessionPrimaryFirstDataMs)},
        {QStringLiteral("endedOffsetMs"), std::max<qint64>(0, finishedAtMs)},
        {QStringLiteral("durationSeconds"), durationSeconds},
        {QStringLiteral("rgbFrameCount"), m_sessionRgbFrames},
        {QStringLiteral("depthFrameCount"), m_sessionDepthFrames},
        {QStringLiteral("lidarScanCount"), m_sessionScanCount},
        {QStringLiteral("gimbalSampleCount"), m_sessionGimbalSamples},
        {QStringLiteral("dropoutCount"), m_sessionDropoutCount},
        {QStringLiteral("staleFrameDiscardCount"), m_sessionStaleFrameDiscardCount},
        {QStringLiteral("minimumFrameRateHz"), primaryMinimumFrameRateHz},
        {QStringLiteral("averageFrameRateHz"), averageFrameRateHz},
        {QStringLiteral("maximumDataGapSeconds"), primaryMaximumGapMs / 1000.0},
        {QStringLiteral("dataAvailabilityPercent"), availabilityPercent},
        {QStringLiteral("rgbAvailabilityPercent"), rgbAvailabilityPercent},
        {QStringLiteral("depthAvailabilityPercent"), depthAvailabilityPercent},
        {QStringLiteral("lidarAvailabilityPercent"), lidarAvailabilityPercent},
        {QStringLiteral("rgbMinimumFrameRateHz"), m_sessionRgbMinimumFrameRateHz},
        {QStringLiteral("depthMinimumFrameRateHz"), m_sessionDepthMinimumFrameRateHz},
        {QStringLiteral("lidarMinimumFrameRateHz"), m_sessionLidarMinimumFrameRateHz},
        {QStringLiteral("rgbMaximumDataGapSeconds"), m_sessionRgbMaximumDataGapMs / 1000.0},
        {QStringLiteral("depthMaximumDataGapSeconds"), m_sessionDepthMaximumDataGapMs / 1000.0},
        {QStringLiteral("lidarMaximumDataGapSeconds"), m_sessionLidarMaximumDataGapMs / 1000.0},
        {QStringLiteral("depthValidRatio"), depthValidRatio},
        {QStringLiteral("nearestObstacleMeters"), m_sessionNearestObstacleMeters},
        {QStringLiteral("obstacleDistanceMeasured"), m_sessionNearestObstacleMeters > 0.0},
        {QStringLiteral("latencyMeasured"), m_sessionLatencySamples > 0},
        {QStringLiteral("latencyMethod"), QStringLiteral("monotonic-sync-v1")},
        {QStringLiteral("latencySampleCount"), m_sessionLatencySamples},
        {QStringLiteral("averageLatencyMilliseconds"),
         m_sessionLatencySamples > 0 ? m_sessionLatencyTotalMs / m_sessionLatencySamples : 0.0},
        {QStringLiteral("sizeBytes"), m_sessionBytesWritten},
        {QStringLiteral("relativeDirectory"), replayable
             ? QStringLiteral("sensor") : QString()},
        {QStringLiteral("timelineFile"), replayable
             ? QStringLiteral("sensor/timeline.ndjson") : QString()},
        {QStringLiteral("slamMapAvailable"), false},
        {QStringLiteral("storageLimitReached"), m_sessionStorageLimitReached},
        {QStringLiteral("writeFailed"), m_sessionWriteFailed},
        {QStringLiteral("writeFailureCount"), m_sessionWriteFailureCount},
        {QStringLiteral("recordedSampleCount"), m_sessionRecordedSampleCount},
        {QStringLiteral("totalSampleCount"), totalSamples},
        {QStringLiteral("statusLabel"), !visualDataAvailable
             ? QStringLiteral("未收到有效传感器数据")
             : requestedRecording && !replayable
               ? QStringLiteral("收到数据，但没有成功保存可回放记录")
             : m_sessionStorageLimitReached
               ? QStringLiteral("已保存部分数据；因容量保护提前停止记录")
             : m_sessionWriteFailed
               ? QStringLiteral("已保存部分数据；写入失败导致记录不完整")
             : replayable ? QStringLiteral("已记录并可同步回放")
                                  : QStringLiteral("已收到数据，但方案选择了仅预览")},
    };
    if (replayable && !writeSessionManifest(summary)) {
        recordingComplete = false;
        summary.insert(QStringLiteral("recordingComplete"), false);
        summary.insert(QStringLiteral("manifestWriteFailed"), true);
        summary.insert(QStringLiteral("statusLabel"),
                       QStringLiteral("数据已保存，但会话索引写入失败"));
    }
    const bool wasRecording = m_sessionRecording;
    m_sessionRecording = false;
    m_sessionTracking = false;
    m_sessionUsage = QStringLiteral("off");
    if (wasRecording) emit sessionRecordingChanged();
    m_sessionSensorDirectory.clear();
    return summary;
}

void SensorServiceController::resetScan()
{
    const bool hadVisibleData = !m_lidarPoints.isEmpty()
                                 || !qFuzzyIsNull(m_lidarNearestDistanceMeters)
                                 || !qFuzzyIsNull(m_lidarScanHz)
                                 || !qFuzzyIsNull(m_lidarFieldOfViewDegrees);
    m_lastScanTimer.invalidate();
    if (!hadVisibleData) return;
    m_lidarPoints.clear();
    m_lidarTimestampEpochMs = 0;
    m_lidarNearestDistanceMeters = 0.0;
    m_lidarNearestAngleDegrees = 0.0;
    m_lidarScanHz = 0.0;
    m_lidarFieldOfViewDegrees = 0.0;
    emit lidarScanChanged();
}

QString SensorServiceController::rgbFrameDataUrl() const
{
    return m_rgbFrameDataUrlCache;
}

QString SensorServiceController::depthFrameDataUrl() const
{
    return m_depthFrameDataUrlCache;
}

QString SensorServiceController::capturesDirectory() const
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(documents).filePath(QStringLiteral("FlightBench/Captures"));
}

double SensorServiceController::depthDistanceAt(double normalizedX, double normalizedY) const
{
    if (m_depthGridWidth <= 0 || m_depthGridHeight <= 0) return 0.0;
    const int x = std::clamp(static_cast<int>(normalizedX * m_depthGridWidth),
                             0, m_depthGridWidth - 1);
    const int y = std::clamp(static_cast<int>(normalizedY * m_depthGridHeight),
                             0, m_depthGridHeight - 1);
    const qsizetype offset = (static_cast<qsizetype>(y) * m_depthGridWidth + x) * 2;
    if (offset + 1 >= m_depthMillimetres.size()) return 0.0;
    const auto low = static_cast<quint8>(m_depthMillimetres.at(offset));
    const auto high = static_cast<quint8>(m_depthMillimetres.at(offset + 1));
    return static_cast<double>(low | (high << 8)) / 1000.0;
}

bool SensorServiceController::saveSnapshot(const QString &stream)
{
    const QByteArray &jpeg = stream == QStringLiteral("depth") ? m_depthJpeg : m_rgbJpeg;
    if (jpeg.isEmpty()) {
        setStatusMessage(QStringLiteral("收到第一帧画面后才能截图"));
        return false;
    }
    QDir directory(capturesDirectory());
    if (!directory.mkpath(QStringLiteral("."))) {
        setStatusMessage(QStringLiteral("无法创建截图保存目录"));
        return false;
    }
    const QString path = directory.filePath(
        QStringLiteral("%1-%2.jpg")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
                 stream == QStringLiteral("depth") ? QStringLiteral("depth")
                                                    : QStringLiteral("rgb")));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(jpeg) != jpeg.size()) {
        setStatusMessage(QStringLiteral("截图保存失败"));
        return false;
    }
    m_lastCapturePath = QDir::toNativeSeparators(path);
    emit captureSaved();
    setStatusMessage(QStringLiteral("截图已保存到 Captures 文件夹"));
    return true;
}

bool SensorServiceController::startRecording(const QString &stream)
{
    if (m_recording) return true;
    const QByteArray &jpeg = stream == QStringLiteral("depth") ? m_depthJpeg : m_rgbJpeg;
    const bool depthStream = stream == QStringLiteral("depth");
    const int streamWidth = depthStream ? m_depthWidth : m_rgbWidth;
    const int streamHeight = depthStream ? m_depthHeight : m_rgbHeight;
    if (jpeg.isEmpty() || streamWidth <= 0 || streamHeight <= 0) {
        setStatusMessage(QStringLiteral("收到第一帧画面后才能开始录像"));
        return false;
    }
    QDir directory(capturesDirectory());
    if (!directory.mkpath(QStringLiteral("."))) return false;
    m_recordingStream = stream == QStringLiteral("depth") ? QStringLiteral("depth")
                                                           : QStringLiteral("rgb");
    m_recordingPath = directory.filePath(
        QStringLiteral("%1-%2.avi")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")),
                 m_recordingStream));
    m_recordingFile.setFileName(m_recordingPath);
    if (!m_recordingFile.open(QIODevice::WriteOnly)) {
        setStatusMessage(QStringLiteral("无法创建录像文件"));
        return false;
    }
    m_recordingWidth = streamWidth;
    m_recordingHeight = streamHeight;
    m_recordingFrames.clear();
    QDataStream out(&m_recordingFile);
    out.setByteOrder(QDataStream::LittleEndian);
    writeFourCc(out, "RIFF"); m_recordingRiffSizeOffset = m_recordingFile.pos(); out << quint32(0);
    writeFourCc(out, "AVI ");
    writeFourCc(out, "LIST"); out << quint32(192); writeFourCc(out, "hdrl");
    writeFourCc(out, "avih"); out << quint32(56) << quint32(100000) << quint32(0)
        << quint32(0) << quint32(0x10);
    m_recordingFrameCountOffset = m_recordingFile.pos(); out << quint32(0);
    out << quint32(0) << quint32(1) << quint32(0) << quint32(m_recordingWidth)
        << quint32(m_recordingHeight) << quint32(0) << quint32(0) << quint32(0) << quint32(0);
    writeFourCc(out, "LIST"); out << quint32(116); writeFourCc(out, "strl");
    writeFourCc(out, "strh"); out << quint32(56); writeFourCc(out, "vids"); writeFourCc(out, "MJPG");
    out << quint32(0) << quint32(0) << quint32(0) << quint32(1) << quint32(10) << quint32(0);
    m_recordingStreamLengthOffset = m_recordingFile.pos(); out << quint32(0);
    out << quint32(0) << quint32(0xFFFFFFFF) << quint32(0)
        << qint16(0) << qint16(0) << qint16(m_recordingWidth) << qint16(m_recordingHeight);
    writeFourCc(out, "strf"); out << quint32(40) << quint32(40) << qint32(m_recordingWidth)
        << qint32(m_recordingHeight) << quint16(1) << quint16(24); writeFourCc(out, "MJPG");
    out << quint32(m_recordingWidth * m_recordingHeight * 3) << qint32(0) << qint32(0)
        << quint32(0) << quint32(0);
    writeFourCc(out, "LIST"); m_recordingMoviSizeOffset = m_recordingFile.pos(); out << quint32(0);
    writeFourCc(out, "movi"); m_recordingMoviDataOffset = m_recordingFile.pos();
    m_recording = true;
    emit recordingChanged();
    setStatusMessage(QStringLiteral("正在录像"));
    return true;
}

void SensorServiceController::appendRecordingFrame(const QByteArray &jpeg, int width, int height)
{
    if (!m_recording || width != m_recordingWidth || height != m_recordingHeight) return;
    const qint64 position = m_recordingFile.pos();
    QDataStream out(&m_recordingFile);
    out.setByteOrder(QDataStream::LittleEndian);
    writeFourCc(out, "00dc"); out << quint32(jpeg.size());
    m_recordingFile.write(jpeg);
    if (jpeg.size() & 1) m_recordingFile.putChar('\0');
    m_recordingFrames.append({static_cast<quint32>(position - m_recordingMoviDataOffset),
                              static_cast<quint32>(jpeg.size())});
}

bool SensorServiceController::finalizeRecording()
{
    if (!m_recording) return false;
    QDataStream out(&m_recordingFile);
    out.setByteOrder(QDataStream::LittleEndian);
    const qint64 indexPosition = m_recordingFile.pos();
    writeFourCc(out, "idx1"); out << quint32(m_recordingFrames.size() * 16);
    for (const AviFrame &frame : std::as_const(m_recordingFrames)) {
        writeFourCc(out, "00dc"); out << quint32(0x10) << frame.offset << frame.size;
    }
    const qint64 finalSize = m_recordingFile.pos();
    m_recordingFile.seek(m_recordingRiffSizeOffset); out << quint32(finalSize - 8);
    m_recordingFile.seek(m_recordingFrameCountOffset); out << quint32(m_recordingFrames.size());
    m_recordingFile.seek(m_recordingStreamLengthOffset); out << quint32(m_recordingFrames.size());
    m_recordingFile.seek(m_recordingMoviSizeOffset);
    out << quint32(indexPosition - (m_recordingMoviSizeOffset + 4));
    m_recordingFile.close();
    m_lastCapturePath = QDir::toNativeSeparators(m_recordingPath);
    emit captureSaved();
    m_recording = false;
    emit recordingChanged();
    setStatusMessage(m_recordingFrames.isEmpty()
                         ? QStringLiteral("录像已结束，但没有收到有效画面")
                         : QStringLiteral("录像已保存到 Captures 文件夹"));
    return !m_recordingFrames.isEmpty();
}

bool SensorServiceController::stopRecording()
{
    return finalizeRecording();
}

bool SensorServiceController::gimbalMoving() const
{
    if (!m_gimbalFeedbackAvailable) return false;
    return std::abs(std::remainder(m_gimbalTargetYawDegrees - m_gimbalYawDegrees, 360.0)) > 1.5
           || std::abs(m_gimbalPitchDegrees - m_gimbalTargetPitchDegrees) > 1.5;
}

bool SensorServiceController::setGimbalAngles(double yawDegrees,
                                              double pitchDegrees,
                                              double rollDegrees)
{
    if (m_sensorKind != QStringLiteral("gimbal-camera") || !m_running
        || m_gateway.state() != QAbstractSocket::ConnectedState) {
        setStatusMessage(QStringLiteral("云台数据服务连接后才能调整视角"));
        return false;
    }
    const double yaw = std::clamp(yawDegrees, -180.0, 180.0);
    const double pitch = std::clamp(pitchDegrees, -135.0, 45.0);
    const double roll = std::clamp(rollDegrees, -45.0, 45.0);
    QJsonObject command{
        {QStringLiteral("type"), QStringLiteral("gimbal-command")},
        {QStringLiteral("yaw"), yaw},
        {QStringLiteral("pitch"), pitch},
        {QStringLiteral("roll"), roll},
    };
    const QByteArray payload = QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n';
    if (m_gateway.write(payload) != payload.size()) {
        setStatusMessage(QStringLiteral("云台控制指令发送失败，请检查 ROS 2 数据服务"));
        return false;
    }
    m_gimbalTargetYawDegrees = yaw;
    m_gimbalTargetPitchDegrees = pitch;
    m_gimbalTargetRollDegrees = roll;
    emit gimbalStateChanged();
    setStatusMessage(QStringLiteral("云台正在转向：水平 %1°，俯仰 %2°")
                         .arg(QString::number(yaw, 'f', 0),
                              QString::number(pitch, 'f', 0)));
    return true;
}

void SensorServiceController::resetCamera()
{
    if (m_recording) finalizeRecording();
    const bool hadData = !m_rgbJpeg.isEmpty() || !m_depthJpeg.isEmpty();
    m_rgbJpeg.clear();
    m_depthJpeg.clear();
    m_rgbFrameDataUrlCache.clear();
    m_depthFrameDataUrlCache.clear();
    m_depthMillimetres.clear();
    m_depthTimestampEpochMs = 0;
    m_depthGridWidth = m_depthGridHeight = 0;
    m_rgbWidth = m_rgbHeight = 0;
    m_depthWidth = m_depthHeight = 0;
    m_rgbFrameHz = m_depthFrameHz = 0.0;
    m_rgbLatencyMilliseconds = m_depthLatencyMilliseconds = -1;
    m_depthCenterDistanceMeters = 0.0;
    m_depthNearestDistanceMeters = 0.0;
    m_depthValidRatio = 0.0;
    m_cameraStreamInterrupted = false;
    m_rgbStreamInterrupted = false;
    m_depthStreamInterrupted = false;
    m_lastCameraFrameTimer.invalidate();
    m_lastRgbFrameTimer.invalidate();
    m_lastDepthFrameTimer.invalidate();
    if (hadData) {
        emit cameraFrameChanged();
        emit depthFrameChanged();
    }
    emit cameraStateChanged();
}

void SensorServiceController::resetGimbal()
{
    const bool changed = m_gimbalFeedbackAvailable
                         || !qFuzzyIsNull(m_gimbalYawDegrees)
                         || !qFuzzyIsNull(m_gimbalPitchDegrees)
                         || !qFuzzyIsNull(m_gimbalRollDegrees)
                         || !qFuzzyIsNull(m_gimbalTargetYawDegrees)
                         || !qFuzzyIsNull(m_gimbalTargetPitchDegrees)
                         || !qFuzzyIsNull(m_gimbalTargetRollDegrees);
    m_gimbalYawDegrees = 0.0;
    m_gimbalPitchDegrees = 0.0;
    m_gimbalRollDegrees = 0.0;
    m_gimbalTargetYawDegrees = 0.0;
    m_gimbalTargetPitchDegrees = 0.0;
    m_gimbalTargetRollDegrees = 0.0;
    m_gimbalFeedbackAvailable = false;
    m_gimbalFeedbackClock.invalidate();
    if (changed) emit gimbalStateChanged();
}

void SensorServiceController::shutdownSynchronously()
{
    m_gateway.abort();
    if (m_process.state() != QProcess::NotRunning && !m_process.waitForFinished(2000)) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    if (!m_startedByUs || m_dockerPath.isEmpty()) return;
    QProcess::execute(m_dockerPath,
                      {QStringLiteral("stop"), QStringLiteral("-t"), QStringLiteral("5"),
                       QString::fromLatin1(ContainerName)});
    m_startedByUs = false;
}

void SensorServiceController::setImageAvailable(bool value)
{
    if (m_imageAvailable == value) return;
    m_imageAvailable = value;
    emit imageAvailableChanged();
}

void SensorServiceController::setRunning(bool value)
{
    if (m_running == value) return;
    m_running = value;
    if (!value) {
        m_monitorTimer.stop();
        m_reconnectTimer.stop();
        m_gateway.abort();
        setConnected(false);
    }
    emit runningChanged();
}

void SensorServiceController::setConnected(bool value)
{
    if (m_connected == value) return;
    m_connected = value;
    emit connectedChanged();
}

void SensorServiceController::setBusy(bool value)
{
    if (m_busy == value) return;
    m_busy = value;
    emit busyChanged();
}

void SensorServiceController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void SensorServiceController::setProblemCode(const QString &code)
{
    if (m_problemCode == code) return;
    m_problemCode = code;
    emit problemCodeChanged();
}
