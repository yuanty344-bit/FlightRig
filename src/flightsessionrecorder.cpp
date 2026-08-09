#include "flightsessionrecorder.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <cmath>

FlightSessionRecorder::FlightSessionRecorder(QObject *parent)
    : QObject(parent)
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    m_sessionsDirectory = QDir(documents).filePath(QStringLiteral("FlightBench/Sessions"));
    QDir().mkpath(m_sessionsDirectory);
    loadCompletedSessions();

    m_summaryTimer.setInterval(1000);
    connect(&m_summaryTimer, &QTimer::timeout, this, [this] {
        if (!m_active) return;
        refreshActiveSummary();
        emit activeSessionChanged();
    });
}

FlightSessionRecorder::~FlightSessionRecorder()
{
    finishSession(QStringLiteral("程序退出"));
}

QString FlightSessionRecorder::activeSessionId() const
{
    return m_metadata.value(QStringLiteral("id")).toString();
}

QVariantMap FlightSessionRecorder::activeSession() const
{
    if (!m_active) return {};
    QVariantMap summary = m_metadata;
    const int duration = static_cast<int>(m_startedAt.secsTo(QDateTime::currentDateTime()));
    summary.insert(QStringLiteral("durationSeconds"), std::max(0, duration));
    summary.insert(QStringLiteral("durationLabel"), formatDuration(std::max(0, duration)));
    summary.insert(QStringLiteral("maxAltitude"), m_maxAltitude);
    summary.insert(QStringLiteral("maxHorizontalSpeed"), m_maxHorizontalSpeed);
    summary.insert(QStringLiteral("maxVerticalSpeed"), m_maxVerticalSpeed);
    summary.insert(QStringLiteral("healthResult"), m_healthPassed ? QStringLiteral("已通过")
                                                                  : QStringLiteral("等待中"));
    summary.insert(QStringLiteral("takeoffResult"),
                   !m_takeoffRequested ? QStringLiteral("未执行")
                                       : m_sawInAir ? QStringLiteral("成功")
                                                    : QStringLiteral("进行中"));
    summary.insert(QStringLiteral("landingResult"),
                   !m_landingRequested ? QStringLiteral("未执行")
                                       : (!m_inAir && !m_armed) ? QStringLiteral("成功")
                                                                : QStringLiteral("进行中"));
    summary.insert(QStringLiteral("issueCount"), m_issueCount);
    summary.insert(QStringLiteral("sampleCount"), m_sampleCount);
    summary.insert(QStringLiteral("missionMaxCrossTrackError"), m_maxMissionCrossTrackError);
    summary.insert(QStringLiteral("missionCompletedItems"), m_missionCompletedItems);
    summary.insert(QStringLiteral("missionTotalItems"), m_missionTotalItems);
    summary.insert(QStringLiteral("missionCompletionPercent"),
                   m_missionTotalItems > 0
                       ? qRound(100.0 * m_missionCompletedItems / m_missionTotalItems)
                       : 0);
    summary.insert(QStringLiteral("missionInterruptionReason"), m_missionInterruptionReason);
    summary.insert(QStringLiteral("status"), QStringLiteral("记录中"));
    return summary;
}

bool FlightSessionRecorder::startSession(const QString &modelId,
                                         const QString &modelName,
                                         double takeoffAltitudeMeters)
{
    if (m_active) finishSession(QStringLiteral("开始新会话"));
    if (!QDir().mkpath(m_sessionsDirectory)) {
        emit recorderError(QStringLiteral("无法创建飞行会话目录：%1").arg(m_sessionsDirectory));
        return false;
    }

    m_startedAt = QDateTime::currentDateTime();
    const QString baseId = QStringLiteral("%1-%2")
                               .arg(m_startedAt.toString(QStringLiteral("yyyyMMdd-HHmmss")),
                                    safeFolderPart(modelName));
    QString sessionId = baseId;
    QString folderPath = QDir(m_sessionsDirectory).filePath(sessionId);
    for (int suffix = 2; QDir(folderPath).exists(); ++suffix) {
        sessionId = QStringLiteral("%1-%2").arg(baseId).arg(suffix);
        folderPath = QDir(m_sessionsDirectory).filePath(sessionId);
    }
    if (!QDir().mkpath(folderPath)) {
        emit recorderError(QStringLiteral("无法创建飞行会话：%1").arg(folderPath));
        return false;
    }

    const QString csvPath = QDir(folderPath).filePath(QStringLiteral("telemetry.csv"));
    const QString eventsPath = QDir(folderPath).filePath(QStringLiteral("events.log"));
    m_csvFile.setFileName(csvPath);
    m_eventsFile.setFileName(eventsPath);
    if (!m_csvFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)
        || !m_eventsFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit recorderError(QStringLiteral("无法创建会话数据文件：%1").arg(folderPath));
        m_csvFile.close();
        m_eventsFile.close();
        return false;
    }

    m_healthPassed = false;
    m_takeoffRequested = false;
    m_landingRequested = false;
    m_sawInAir = false;
    m_armed = false;
    m_inAir = false;
    m_metadataErrorReported = false;
    m_issueCount = 0;
    m_sampleCount = 0;
    m_maxAltitude = 0.0;
    m_maxHorizontalSpeed = 0.0;
    m_maxVerticalSpeed = 0.0;
    m_maxMissionCrossTrackError = 0.0;
    m_missionCompletedItems = 0;
    m_missionTotalItems = 0;
    m_missionInterruptionReason.clear();

    m_metadata = {
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("folderPath"), QDir::toNativeSeparators(folderPath)},
        {QStringLiteral("csvPath"), QDir::toNativeSeparators(csvPath)},
        {QStringLiteral("eventsPath"), QDir::toNativeSeparators(eventsPath)},
        {QStringLiteral("modelId"), modelId},
        {QStringLiteral("modelName"), modelName},
        {QStringLiteral("startedAt"), m_startedAt.toString(Qt::ISODate)},
        {QStringLiteral("startedLabel"), m_startedAt.toString(QStringLiteral("MM月dd日 HH:mm"))},
        {QStringLiteral("takeoffAltitude"), takeoffAltitudeMeters},
        {QStringLiteral("durationSeconds"), 0},
        {QStringLiteral("durationLabel"), QStringLiteral("00:00")},
        {QStringLiteral("maxAltitude"), 0.0},
        {QStringLiteral("maxHorizontalSpeed"), 0.0},
        {QStringLiteral("maxVerticalSpeed"), 0.0},
        {QStringLiteral("healthResult"), QStringLiteral("未通过")},
        {QStringLiteral("takeoffResult"), QStringLiteral("未执行")},
        {QStringLiteral("landingResult"), QStringLiteral("未执行")},
        {QStringLiteral("issueCount"), 0},
        {QStringLiteral("sampleCount"), 0},
        {QStringLiteral("missionMaxCrossTrackError"), 0.0},
        {QStringLiteral("missionCompletedItems"), 0},
        {QStringLiteral("missionTotalItems"), 0},
        {QStringLiteral("missionCompletionPercent"), 0},
        {QStringLiteral("missionInterruptionReason"), QString()},
        {QStringLiteral("status"), QStringLiteral("记录中")},
    };

    m_csvFile.write("elapsed_ms,timestamp_iso,model_id,connected,ready,armed,in_air,telemetry_available,altitude_m,horizontal_speed_m_s,vertical_speed_m_s,heading_deg,roll_deg,pitch_deg,yaw_deg,imu_available,gyro_roll_deg_s,gyro_pitch_deg_s,gyro_yaw_deg_s,gps_available,gps_fix,satellites,battery_available,battery_percent,battery_voltage_v,keyboard_control_active,command_forward_m_s,command_right_m_s,command_up_m_s,command_yaw_deg_s,precise_turn_active,precise_turn_target_deg,local_position_available,scene_x_east_m,scene_y_up_m,scene_z_south_m\n");
    m_csvFile.flush();
    m_active = true;
    m_summaryTimer.start();
    appendEvent(QStringLiteral("[%1] [会话] 开始记录 %2（%3），默认起飞高度 %4 米")
                    .arg(m_startedAt.toString(QStringLiteral("HH:mm:ss")),
                         modelName,
                         modelId,
                         QString::number(takeoffAltitudeMeters, 'f', 1)));
    writeMetadata();
    emit activeSessionChanged();
    return true;
}

void FlightSessionRecorder::finishSession(const QString &reason)
{
    if (!m_active) return;
    const QDateTime endedAt = QDateTime::currentDateTime();
    const int duration = std::max(0, static_cast<int>(m_startedAt.secsTo(endedAt)));
    const QString takeoffResult = !m_takeoffRequested ? QStringLiteral("未执行")
                                                      : m_sawInAir ? QStringLiteral("成功")
                                                                   : QStringLiteral("未完成");
    const QString landingResult = !m_landingRequested ? QStringLiteral("未执行")
                                                      : (!m_inAir && !m_armed) ? QStringLiteral("成功")
                                                                               : QStringLiteral("未完成");
    QString status = m_issueCount > 0 ? QStringLiteral("有异常") : QStringLiteral("完成");
    if (takeoffResult == QStringLiteral("未完成") || landingResult == QStringLiteral("未完成")) {
        status = QStringLiteral("未完成");
    }

    m_metadata.insert(QStringLiteral("endedAt"), endedAt.toString(Qt::ISODate));
    m_metadata.insert(QStringLiteral("durationSeconds"), duration);
    m_metadata.insert(QStringLiteral("durationLabel"), formatDuration(duration));
    m_metadata.insert(QStringLiteral("maxAltitude"), m_maxAltitude);
    m_metadata.insert(QStringLiteral("maxHorizontalSpeed"), m_maxHorizontalSpeed);
    m_metadata.insert(QStringLiteral("maxVerticalSpeed"), m_maxVerticalSpeed);
    m_metadata.insert(QStringLiteral("healthResult"), m_healthPassed ? QStringLiteral("已通过")
                                                                     : QStringLiteral("未通过"));
    m_metadata.insert(QStringLiteral("takeoffResult"), takeoffResult);
    m_metadata.insert(QStringLiteral("landingResult"), landingResult);
    m_metadata.insert(QStringLiteral("issueCount"), m_issueCount);
    m_metadata.insert(QStringLiteral("sampleCount"), m_sampleCount);
    m_metadata.insert(QStringLiteral("missionMaxCrossTrackError"), m_maxMissionCrossTrackError);
    m_metadata.insert(QStringLiteral("missionCompletedItems"), m_missionCompletedItems);
    m_metadata.insert(QStringLiteral("missionTotalItems"), m_missionTotalItems);
    m_metadata.insert(QStringLiteral("missionCompletionPercent"),
                      m_missionTotalItems > 0
                          ? qRound(100.0 * m_missionCompletedItems / m_missionTotalItems)
                          : 0);
    m_metadata.insert(QStringLiteral("missionInterruptionReason"), m_missionInterruptionReason);
    m_metadata.insert(QStringLiteral("status"), status);
    m_metadata.insert(QStringLiteral("endReason"), reason);
    appendEvent(QStringLiteral("[%1] [会话] 结束记录：%2")
                    .arg(endedAt.toString(QStringLiteral("HH:mm:ss")), reason));
    writeMetadata();

    m_summaryTimer.stop();
    m_csvFile.flush();
    m_eventsFile.flush();
    m_csvFile.close();
    m_eventsFile.close();
    m_completedSessions.prepend(m_metadata);
    while (m_completedSessions.size() > 50) m_completedSessions.removeLast();
    m_metadata.clear();
    m_active = false;
    emit activeSessionChanged();
    emit completedSessionsChanged();
}

void FlightSessionRecorder::appendEvent(const QString &entry)
{
    if (!m_active || entry.isEmpty() || !m_eventsFile.isOpen()) return;
    m_eventsFile.write(entry.toUtf8());
    m_eventsFile.write("\n");
    m_eventsFile.flush();
}

void FlightSessionRecorder::setMissionPlan(const QString &routeId,
                                           const QString &routeName,
                                           const QVariantList &relativeWaypoints,
                                           const QString &endAction)
{
    if (!m_active) return;
    m_metadata.insert(QStringLiteral("missionRouteId"), routeId);
    m_metadata.insert(QStringLiteral("missionRouteName"),
                      routeName.isEmpty() ? QStringLiteral("未命名航线") : routeName);
    m_metadata.insert(QStringLiteral("missionEndAction"), endAction);
    m_metadata.insert(QStringLiteral("missionWaypoints"), relativeWaypoints);
    appendEvent(QStringLiteral("[%1] [任务] 已保存执行航线快照：%2（%3 个航点）")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                         m_metadata.value(QStringLiteral("missionRouteName")).toString())
                    .arg(relativeWaypoints.size()));
    writeMetadata();
    emit activeSessionChanged();
}

void FlightSessionRecorder::updateMissionMetrics(double crossTrackErrorMeters,
                                                 int completedItems,
                                                 int totalItems,
                                                 const QString &interruptionReason)
{
    if (!m_active) return;
    m_maxMissionCrossTrackError = std::max(m_maxMissionCrossTrackError,
                                          std::max(0.0, crossTrackErrorMeters));
    m_missionTotalItems = std::max(m_missionTotalItems, std::max(0, totalItems));
    m_missionCompletedItems = std::max(m_missionCompletedItems,
                                       std::clamp(completedItems, 0, m_missionTotalItems));
    if (!interruptionReason.isEmpty()) m_missionInterruptionReason = interruptionReason;
}

void FlightSessionRecorder::markHealthReady()
{
    if (!m_active || m_healthPassed) return;
    m_healthPassed = true;
    appendEvent(QStringLiteral("[%1] [会话] 健康检查已稳定通过")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    emit activeSessionChanged();
}

void FlightSessionRecorder::markIssue(const QString &message)
{
    if (!m_active) return;
    ++m_issueCount;
    appendEvent(QStringLiteral("[%1] [问题] %2")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
    emit activeSessionChanged();
}

void FlightSessionRecorder::markTakeoffRequested()
{
    if (!m_active) return;
    m_takeoffRequested = true;
    appendEvent(QStringLiteral("[%1] [会话] 已请求起飞")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
}

void FlightSessionRecorder::markLandingRequested()
{
    if (!m_active) return;
    m_landingRequested = true;
    appendEvent(QStringLiteral("[%1] [会话] 已请求降落")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
}

void FlightSessionRecorder::updateFlightState(bool armed, bool inAir)
{
    if (!m_active) return;
    if (inAir && !m_sawInAir) {
        m_sawInAir = true;
        appendEvent(QStringLiteral("[%1] [会话] 已确认飞行器离地")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    }
    if (m_landingRequested && m_sawInAir && !inAir && !armed && (m_inAir || m_armed)) {
        appendEvent(QStringLiteral("[%1] [会话] 已确认降落并解除锁定")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    }
    m_armed = armed;
    m_inAir = inAir;
}

void FlightSessionRecorder::recordSample(bool connected,
                                         bool ready,
                                         bool armed,
                                         bool inAir,
                                         bool telemetryAvailable,
                                         double altitudeMeters,
                                         double horizontalSpeedMetersPerSecond,
                                         double verticalSpeedMetersPerSecond,
                                         double headingDegrees,
                                         double rollDegrees,
                                         double pitchDegrees,
                                         double yawDegrees,
                                         bool imuAvailable,
                                         double gyroRollDegreesPerSecond,
                                         double gyroPitchDegreesPerSecond,
                                         double gyroYawDegreesPerSecond,
                                         bool gpsAvailable,
                                         const QString &gpsFixType,
                                         int gpsSatellites,
                                         bool batteryAvailable,
                                         double batteryRemainingPercent,
                                         double batteryVoltageVolts,
                                         bool keyboardControlActive,
                                         double commandForwardMetersPerSecond,
                                         double commandRightMetersPerSecond,
                                         double commandUpMetersPerSecond,
                                         double commandYawDegreesPerSecond,
                                         bool preciseTurnActive,
                                         double preciseTurnTargetDegrees,
                                         bool localPositionAvailable,
                                         double sceneEastMeters,
                                         double sceneUpMeters,
                                         double sceneSouthMeters)
{
    if (!m_active || !m_csvFile.isOpen()) return;
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 elapsedMs = m_startedAt.msecsTo(now);
    m_maxAltitude = std::max(m_maxAltitude, std::max(0.0, altitudeMeters));
    m_maxHorizontalSpeed = std::max(m_maxHorizontalSpeed,
                                    std::abs(horizontalSpeedMetersPerSecond));
    m_maxVerticalSpeed = std::max(m_maxVerticalSpeed,
                                  std::abs(verticalSpeedMetersPerSecond));
    updateFlightState(armed, inAir);
    if (ready) m_healthPassed = true;

    const QStringList fields{
        QString::number(elapsedMs),
        csvField(now.toString(Qt::ISODateWithMs)),
        csvField(m_metadata.value(QStringLiteral("modelId")).toString()),
        connected ? QStringLiteral("1") : QStringLiteral("0"),
        ready ? QStringLiteral("1") : QStringLiteral("0"),
        armed ? QStringLiteral("1") : QStringLiteral("0"),
        inAir ? QStringLiteral("1") : QStringLiteral("0"),
        telemetryAvailable ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(altitudeMeters, 'f', 3),
        QString::number(horizontalSpeedMetersPerSecond, 'f', 3),
        QString::number(verticalSpeedMetersPerSecond, 'f', 3),
        QString::number(headingDegrees, 'f', 2),
        QString::number(rollDegrees, 'f', 3),
        QString::number(pitchDegrees, 'f', 3),
        QString::number(yawDegrees, 'f', 3),
        imuAvailable ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(gyroRollDegreesPerSecond, 'f', 3),
        QString::number(gyroPitchDegreesPerSecond, 'f', 3),
        QString::number(gyroYawDegreesPerSecond, 'f', 3),
        gpsAvailable ? QStringLiteral("1") : QStringLiteral("0"),
        csvField(gpsFixType),
        QString::number(gpsSatellites),
        batteryAvailable ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(batteryRemainingPercent, 'f', 2),
        QString::number(batteryVoltageVolts, 'f', 3),
        keyboardControlActive ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(commandForwardMetersPerSecond, 'f', 3),
        QString::number(commandRightMetersPerSecond, 'f', 3),
        QString::number(commandUpMetersPerSecond, 'f', 3),
        QString::number(commandYawDegreesPerSecond, 'f', 2),
        preciseTurnActive ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(preciseTurnTargetDegrees, 'f', 2),
        localPositionAvailable ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(sceneEastMeters, 'f', 3),
        QString::number(sceneUpMeters, 'f', 3),
        QString::number(sceneSouthMeters, 'f', 3),
    };
    m_csvFile.write(fields.join(QLatin1Char(',')).toUtf8());
    m_csvFile.write("\n");
    ++m_sampleCount;
    if (m_sampleCount % 5 == 0) m_csvFile.flush();
}

QVariantMap FlightSessionRecorder::sessionDetails(const QString &sessionId) const
{
    QVariantMap details = metadataForSession(sessionId);
    if (details.isEmpty()) return {};
    QFile eventsFile(details.value(QStringLiteral("eventsPath")).toString());
    QStringList events;
    if (eventsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&eventsFile);
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            if (!line.isEmpty()) events.append(line);
        }
    }
    details.insert(QStringLiteral("events"), events);
    return details;
}

QString FlightSessionRecorder::csvPathForSession(const QString &sessionId) const
{
    return metadataForSession(sessionId).value(QStringLiteral("csvPath")).toString();
}

QString FlightSessionRecorder::folderPathForSession(const QString &sessionId) const
{
    return metadataForSession(sessionId).value(QStringLiteral("folderPath")).toString();
}

void FlightSessionRecorder::loadCompletedSessions()
{
    m_completedSessions.clear();
    QDir root(m_sessionsDirectory);
    const QFileInfoList folders = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                      QDir::Time);
    for (const QFileInfo &folder : folders) {
        QFile metadataFile(QDir(folder.absoluteFilePath()).filePath(QStringLiteral("session.json")));
        if (!metadataFile.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll());
        if (!document.isObject()) continue;
        QVariantMap metadata = document.object().toVariantMap();
        if (metadata.value(QStringLiteral("id")).toString().isEmpty()) continue;
        // Sessions created before route reuse was introduced do not contain this key.
        // Normalizing it here keeps old local data compatible with the current QML model.
        if (!metadata.contains(QStringLiteral("missionRouteName")))
            metadata.insert(QStringLiteral("missionRouteName"), QString());
        if (metadata.value(QStringLiteral("status")).toString() == QStringLiteral("记录中")) {
            metadata.insert(QStringLiteral("status"), QStringLiteral("未完成"));
            metadata.insert(QStringLiteral("endReason"), QStringLiteral("上次会话未正常结束"));
        }
        m_completedSessions.append(metadata);
        if (m_completedSessions.size() >= 50) break;
    }
}

void FlightSessionRecorder::writeMetadata()
{
    if (m_metadata.isEmpty()) return;
    const QString folderPath = m_metadata.value(QStringLiteral("folderPath")).toString();
    QSaveFile file(QDir(folderPath).filePath(QStringLiteral("session.json")));
    const QByteArray contents = QJsonDocument(QJsonObject::fromVariantMap(m_metadata))
                                    .toJson(QJsonDocument::Indented);
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    const bool written = opened && file.write(contents) == contents.size();
    const bool committed = written && file.commit();
    if (!committed && !m_metadataErrorReported) {
        m_metadataErrorReported = true;
        emit recorderError(QStringLiteral("无法保存会话摘要：%1")
                               .arg(QDir(folderPath).filePath(QStringLiteral("session.json"))));
    }
}

void FlightSessionRecorder::refreshActiveSummary()
{
    if (!m_active) return;
    const int duration = std::max(0, static_cast<int>(m_startedAt.secsTo(QDateTime::currentDateTime())));
    m_metadata.insert(QStringLiteral("durationSeconds"), duration);
    m_metadata.insert(QStringLiteral("durationLabel"), formatDuration(duration));
    m_metadata.insert(QStringLiteral("maxAltitude"), m_maxAltitude);
    m_metadata.insert(QStringLiteral("maxHorizontalSpeed"), m_maxHorizontalSpeed);
    m_metadata.insert(QStringLiteral("maxVerticalSpeed"), m_maxVerticalSpeed);
    m_metadata.insert(QStringLiteral("healthResult"), m_healthPassed ? QStringLiteral("已通过")
                                                                     : QStringLiteral("等待中"));
    m_metadata.insert(QStringLiteral("issueCount"), m_issueCount);
    m_metadata.insert(QStringLiteral("sampleCount"), m_sampleCount);
    m_metadata.insert(QStringLiteral("missionMaxCrossTrackError"), m_maxMissionCrossTrackError);
    m_metadata.insert(QStringLiteral("missionCompletedItems"), m_missionCompletedItems);
    m_metadata.insert(QStringLiteral("missionTotalItems"), m_missionTotalItems);
    m_metadata.insert(QStringLiteral("missionCompletionPercent"),
                      m_missionTotalItems > 0
                          ? qRound(100.0 * m_missionCompletedItems / m_missionTotalItems)
                          : 0);
    m_metadata.insert(QStringLiteral("missionInterruptionReason"), m_missionInterruptionReason);
    writeMetadata();
}

QVariantMap FlightSessionRecorder::metadataForSession(const QString &sessionId) const
{
    if (m_active && activeSessionId() == sessionId) return activeSession();
    for (const QVariant &entry : m_completedSessions) {
        const QVariantMap metadata = entry.toMap();
        if (metadata.value(QStringLiteral("id")).toString() == sessionId) return metadata;
    }
    return {};
}

QString FlightSessionRecorder::formatDuration(int seconds)
{
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int remainingSeconds = seconds % 60;
    return hours > 0
               ? QStringLiteral("%1:%2:%3").arg(hours, 2, 10, QLatin1Char('0'))
                     .arg(minutes, 2, 10, QLatin1Char('0'))
                     .arg(remainingSeconds, 2, 10, QLatin1Char('0'))
               : QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0'))
                     .arg(remainingSeconds, 2, 10, QLatin1Char('0'));
}

QString FlightSessionRecorder::csvField(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString FlightSessionRecorder::safeFolderPart(const QString &value)
{
    QString result;
    for (const QChar character : value) {
        if (character.isLetterOrNumber()) result.append(character);
        else if (!result.endsWith(QLatin1Char('-'))) result.append(QLatin1Char('-'));
    }
    while (result.endsWith(QLatin1Char('-'))) result.chop(1);
    return result.isEmpty() ? QStringLiteral("vehicle") : result;
}
