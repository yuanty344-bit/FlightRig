#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QQuaternion>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtGlobal>
#include <memory>

class VehicleConnection final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool armed READ armed NOTIFY armedChanged)
    Q_PROPERTY(bool inAir READ inAir NOTIFY inAirChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool gyrometerOk READ gyrometerOk NOTIFY healthChanged)
    Q_PROPERTY(bool accelerometerOk READ accelerometerOk NOTIFY healthChanged)
    Q_PROPERTY(bool magnetometerOk READ magnetometerOk NOTIFY healthChanged)
    Q_PROPERTY(bool localPositionOk READ localPositionOk NOTIFY healthChanged)
    Q_PROPERTY(bool globalPositionOk READ globalPositionOk NOTIFY healthChanged)
    Q_PROPERTY(bool homePositionOk READ homePositionOk NOTIFY healthChanged)
    Q_PROPERTY(bool armable READ armable NOTIFY healthChanged)
    Q_PROPERTY(double altitudeMeters READ altitudeMeters NOTIFY altitudeMetersChanged)
    Q_PROPERTY(double speedMetersPerSecond READ speedMetersPerSecond NOTIFY speedMetersPerSecondChanged)
    Q_PROPERTY(double verticalSpeedMetersPerSecond READ verticalSpeedMetersPerSecond NOTIFY velocityChanged)
    Q_PROPERTY(bool localPositionAvailable READ localPositionAvailable NOTIFY localPositionChanged)
    Q_PROPERTY(double localNorthMeters READ localNorthMeters NOTIFY localPositionChanged)
    Q_PROPERTY(double localEastMeters READ localEastMeters NOTIFY localPositionChanged)
    Q_PROPERTY(double localDownMeters READ localDownMeters NOTIFY localPositionChanged)
    Q_PROPERTY(QQuaternion sceneRotation READ sceneRotation NOTIFY sceneRotationChanged)
    Q_PROPERTY(double headingDegrees READ headingDegrees NOTIFY headingChanged)
    Q_PROPERTY(double rollDegrees READ rollDegrees NOTIFY attitudeChanged)
    Q_PROPERTY(double pitchDegrees READ pitchDegrees NOTIFY attitudeChanged)
    Q_PROPERTY(double yawDegrees READ yawDegrees NOTIFY attitudeChanged)
    Q_PROPERTY(double gyroRollRateDegreesPerSecond READ gyroRollRateDegreesPerSecond NOTIFY angularVelocityChanged)
    Q_PROPERTY(double gyroPitchRateDegreesPerSecond READ gyroPitchRateDegreesPerSecond NOTIFY angularVelocityChanged)
    Q_PROPERTY(double gyroYawRateDegreesPerSecond READ gyroYawRateDegreesPerSecond NOTIFY angularVelocityChanged)
    Q_PROPERTY(double batteryRemainingPercent READ batteryRemainingPercent NOTIFY batteryChanged)
    Q_PROPERTY(double batteryVoltageVolts READ batteryVoltageVolts NOTIFY batteryChanged)
    Q_PROPERTY(int gpsSatellites READ gpsSatellites NOTIFY gpsChanged)
    Q_PROPERTY(QString gpsFixType READ gpsFixType NOTIFY gpsChanged)
    Q_PROPERTY(bool telemetryAvailable READ telemetryAvailable NOTIFY telemetryAvailableChanged)
    Q_PROPERTY(bool imuAvailable READ imuAvailable NOTIFY imuAvailableChanged)
    Q_PROPERTY(bool batteryAvailable READ batteryAvailable NOTIFY batteryAvailableChanged)
    Q_PROPERTY(bool gpsAvailable READ gpsAvailable NOTIFY gpsAvailableChanged)
    Q_PROPERTY(double takeoffAltitude READ takeoffAltitude WRITE setTakeoffAltitude NOTIFY takeoffAltitudeChanged)
    Q_PROPERTY(QString flightMode READ flightMode NOTIFY flightModeChanged)
    Q_PROPERTY(bool keyboardControlActive READ keyboardControlActive NOTIFY keyboardControlActiveChanged)
    Q_PROPERTY(bool keyboardControlTransitioning READ keyboardControlTransitioning NOTIFY keyboardControlTransitioningChanged)
    Q_PROPERTY(bool preciseTurnActive READ preciseTurnActive NOTIFY preciseTurnChanged)
    Q_PROPERTY(double preciseTurnTargetDegrees READ preciseTurnTargetDegrees NOTIFY preciseTurnChanged)
    Q_PROPERTY(double controlSpeedMetersPerSecond READ controlSpeedMetersPerSecond WRITE setControlSpeedMetersPerSecond NOTIFY controlSpeedChanged)
    Q_PROPERTY(double controlVerticalSpeedMetersPerSecond READ controlVerticalSpeedMetersPerSecond WRITE setControlVerticalSpeedMetersPerSecond NOTIFY controlSpeedChanged)
    Q_PROPERTY(double controlYawRateDegreesPerSecond READ controlYawRateDegreesPerSecond WRITE setControlYawRateDegreesPerSecond NOTIFY controlSpeedChanged)
    Q_PROPERTY(QString controlCommandText READ controlCommandText NOTIFY controlCommandChanged)
    Q_PROPERTY(bool globalPositionAvailable READ globalPositionAvailable NOTIFY globalPositionChanged)
    Q_PROPERTY(bool missionBusy READ missionBusy NOTIFY missionStateChanged)
    Q_PROPERTY(bool missionUploaded READ missionUploaded NOTIFY missionStateChanged)
    Q_PROPERTY(bool missionRunning READ missionRunning NOTIFY missionStateChanged)
    Q_PROPERTY(bool missionPaused READ missionPaused NOTIFY missionStateChanged)
    Q_PROPERTY(int missionCurrentItem READ missionCurrentItem NOTIFY missionProgressChanged)
    Q_PROPERTY(int missionTotalItems READ missionTotalItems NOTIFY missionProgressChanged)
    Q_PROPERTY(QString missionStatusMessage READ missionStatusMessage NOTIFY missionStateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit VehicleConnection(QObject *parent = nullptr);
    ~VehicleConnection() override;

    [[nodiscard]] bool connected() const { return m_connected; }
    [[nodiscard]] bool ready() const { return m_ready; }
    [[nodiscard]] bool armed() const { return m_armed; }
    [[nodiscard]] bool inAir() const { return m_inAir; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] bool gyrometerOk() const { return m_gyrometerOk; }
    [[nodiscard]] bool accelerometerOk() const { return m_accelerometerOk; }
    [[nodiscard]] bool magnetometerOk() const { return m_magnetometerOk; }
    [[nodiscard]] bool localPositionOk() const { return m_localPositionOk; }
    [[nodiscard]] bool globalPositionOk() const { return m_globalPositionOk; }
    [[nodiscard]] bool homePositionOk() const { return m_homePositionOk; }
    [[nodiscard]] bool armable() const { return m_armable; }
    [[nodiscard]] double altitudeMeters() const { return m_altitudeMeters; }
    [[nodiscard]] double speedMetersPerSecond() const { return m_speedMetersPerSecond; }
    [[nodiscard]] double verticalSpeedMetersPerSecond() const { return m_verticalSpeedMetersPerSecond; }
    [[nodiscard]] bool localPositionAvailable() const { return m_localPositionAvailable; }
    [[nodiscard]] double localNorthMeters() const { return m_localNorthMeters; }
    [[nodiscard]] double localEastMeters() const { return m_localEastMeters; }
    [[nodiscard]] double localDownMeters() const { return m_localDownMeters; }
    [[nodiscard]] QQuaternion sceneRotation() const { return m_sceneRotation; }
    [[nodiscard]] double headingDegrees() const { return m_headingDegrees; }
    [[nodiscard]] double rollDegrees() const { return m_rollDegrees; }
    [[nodiscard]] double pitchDegrees() const { return m_pitchDegrees; }
    [[nodiscard]] double yawDegrees() const { return m_yawDegrees; }
    [[nodiscard]] double gyroRollRateDegreesPerSecond() const { return m_gyroRollRateDegreesPerSecond; }
    [[nodiscard]] double gyroPitchRateDegreesPerSecond() const { return m_gyroPitchRateDegreesPerSecond; }
    [[nodiscard]] double gyroYawRateDegreesPerSecond() const { return m_gyroYawRateDegreesPerSecond; }
    [[nodiscard]] double batteryRemainingPercent() const { return m_batteryRemainingPercent; }
    [[nodiscard]] double batteryVoltageVolts() const { return m_batteryVoltageVolts; }
    [[nodiscard]] int gpsSatellites() const { return m_gpsSatellites; }
    [[nodiscard]] QString gpsFixType() const { return m_gpsFixType; }
    [[nodiscard]] bool telemetryAvailable() const { return m_telemetryAvailable; }
    [[nodiscard]] bool imuAvailable() const { return m_imuAvailable; }
    [[nodiscard]] bool batteryAvailable() const { return m_batteryAvailable; }
    [[nodiscard]] bool gpsAvailable() const { return m_gpsAvailable; }
    [[nodiscard]] double takeoffAltitude() const { return m_takeoffAltitude; }
    [[nodiscard]] QString flightMode() const { return m_flightMode; }
    [[nodiscard]] bool keyboardControlActive() const { return m_keyboardControlActive; }
    [[nodiscard]] bool keyboardControlTransitioning() const;
    [[nodiscard]] bool preciseTurnActive() const { return m_preciseTurnActive; }
    [[nodiscard]] double preciseTurnTargetDegrees() const { return m_preciseTurnTargetDegrees; }
    [[nodiscard]] double controlSpeedMetersPerSecond() const
    {
        return m_controlSpeedMetersPerSecond;
    }
    [[nodiscard]] double controlVerticalSpeedMetersPerSecond() const
    {
        return m_controlVerticalSpeedMetersPerSecond;
    }
    [[nodiscard]] double controlYawRateDegreesPerSecond() const
    {
        return m_controlYawRateDegreesPerSecond;
    }
    [[nodiscard]] QString controlCommandText() const;
    [[nodiscard]] bool globalPositionAvailable() const { return m_globalPositionAvailable; }
    [[nodiscard]] bool missionBusy() const { return m_missionBusy; }
    [[nodiscard]] bool missionUploaded() const { return m_missionUploaded; }
    [[nodiscard]] bool missionRunning() const { return m_missionRunning; }
    [[nodiscard]] bool missionPaused() const { return m_missionPaused; }
    [[nodiscard]] int missionCurrentItem() const { return m_missionCurrentItem; }
    [[nodiscard]] int missionTotalItems() const { return m_missionTotalItems; }
    [[nodiscard]] QString missionStatusMessage() const { return m_missionStatusMessage; }
    [[nodiscard]] double controlForwardMetersPerSecond() const { return m_controlForwardMetersPerSecond; }
    [[nodiscard]] double controlRightMetersPerSecond() const { return m_controlRightMetersPerSecond; }
    [[nodiscard]] double controlUpMetersPerSecond() const { return m_controlUpMetersPerSecond; }
    [[nodiscard]] double controlYawDegreesPerSecond() const { return m_controlYawDegreesPerSecond; }
    [[nodiscard]] QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void takeoff();
    Q_INVOKABLE void land();
    Q_INVOKABLE void startKeyboardControl();
    Q_INVOKABLE void stopKeyboardControl();
    Q_INVOKABLE void releaseKeyboardControl();
    Q_INVOKABLE void heartbeatKeyboardControl();
    Q_INVOKABLE void turnKeyboardControl(double degrees);
    Q_INVOKABLE void setKeyboardControlAxes(double forward,
                                            double right,
                                            double up,
                                            double yaw);
    Q_INVOKABLE void uploadAndStartMission(const QVariantList &waypoints,
                                           const QString &endAction);
    Q_INVOKABLE void pauseMission();
    Q_INVOKABLE void resumeMission();
    Q_INVOKABLE void clearMission();
    Q_INVOKABLE void abortMissionToHold();
    Q_INVOKABLE void returnToLaunch();
    void setSitlExpected(bool expected);
    void setTakeoffAltitude(double value);
    void setControlSpeedMetersPerSecond(double value);
    void setControlVerticalSpeedMetersPerSecond(double value);
    void setControlYawRateDegreesPerSecond(double value);

signals:
    void connectedChanged();
    void readyChanged();
    void armedChanged();
    void inAirChanged();
    void busyChanged();
    void healthChanged();
    void altitudeMetersChanged();
    void speedMetersPerSecondChanged();
    void velocityChanged();
    void localPositionChanged();
    void sceneRotationChanged();
    void headingChanged();
    void attitudeChanged();
    void angularVelocityChanged();
    void batteryChanged();
    void gpsChanged();
    void telemetryAvailableChanged();
    void imuAvailableChanged();
    void batteryAvailableChanged();
    void gpsAvailableChanged();
    void takeoffAltitudeChanged();
    void flightModeChanged();
    void keyboardControlActiveChanged();
    void keyboardControlTransitioningChanged();
    void preciseTurnChanged();
    void controlSpeedChanged();
    void controlCommandChanged();
    void globalPositionChanged();
    void missionStateChanged();
    void missionProgressChanged();
    void statusMessageChanged();
    void landingCommandFinished(bool success, const QString &message);

private:
    struct Impl;
    friend struct Impl;

    void setConnected(bool value);
    void updateReadyCandidate(bool value);
    void setHealth(bool gyrometerOk,
                   bool accelerometerOk,
                   bool magnetometerOk,
                   bool localPositionOk,
                   bool globalPositionOk,
                   bool homePositionOk,
                   bool armable);
    void setReady(bool value);
    void setArmed(bool value);
    void setInAir(bool value);
    void setBusy(bool value);
    void setAltitudeMeters(double value);
    void setSpeedMetersPerSecond(double value);
    void setVelocity(double horizontalMetersPerSecond, double verticalMetersPerSecond);
    void setLocalPosition(double northMeters, double eastMeters, double downMeters);
    void setGlobalPosition(double latitudeDegrees, double longitudeDegrees);
    void setSceneRotation(const QQuaternion &rotation);
    void setHeadingDegrees(double value);
    void setAttitude(double rollDegrees, double pitchDegrees, double yawDegrees);
    void setAngularVelocity(double rollRateDegreesPerSecond,
                            double pitchRateDegreesPerSecond,
                            double yawRateDegreesPerSecond);
    void setBattery(double remainingPercent, double voltageVolts);
    void setGps(int satellites, const QString &fixType);
    void markCoreTelemetry(quint8 flag);
    void refreshTelemetryFreshness();
    void resetTelemetry();
    void setKeyboardControlActive(bool value);
    void completeKeyboardControlStart();
    void completeKeyboardControlStop(const QString &message);
    void resetControlAxes();
    void abortKeyboardControl();
    void sendLandingCommand();
    void setFlightMode(const QString &value);
    void setStatusMessage(const QString &value);
    void setMissionState(bool busy,
                         bool uploaded,
                         bool running,
                         bool paused,
                         const QString &message);
    void setMissionProgress(int current, int total);
    void startUploadedMission();
    void beginMissionPlayback();
    void confirmMissionPlayback();
    void cancelMissionStartConfirmation();

    enum class KeyboardControlTransition {
        None,
        Starting,
        Stopping
    };
    void setKeyboardControlTransition(KeyboardControlTransition transition);

    std::unique_ptr<Impl> m_impl;
    QTimer m_discoveryTimer;
    QTimer m_controlTimer;
    QTimer m_missionStartConfirmationTimer;
    QTimer m_telemetryFreshnessTimer;
    QElapsedTimer m_controlInputClock;
    QElapsedTimer m_positionTelemetryClock;
    QElapsedTimer m_velocityTelemetryClock;
    QElapsedTimer m_attitudeTelemetryClock;
    QElapsedTimer m_headingTelemetryClock;
    QElapsedTimer m_localPositionClock;
    QElapsedTimer m_globalPositionClock;
    QElapsedTimer m_imuClock;
    QElapsedTimer m_batteryClock;
    QElapsedTimer m_gpsClock;
    quint64 m_readyGeneration{0};
    bool m_readyCandidatePending{false};
    bool m_connected{false};
    bool m_ready{false};
    bool m_armed{false};
    bool m_inAir{false};
    bool m_busy{false};
    bool m_sitlExpected{false};
    bool m_gyrometerOk{false};
    bool m_accelerometerOk{false};
    bool m_magnetometerOk{false};
    bool m_localPositionOk{false};
    bool m_globalPositionOk{false};
    bool m_homePositionOk{false};
    bool m_armable{false};
    double m_altitudeMeters{0.0};
    double m_speedMetersPerSecond{0.0};
    double m_verticalSpeedMetersPerSecond{0.0};
    bool m_localPositionAvailable{false};
    double m_localNorthMeters{0.0};
    double m_localEastMeters{0.0};
    double m_localDownMeters{0.0};
    bool m_globalPositionAvailable{false};
    double m_latitudeDegrees{0.0};
    double m_longitudeDegrees{0.0};
    QQuaternion m_sceneRotation;
    double m_headingDegrees{0.0};
    double m_rollDegrees{0.0};
    double m_pitchDegrees{0.0};
    double m_yawDegrees{0.0};
    double m_gyroRollRateDegreesPerSecond{0.0};
    double m_gyroPitchRateDegreesPerSecond{0.0};
    double m_gyroYawRateDegreesPerSecond{0.0};
    double m_batteryRemainingPercent{0.0};
    double m_batteryVoltageVolts{0.0};
    int m_gpsSatellites{0};
    QString m_gpsFixType{QStringLiteral("无定位")};
    quint8 m_coreTelemetryMask{0};
    bool m_telemetryAvailable{false};
    bool m_imuAvailable{false};
    bool m_batteryAvailable{false};
    bool m_gpsAvailable{false};
    double m_takeoffAltitude{5.0};
    QString m_flightMode{QStringLiteral("未知")};
    bool m_keyboardControlActive{false};
    KeyboardControlTransition m_keyboardControlTransition{KeyboardControlTransition::None};
    bool m_preciseTurnActive{false};
    double m_preciseTurnTargetDegrees{0.0};
    double m_preciseTurnRequestedDegrees{0.0};
    double m_controlSpeedMetersPerSecond{1.0};
    double m_controlVerticalSpeedMetersPerSecond{0.5};
    double m_controlYawRateDegreesPerSecond{30.0};
    double m_controlForwardAxis{0.0};
    double m_controlRightAxis{0.0};
    double m_controlUpAxis{0.0};
    double m_controlYawAxis{0.0};
    double m_controlForwardMetersPerSecond{0.0};
    double m_controlRightMetersPerSecond{0.0};
    double m_controlUpMetersPerSecond{0.0};
    double m_controlYawDegreesPerSecond{0.0};
    quint64 m_controlGeneration{0};
    bool m_missionBusy{false};
    bool m_missionUploaded{false};
    bool m_missionRunning{false};
    bool m_missionPaused{false};
    int m_missionCurrentItem{0};
    int m_missionTotalItems{0};
    quint64 m_missionGeneration{0};
    bool m_missionStartPending{false};
    int m_missionStartAttempts{0};
    QString m_missionStatusMessage{QStringLiteral("尚未上传任务")};
    QString m_statusMessage{QStringLiteral("正在监听 UDP 14540…")};
};
