#include "sessions/flightsessionrecorder.h"
#include "sessions/sessionarchive.h"
#include "testing/acceptanceevaluator.h"
#include "mission/routedeviationanalyzer.h"
#include "infrastructure/storagepaths.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <utility>

FlightSessionRecorder::FlightSessionRecorder(QObject *parent,
                                             const QString &sessionsDirectory)
    : QObject(parent)
{
    m_sessionsDirectory = sessionsDirectory.isEmpty()
                              ? StoragePaths::directory(QStringLiteral("Sessions"))
                              : QDir::cleanPath(QFileInfo(sessionsDirectory).absoluteFilePath());
    m_trashDirectory = QDir(QFileInfo(m_sessionsDirectory).absolutePath())
                           .filePath(QStringLiteral("Trash"));
    QDir().mkpath(m_sessionsDirectory);
    QDir().mkpath(m_trashDirectory);
    loadCompletedSessions();
    loadTrashedSessions();

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
                   m_sawInAir ? QStringLiteral("成功")
                              : !m_takeoffRequested ? QStringLiteral("未执行")
                                                    : QStringLiteral("进行中"));
    summary.insert(QStringLiteral("landingResult"),
                   m_landingRequested
                       ? (!m_inAir && !m_armed ? QStringLiteral("成功")
                                              : QStringLiteral("进行中"))
                       : (m_sawInAir && !m_inAir && !m_armed ? QStringLiteral("成功")
                                                             : QStringLiteral("未执行")));
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
    qint64 currentMaximumGapMs = m_maxTelemetryGapMs;
    if (m_telemetryGapStartedMs >= 0 && m_lastSampleElapsedMs >= m_telemetryGapStartedMs)
        currentMaximumGapMs = std::max(currentMaximumGapMs,
                                       m_lastSampleElapsedMs - m_telemetryGapStartedMs);
    summary.insert(QStringLiteral("maximumTelemetryGapSeconds"),
                   currentMaximumGapMs / 1000.0);
    summary.insert(QStringLiteral("obstacleClearanceAvailable"),
                   m_obstacleClearanceAvailable);
    summary.insert(QStringLiteral("minimumObstacleClearanceMeters"),
                   m_minimumObstacleClearanceMeters);
    summary.insert(QStringLiteral("obstacleInfringementCount"),
                   m_obstacleInfringementCount);
    summary.insert(QStringLiteral("status"), QStringLiteral("记录中"));
    return summary;
}

bool FlightSessionRecorder::startSession(const QString &modelId,
                                         const QString &modelName,
                                         double takeoffAltitudeMeters,
                                         const QString &worldId,
                                         const QString &worldName,
                                         const QString &applicationVersion,
                                         const QString &simulationImage,
                                         const QString &qtVersion,
                                         const QString &mavsdkVersion)
{
    if (m_active) finishSession(QStringLiteral("开始新的飞行记录"));
    if (!QDir().mkpath(m_sessionsDirectory)) {
        emit recorderError(QStringLiteral("无法创建飞行记录文件夹：%1").arg(m_sessionsDirectory));
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
        emit recorderError(QStringLiteral("无法创建飞行记录：%1").arg(folderPath));
        return false;
    }

    const QString csvPath = QDir(folderPath).filePath(QStringLiteral("telemetry.csv"));
    const QString eventsPath = QDir(folderPath).filePath(QStringLiteral("events.log"));
    m_csvFile.setFileName(csvPath);
    m_eventsFile.setFileName(eventsPath);
    if (!m_csvFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)
        || !m_eventsFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit recorderError(QStringLiteral("无法创建飞行记录数据文件：%1").arg(folderPath));
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
    m_lastSampleElapsedMs = -1;
    m_sawTelemetry = false;
    m_telemetryGapStartedMs = -1;
    m_maxTelemetryGapMs = 0;
    m_obstacleClearanceAvailable = false;
    m_minimumObstacleClearanceMeters = 0.0;
    m_obstacleInfringementCount = 0;
    m_lastObstacleInfringed = false;
    m_missionCompletedItems = 0;
    m_missionTotalItems = 0;
    m_missionInterruptionReason.clear();

    m_metadata = {
        {QStringLiteral("format"), QStringLiteral("FlightBenchSession")},
        {QStringLiteral("formatVersion"), 5},
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("folderPath"), QDir::toNativeSeparators(folderPath)},
        {QStringLiteral("csvPath"), QDir::toNativeSeparators(csvPath)},
        {QStringLiteral("eventsPath"), QDir::toNativeSeparators(eventsPath)},
        {QStringLiteral("modelId"), modelId},
        {QStringLiteral("modelName"), modelName},
        {QStringLiteral("worldId"), worldId},
        {QStringLiteral("worldName"), worldName},
        {QStringLiteral("displayName"), QString()},
        {QStringLiteral("notes"), QString()},
        {QStringLiteral("applicationVersion"), applicationVersion},
        {QStringLiteral("qtVersion"), qtVersion},
        {QStringLiteral("mavsdkVersion"), mavsdkVersion},
        {QStringLiteral("simulationImage"), simulationImage},
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
        {QStringLiteral("maximumTelemetryGapSeconds"), 0.0},
        {QStringLiteral("obstacleClearanceAvailable"), false},
        {QStringLiteral("minimumObstacleClearanceMeters"), 0.0},
        {QStringLiteral("obstacleInfringementCount"), 0},
        {QStringLiteral("testPlanId"), QString()},
        {QStringLiteral("testPlanName"), QString()},
        {QStringLiteral("testPlanDescription"), QString()},
        {QStringLiteral("testPlanType"), QString()},
        {QStringLiteral("testPlanTypeLabel"), QString()},
        {QStringLiteral("testPlanEndAction"), QStringLiteral("hold")},
        {QStringLiteral("testPlanAcceptancePreset"), QStringLiteral("standard")},
        {QStringLiteral("testPlanAcceptanceRules"), QVariantMap{}},
        {QStringLiteral("testPlanControlSettings"), QVariantMap{}},
        {QStringLiteral("testPlanSensorSettings"), QVariantMap{}},
        {QStringLiteral("sensorRecording"), QVariantMap{}},
        {QStringLiteral("testPlanBuiltIn"), false},
        {QStringLiteral("testResult"), QStringLiteral("running")},
        {QStringLiteral("testResultLabel"), QStringLiteral("进行中")},
        {QStringLiteral("testResultReason"), QStringLiteral("测试正在进行")},
        {QStringLiteral("status"), QStringLiteral("记录中")},
    };

    m_csvFile.write("elapsed_ms,timestamp_iso,model_id,connected,ready,armed,in_air,telemetry_available,altitude_m,horizontal_speed_m_s,vertical_speed_m_s,heading_deg,roll_deg,pitch_deg,yaw_deg,imu_available,gyro_roll_deg_s,gyro_pitch_deg_s,gyro_yaw_deg_s,gps_available,gps_fix,satellites,battery_available,battery_percent,battery_voltage_v,keyboard_control_active,command_forward_m_s,command_right_m_s,command_up_m_s,command_yaw_deg_s,precise_turn_active,precise_turn_target_deg,local_position_available,scene_x_east_m,scene_y_up_m,scene_z_south_m\n");
    m_csvFile.flush();
    m_active = true;
    m_summaryTimer.start();
    appendEvent(QStringLiteral("[%1] [记录] 开始记录 %2（%3），默认起飞高度 %4 米")
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
    const QString takeoffResult = m_sawInAir ? QStringLiteral("成功")
                                             : !m_takeoffRequested ? QStringLiteral("未执行")
                                                                   : QStringLiteral("未完成");
    const QString landingResult = m_sawInAir
                                      ? (!m_inAir && !m_armed ? QStringLiteral("成功")
                                                               : QStringLiteral("未完成"))
                                      : (!m_landingRequested ? QStringLiteral("未执行")
                                                              : QStringLiteral("未完成"));
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
    if (m_telemetryGapStartedMs >= 0)
        m_maxTelemetryGapMs = std::max(m_maxTelemetryGapMs,
                                       m_lastSampleElapsedMs - m_telemetryGapStartedMs);
    m_metadata.insert(QStringLiteral("maximumTelemetryGapSeconds"),
                      m_maxTelemetryGapMs / 1000.0);
    m_metadata.insert(QStringLiteral("obstacleClearanceAvailable"),
                      m_obstacleClearanceAvailable);
    m_metadata.insert(QStringLiteral("minimumObstacleClearanceMeters"),
                      m_minimumObstacleClearanceMeters);
    m_metadata.insert(QStringLiteral("obstacleInfringementCount"),
                      m_obstacleInfringementCount);
    m_metadata.insert(QStringLiteral("endedArmed"), m_armed);
    m_metadata.insert(QStringLiteral("endedInAir"), m_inAir);
    m_metadata.insert(QStringLiteral("endReason"), reason);
    const QVariantMap evaluation = AcceptanceEvaluator::evaluate(m_metadata);
    m_metadata.insert(QStringLiteral("testResult"),
                      evaluation.value(QStringLiteral("result")));
    m_metadata.insert(QStringLiteral("testResultLabel"),
                      evaluation.value(QStringLiteral("resultLabel")));
    m_metadata.insert(QStringLiteral("testResultReason"),
                      evaluation.value(QStringLiteral("reason")));
    m_metadata.insert(QStringLiteral("acceptanceChecks"),
                      evaluation.value(QStringLiteral("checks")));
    m_metadata.insert(QStringLiteral("acceptancePassedCount"),
                      evaluation.value(QStringLiteral("passedCount")));
    m_metadata.insert(QStringLiteral("acceptanceWarningCount"),
                      evaluation.value(QStringLiteral("warningCount")));
    m_metadata.insert(QStringLiteral("acceptanceFailedCount"),
                      evaluation.value(QStringLiteral("failedCount")));
    m_metadata.insert(QStringLiteral("acceptancePresetLabel"),
                      evaluation.value(QStringLiteral("presetLabel")));
    m_csvFile.flush();
    if (!m_metadata.value(QStringLiteral("missionWaypoints")).toList().isEmpty()) {
        const QVariantMap routeAnalysis = RouteDeviationAnalyzer::analyze(
            m_metadata.value(QStringLiteral("csvPath")).toString(),
            m_metadata.value(QStringLiteral("missionWaypoints")).toList(),
            m_metadata.value(QStringLiteral("missionStartEastMeters")).toDouble(),
            m_metadata.value(QStringLiteral("missionStartUpMeters")).toDouble(),
            m_metadata.value(QStringLiteral("missionStartSouthMeters")).toDouble(),
            m_metadata.value(QStringLiteral("missionStartElapsedMs"), -1).toLongLong(),
            m_metadata.value(QStringLiteral("missionEndElapsedMs"), -1).toLongLong());
        if (routeAnalysis.value(QStringLiteral("available")).toBool())
            m_metadata.insert(QStringLiteral("routeAnalysis"), routeAnalysis);
    }
    m_metadata.insert(QStringLiteral("status"), status);
    appendEvent(QStringLiteral("[%1] [记录] 结束记录：%2")
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
                                           const QString &endAction,
                                           double startEastMeters,
                                           double startUpMeters,
                                           double startSouthMeters)
{
    if (!m_active) return;
    m_metadata.insert(QStringLiteral("missionRouteId"), routeId);
    m_metadata.insert(QStringLiteral("missionRouteName"),
                      routeName.isEmpty() ? QStringLiteral("未命名航线") : routeName);
    m_metadata.insert(QStringLiteral("missionEndAction"), endAction);
    m_metadata.insert(QStringLiteral("missionWaypoints"), relativeWaypoints);
    m_metadata.insert(QStringLiteral("missionSnapshotVersion"), 1);
    m_metadata.insert(QStringLiteral("missionStartElapsedMs"),
                      std::max<qint64>(0, m_startedAt.msecsTo(QDateTime::currentDateTime())));
    m_metadata.insert(QStringLiteral("missionStartEastMeters"), startEastMeters);
    m_metadata.insert(QStringLiteral("missionStartUpMeters"), startUpMeters);
    m_metadata.insert(QStringLiteral("missionStartSouthMeters"), startSouthMeters);
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
    const bool missionFinished = m_missionTotalItems > 0
                                 && m_missionCompletedItems >= m_missionTotalItems;
    if ((missionFinished || !interruptionReason.isEmpty())
        && !m_metadata.contains(QStringLiteral("missionEndElapsedMs"))) {
        m_metadata.insert(QStringLiteral("missionEndElapsedMs"),
                          std::max<qint64>(0,
                              m_startedAt.msecsTo(QDateTime::currentDateTime())));
    }
}

void FlightSessionRecorder::updateObstacleClearance(bool available,
                                                    double clearanceMeters,
                                                    bool infringed)
{
    if (!m_active || !available || !std::isfinite(clearanceMeters)) return;
    const double normalizedClearance = std::max(0.0, clearanceMeters);
    if (!m_obstacleClearanceAvailable) {
        m_minimumObstacleClearanceMeters = normalizedClearance;
    } else {
        m_minimumObstacleClearanceMeters = std::min(
            m_minimumObstacleClearanceMeters, normalizedClearance);
    }
    m_obstacleClearanceAvailable = true;
    if (infringed && !m_lastObstacleInfringed) {
        ++m_obstacleInfringementCount;
        appendEvent(QStringLiteral("[%1] [安全] 飞行轨迹进入建筑安全范围")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    }
    m_lastObstacleInfringed = infringed;
}

void FlightSessionRecorder::markHealthReady()
{
    if (!m_active || m_healthPassed) return;
    m_healthPassed = true;
    appendEvent(QStringLiteral("[%1] [记录] 起飞检查已稳定通过")
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
    appendEvent(QStringLiteral("[%1] [记录] 已请求起飞")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
}

void FlightSessionRecorder::markLandingRequested()
{
    if (!m_active) return;
    m_landingRequested = true;
    appendEvent(QStringLiteral("[%1] [记录] 已请求降落")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
}

void FlightSessionRecorder::updateFlightState(bool armed, bool inAir)
{
    if (!m_active) return;
    if (inAir && !m_sawInAir) {
        m_sawInAir = true;
        appendEvent(QStringLiteral("[%1] [记录] 已确认飞行器离地")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    }
    if (m_landingRequested && m_sawInAir && !inAir && !armed && (m_inAir || m_armed)) {
        appendEvent(QStringLiteral("[%1] [记录] 已确认降落并解除锁定")
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
    m_lastSampleElapsedMs = elapsedMs;
    if (!telemetryAvailable && m_sawTelemetry) {
        if (m_telemetryGapStartedMs < 0) m_telemetryGapStartedMs = elapsedMs;
        m_maxTelemetryGapMs = std::max(m_maxTelemetryGapMs,
                                       elapsedMs - m_telemetryGapStartedMs);
    } else if (telemetryAvailable) {
        if (m_telemetryGapStartedMs >= 0) {
            m_maxTelemetryGapMs = std::max(m_maxTelemetryGapMs,
                                           elapsedMs - m_telemetryGapStartedMs);
        }
        m_telemetryGapStartedMs = -1;
        m_sawTelemetry = true;
    }
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

void FlightSessionRecorder::setTestPlan(const QVariantMap &plan)
{
    if (!m_active || plan.isEmpty()) return;
    m_metadata.insert(QStringLiteral("testPlanId"),
                      plan.value(QStringLiteral("id")).toString());
    m_metadata.insert(QStringLiteral("testPlanName"),
                      plan.value(QStringLiteral("name")).toString());
    m_metadata.insert(QStringLiteral("testPlanDescription"),
                      plan.value(QStringLiteral("description")).toString());
    m_metadata.insert(QStringLiteral("testPlanType"),
                      plan.value(QStringLiteral("type"), QStringLiteral("free-flight")));
    m_metadata.insert(QStringLiteral("testPlanTypeLabel"),
                      plan.value(QStringLiteral("typeLabel"), QStringLiteral("自由飞行")));
    m_metadata.insert(QStringLiteral("testPlanEndAction"),
                      plan.value(QStringLiteral("endAction"), QStringLiteral("hold")));
    m_metadata.insert(QStringLiteral("testPlanAcceptancePreset"),
                      plan.value(QStringLiteral("acceptancePreset"), QStringLiteral("standard")));
    m_metadata.insert(QStringLiteral("testPlanAcceptanceRules"),
                      plan.value(QStringLiteral("acceptanceRules"), QVariantMap{}));
    m_metadata.insert(QStringLiteral("testPlanControlSettings"),
                      plan.value(QStringLiteral("controlSettings"), QVariantMap{}));
    m_metadata.insert(QStringLiteral("testPlanSensorSettings"),
                      plan.value(QStringLiteral("sensorSettings"), QVariantMap{}));
    m_metadata.insert(QStringLiteral("testPlanBuiltIn"),
                      plan.value(QStringLiteral("builtIn"), false));
    appendEvent(QStringLiteral("[%1] [测试方案] %2（%3）")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                         plan.value(QStringLiteral("name")).toString(),
                         plan.value(QStringLiteral("typeLabel")).toString()));
    writeMetadata();
    emit activeSessionChanged();
}

void FlightSessionRecorder::setSensorSummary(const QVariantMap &summary)
{
    if (!m_active) return;
    m_metadata.insert(QStringLiteral("sensorRecording"), summary);
    writeMetadata();
    emit activeSessionChanged();
}

QString FlightSessionRecorder::csvPathForSession(const QString &sessionId) const
{
    return metadataForSession(sessionId).value(QStringLiteral("csvPath")).toString();
}

QString FlightSessionRecorder::folderPathForSession(const QString &sessionId) const
{
    return metadataForSession(sessionId).value(QStringLiteral("folderPath")).toString();
}

bool FlightSessionRecorder::updateSessionDetails(const QString &sessionId,
                                                 const QString &displayName,
                                                 const QString &notes)
{
    const QString normalizedName = displayName.trimmed().left(60);
    const QString normalizedNotes = notes.trimmed().left(500);
    if (sessionId.isEmpty()) {
        emit recorderError(QStringLiteral("没有找到要编辑的飞行记录"));
        return false;
    }

    if (m_active && activeSessionId() == sessionId) {
        m_metadata.insert(QStringLiteral("displayName"), normalizedName);
        m_metadata.insert(QStringLiteral("notes"), normalizedNotes);
        m_metadata.insert(QStringLiteral("editedAt"),
                          QDateTime::currentDateTime().toString(Qt::ISODate));
        writeMetadata();
        emit activeSessionChanged();
        return true;
    }

    for (qsizetype index = 0; index < m_completedSessions.size(); ++index) {
        QVariantMap metadata = m_completedSessions.at(index).toMap();
        if (metadata.value(QStringLiteral("id")).toString() != sessionId) continue;

        metadata.insert(QStringLiteral("displayName"), normalizedName);
        metadata.insert(QStringLiteral("notes"), normalizedNotes);
        metadata.insert(QStringLiteral("editedAt"),
                        QDateTime::currentDateTime().toString(Qt::ISODate));
        const QString folderPath = metadata.value(QStringLiteral("folderPath")).toString();
        QSaveFile file(QDir(folderPath).filePath(QStringLiteral("session.json")));
        const QByteArray contents = QJsonDocument(QJsonObject::fromVariantMap(metadata))
                                        .toJson(QJsonDocument::Indented);
        const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        const bool written = opened && file.write(contents) == contents.size();
        if (!written || !file.commit()) {
            emit recorderError(QStringLiteral("无法保存飞行记录名称和备注"));
            return false;
        }
        m_completedSessions[index] = metadata;
        emit completedSessionsChanged();
        return true;
    }

    emit recorderError(QStringLiteral("没有找到要编辑的飞行记录"));
    return false;
}

bool FlightSessionRecorder::setSessionReport(const QString &sessionId,
                                             const QString &reportPath)
{
    const QFileInfo report(reportPath);
    if (sessionId.isEmpty() || !report.isFile()) {
        emit recorderError(QStringLiteral("无法登记测试报告：报告文件不存在"));
        return false;
    }

    for (qsizetype index = 0; index < m_completedSessions.size(); ++index) {
        QVariantMap metadata = m_completedSessions.at(index).toMap();
        if (metadata.value(QStringLiteral("id")).toString() != sessionId) continue;
        metadata.insert(QStringLiteral("reportPath"),
                        QDir::toNativeSeparators(report.absoluteFilePath()));
        metadata.insert(QStringLiteral("reportFileName"), report.fileName());
        metadata.insert(QStringLiteral("reportGeneratedAt"),
                        QDateTime::currentDateTime().toString(Qt::ISODate));
        metadata.insert(QStringLiteral("reportFormatVersion"), 1);
        const QString folderPath = metadata.value(QStringLiteral("folderPath")).toString();
        if (!saveStandaloneMetadata(metadata, folderPath)) {
            emit recorderError(QStringLiteral("无法把测试报告信息写入飞行记录摘要"));
            return false;
        }
        m_completedSessions[index] = metadata;
        emit completedSessionsChanged();
        return true;
    }

    emit recorderError(QStringLiteral("无法登记测试报告：没有找到对应的飞行记录"));
    return false;
}

bool FlightSessionRecorder::updateSessionSensorSummary(const QString &sessionId,
                                                        const QVariantMap &summary)
{
    for (qsizetype index = 0; index < m_completedSessions.size(); ++index) {
        QVariantMap metadata = m_completedSessions.at(index).toMap();
        if (metadata.value(QStringLiteral("id")).toString() != sessionId) continue;
        metadata.insert(QStringLiteral("sensorRecording"), summary);
        const QString folder = metadata.value(QStringLiteral("folderPath")).toString();
        if (!saveStandaloneMetadata(metadata, folder)) {
            emit recorderError(QStringLiteral("无法更新飞行记录中的视觉巡检备注"));
            return false;
        }
        m_completedSessions[index] = metadata;
        emit completedSessionsChanged();
        return true;
    }
    return false;
}

bool FlightSessionRecorder::deleteSession(const QString &sessionId)
{
    if (sessionId.isEmpty() || (m_active && activeSessionId() == sessionId)) {
        emit recorderError(QStringLiteral("正在生成的飞行记录不能删除"));
        return false;
    }

    const QVariantMap metadata = metadataForSession(sessionId);
    if (metadata.isEmpty()) {
        emit recorderError(QStringLiteral("没有找到要删除的飞行记录"));
        return false;
    }

    const QString rootPath = QFileInfo(m_sessionsDirectory).canonicalFilePath();
    const QString folderPath = QFileInfo(metadata.value(QStringLiteral("folderPath")).toString())
                                   .canonicalFilePath();
    if (rootPath.isEmpty() || folderPath.isEmpty()
        || QFileInfo(folderPath).dir().canonicalPath() != rootPath
        || QFileInfo(folderPath).fileName() != sessionId) {
        emit recorderError(QStringLiteral("飞行记录文件夹校验失败，已取消删除"));
        return false;
    }

    const QString trashTarget = QDir(m_trashDirectory).filePath(sessionId);
    if (QFileInfo::exists(trashTarget)) {
        emit recorderError(QStringLiteral("回收站里已有同名飞行记录，请先处理后再重试"));
        return false;
    }
    if (!QDir().rename(folderPath, trashTarget)) {
        emit recorderError(QStringLiteral("无法把飞行记录移到回收站：%1")
                               .arg(QDir::toNativeSeparators(folderPath)));
        return false;
    }

    QVariantMap trashedMetadata = metadata;
    trashedMetadata.insert(QStringLiteral("trashedAt"),
                           QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!saveStandaloneMetadata(trashedMetadata, trashTarget)) {
        const bool rolledBack = QDir().rename(trashTarget, folderPath);
        emit recorderError(rolledBack
                               ? QStringLiteral("无法保存回收站中的飞行记录信息，已恢复原记录")
                               : QStringLiteral("无法保存回收站中的飞行记录信息，且自动恢复文件夹失败；数据仍保留在回收站文件夹"));
        return false;
    }

    for (qsizetype index = 0; index < m_completedSessions.size(); ++index) {
        if (m_completedSessions.at(index).toMap().value(QStringLiteral("id")).toString()
            == sessionId) {
            m_completedSessions.removeAt(index);
            break;
        }
    }
    m_trashedSessions.prepend(trashedMetadata);
    emit completedSessionsChanged();
    emit trashedSessionsChanged();
    return true;
}

bool FlightSessionRecorder::restoreSession(const QString &sessionId)
{
    for (qsizetype index = 0; index < m_trashedSessions.size(); ++index) {
        QVariantMap metadata = m_trashedSessions.at(index).toMap();
        if (metadata.value(QStringLiteral("id")).toString() != sessionId) continue;
        const QString source = QFileInfo(metadata.value(QStringLiteral("folderPath")).toString())
                                   .canonicalFilePath();
        const QString trashRoot = QFileInfo(m_trashDirectory).canonicalFilePath();
        const QString destination = QDir(m_sessionsDirectory).filePath(sessionId);
        if (source.isEmpty() || trashRoot.isEmpty()
            || QFileInfo(source).dir().canonicalPath() != trashRoot
            || QFileInfo::exists(destination)) {
            emit recorderError(QStringLiteral("飞行记录恢复路径校验失败"));
            return false;
        }
        if (!QDir().rename(source, destination)) {
            emit recorderError(QStringLiteral("无法恢复这条飞行记录"));
            return false;
        }
        metadata.remove(QStringLiteral("trashedAt"));
        if (!saveStandaloneMetadata(metadata, destination)) {
            const bool rolledBack = QDir().rename(destination, source);
            emit recorderError(rolledBack
                                   ? QStringLiteral("无法保存恢复后的飞行记录信息，已放回回收站")
                                   : QStringLiteral("无法保存恢复后的飞行记录信息，且自动放回回收站失败；数据仍保留在飞行记录文件夹"));
            return false;
        }
        m_trashedSessions.removeAt(index);
        m_completedSessions.prepend(metadata);
        emit trashedSessionsChanged();
        emit completedSessionsChanged();
        return true;
    }
    emit recorderError(QStringLiteral("回收站中没有找到这条飞行记录"));
    return false;
}

bool FlightSessionRecorder::permanentlyDeleteSession(const QString &sessionId)
{
    for (qsizetype index = 0; index < m_trashedSessions.size(); ++index) {
        const QVariantMap metadata = m_trashedSessions.at(index).toMap();
        if (metadata.value(QStringLiteral("id")).toString() != sessionId) continue;
        const QString folderPath = QFileInfo(metadata.value(QStringLiteral("folderPath")).toString())
                                       .canonicalFilePath();
        const QString trashRoot = QFileInfo(m_trashDirectory).canonicalFilePath();
        if (folderPath.isEmpty() || trashRoot.isEmpty()
            || QFileInfo(folderPath).dir().canonicalPath() != trashRoot) {
            emit recorderError(QStringLiteral("回收站目录校验失败，已取消永久删除"));
            return false;
        }
        if (!QDir(folderPath).removeRecursively()) {
            emit recorderError(QStringLiteral("无法永久删除飞行记录"));
            return false;
        }
        m_trashedSessions.removeAt(index);
        emit trashedSessionsChanged();
        return true;
    }
    emit recorderError(QStringLiteral("回收站中没有找到这条飞行记录"));
    return false;
}

bool FlightSessionRecorder::deleteSessionSensorData(const QString &sessionId)
{
    if (sessionId.isEmpty() || (m_active && activeSessionId() == sessionId)) return false;
    QVariantMap metadata = metadataForSession(sessionId);
    if (metadata.isEmpty()) return false;
    const QString folderPath = QDir::cleanPath(
        metadata.value(QStringLiteral("folderPath")).toString());
    const QString sensorPath = QDir(folderPath).filePath(QStringLiteral("sensor"));
    const QFileInfo folderInfo(folderPath);
    const QFileInfo sensorInfo(sensorPath);
    const QString normalizedFolderPath = QDir::fromNativeSeparators(
        QDir::cleanPath(folderInfo.absoluteFilePath()));
    const QString normalizedSensorPath = QDir::fromNativeSeparators(
        QDir::cleanPath(sensorInfo.absoluteFilePath()));
    if (!folderInfo.exists() || !sensorInfo.exists() || !sensorInfo.isDir()
        || !normalizedSensorPath.startsWith(normalizedFolderPath + QLatin1Char('/'))) {
        return false;
    }
    if (!QDir(sensorInfo.absoluteFilePath()).removeRecursively()) return false;
    metadata.insert(QStringLiteral("sensorRecording"),
                    QVariantMap{{QStringLiteral("format"), QStringLiteral("FlightBenchSensorSession")},
                                {QStringLiteral("formatVersion"), 2},
                                {QStringLiteral("available"), false},
                                {QStringLiteral("deleted"), true},
                                {QStringLiteral("statusLabel"), QStringLiteral("传感器数据已删除")}});
    if (!saveStandaloneMetadata(metadata, folderPath)) return false;
    loadCompletedSessions();
    emit completedSessionsChanged();
    return true;
}

int FlightSessionRecorder::purgeTrashOlderThan(int retentionDays)
{
    if (retentionDays <= 0) return 0;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-retentionDays);
    QStringList expiredIds;
    for (const QVariant &entry : std::as_const(m_trashedSessions)) {
        const QVariantMap metadata = entry.toMap();
        QDateTime trashedAt = QDateTime::fromString(
            metadata.value(QStringLiteral("trashedAt")).toString(), Qt::ISODate);
        if (!trashedAt.isValid()) {
            const QFileInfo folder(metadata.value(QStringLiteral("folderPath")).toString());
            trashedAt = folder.lastModified();
        }
        if (trashedAt.isValid() && trashedAt < cutoff)
            expiredIds.append(metadata.value(QStringLiteral("id")).toString());
    }

    int removed = 0;
    for (const QString &id : std::as_const(expiredIds)) {
        if (permanentlyDeleteSession(id)) ++removed;
    }
    return removed;
}

int FlightSessionRecorder::clearTrash()
{
    QStringList ids;
    ids.reserve(m_trashedSessions.size());
    for (const QVariant &entry : std::as_const(m_trashedSessions))
        ids.append(entry.toMap().value(QStringLiteral("id")).toString());

    int removed = 0;
    for (const QString &id : std::as_const(ids)) {
        if (permanentlyDeleteSession(id)) ++removed;
    }
    return removed;
}

bool FlightSessionRecorder::saveStandaloneMetadata(QVariantMap &metadata,
                                                    const QString &folderPath) const
{
    return SessionArchive::saveMetadata(metadata, folderPath);
}

void FlightSessionRecorder::loadCompletedSessions()
{
    m_completedSessions = SessionArchive::loadCompleted(m_sessionsDirectory);
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
        emit recorderError(QStringLiteral("无法保存飞行记录摘要：%1")
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
    m_metadata.insert(QStringLiteral("maximumTelemetryGapSeconds"),
                      m_maxTelemetryGapMs / 1000.0);
    m_metadata.insert(QStringLiteral("obstacleClearanceAvailable"),
                      m_obstacleClearanceAvailable);
    m_metadata.insert(QStringLiteral("minimumObstacleClearanceMeters"),
                      m_minimumObstacleClearanceMeters);
    m_metadata.insert(QStringLiteral("obstacleInfringementCount"),
                      m_obstacleInfringementCount);
    writeMetadata();
}

void FlightSessionRecorder::loadTrashedSessions()
{
    m_trashedSessions = SessionArchive::loadTrashed(m_trashDirectory);
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
