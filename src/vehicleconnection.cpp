#include "vehicleconnection.h"

#include <QMetaObject>
#include <QMatrix3x3>
#include <QPointer>
#include <QTimer>

#include <mavsdk/mavsdk.h>
#include <mavsdk/log_callback.h>
#include <mavsdk/plugins/action/action.h>
#include <mavsdk/plugins/mission/mission.h>
#include <mavsdk/plugins/offboard/offboard.h>
#include <mavsdk/plugins/telemetry/telemetry.h>

#include <cmath>
#include <atomic>
#include <numbers>
#include <optional>
#include <mutex>
#include <sstream>

using namespace mavsdk;

namespace {
constexpr quint8 PositionTelemetryFlag = 1U << 0U;
constexpr quint8 VelocityTelemetryFlag = 1U << 1U;
constexpr quint8 AttitudeTelemetryFlag = 1U << 2U;
constexpr quint8 HeadingTelemetryFlag = 1U << 3U;
constexpr quint8 CompleteCoreTelemetryMask = PositionTelemetryFlag | VelocityTelemetryFlag
                                              | AttitudeTelemetryFlag | HeadingTelemetryFlag;
constexpr double RadiansToDegrees = 180.0 / std::numbers::pi_v<double>;
constexpr double MinimumControlSpeed = 0.5;
constexpr double MaximumControlSpeed = 5.0;
constexpr double MinimumVerticalControlSpeed = 0.2;
constexpr double MaximumVerticalControlSpeed = 3.0;
constexpr double MinimumYawControlRate = 5.0;
constexpr double MaximumYawControlRate = 90.0;
constexpr qint64 CoreTelemetryTimeoutMs = 1500;
constexpr qint64 AuxiliaryTelemetryTimeoutMs = 3500;

void installMavsdkLogFilter()
{
    static std::once_flag once;
    std::call_once(once, [] {
        mavsdk::log::subscribe([](mavsdk::log::Level,
                                  const std::string &message,
                                  const std::string &,
                                  int) {
            // PX4 SITL emits these transient messages while the local heartbeat
            // and an empty mission store are being initialized. They are harmless
            // in this application and otherwise look like user-facing failures.
            return message.find("Preflight Fail: No connection to the GCS") != std::string::npos
                   || message.find("MAVSDK version:") != std::string::npos
                   || message.find("Mission rejected: empty") != std::string::npos
                   || message.find("Received ack for not-existing command: 176")
                          != std::string::npos;
        });
    });
}

double normalizedHeading(double value)
{
    double result = std::fmod(value, 360.0);
    if (result < 0.0) result += 360.0;
    return result;
}

double signedHeadingError(double target, double current)
{
    double error = std::fmod(target - current + 540.0, 360.0) - 180.0;
    if (error <= -180.0) error += 360.0;
    return error;
}

QString actionResultText(Action::Result result)
{
    std::ostringstream stream;
    stream << result;
    return QString::fromStdString(stream.str());
}

QString offboardResultText(Offboard::Result result)
{
    std::ostringstream stream;
    stream << result;
    return QString::fromStdString(stream.str());
}

QString missionResultText(Mission::Result result)
{
    std::ostringstream stream;
    stream << result;
    return QString::fromStdString(stream.str());
}

QString flightModeText(Telemetry::FlightMode mode)
{
    switch (mode) {
    case Telemetry::FlightMode::Ready: return QStringLiteral("准备");
    case Telemetry::FlightMode::Takeoff: return QStringLiteral("起飞");
    case Telemetry::FlightMode::Hold: return QStringLiteral("悬停");
    case Telemetry::FlightMode::Mission: return QStringLiteral("任务");
    case Telemetry::FlightMode::ReturnToLaunch: return QStringLiteral("返航");
    case Telemetry::FlightMode::Land: return QStringLiteral("降落");
    case Telemetry::FlightMode::Offboard: return QStringLiteral("外部控制");
    case Telemetry::FlightMode::FollowMe: return QStringLiteral("跟随");
    case Telemetry::FlightMode::Manual: return QStringLiteral("手动");
    case Telemetry::FlightMode::Altctl: return QStringLiteral("高度控制");
    case Telemetry::FlightMode::Posctl: return QStringLiteral("位置控制");
    case Telemetry::FlightMode::Acro: return QStringLiteral("特技");
    case Telemetry::FlightMode::Stabilized: return QStringLiteral("增稳");
    case Telemetry::FlightMode::Rattitude: return QStringLiteral("姿态辅助");
    case Telemetry::FlightMode::Unknown: return QStringLiteral("未知");
    }
    return QStringLiteral("未知");
}

QString gpsFixText(Telemetry::FixType fixType)
{
    switch (fixType) {
    case Telemetry::FixType::NoGps: return QStringLiteral("无 GPS");
    case Telemetry::FixType::NoFix: return QStringLiteral("未定位");
    case Telemetry::FixType::Fix2D: return QStringLiteral("2D");
    case Telemetry::FixType::Fix3D: return QStringLiteral("3D");
    case Telemetry::FixType::FixDgps: return QStringLiteral("DGPS");
    case Telemetry::FixType::RtkFloat: return QStringLiteral("RTK Float");
    case Telemetry::FixType::RtkFixed: return QStringLiteral("RTK Fixed");
    }
    return QStringLiteral("未知");
}

QQuaternion sceneRotationFromNedQuaternion(const Telemetry::Quaternion &value)
{
    const QQuaternion bodyToNed(value.w, value.x, value.y, value.z);

    QMatrix3x3 nedToScene;
    nedToScene(0, 0) = 0.0f;  nedToScene(0, 1) = 1.0f;  nedToScene(0, 2) = 0.0f;
    nedToScene(1, 0) = 0.0f;  nedToScene(1, 1) = 0.0f;  nedToScene(1, 2) = -1.0f;
    nedToScene(2, 0) = -1.0f; nedToScene(2, 1) = 0.0f;  nedToScene(2, 2) = 0.0f;

    QMatrix3x3 modelToBody;
    modelToBody(0, 0) = 0.0f; modelToBody(0, 1) = 0.0f;  modelToBody(0, 2) = -1.0f;
    modelToBody(1, 0) = 1.0f; modelToBody(1, 1) = 0.0f;  modelToBody(1, 2) = 0.0f;
    modelToBody(2, 0) = 0.0f; modelToBody(2, 1) = -1.0f; modelToBody(2, 2) = 0.0f;

    return QQuaternion::fromRotationMatrix(
               nedToScene * bodyToNed.normalized().toRotationMatrix() * modelToBody)
        .normalized();
}
}

struct VehicleConnection::Impl
{
    explicit Impl(VehicleConnection *owner)
        : q(owner), sdk(Mavsdk::Configuration(ComponentType::GroundStation))
    {
    }

    template<typename Function>
    void post(Function &&function)
    {
        if (shuttingDown.load(std::memory_order_acquire)) return;
        QPointer<VehicleConnection> guard(q);
        QMetaObject::invokeMethod(q,
                                  [guard, fn = std::forward<Function>(function)]() mutable {
                                      if (guard) fn(*guard);
                                  },
                                  Qt::QueuedConnection);
    }

    void attachFirstAutopilot()
    {
        if (!q->m_sitlExpected) return;
        for (const auto &candidate : sdk.systems()) {
            // The new-system callback may run before MAVSDK has updated has_autopilot().
            // This listener is dedicated to the PX4 SITL offboard port, so attach to the
            // first discovered MAVLink system and let its live connection/telemetry state
            // determine readiness.
            if (!candidate || !candidate->is_connected() || !candidate->has_autopilot()
                || candidate->get_system_id() != 1) {
                continue;
            }
            if (system == candidate) {
                return;
            }

            system = candidate;
            telemetry = std::make_unique<Telemetry>(system);
            action = std::make_unique<Action>(system);
            mission = std::make_unique<Mission>(system);
            offboard = std::make_unique<Offboard>(system);
            post([](VehicleConnection &owner) { owner.m_discoveryTimer.stop(); });

            systemConnectedHandle = system->subscribe_is_connected([this](bool value) {
                post([value](VehicleConnection &owner) {
                    owner.setConnected(value);
                    owner.setStatusMessage(value ? QStringLiteral("飞行器已通过 MAVSDK 连接")
                                                 : QStringLiteral("飞行器连接已断开，等待重新连接…"));
                });
            });

            healthAllOkHandle = telemetry->subscribe_health_all_ok([this](bool value) {
                post([value](VehicleConnection &owner) { owner.updateReadyCandidate(value); });
            });
            healthHandle = telemetry->subscribe_health([this](Telemetry::Health health) {
                post([health](VehicleConnection &owner) {
                    owner.setHealth(health.is_gyrometer_calibration_ok,
                                    health.is_accelerometer_calibration_ok,
                                    health.is_magnetometer_calibration_ok,
                                    health.is_local_position_ok,
                                    health.is_global_position_ok,
                                    health.is_home_position_ok,
                                    health.is_armable);
                });
            });
            armedHandle = telemetry->subscribe_armed([this](bool value) {
                post([value](VehicleConnection &owner) { owner.setArmed(value); });
            });
            inAirHandle = telemetry->subscribe_in_air([this](bool value) {
                post([value](VehicleConnection &owner) { owner.setInAir(value); });
            });
            positionHandle = telemetry->subscribe_position([this](Telemetry::Position position) {
                if (std::isfinite(position.latitude_deg)
                    && std::isfinite(position.longitude_deg)) {
                    const double latitude = position.latitude_deg;
                    const double longitude = position.longitude_deg;
                    post([latitude, longitude](VehicleConnection &owner) {
                        owner.setGlobalPosition(latitude, longitude);
                    });
                }
                if (std::isfinite(position.relative_altitude_m)) {
                    const double altitude = position.relative_altitude_m;
                    post([altitude](VehicleConnection &owner) {
                        owner.setAltitudeMeters(altitude);
                        owner.markCoreTelemetry(PositionTelemetryFlag);
                    });
                }
            });
            positionVelocityNedHandle = telemetry->subscribe_position_velocity_ned([this](Telemetry::PositionVelocityNed value) {
                const double positionNorth = value.position.north_m;
                const double positionEast = value.position.east_m;
                const double positionDown = value.position.down_m;
                const double north = value.velocity.north_m_s;
                const double east = value.velocity.east_m_s;
                const double down = value.velocity.down_m_s;
                const double horizontalSpeed = std::sqrt(north * north + east * east);
                const double verticalSpeed = -down;
                if (std::isfinite(horizontalSpeed) && std::isfinite(verticalSpeed)) {
                    post([horizontalSpeed, verticalSpeed](VehicleConnection &owner) {
                        owner.setVelocity(horizontalSpeed, verticalSpeed);
                        owner.markCoreTelemetry(VelocityTelemetryFlag);
                    });
                }
                if (std::isfinite(positionNorth) && std::isfinite(positionEast)
                    && std::isfinite(positionDown)) {
                    post([positionNorth, positionEast, positionDown](VehicleConnection &owner) {
                        owner.setLocalPosition(positionNorth, positionEast, positionDown);
                    });
                }
            });
            flightModeHandle = telemetry->subscribe_flight_mode([this](Telemetry::FlightMode mode) {
                const QString text = flightModeText(mode);
                post([text](VehicleConnection &owner) { owner.setFlightMode(text); });
            });
            attitudeEulerHandle = telemetry->subscribe_attitude_euler([this](Telemetry::EulerAngle value) {
                if (std::isfinite(value.roll_deg) && std::isfinite(value.pitch_deg)
                    && std::isfinite(value.yaw_deg)) {
                    post([value](VehicleConnection &owner) {
                        owner.setAttitude(value.roll_deg, value.pitch_deg, value.yaw_deg);
                        owner.markCoreTelemetry(AttitudeTelemetryFlag);
                    });
                }
            });
            attitudeQuaternionHandle = telemetry->subscribe_attitude_quaternion([this](Telemetry::Quaternion value) {
                if (std::isfinite(value.w) && std::isfinite(value.x)
                    && std::isfinite(value.y) && std::isfinite(value.z)) {
                    const QQuaternion rotation = sceneRotationFromNedQuaternion(value);
                    post([rotation](VehicleConnection &owner) {
                        owner.setSceneRotation(rotation);
                    });
                }
            });
            headingHandle = telemetry->subscribe_heading([this](Telemetry::Heading value) {
                if (std::isfinite(value.heading_deg)) {
                    post([value](VehicleConnection &owner) {
                        owner.setHeadingDegrees(value.heading_deg);
                        owner.markCoreTelemetry(HeadingTelemetryFlag);
                    });
                }
            });
            // PX4 SITL publishes the primary vehicle IMU as HIGHRES_IMU. The
            // Scaled IMU stream is absent on the normal x500, Mono, Depth and
            // LiDAR airframes (and on Gimbal it may describe the payload IMU),
            // which left the aircraft angular-rate rows empty or misleading.
            imuHandle = telemetry->subscribe_imu([this](Telemetry::Imu value) {
                const auto &angular = value.angular_velocity_frd;
                if (std::isfinite(angular.forward_rad_s) && std::isfinite(angular.right_rad_s)
                    && std::isfinite(angular.down_rad_s)) {
                    const double rollRate = angular.forward_rad_s * RadiansToDegrees;
                    const double pitchRate = angular.right_rad_s * RadiansToDegrees;
                    const double yawRate = angular.down_rad_s * RadiansToDegrees;
                    post([rollRate, pitchRate, yawRate](VehicleConnection &owner) {
                        owner.setAngularVelocity(rollRate, pitchRate, yawRate);
                    });
                }
            });
            batteryHandle = telemetry->subscribe_battery([this](Telemetry::Battery value) {
                if (std::isfinite(value.remaining_percent) || std::isfinite(value.voltage_v)) {
                    post([value](VehicleConnection &owner) {
                        owner.setBattery(value.remaining_percent, value.voltage_v);
                    });
                }
            });
            gpsInfoHandle = telemetry->subscribe_gps_info([this](Telemetry::GpsInfo value) {
                const QString fixType = gpsFixText(value.fix_type);
                post([value, fixType](VehicleConnection &owner) {
                    owner.setGps(value.num_satellites, fixType);
                });
            });

            const auto ignoreRateResult = [](Telemetry::Result) {};
            telemetry->set_rate_position_async(5.0, ignoreRateResult);
            telemetry->set_rate_position_velocity_ned_async(10.0, ignoreRateResult);
            telemetry->set_rate_attitude_euler_async(10.0, ignoreRateResult);
            telemetry->set_rate_attitude_quaternion_async(10.0, ignoreRateResult);
            telemetry->set_rate_imu_async(10.0, ignoreRateResult);
            telemetry->set_rate_battery_async(1.0, ignoreRateResult);
            telemetry->set_rate_gps_info_async(1.0, ignoreRateResult);

            missionProgressHandle = mission->subscribe_mission_progress(
                [this](Mission::MissionProgress progress) {
                    post([progress](VehicleConnection &owner) {
                        owner.setMissionProgress(progress.current, progress.total);
                        if (progress.total > 0 && progress.current >= progress.total
                            && owner.m_missionRunning) {
                            owner.setMissionState(false,
                                                  true,
                                                  false,
                                                  false,
                                                  QStringLiteral("任务已完成"));
                            owner.setStatusMessage(QStringLiteral("航点任务已完成"));
                        }
                    });
                });

            const bool initiallyConnected = system->is_connected();
            post([initiallyConnected](VehicleConnection &owner) {
                owner.setConnected(initiallyConnected);
                if (initiallyConnected) {
                    owner.setStatusMessage(QStringLiteral("飞行器已通过 MAVSDK 连接"));
                }
            });
            return;
        }
    }

    VehicleConnection *q;
    Mavsdk sdk;
    std::shared_ptr<System> system;
    std::unique_ptr<Telemetry> telemetry;
    std::unique_ptr<Action> action;
    std::unique_ptr<Mission> mission;
    std::unique_ptr<Offboard> offboard;
    std::optional<Mission::MissionProgressHandle> missionProgressHandle;
    std::optional<Mavsdk::NewSystemHandle> newSystemHandle;
    std::optional<System::IsConnectedHandle> systemConnectedHandle;
    std::optional<Telemetry::HealthAllOkHandle> healthAllOkHandle;
    std::optional<Telemetry::HealthHandle> healthHandle;
    std::optional<Telemetry::ArmedHandle> armedHandle;
    std::optional<Telemetry::InAirHandle> inAirHandle;
    std::optional<Telemetry::PositionHandle> positionHandle;
    std::optional<Telemetry::PositionVelocityNedHandle> positionVelocityNedHandle;
    std::optional<Telemetry::FlightModeHandle> flightModeHandle;
    std::optional<Telemetry::AttitudeEulerHandle> attitudeEulerHandle;
    std::optional<Telemetry::AttitudeQuaternionHandle> attitudeQuaternionHandle;
    std::optional<Telemetry::HeadingHandle> headingHandle;
    std::optional<Telemetry::ImuHandle> imuHandle;
    std::optional<Telemetry::BatteryHandle> batteryHandle;
    std::optional<Telemetry::GpsInfoHandle> gpsInfoHandle;
    std::atomic_bool shuttingDown{false};
    bool listenerStarted{false};
};

VehicleConnection::VehicleConnection(QObject *parent)
    : QObject(parent),
      m_impl((installMavsdkLogFilter(), std::make_unique<Impl>(this)))
{
    m_discoveryTimer.setInterval(500);
    connect(&m_discoveryTimer, &QTimer::timeout, this,
            [this] { m_impl->attachFirstAutopilot(); });

    m_missionStartConfirmationTimer.setSingleShot(true);
    m_missionStartConfirmationTimer.setInterval(4000);
    connect(&m_missionStartConfirmationTimer, &QTimer::timeout, this, [this] {
        if (!m_missionStartPending) return;
        m_missionStartPending = false;
        if (m_missionStartAttempts < 3 && m_connected && m_missionUploaded
            && !m_missionRunning) {
            const quint64 generation = m_missionGeneration;
            setMissionState(true, true, false, false,
                            QStringLiteral("PX4 尚未确认任务模式，正在重试（%1/3）…")
                                .arg(m_missionStartAttempts + 1));
            setStatusMessage(QStringLiteral("任务已上传，等待 PX4 完成任务校验后重试…"));
            QTimer::singleShot(1000, this, [this, generation] {
                if (generation != m_missionGeneration || !m_connected
                    || !m_missionUploaded || m_missionRunning) {
                    return;
                }
                beginMissionPlayback();
            });
            return;
        }
        m_missionStartAttempts = 0;
        setMissionState(false, true, false, false,
                        QStringLiteral("启动未确认：PX4 未进入任务模式，请检查飞行状态后重试"));
        setStatusMessage(QStringLiteral("航点任务未启动：PX4 未进入任务模式"));
    });

    m_controlTimer.setInterval(50);
    connect(&m_controlTimer, &QTimer::timeout, this, [this] {
        if (!m_keyboardControlActive || !m_impl->offboard) return;
        if (!m_connected || !m_ready || !m_armed || !m_inAir || !m_telemetryAvailable) {
            abortKeyboardControl();
            setStatusMessage(QStringLiteral("键盘控制已安全退出：飞行状态不再满足要求"));
            return;
        }
        if (!m_controlInputClock.isValid() || m_controlInputClock.elapsed() > 350) {
            const bool hadCommand = m_preciseTurnActive
                                    || !qFuzzyIsNull(m_controlForwardAxis)
                                    || !qFuzzyIsNull(m_controlRightAxis)
                                    || !qFuzzyIsNull(m_controlUpAxis)
                                    || !qFuzzyIsNull(m_controlYawAxis);
            resetControlAxes();
            if (hadCommand) {
                setStatusMessage(QStringLiteral("键盘输入超时，飞行器已恢复悬停"));
            }
        }
        if (m_preciseTurnActive) {
            const double error = signedHeadingError(m_preciseTurnTargetDegrees,
                                                    m_headingDegrees);
            if (std::abs(error) <= 1.5) {
                const double completedHeading = m_headingDegrees;
                m_preciseTurnActive = false;
                m_preciseTurnTargetDegrees = 0.0;
                m_preciseTurnRequestedDegrees = 0.0;
                m_controlYawDegreesPerSecond = 0.0;
                emit preciseTurnChanged();
                emit controlCommandChanged();
                setStatusMessage(QStringLiteral("精确转向完成，当前航向 %1°")
                                     .arg(QString::number(completedHeading, 'f', 0)));
            } else {
                const double speed = qBound(5.0,
                                            std::abs(error) * 1.2,
                                            m_controlYawRateDegreesPerSecond);
                m_controlYawDegreesPerSecond = std::copysign(speed, error);
            }
        }
        const Offboard::VelocityBodyYawspeed setpoint{
            static_cast<float>(m_controlForwardMetersPerSecond),
            static_cast<float>(m_controlRightMetersPerSecond),
            static_cast<float>(-m_controlUpMetersPerSecond),
            static_cast<float>(m_controlYawDegreesPerSecond)};
        const Offboard::Result result = m_impl->offboard->set_velocity_body(setpoint);
        if (result != Offboard::Result::Success) {
            abortKeyboardControl();
            setStatusMessage(QStringLiteral("键盘控制已中断：%1").arg(offboardResultText(result)));
        }
    });

    m_telemetryFreshnessTimer.setInterval(250);
    connect(&m_telemetryFreshnessTimer, &QTimer::timeout,
            this, &VehicleConnection::refreshTelemetryFreshness);
    m_telemetryFreshnessTimer.start();
}

VehicleConnection::~VehicleConnection()
{
    m_impl->shuttingDown.store(true, std::memory_order_release);
    m_controlTimer.stop();
    m_telemetryFreshnessTimer.stop();
    m_discoveryTimer.stop();
    m_missionStartConfirmationTimer.stop();
    if (m_impl->newSystemHandle) {
        m_impl->sdk.unsubscribe_on_new_system(*m_impl->newSystemHandle);
        m_impl->newSystemHandle.reset();
    }
    if (m_impl->system && m_impl->systemConnectedHandle) {
        m_impl->system->unsubscribe_is_connected(*m_impl->systemConnectedHandle);
        m_impl->systemConnectedHandle.reset();
    }
    if (m_impl->telemetry) {
        if (m_impl->healthAllOkHandle) m_impl->telemetry->unsubscribe_health_all_ok(*m_impl->healthAllOkHandle);
        if (m_impl->healthHandle) m_impl->telemetry->unsubscribe_health(*m_impl->healthHandle);
        if (m_impl->armedHandle) m_impl->telemetry->unsubscribe_armed(*m_impl->armedHandle);
        if (m_impl->inAirHandle) m_impl->telemetry->unsubscribe_in_air(*m_impl->inAirHandle);
        if (m_impl->positionHandle) m_impl->telemetry->unsubscribe_position(*m_impl->positionHandle);
        if (m_impl->positionVelocityNedHandle) m_impl->telemetry->unsubscribe_position_velocity_ned(*m_impl->positionVelocityNedHandle);
        if (m_impl->flightModeHandle) m_impl->telemetry->unsubscribe_flight_mode(*m_impl->flightModeHandle);
        if (m_impl->attitudeEulerHandle) m_impl->telemetry->unsubscribe_attitude_euler(*m_impl->attitudeEulerHandle);
        if (m_impl->attitudeQuaternionHandle) m_impl->telemetry->unsubscribe_attitude_quaternion(*m_impl->attitudeQuaternionHandle);
        if (m_impl->headingHandle) m_impl->telemetry->unsubscribe_heading(*m_impl->headingHandle);
        if (m_impl->imuHandle) m_impl->telemetry->unsubscribe_imu(*m_impl->imuHandle);
        if (m_impl->batteryHandle) m_impl->telemetry->unsubscribe_battery(*m_impl->batteryHandle);
        if (m_impl->gpsInfoHandle) m_impl->telemetry->unsubscribe_gps_info(*m_impl->gpsInfoHandle);
    }
    if (m_impl->mission && m_impl->missionProgressHandle) {
        m_impl->mission->unsubscribe_mission_progress(*m_impl->missionProgressHandle);
        m_impl->missionProgressHandle.reset();
    }
    if (m_keyboardControlActive && m_impl->offboard && m_connected) {
        m_impl->offboard->set_velocity_body({});
        m_impl->offboard->stop();
    }
}

void VehicleConnection::start()
{
    if (m_impl->listenerStarted) {
        return;
    }

    const ConnectionResult result = m_impl->sdk.add_any_connection("udpin://127.0.0.1:14540");
    if (result != ConnectionResult::Success) {
        std::ostringstream stream;
        stream << result;
        setStatusMessage(QStringLiteral("MAVSDK 无法监听 UDP 14540：%1")
                             .arg(QString::fromStdString(stream.str())));
        return;
    }

    m_impl->listenerStarted = true;
    setStatusMessage(QStringLiteral("MAVSDK 正在监听 UDP 14540，等待飞行器…"));
    m_impl->newSystemHandle = m_impl->sdk.subscribe_on_new_system([impl = m_impl.get()] {
        impl->post([](VehicleConnection &owner) { owner.m_impl->attachFirstAutopilot(); });
    });
    m_discoveryTimer.start();
    m_impl->attachFirstAutopilot();
}

void VehicleConnection::setSitlExpected(bool expected)
{
    if (m_sitlExpected == expected) return;
    m_sitlExpected = expected;
    if (!expected) {
        abortKeyboardControl();
        // A stopped simulation is definitive. Clear values that are preserved
        // during a brief heartbeat loss so the next session never inherits an
        // old armed, airborne, altitude, or mode state.
        setReady(false);
        setArmed(false);
        setInAir(false);
        setAltitudeMeters(0.0);
        resetTelemetry();
        setFlightMode(QStringLiteral("未知"));
        setHealth(false, false, false, false, false, false, false);
    } else if (m_impl->listenerStarted) {
        m_impl->attachFirstAutopilot();
    }
}

void VehicleConnection::takeoff()
{
    if (!m_sitlExpected || !m_impl->action || !m_connected || m_busy) {
        setStatusMessage(QStringLiteral("起飞被阻止：飞行器尚未连接"));
        return;
    }
    if (!m_ready) {
        setStatusMessage(QStringLiteral("起飞被阻止：飞行器健康检查尚未通过"));
        return;
    }
    if (!m_telemetryAvailable) {
        setStatusMessage(QStringLiteral("起飞被阻止：飞行遥测数据尚未完整"));
        return;
    }
    if (m_inAir) {
        setStatusMessage(QStringLiteral("飞行器已经在空中"));
        return;
    }

    setBusy(true);
    setStatusMessage(QStringLiteral("正在设置 %1 米安全起飞高度…")
                         .arg(QString::number(m_takeoffAltitude, 'f', 1)));
    auto *impl = m_impl.get();
    const float targetAltitude = static_cast<float>(m_takeoffAltitude);
    impl->action->set_takeoff_altitude_async(targetAltitude, [impl, targetAltitude](Action::Result altitudeResult) {
        if (altitudeResult != Action::Result::Success) {
            impl->post([altitudeResult](VehicleConnection &owner) {
                owner.setBusy(false);
                owner.setStatusMessage(QStringLiteral("设置起飞高度失败：%1")
                                           .arg(actionResultText(altitudeResult)));
            });
            return;
        }

        impl->post([](VehicleConnection &owner) {
            owner.setStatusMessage(QStringLiteral("健康检查通过，正在解锁飞行器…"));
        });
        impl->action->arm_async([impl, targetAltitude](Action::Result armResult) {
            if (armResult != Action::Result::Success) {
                impl->post([armResult](VehicleConnection &owner) {
                    owner.setBusy(false);
                    owner.setStatusMessage(QStringLiteral("解锁失败：%1")
                                               .arg(actionResultText(armResult)));
                });
                return;
            }

            impl->post([targetAltitude](VehicleConnection &owner) {
                owner.setStatusMessage(QStringLiteral("飞行器已解锁，正在起飞至 %1 米…")
                                           .arg(QString::number(targetAltitude, 'f', 1)));
            });
            impl->action->takeoff_async([impl](Action::Result takeoffResult) {
                impl->post([takeoffResult](VehicleConnection &owner) {
                    owner.setBusy(false);
                    owner.setStatusMessage(
                        takeoffResult == Action::Result::Success
                            ? QStringLiteral("起飞指令已接受")
                            : QStringLiteral("起飞失败：%1")
                                  .arg(actionResultText(takeoffResult)));
                });
            });
        });
    });
}

void VehicleConnection::land()
{
    if (!m_sitlExpected || !m_impl->action || !m_connected || m_busy) {
        setStatusMessage(QStringLiteral("降落被阻止：飞行器尚未连接或正在执行其他指令"));
        return;
    }

    if (m_missionBusy || m_missionRunning || m_missionPaused || m_missionStartPending) {
        ++m_missionGeneration;
        cancelMissionStartConfirmation();
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("航点任务已终止，正在降落…"));
    }
    setBusy(true);
    if (m_keyboardControlActive && m_impl->offboard) {
        setStatusMessage(QStringLiteral("正在退出键盘控制并准备降落…"));
        m_controlTimer.stop();
        resetControlAxes();
        setKeyboardControlActive(false);
        ++m_controlGeneration;
        auto *impl = m_impl.get();
        impl->offboard->set_velocity_body({});
        impl->offboard->stop_async([impl](Offboard::Result) {
            impl->post([](VehicleConnection &owner) {
                if (!owner.m_connected) {
                    owner.setBusy(false);
                    owner.setStatusMessage(QStringLiteral("降落失败：飞行器连接已断开"));
                    return;
                }
                owner.sendLandingCommand();
            });
        });
        return;
    }

    sendLandingCommand();
}

void VehicleConnection::sendLandingCommand()
{
    if (!m_impl->action || !m_connected) {
        setBusy(false);
        setStatusMessage(QStringLiteral("降落失败：飞行器连接已断开"));
        return;
    }
    setStatusMessage(QStringLiteral("正在发送降落指令…"));
    auto *impl = m_impl.get();
    impl->action->land_async([impl](Action::Result result) {
        impl->post([result](VehicleConnection &owner) {
            owner.setBusy(false);
            const bool success = result == Action::Result::Success;
            const QString message = success
                                        ? QStringLiteral("降落指令已接受，请等待飞行器落地")
                                        : QStringLiteral("降落失败：%1")
                                              .arg(actionResultText(result));
            owner.setStatusMessage(message);
            emit owner.landingCommandFinished(success, message);
        });
    });
}

void VehicleConnection::startKeyboardControl()
{
    if (!m_sitlExpected || !m_impl->offboard || !m_connected || m_busy
        || keyboardControlTransitioning()) {
        setStatusMessage(QStringLiteral("键盘控制无法启用：飞行器尚未连接或正在执行其他指令"));
        return;
    }
    if (m_missionRunning || m_missionPaused || m_missionBusy) {
        setStatusMessage(QStringLiteral("键盘控制无法启用：请先结束或清除航点任务"));
        return;
    }
    if (!m_ready || !m_telemetryAvailable || !m_armed || !m_inAir) {
        setStatusMessage(QStringLiteral("键盘控制无法启用：请先完成健康检查并起飞悬停"));
        return;
    }
    if (m_keyboardControlActive) return;

    resetControlAxes();
    const Offboard::Result setpointResult = m_impl->offboard->set_velocity_body({});
    if (setpointResult != Offboard::Result::Success) {
        setStatusMessage(QStringLiteral("键盘控制启动失败：%1")
                             .arg(offboardResultText(setpointResult)));
        return;
    }

    setBusy(true);
    setKeyboardControlTransition(KeyboardControlTransition::Starting);
    const quint64 generation = ++m_controlGeneration;
    setStatusMessage(QStringLiteral("正在启用键盘飞行控制…"));
    auto *impl = m_impl.get();
    impl->offboard->start_async([impl, generation](Offboard::Result result) {
        impl->post([result, generation](VehicleConnection &owner) {
            if (generation != owner.m_controlGeneration) {
                if (result == Offboard::Result::Success && owner.m_impl->offboard) {
                    owner.m_impl->offboard->set_velocity_body({});
                    owner.m_impl->offboard->stop_async([](Offboard::Result) {});
                }
                return;
            }
            if (owner.m_keyboardControlTransition != KeyboardControlTransition::Starting) return;
            if (result != Offboard::Result::Success) {
                owner.setKeyboardControlTransition(KeyboardControlTransition::None);
                owner.setBusy(false);
                owner.setStatusMessage(QStringLiteral("键盘控制启动失败：%1")
                                           .arg(offboardResultText(result)));
                return;
            }
            if (owner.m_flightMode == QStringLiteral("外部控制")) {
                owner.completeKeyboardControlStart();
                return;
            }

            owner.setStatusMessage(QStringLiteral("控制指令已接受，正在确认外部控制模式…"));
            auto *ownerPointer = &owner;
            QTimer::singleShot(4000, ownerPointer, [ownerPointer, generation] {
                if (generation != ownerPointer->m_controlGeneration
                    || ownerPointer->m_keyboardControlTransition
                           != KeyboardControlTransition::Starting) {
                    return;
                }
                if (ownerPointer->m_impl->offboard) {
                    ownerPointer->m_impl->offboard->set_velocity_body({});
                    ownerPointer->m_impl->offboard->stop_async([](Offboard::Result) {});
                }
                ownerPointer->setKeyboardControlTransition(KeyboardControlTransition::None);
                ownerPointer->setBusy(false);
                ownerPointer->setStatusMessage(
                    QStringLiteral("键盘控制启动失败：未收到外部控制模式确认"));
            });
        });
    });
}

void VehicleConnection::stopKeyboardControl()
{
    const bool wasStarting = m_keyboardControlTransition == KeyboardControlTransition::Starting;
    if (m_keyboardControlTransition == KeyboardControlTransition::Stopping) return;
    ++m_controlGeneration;
    m_controlTimer.stop();
    resetControlAxes();
    if (!m_keyboardControlActive || !m_impl->offboard) {
        setKeyboardControlTransition(KeyboardControlTransition::None);
        setKeyboardControlActive(false);
        if (wasStarting) {
            setBusy(false);
            setStatusMessage(QStringLiteral("键盘控制启动已取消"));
        }
        return;
    }

    setKeyboardControlTransition(KeyboardControlTransition::Stopping);
    setBusy(true);
    setStatusMessage(QStringLiteral("正在退出键盘控制并保持悬停…"));
    auto *impl = m_impl.get();
    const quint64 generation = m_controlGeneration;
    impl->offboard->set_velocity_body({});
    impl->offboard->stop_async([impl, generation](Offboard::Result result) {
        impl->post([result, generation](VehicleConnection &owner) {
            if (generation != owner.m_controlGeneration
                || owner.m_keyboardControlTransition != KeyboardControlTransition::Stopping) {
                return;
            }
            if (result != Offboard::Result::Success) {
                owner.completeKeyboardControlStop(
                    QStringLiteral("键盘控制退出失败：%1").arg(offboardResultText(result)));
                return;
            }
            if (owner.m_flightMode != QStringLiteral("外部控制")
                && owner.m_flightMode != QStringLiteral("未知")) {
                owner.completeKeyboardControlStop(
                    QStringLiteral("键盘控制已退出，飞行器保持悬停"));
                return;
            }

            owner.setStatusMessage(QStringLiteral("退出指令已接受，正在确认悬停模式…"));
            auto *ownerPointer = &owner;
            QTimer::singleShot(4000, ownerPointer, [ownerPointer, generation] {
                if (generation != ownerPointer->m_controlGeneration
                    || ownerPointer->m_keyboardControlTransition
                           != KeyboardControlTransition::Stopping) {
                    return;
                }
                ownerPointer->completeKeyboardControlStop(
                    QStringLiteral("键盘控制退出失败：未收到悬停模式确认"));
            });
        });
    });
}

void VehicleConnection::releaseKeyboardControl()
{
    if (!m_keyboardControlActive) return;
    setKeyboardControlAxes(0.0, 0.0, 0.0, 0.0);
}

void VehicleConnection::heartbeatKeyboardControl()
{
    if (m_keyboardControlActive) m_controlInputClock.restart();
}

void VehicleConnection::turnKeyboardControl(double degrees)
{
    if (!m_keyboardControlActive || !m_connected || !m_telemetryAvailable
        || !m_armed || !m_inAir) {
        setStatusMessage(QStringLiteral("精确转向无法执行：键盘控制尚未启用"));
        return;
    }
    const double bounded = qBound(-90.0, degrees, 90.0);
    if (std::abs(bounded) < 1.0) return;

    resetControlAxes();
    m_preciseTurnRequestedDegrees = bounded;
    m_preciseTurnTargetDegrees = normalizedHeading(m_headingDegrees + bounded);
    m_preciseTurnActive = true;
    m_controlInputClock.restart();
    emit preciseTurnChanged();
    emit controlCommandChanged();
    setStatusMessage(QStringLiteral("正在精确%1 %2°，目标航向 %3°")
                         .arg(bounded < 0.0 ? QStringLiteral("左转") : QStringLiteral("右转"),
                              QString::number(std::abs(bounded), 'f', 0),
                              QString::number(m_preciseTurnTargetDegrees, 'f', 0)));
}

void VehicleConnection::setKeyboardControlAxes(double forward,
                                               double right,
                                               double up,
                                               double yaw)
{
    if (!m_keyboardControlActive) return;
    m_controlInputClock.restart();
    if (m_preciseTurnActive) {
        m_preciseTurnActive = false;
        m_preciseTurnTargetDegrees = 0.0;
        m_preciseTurnRequestedDegrees = 0.0;
        emit preciseTurnChanged();
    }
    const double boundedForward = qBound(-1.0, forward, 1.0);
    const double boundedRight = qBound(-1.0, right, 1.0);
    const double boundedUp = qBound(-1.0, up, 1.0);
    const double boundedYaw = qBound(-1.0, yaw, 1.0);
    if (qFuzzyCompare(m_controlForwardAxis, boundedForward)
        && qFuzzyCompare(m_controlRightAxis, boundedRight)
        && qFuzzyCompare(m_controlUpAxis, boundedUp)
        && qFuzzyCompare(m_controlYawAxis, boundedYaw)) {
        return;
    }

    m_controlForwardAxis = boundedForward;
    m_controlRightAxis = boundedRight;
    m_controlUpAxis = boundedUp;
    m_controlYawAxis = boundedYaw;
    m_controlForwardMetersPerSecond = boundedForward * m_controlSpeedMetersPerSecond;
    m_controlRightMetersPerSecond = boundedRight * m_controlSpeedMetersPerSecond;
    m_controlUpMetersPerSecond = boundedUp * m_controlVerticalSpeedMetersPerSecond;
    m_controlYawDegreesPerSecond = boundedYaw * m_controlYawRateDegreesPerSecond;
    emit controlCommandChanged();
}

void VehicleConnection::setControlSpeedMetersPerSecond(double value)
{
    const double bounded = qBound(MinimumControlSpeed, value, MaximumControlSpeed);
    const double rounded = std::round(bounded * 10.0) / 10.0;
    if (qFuzzyCompare(m_controlSpeedMetersPerSecond, rounded)) return;
    m_controlSpeedMetersPerSecond = rounded;
    emit controlSpeedChanged();
    if (m_keyboardControlActive) {
        m_controlForwardMetersPerSecond = m_controlForwardAxis * rounded;
        m_controlRightMetersPerSecond = m_controlRightAxis * rounded;
        emit controlCommandChanged();
    }
}

void VehicleConnection::setControlVerticalSpeedMetersPerSecond(double value)
{
    const double bounded = qBound(MinimumVerticalControlSpeed, value,
                                  MaximumVerticalControlSpeed);
    const double rounded = std::round(bounded * 10.0) / 10.0;
    if (qFuzzyCompare(m_controlVerticalSpeedMetersPerSecond, rounded)) return;
    m_controlVerticalSpeedMetersPerSecond = rounded;
    emit controlSpeedChanged();
    if (m_keyboardControlActive) {
        m_controlUpMetersPerSecond = m_controlUpAxis * rounded;
        emit controlCommandChanged();
    }
}

void VehicleConnection::setControlYawRateDegreesPerSecond(double value)
{
    const double bounded = qBound(MinimumYawControlRate, value, MaximumYawControlRate);
    const double rounded = std::round(bounded);
    if (qFuzzyCompare(m_controlYawRateDegreesPerSecond, rounded)) return;
    m_controlYawRateDegreesPerSecond = rounded;
    emit controlSpeedChanged();
    if (m_keyboardControlActive && !m_preciseTurnActive) {
        m_controlYawDegreesPerSecond = m_controlYawAxis * rounded;
        emit controlCommandChanged();
    }
}

void VehicleConnection::uploadAndStartMission(const QVariantList &waypoints,
                                               const QString &endAction)
{
    if (!m_sitlExpected || !m_impl->mission || !m_impl->action || !m_connected || m_busy || m_missionBusy
        || m_missionRunning || m_missionPaused) {
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("无法上传：飞行器尚未连接或正在处理其他指令"));
        setStatusMessage(QStringLiteral("航点任务被阻止：飞行器尚未连接或正忙"));
        return;
    }
    if (!m_ready || !m_telemetryAvailable || !m_globalPositionAvailable) {
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("无法上传：请等待健康检查和全局定位通过"));
        setStatusMessage(QStringLiteral("航点任务被阻止：健康检查或全局定位尚未完成"));
        return;
    }
    if (m_keyboardControlActive || keyboardControlTransitioning()) {
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("无法上传：请先退出键盘飞行控制"));
        setStatusMessage(QStringLiteral("航点任务被阻止：键盘飞行控制仍在运行"));
        return;
    }
    if (waypoints.size() < 2 || waypoints.size() > 20) {
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("航线需要 2–20 个有效航点"));
        setStatusMessage(QStringLiteral("航点任务被阻止：航点数量无效"));
        return;
    }
    if (endAction != QStringLiteral("rtl")
        && endAction != QStringLiteral("land")
        && endAction != QStringLiteral("hold")) {
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("任务结束动作无效"));
        return;
    }

    constexpr double EarthRadiusMeters = 6378137.0;
    const double latitudeRadians = m_latitudeDegrees * std::numbers::pi_v<double> / 180.0;
    const double longitudeScale = std::cos(latitudeRadians);
    if (std::abs(longitudeScale) < 1.0e-6) {
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("当前位置无法建立本地任务坐标"));
        return;
    }

    cancelMissionStartConfirmation();
    m_missionStartAttempts = 0;
    Mission::MissionPlan plan;
    plan.mission_items.reserve(static_cast<std::size_t>(waypoints.size()));
    for (qsizetype index = 0; index < waypoints.size(); ++index) {
        const QVariantMap source = waypoints.at(index).toMap();
        bool northOk = false;
        bool eastOk = false;
        bool altitudeOk = false;
        bool speedOk = false;
        bool yawOk = false;
        const double north = source.value(QStringLiteral("northMeters")).toDouble(&northOk);
        const double east = source.value(QStringLiteral("eastMeters")).toDouble(&eastOk);
        const double altitude = source.value(QStringLiteral("altitudeMeters")).toDouble(&altitudeOk);
        const double speed = source.value(QStringLiteral("speedMetersPerSecond")).toDouble(&speedOk);
        const double yaw = source.value(QStringLiteral("yawDegrees")).toDouble(&yawOk);
        if (!northOk || !eastOk || !altitudeOk || !speedOk || !yawOk
            || !std::isfinite(north) || !std::isfinite(east)
            || !std::isfinite(altitude) || !std::isfinite(speed) || !std::isfinite(yaw)
            || std::abs(north) > 2000.0 || std::abs(east) > 2000.0
            || altitude < 1.0 || altitude > 100.0
            || speed < 0.5 || speed > 10.0
            || yaw < -180.0 || yaw > 180.0) {
            setMissionState(false, m_missionUploaded, false, false,
                            QStringLiteral("航点 %1 的位置或飞行参数超出安全范围")
                                .arg(index + 1));
            setStatusMessage(QStringLiteral("航点任务被阻止：请检查第 %1 个航点")
                                 .arg(index + 1));
            return;
        }

        Mission::MissionItem item;
        item.latitude_deg = m_latitudeDegrees
                            + north / EarthRadiusMeters * 180.0
                                  / std::numbers::pi_v<double>;
        item.longitude_deg = m_longitudeDegrees
                             + east / (EarthRadiusMeters * longitudeScale) * 180.0
                                   / std::numbers::pi_v<double>;
        item.relative_altitude_m = static_cast<float>(altitude);
        item.speed_m_s = static_cast<float>(speed);
        item.yaw_deg = static_cast<float>(yaw);
        item.acceptance_radius_m = 1.0f;
        // The planner renders an exact polyline. Require arrival at every
        // intermediate waypoint so PX4 does not smooth across sharp corners.
        item.is_fly_through = false;
        if (index == 0 && !m_inAir) {
            item.vehicle_action = Mission::MissionItem::VehicleAction::Takeoff;
        }
        if (index + 1 == waypoints.size() && endAction == QStringLiteral("land")) {
            item.vehicle_action = Mission::MissionItem::VehicleAction::Land;
        }
        plan.mission_items.push_back(item);
    }

    ++m_missionGeneration;
    const quint64 generation = m_missionGeneration;
    setMissionProgress(0, static_cast<int>(waypoints.size()));
    setMissionState(true, false, false, false, QStringLiteral("正在上传航线…"));
    setStatusMessage(QStringLiteral("正在上传 %1 个航点…").arg(waypoints.size()));
    auto *impl = m_impl.get();
    const bool returnToLaunch = endAction == QStringLiteral("rtl");
    const Mission::Result rtlResult = impl->mission->set_return_to_launch_after_mission(returnToLaunch);
    if (rtlResult != Mission::Result::Success) {
        setMissionState(false, false, false, false,
                        QStringLiteral("设置结束动作失败：%1").arg(missionResultText(rtlResult)));
        setStatusMessage(QStringLiteral("航点任务上传失败：无法设置结束动作"));
        return;
    }
    impl->mission->upload_mission_async(std::move(plan), [impl, generation](Mission::Result result) {
        impl->post([result, generation](VehicleConnection &owner) {
            if (generation != owner.m_missionGeneration) return;
            if (result != Mission::Result::Success) {
                owner.setMissionState(false, false, false, false,
                                      QStringLiteral("上传失败：%1")
                                          .arg(missionResultText(result)));
                owner.setStatusMessage(QStringLiteral("航点任务上传失败：%1")
                                           .arg(missionResultText(result)));
                return;
            }
            owner.setMissionState(true, true, false, false,
                                  QStringLiteral("航线已上传，正在等待 PX4 完成任务校验…"));
            auto *ownerPointer = &owner;
            QTimer::singleShot(1000, ownerPointer, [ownerPointer, generation] {
                if (generation != ownerPointer->m_missionGeneration
                    || !ownerPointer->m_connected || !ownerPointer->m_missionUploaded) {
                    return;
                }
                ownerPointer->startUploadedMission();
            });
        });
    });
}

void VehicleConnection::startUploadedMission()
{
    if (!m_impl->mission || !m_impl->action || !m_connected) {
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("任务启动失败：飞行器连接已断开"));
        return;
    }
    if (m_armed) {
        beginMissionPlayback();
        return;
    }

    const quint64 generation = m_missionGeneration;
    setMissionState(true, true, false, false, QStringLiteral("航线已上传，正在解锁…"));
    auto *impl = m_impl.get();
    impl->action->arm_async([impl, generation](Action::Result result) {
        impl->post([result, generation](VehicleConnection &owner) {
            if (generation != owner.m_missionGeneration) return;
            if (result != Action::Result::Success) {
                owner.setMissionState(false, true, false, false,
                                      QStringLiteral("解锁失败：%1")
                                          .arg(actionResultText(result)));
                owner.setStatusMessage(QStringLiteral("航点任务启动失败：无法解锁飞行器"));
                return;
            }
            owner.beginMissionPlayback();
        });
    });
}

void VehicleConnection::beginMissionPlayback()
{
    if (!m_impl->mission || !m_connected) {
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("任务启动失败：飞行器连接已断开"));
        return;
    }
    const quint64 generation = m_missionGeneration;
    m_missionStartConfirmationTimer.stop();
    m_missionStartPending = false;
    ++m_missionStartAttempts;
    setMissionState(true, true, false, false,
                    m_missionStartAttempts == 1
                        ? QStringLiteral("正在启动任务…")
                        : QStringLiteral("正在重试启动任务（%1/3）…")
                              .arg(m_missionStartAttempts));
    m_missionStartPending = true;
    auto *impl = m_impl.get();
    impl->mission->start_mission_async([impl, generation](Mission::Result result) {
        impl->post([result, generation](VehicleConnection &owner) {
            if (generation != owner.m_missionGeneration) return;
            if (result != Mission::Result::Success) {
                owner.cancelMissionStartConfirmation();
                owner.setMissionState(false, true, false, false,
                                      QStringLiteral("启动失败：%1")
                                          .arg(missionResultText(result)));
                owner.setStatusMessage(QStringLiteral("航点任务启动失败：%1")
                                           .arg(missionResultText(result)));
                return;
            }
            if (!owner.m_missionStartPending || owner.m_missionRunning) return;
            if (owner.m_flightMode == QStringLiteral("任务")) {
                owner.confirmMissionPlayback();
                return;
            }
            owner.setMissionState(true, true, false, false,
                                  QStringLiteral("启动指令已接受，等待 PX4 进入任务模式…"));
            owner.m_missionStartConfirmationTimer.start();
        });
    });
}

void VehicleConnection::confirmMissionPlayback()
{
    if (!m_missionStartPending) return;
    m_missionStartPending = false;
    m_missionStartAttempts = 0;
    m_missionStartConfirmationTimer.stop();
    setMissionState(false, true, true, false, QStringLiteral("任务执行中"));
    setStatusMessage(QStringLiteral("航点任务已启动，PX4 已进入任务模式"));
}

void VehicleConnection::cancelMissionStartConfirmation()
{
    m_missionStartPending = false;
    m_missionStartAttempts = 0;
    m_missionStartConfirmationTimer.stop();
}

void VehicleConnection::pauseMission()
{
    if (!m_sitlExpected || !m_impl->mission || !m_connected || !m_missionRunning
        || m_missionBusy) return;
    const quint64 generation = m_missionGeneration;
    setMissionState(true, true, true, false, QStringLiteral("正在暂停任务…"));
    auto *impl = m_impl.get();
    impl->mission->pause_mission_async([impl, generation](Mission::Result result) {
        impl->post([result, generation](VehicleConnection &owner) {
            if (generation != owner.m_missionGeneration) return;
            if (result == Mission::Result::Success) {
                owner.setMissionState(false, true, false, true,
                                      QStringLiteral("任务已暂停，飞行器保持当前位置"));
                owner.setStatusMessage(QStringLiteral("航点任务已暂停"));
            } else {
                owner.setMissionState(false, true, true, false,
                                      QStringLiteral("暂停失败：%1")
                                          .arg(missionResultText(result)));
                owner.setStatusMessage(QStringLiteral("航点任务暂停失败：%1")
                                           .arg(missionResultText(result)));
            }
        });
    });
}

void VehicleConnection::resumeMission()
{
    if (!m_sitlExpected || !m_impl->mission || !m_connected || !m_missionPaused
        || m_missionBusy) return;
    beginMissionPlayback();
}

void VehicleConnection::clearMission()
{
    if (!m_sitlExpected || !m_impl->mission || !m_connected || m_missionBusy
        || m_missionRunning) return;
    if (m_missionPaused) {
        setMissionState(false, m_missionUploaded, false, true,
                        QStringLiteral("任务暂停中：请先继续任务，不能在空中直接清除"));
        setStatusMessage(QStringLiteral("为避免空中任务变为空，暂停状态禁止清除任务"));
        return;
    }
    cancelMissionStartConfirmation();
    const quint64 generation = ++m_missionGeneration;
    setMissionState(true, m_missionUploaded, false, m_missionPaused,
                    QStringLiteral("正在清除飞行器中的任务…"));
    auto *impl = m_impl.get();
    impl->mission->clear_mission_async([impl, generation](Mission::Result result) {
        impl->post([result, generation](VehicleConnection &owner) {
            if (generation != owner.m_missionGeneration) return;
            if (result == Mission::Result::Success) {
                owner.setMissionProgress(0, 0);
                owner.setMissionState(false, false, false, false,
                                      QStringLiteral("飞行器中的任务已清除"));
            } else {
                owner.setMissionState(false, owner.m_missionUploaded, false,
                                      owner.m_missionPaused,
                                      QStringLiteral("清除失败：%1")
                                          .arg(missionResultText(result)));
            }
        });
    });
}

void VehicleConnection::abortMissionToHold()
{
    if (!m_sitlExpected || !m_impl->action || !m_connected || m_missionBusy) return;
    if (!m_missionRunning && !m_missionPaused && !m_missionStartPending) return;

    cancelMissionStartConfirmation();
    const quint64 generation = ++m_missionGeneration;
    const bool uploaded = m_missionUploaded;
    setMissionState(true, uploaded, false, false,
                    QStringLiteral("正在终止任务并切换到悬停…"));
    auto *impl = m_impl.get();
    impl->action->hold_async([impl, generation, uploaded](Action::Result result) {
        impl->post([result, generation, uploaded](VehicleConnection &owner) {
            if (generation != owner.m_missionGeneration) return;
            const bool success = result == Action::Result::Success;
            const QString message = success
                                        ? QStringLiteral("任务已终止，飞行器正在悬停")
                                        : QStringLiteral("终止任务失败：%1")
                                              .arg(actionResultText(result));
            owner.setMissionState(false, uploaded, false, false, message);
            owner.setStatusMessage(message);
        });
    });
}

void VehicleConnection::returnToLaunch()
{
    if (!m_sitlExpected || !m_impl->action || !m_connected || m_missionBusy || m_busy) return;

    cancelMissionStartConfirmation();
    const quint64 generation = ++m_missionGeneration;
    const bool uploaded = m_missionUploaded;
    setMissionState(true, uploaded, false, false,
                    QStringLiteral("正在终止任务并返航…"));
    auto *impl = m_impl.get();
    impl->action->return_to_launch_async(
        [impl, generation, uploaded](Action::Result result) {
            impl->post([result, generation, uploaded](VehicleConnection &owner) {
                if (generation != owner.m_missionGeneration) return;
                const bool success = result == Action::Result::Success;
                const QString message = success
                                            ? QStringLiteral("返航指令已接受")
                                            : QStringLiteral("返航失败：%1")
                                                  .arg(actionResultText(result));
                owner.setMissionState(false, uploaded, false, false, message);
                owner.setStatusMessage(message);
            });
        });
}

QString VehicleConnection::controlCommandText() const
{
    if (m_preciseTurnActive) {
        return QStringLiteral("精确%1 %2°")
            .arg(m_preciseTurnRequestedDegrees < 0.0 ? QStringLiteral("左转")
                                                     : QStringLiteral("右转"),
                 QString::number(std::abs(m_preciseTurnRequestedDegrees), 'f', 0));
    }
    QStringList parts;
    if (m_controlForwardAxis > 0.0) parts.append(QStringLiteral("前进"));
    else if (m_controlForwardAxis < 0.0) parts.append(QStringLiteral("后退"));
    if (m_controlRightAxis > 0.0) parts.append(QStringLiteral("右移"));
    else if (m_controlRightAxis < 0.0) parts.append(QStringLiteral("左移"));
    if (m_controlUpAxis > 0.0) parts.append(QStringLiteral("上升"));
    else if (m_controlUpAxis < 0.0) parts.append(QStringLiteral("下降"));
    if (m_controlYawAxis > 0.0) parts.append(QStringLiteral("右转"));
    else if (m_controlYawAxis < 0.0) parts.append(QStringLiteral("左转"));
    return parts.isEmpty() ? QStringLiteral("悬停") : parts.join(QStringLiteral(" + "));
}

bool VehicleConnection::keyboardControlTransitioning() const
{
    return m_keyboardControlTransition != KeyboardControlTransition::None;
}

void VehicleConnection::setKeyboardControlActive(bool value)
{
    if (m_keyboardControlActive == value) return;
    m_keyboardControlActive = value;
    emit keyboardControlActiveChanged();
}

void VehicleConnection::setKeyboardControlTransition(KeyboardControlTransition transition)
{
    if (m_keyboardControlTransition == transition) return;
    const bool wasTransitioning = keyboardControlTransitioning();
    m_keyboardControlTransition = transition;
    if (wasTransitioning != keyboardControlTransitioning()) {
        emit keyboardControlTransitioningChanged();
    }
}

void VehicleConnection::completeKeyboardControlStart()
{
    if (m_keyboardControlTransition != KeyboardControlTransition::Starting) return;
    setKeyboardControlTransition(KeyboardControlTransition::None);
    setBusy(false);
    m_controlInputClock.start();
    setKeyboardControlActive(true);
    m_controlTimer.start();
    setStatusMessage(QStringLiteral("键盘控制已启用，松开按键即悬停"));
}

void VehicleConnection::completeKeyboardControlStop(const QString &message)
{
    if (m_keyboardControlTransition != KeyboardControlTransition::Stopping) return;
    setKeyboardControlTransition(KeyboardControlTransition::None);
    setKeyboardControlActive(false);
    setBusy(false);
    setStatusMessage(message);
}

void VehicleConnection::resetControlAxes()
{
    const bool changed = !qFuzzyIsNull(m_controlForwardAxis)
                         || !qFuzzyIsNull(m_controlRightAxis)
                         || !qFuzzyIsNull(m_controlUpAxis)
                         || !qFuzzyIsNull(m_controlYawAxis)
                         || m_preciseTurnActive;
    const bool preciseChanged = m_preciseTurnActive;
    m_preciseTurnActive = false;
    m_preciseTurnTargetDegrees = 0.0;
    m_preciseTurnRequestedDegrees = 0.0;
    m_controlForwardAxis = 0.0;
    m_controlRightAxis = 0.0;
    m_controlUpAxis = 0.0;
    m_controlYawAxis = 0.0;
    m_controlForwardMetersPerSecond = 0.0;
    m_controlRightMetersPerSecond = 0.0;
    m_controlUpMetersPerSecond = 0.0;
    m_controlYawDegreesPerSecond = 0.0;
    if (preciseChanged) emit preciseTurnChanged();
    if (changed) emit controlCommandChanged();
}

void VehicleConnection::abortKeyboardControl()
{
    ++m_controlGeneration;
    setKeyboardControlTransition(KeyboardControlTransition::None);
    m_controlTimer.stop();
    resetControlAxes();
    setKeyboardControlActive(false);
    if (m_busy) setBusy(false);
}

void VehicleConnection::setConnected(bool value)
{
    if (m_connected == value) return;
    m_connected = value;
    if (!value) {
        abortKeyboardControl();
        cancelMissionStartConfirmation();
        ++m_missionGeneration;
        setMissionProgress(0, 0);
        setMissionState(false, false, false, false,
                        QStringLiteral("连接已断开，等待重新上传任务"));
        ++m_readyGeneration;
        m_readyCandidatePending = false;
        setReady(false);
        // A MAVSDK heartbeat timeout means the current flight state is
        // unknown, not that the vehicle has landed. Preserve the last armed,
        // in-air, and altitude values until telemetry returns or the
        // simulation is definitively stopped by setSitlExpected(false).
        resetTelemetry();
        setFlightMode(QStringLiteral("未知"));
        setHealth(false, false, false, false, false, false, false);
    }
    emit connectedChanged();
}

void VehicleConnection::setHealth(bool gyrometerOk,
                                  bool accelerometerOk,
                                  bool magnetometerOk,
                                  bool localPositionOk,
                                  bool globalPositionOk,
                                  bool homePositionOk,
                                  bool armable)
{
    if (m_gyrometerOk == gyrometerOk
        && m_accelerometerOk == accelerometerOk
        && m_magnetometerOk == magnetometerOk
        && m_localPositionOk == localPositionOk
        && m_globalPositionOk == globalPositionOk
        && m_homePositionOk == homePositionOk
        && m_armable == armable) {
        return;
    }

    m_gyrometerOk = gyrometerOk;
    m_accelerometerOk = accelerometerOk;
    m_magnetometerOk = magnetometerOk;
    m_localPositionOk = localPositionOk;
    m_globalPositionOk = globalPositionOk;
    m_homePositionOk = homePositionOk;
    m_armable = armable;
    emit healthChanged();
}

void VehicleConnection::setTakeoffAltitude(double value)
{
    const double bounded = qBound(1.0, value, 100.0);
    const double rounded = std::round(bounded * 10.0) / 10.0;
    if (qFuzzyCompare(m_takeoffAltitude, rounded)) return;
    m_takeoffAltitude = rounded;
    emit takeoffAltitudeChanged();
}

void VehicleConnection::updateReadyCandidate(bool value)
{
    if (!value) {
        ++m_readyGeneration;
        m_readyCandidatePending = false;
        setReady(false);
        return;
    }

    if (m_ready || m_readyCandidatePending) return;

    m_readyCandidatePending = true;
    const quint64 generation = ++m_readyGeneration;
    setStatusMessage(QStringLiteral("PX4 健康检查通过，正在确认状态稳定…"));
    QTimer::singleShot(5000, this, [this, generation] {
        if (m_connected && !m_ready && generation == m_readyGeneration) {
            m_readyCandidatePending = false;
            setReady(true);
            setStatusMessage(QStringLiteral("PX4 健康状态已稳定，可以安全起飞"));
        }
    });
}

void VehicleConnection::setReady(bool value)
{
    if (m_ready == value) return;
    m_ready = value;
    emit readyChanged();
    if (!value && m_keyboardControlActive) stopKeyboardControl();
}

void VehicleConnection::setArmed(bool value)
{
    if (m_armed == value) return;
    m_armed = value;
    emit armedChanged();
    if (!value && m_keyboardControlActive) abortKeyboardControl();
}

void VehicleConnection::setInAir(bool value)
{
    if (m_inAir == value) return;
    m_inAir = value;
    emit inAirChanged();
    if (!value && m_keyboardControlActive) abortKeyboardControl();
    if (!value && (m_missionBusy || m_missionRunning || m_missionPaused
                   || m_missionStartPending)) {
        cancelMissionStartConfirmation();
        setMissionState(false, m_missionUploaded, false, false,
                        QStringLiteral("飞行器已落地，航点任务已停止"));
    }
}

void VehicleConnection::setBusy(bool value)
{
    if (m_busy == value) return;
    m_busy = value;
    emit busyChanged();
}

void VehicleConnection::setAltitudeMeters(double value)
{
    if (qFuzzyCompare(m_altitudeMeters, value)) return;
    m_altitudeMeters = value;
    emit altitudeMetersChanged();
}

void VehicleConnection::setSpeedMetersPerSecond(double value)
{
    if (qFuzzyCompare(m_speedMetersPerSecond, value)) return;
    m_speedMetersPerSecond = value;
    emit speedMetersPerSecondChanged();
}

void VehicleConnection::setVelocity(double horizontalMetersPerSecond,
                                    double verticalMetersPerSecond)
{
    const bool horizontalChanged = !qFuzzyCompare(m_speedMetersPerSecond,
                                                   horizontalMetersPerSecond);
    const bool verticalChanged = !qFuzzyCompare(m_verticalSpeedMetersPerSecond,
                                                 verticalMetersPerSecond);
    if (!horizontalChanged && !verticalChanged) return;

    if (horizontalChanged) {
        m_speedMetersPerSecond = horizontalMetersPerSecond;
        emit speedMetersPerSecondChanged();
    }
    if (verticalChanged) {
        m_verticalSpeedMetersPerSecond = verticalMetersPerSecond;
    }
    emit velocityChanged();
}

void VehicleConnection::setLocalPosition(double northMeters,
                                          double eastMeters,
                                          double downMeters)
{
    m_localPositionClock.restart();
    const bool changed = !m_localPositionAvailable
                         || !qFuzzyCompare(m_localNorthMeters, northMeters)
                         || !qFuzzyCompare(m_localEastMeters, eastMeters)
                         || !qFuzzyCompare(m_localDownMeters, downMeters);
    if (!changed) return;
    m_localPositionAvailable = true;
    m_localNorthMeters = northMeters;
    m_localEastMeters = eastMeters;
    m_localDownMeters = downMeters;
    emit localPositionChanged();
}

void VehicleConnection::setGlobalPosition(double latitudeDegrees,
                                           double longitudeDegrees)
{
    m_globalPositionClock.restart();
    const bool changed = !m_globalPositionAvailable
                         || !qFuzzyCompare(m_latitudeDegrees, latitudeDegrees)
                         || !qFuzzyCompare(m_longitudeDegrees, longitudeDegrees);
    if (!changed) return;
    const bool availabilityChanged = !m_globalPositionAvailable;
    m_globalPositionAvailable = true;
    m_latitudeDegrees = latitudeDegrees;
    m_longitudeDegrees = longitudeDegrees;
    if (availabilityChanged) emit globalPositionChanged();
}

void VehicleConnection::setSceneRotation(const QQuaternion &rotation)
{
    if (qFuzzyCompare(std::abs(QQuaternion::dotProduct(m_sceneRotation, rotation)), 1.0f)) return;
    m_sceneRotation = rotation;
    emit sceneRotationChanged();
}

void VehicleConnection::setHeadingDegrees(double value)
{
    double normalized = std::fmod(value, 360.0);
    if (normalized < 0.0) normalized += 360.0;
    if (qFuzzyCompare(m_headingDegrees, normalized)) return;
    m_headingDegrees = normalized;
    emit headingChanged();
}

void VehicleConnection::setAttitude(double rollDegrees,
                                    double pitchDegrees,
                                    double yawDegrees)
{
    if (qFuzzyCompare(m_rollDegrees, rollDegrees)
        && qFuzzyCompare(m_pitchDegrees, pitchDegrees)
        && qFuzzyCompare(m_yawDegrees, yawDegrees)) {
        return;
    }
    m_rollDegrees = rollDegrees;
    m_pitchDegrees = pitchDegrees;
    m_yawDegrees = yawDegrees;
    emit attitudeChanged();
}

void VehicleConnection::setAngularVelocity(double rollRateDegreesPerSecond,
                                           double pitchRateDegreesPerSecond,
                                           double yawRateDegreesPerSecond)
{
    m_imuClock.restart();
    if (qFuzzyCompare(m_gyroRollRateDegreesPerSecond, rollRateDegreesPerSecond)
        && qFuzzyCompare(m_gyroPitchRateDegreesPerSecond, pitchRateDegreesPerSecond)
        && qFuzzyCompare(m_gyroYawRateDegreesPerSecond, yawRateDegreesPerSecond)) {
        return;
    }
    m_gyroRollRateDegreesPerSecond = rollRateDegreesPerSecond;
    m_gyroPitchRateDegreesPerSecond = pitchRateDegreesPerSecond;
    m_gyroYawRateDegreesPerSecond = yawRateDegreesPerSecond;
    if (!m_imuAvailable) {
        m_imuAvailable = true;
        emit imuAvailableChanged();
    }
    emit angularVelocityChanged();
}

void VehicleConnection::setBattery(double remainingPercent, double voltageVolts)
{
    m_batteryClock.restart();
    const double safePercent = std::isfinite(remainingPercent)
                                   ? qBound(0.0, remainingPercent, 100.0)
                                   : m_batteryRemainingPercent;
    const double safeVoltage = std::isfinite(voltageVolts) ? voltageVolts
                                                            : m_batteryVoltageVolts;
    const bool changed = !qFuzzyCompare(m_batteryRemainingPercent, safePercent)
                         || !qFuzzyCompare(m_batteryVoltageVolts, safeVoltage);
    m_batteryRemainingPercent = safePercent;
    m_batteryVoltageVolts = safeVoltage;
    if (!m_batteryAvailable) {
        m_batteryAvailable = true;
        emit batteryAvailableChanged();
    }
    if (changed) emit batteryChanged();
}

void VehicleConnection::setGps(int satellites, const QString &fixType)
{
    m_gpsClock.restart();
    const int safeSatellites = qMax(0, satellites);
    const bool changed = m_gpsSatellites != safeSatellites || m_gpsFixType != fixType;
    m_gpsSatellites = safeSatellites;
    m_gpsFixType = fixType;
    if (!m_gpsAvailable) {
        m_gpsAvailable = true;
        emit gpsAvailableChanged();
    }
    if (changed) emit gpsChanged();
}

void VehicleConnection::markCoreTelemetry(quint8 flag)
{
    if (flag == PositionTelemetryFlag) m_positionTelemetryClock.restart();
    else if (flag == VelocityTelemetryFlag) m_velocityTelemetryClock.restart();
    else if (flag == AttitudeTelemetryFlag) m_attitudeTelemetryClock.restart();
    else if (flag == HeadingTelemetryFlag) m_headingTelemetryClock.restart();
    m_coreTelemetryMask |= flag;
    const bool available = (m_coreTelemetryMask & CompleteCoreTelemetryMask)
                           == CompleteCoreTelemetryMask;
    if (m_telemetryAvailable == available) return;
    m_telemetryAvailable = available;
    emit telemetryAvailableChanged();
}

void VehicleConnection::refreshTelemetryFreshness()
{
    if (!m_connected) return;

    auto clearStaleFlag = [this](quint8 flag, const QElapsedTimer &clock) {
        if ((m_coreTelemetryMask & flag) != 0U
            && (!clock.isValid() || clock.elapsed() > CoreTelemetryTimeoutMs)) {
            m_coreTelemetryMask &= static_cast<quint8>(~flag);
        }
    };
    clearStaleFlag(PositionTelemetryFlag, m_positionTelemetryClock);
    clearStaleFlag(VelocityTelemetryFlag, m_velocityTelemetryClock);
    clearStaleFlag(AttitudeTelemetryFlag, m_attitudeTelemetryClock);
    clearStaleFlag(HeadingTelemetryFlag, m_headingTelemetryClock);

    const bool coreAvailable = (m_coreTelemetryMask & CompleteCoreTelemetryMask)
                               == CompleteCoreTelemetryMask;
    if (m_telemetryAvailable != coreAvailable) {
        m_telemetryAvailable = coreAvailable;
        emit telemetryAvailableChanged();
        if (!coreAvailable && m_keyboardControlActive) {
            abortKeyboardControl();
            setStatusMessage(QStringLiteral("键盘控制已安全退出：实时遥测已中断"));
        }
    }

    auto expire = [](bool &available, const QElapsedTimer &clock, qint64 timeout) {
        if (available && (!clock.isValid() || clock.elapsed() > timeout)) {
            available = false;
            return true;
        }
        return false;
    };
    if (expire(m_localPositionAvailable, m_localPositionClock, CoreTelemetryTimeoutMs))
        emit localPositionChanged();
    if (expire(m_globalPositionAvailable, m_globalPositionClock, CoreTelemetryTimeoutMs))
        emit globalPositionChanged();
    if (expire(m_imuAvailable, m_imuClock, CoreTelemetryTimeoutMs))
        emit imuAvailableChanged();
    if (expire(m_batteryAvailable, m_batteryClock, AuxiliaryTelemetryTimeoutMs))
        emit batteryAvailableChanged();
    if (expire(m_gpsAvailable, m_gpsClock, AuxiliaryTelemetryTimeoutMs))
        emit gpsAvailableChanged();
}

void VehicleConnection::resetTelemetry()
{
    m_positionTelemetryClock.invalidate();
    m_velocityTelemetryClock.invalidate();
    m_attitudeTelemetryClock.invalidate();
    m_headingTelemetryClock.invalidate();
    m_localPositionClock.invalidate();
    m_globalPositionClock.invalidate();
    m_imuClock.invalidate();
    m_batteryClock.invalidate();
    m_gpsClock.invalidate();
    m_coreTelemetryMask = 0;
    if (m_telemetryAvailable) {
        m_telemetryAvailable = false;
        emit telemetryAvailableChanged();
    }
    if (m_imuAvailable) {
        m_imuAvailable = false;
        emit imuAvailableChanged();
    }
    if (m_batteryAvailable) {
        m_batteryAvailable = false;
        emit batteryAvailableChanged();
    }
    if (m_gpsAvailable) {
        m_gpsAvailable = false;
        emit gpsAvailableChanged();
    }

    if (m_localPositionAvailable) {
        m_localPositionAvailable = false;
        m_localNorthMeters = 0.0;
        m_localEastMeters = 0.0;
        m_localDownMeters = 0.0;
        emit localPositionChanged();
    }
    if (m_globalPositionAvailable) {
        m_globalPositionAvailable = false;
        m_latitudeDegrees = 0.0;
        m_longitudeDegrees = 0.0;
        emit globalPositionChanged();
    }
    if (!m_sceneRotation.isIdentity()) {
        m_sceneRotation = QQuaternion();
        emit sceneRotationChanged();
    }

    const bool velocityChangedValue = !qFuzzyIsNull(m_speedMetersPerSecond)
                                      || !qFuzzyIsNull(m_verticalSpeedMetersPerSecond);
    m_speedMetersPerSecond = 0.0;
    m_verticalSpeedMetersPerSecond = 0.0;
    if (velocityChangedValue) {
        emit speedMetersPerSecondChanged();
        emit velocityChanged();
    }

    m_headingDegrees = 0.0;
    m_rollDegrees = 0.0;
    m_pitchDegrees = 0.0;
    m_yawDegrees = 0.0;
    m_gyroRollRateDegreesPerSecond = 0.0;
    m_gyroPitchRateDegreesPerSecond = 0.0;
    m_gyroYawRateDegreesPerSecond = 0.0;
    m_batteryRemainingPercent = 0.0;
    m_batteryVoltageVolts = 0.0;
    m_gpsSatellites = 0;
    m_gpsFixType = QStringLiteral("无定位");
    emit headingChanged();
    emit attitudeChanged();
    emit angularVelocityChanged();
    emit batteryChanged();
    emit gpsChanged();
}

void VehicleConnection::setFlightMode(const QString &value)
{
    if (m_flightMode != value) {
        m_flightMode = value;
        emit flightModeChanged();
    }
    if (m_keyboardControlTransition == KeyboardControlTransition::Starting
        && value == QStringLiteral("外部控制")) {
        completeKeyboardControlStart();
    } else if (m_keyboardControlTransition == KeyboardControlTransition::Stopping
               && value != QStringLiteral("外部控制")
               && value != QStringLiteral("未知")) {
        completeKeyboardControlStop(QStringLiteral("键盘控制已退出，飞行器保持悬停"));
    }
    if (m_missionStartPending && value == QStringLiteral("任务")) {
        confirmMissionPlayback();
    }
}

void VehicleConnection::setStatusMessage(const QString &value)
{
    if (m_statusMessage == value) return;
    m_statusMessage = value;
    emit statusMessageChanged();
}

void VehicleConnection::setMissionState(bool busy,
                                        bool uploaded,
                                        bool running,
                                        bool paused,
                                        const QString &message)
{
    if (m_missionBusy == busy
        && m_missionUploaded == uploaded
        && m_missionRunning == running
        && m_missionPaused == paused
        && m_missionStatusMessage == message) {
        return;
    }
    m_missionBusy = busy;
    m_missionUploaded = uploaded;
    m_missionRunning = running;
    m_missionPaused = paused;
    m_missionStatusMessage = message;
    emit missionStateChanged();
}

void VehicleConnection::setMissionProgress(int current, int total)
{
    const int safeTotal = qMax(0, total);
    const int safeCurrent = qBound(0, current, safeTotal);
    if (m_missionCurrentItem == safeCurrent && m_missionTotalItems == safeTotal) return;
    m_missionCurrentItem = safeCurrent;
    m_missionTotalItems = safeTotal;
    emit missionProgressChanged();
}
