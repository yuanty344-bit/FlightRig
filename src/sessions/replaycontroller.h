#pragma once
#include "sessions/replaydata.h"

#include "mapping/flightpathgeometry.h"

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QVector3D>

class ReplayController final : public QObject
{
    Q_OBJECT

public:
    using Sample = ReplayData::Sample;

    explicit ReplayController(QObject *parent = nullptr);

    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] bool playing() const { return m_playing; }
    [[nodiscard]] qint64 timeMs() const { return m_timeMs; }
    [[nodiscard]] qint64 durationMs() const { return m_durationMs; }
    [[nodiscard]] double speedMultiplier() const { return m_speedMultiplier; }
    [[nodiscard]] QVector3D position() const { return m_position; }
    [[nodiscard]] QQuaternion rotation() const { return m_rotation; }
    [[nodiscard]] double headingDegrees() const { return m_headingDegrees; }
    [[nodiscard]] double rollDegrees() const { return m_rollDegrees; }
    [[nodiscard]] double pitchDegrees() const { return m_pitchDegrees; }
    [[nodiscard]] double yawDegrees() const { return m_yawDegrees; }
    [[nodiscard]] double altitudeMeters() const { return m_altitudeMeters; }
    [[nodiscard]] double horizontalSpeedMetersPerSecond() const { return m_horizontalSpeedMetersPerSecond; }
    [[nodiscard]] double verticalSpeedMetersPerSecond() const { return m_verticalSpeedMetersPerSecond; }
    [[nodiscard]] FlightPathGeometry *pathGeometry() { return &m_pathGeometry; }
    [[nodiscard]] const FlightPathGeometry *pathGeometry() const { return &m_pathGeometry; }
    [[nodiscard]] QVariantList events() const { return m_events; }
    [[nodiscard]] int currentEventIndex() const { return m_currentEventIndex; }
    [[nodiscard]] QString currentEventLabel() const;
    [[nodiscard]] QVariantList telemetrySeries() const { return m_telemetrySeries; }
    [[nodiscard]] QString errorMessage() const { return m_errorMessage; }
    [[nodiscard]] bool sensorAvailable() const { return m_sensorTimeline.available; }
    [[nodiscard]] QString sensorKind() const { return m_sensorTimeline.kind; }
    [[nodiscard]] QString rgbFrameDataUrl() const { return m_rgbFrameDataUrl; }
    [[nodiscard]] QString depthFrameDataUrl() const { return m_depthFrameDataUrl; }
    [[nodiscard]] QVariantList lidarPoints() const { return m_lidarPoints; }
    [[nodiscard]] const QVariantList &lidarPointsForMapping() const { return m_lidarPoints; }
    [[nodiscard]] double lidarNearestDistanceMeters() const { return m_lidarNearestDistanceMeters; }
    [[nodiscard]] double lidarNearestAngleDegrees() const { return m_lidarNearestAngleDegrees; }
    [[nodiscard]] double lidarScanHz() const { return m_lidarScanHz; }
    [[nodiscard]] double lidarFieldOfViewDegrees() const { return m_lidarFieldOfViewDegrees; }
    [[nodiscard]] double rgbFrameHz() const { return m_rgbFrameHz; }
    [[nodiscard]] double depthFrameHz() const { return m_depthFrameHz; }
    [[nodiscard]] int rgbLatencyMilliseconds() const { return m_rgbLatencyMilliseconds; }
    [[nodiscard]] int depthLatencyMilliseconds() const { return m_depthLatencyMilliseconds; }
    [[nodiscard]] bool rgbStreamInterrupted() const { return m_rgbStreamInterrupted; }
    [[nodiscard]] bool depthStreamInterrupted() const { return m_depthStreamInterrupted; }
    [[nodiscard]] double depthCenterDistanceMeters() const { return m_depthCenterDistanceMeters; }
    [[nodiscard]] double depthNearestDistanceMeters() const { return m_depthNearestDistanceMeters; }
    [[nodiscard]] double depthValidRatio() const { return m_depthValidRatio; }
    [[nodiscard]] const QByteArray &depthMillimetresForMapping() const
    {
        return m_depthMillimetres;
    }
    [[nodiscard]] int depthGridWidthForMapping() const { return m_depthGridWidth; }
    [[nodiscard]] int depthGridHeightForMapping() const { return m_depthGridHeight; }
    [[nodiscard]] double gimbalYawDegrees() const { return m_gimbalYawDegrees; }
    [[nodiscard]] double gimbalPitchDegrees() const { return m_gimbalPitchDegrees; }
    [[nodiscard]] double gimbalRollDegrees() const { return m_gimbalRollDegrees; }
    [[nodiscard]] double gimbalTargetYawDegrees() const { return m_gimbalTargetYawDegrees; }
    [[nodiscard]] double gimbalTargetPitchDegrees() const { return m_gimbalTargetPitchDegrees; }
    [[nodiscard]] bool gimbalFeedbackAvailable() const { return m_gimbalFeedbackAvailable; }
    [[nodiscard]] bool gimbalMoving() const;
    [[nodiscard]] double depthDistanceAt(double normalizedX, double normalizedY) const;
    [[nodiscard]] bool saveCurrentCameraFrame(const QString &stream,
                                              const QString &directory,
                                              QString *savedPath = nullptr) const;

    bool start(const QString &csvPath, const QString &eventsPath = {},
               const QString &startedAtIso = {}, const QString &sessionFolder = {});
    void stop();
    void pause();
    void resume();
    void seek(qint64 timeMs);
    void setSpeedMultiplier(double multiplier);
    void seekEvent(int eventIndex);
    void seekPreviousEvent();
    void seekNextEvent();

    [[nodiscard]] static bool containsPositionSamples(const QString &csvPath);

signals:
    void activeChanged();
    void playingChanged();
    void timeChanged();
    void positionChanged();
    void dataChanged();
    void sensorDataChanged();

private:
    using SensorSample = ReplayData::SensorTimeline::SensorSample;
    void onTick();
    void synchronizeClock();
    void updateInterpolatedState();
    void rebuildPlayedPath();
    void updateCurrentEventIndex();
    void loadEvents(const QString &eventsPath, const QString &startedAtIso);
    void buildTelemetrySeries();
    void loadSensorTimeline(const QString &sessionFolder);
    void updateSensorState();
    [[nodiscard]] QVariantMap loadSensorSample(const SensorSample &sample) const;
    [[nodiscard]] static QVector<Sample> loadSamples(const QString &csvPath, QString *errorMessage = nullptr);

    FlightPathGeometry m_pathGeometry;
    QTimer m_timer;
    QElapsedTimer m_playClock;
    QVector<Sample> m_samples;
    QVariantList m_events;
    QVariantList m_telemetrySeries;
    bool m_active{false};
    bool m_playing{false};
    qint64 m_timeMs{0};
    qint64 m_durationMs{0};
    qint64 m_clockAnchorMs{0};
    qint64 m_lastPathBuildMs{-1};
    double m_speedMultiplier{1.0};
    QVector3D m_position;
    QQuaternion m_rotation;
    double m_headingDegrees{0.0};
    double m_rollDegrees{0.0};
    double m_pitchDegrees{0.0};
    double m_yawDegrees{0.0};
    double m_altitudeMeters{0.0};
    double m_horizontalSpeedMetersPerSecond{0.0};
    double m_verticalSpeedMetersPerSecond{0.0};
    int m_currentEventIndex{-1};
    QString m_errorMessage;
    ReplayData::SensorTimeline m_sensorTimeline;
    int m_rgbSampleIndex{-1};
    int m_depthSampleIndex{-1};
    int m_lidarSampleIndex{-1};
    int m_gimbalTargetSampleIndex{-1};
    int m_gimbalFeedbackSampleIndex{-1};
    QString m_rgbFrameDataUrl;
    QString m_depthFrameDataUrl;
    QVariantList m_lidarPoints;
    double m_lidarNearestDistanceMeters{0.0};
    double m_lidarNearestAngleDegrees{0.0};
    double m_lidarScanHz{0.0};
    double m_lidarFieldOfViewDegrees{0.0};
    QString m_rgbFramePath;
    QString m_depthFramePath;
    double m_rgbFrameHz{0.0};
    double m_depthFrameHz{0.0};
    int m_rgbLatencyMilliseconds{-1};
    int m_depthLatencyMilliseconds{-1};
    bool m_rgbStreamInterrupted{false};
    bool m_depthStreamInterrupted{false};
    bool m_lidarStreamInterrupted{false};
    double m_depthCenterDistanceMeters{0.0};
    double m_depthNearestDistanceMeters{0.0};
    double m_depthValidRatio{0.0};
    double m_gimbalYawDegrees{0.0};
    double m_gimbalPitchDegrees{0.0};
    double m_gimbalRollDegrees{0.0};
    double m_gimbalTargetYawDegrees{0.0};
    double m_gimbalTargetPitchDegrees{0.0};
    bool m_gimbalFeedbackAvailable{false};
    QByteArray m_depthMillimetres;
    int m_depthGridWidth{0};
    int m_depthGridHeight{0};
};
