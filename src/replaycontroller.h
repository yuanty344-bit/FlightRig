#pragma once

#include "flightpathgeometry.h"

#include <QElapsedTimer>
#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <QVector3D>

class ReplayController final : public QObject
{
    Q_OBJECT

public:
    struct Sample
    {
        qint64 elapsedMs{0};
        QVector3D position;
        QQuaternion rotation;
        double headingDegrees{0.0};
        double rollDegrees{0.0};
        double pitchDegrees{0.0};
        double yawDegrees{0.0};
        double altitudeMeters{0.0};
        double horizontalSpeedMetersPerSecond{0.0};
        double verticalSpeedMetersPerSecond{0.0};
    };

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

    bool start(const QString &csvPath, const QString &eventsPath = {}, const QString &startedAtIso = {});
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

private:
    void onTick();
    void synchronizeClock();
    void updateInterpolatedState();
    void rebuildPlayedPath();
    void updateCurrentEventIndex();
    void loadEvents(const QString &eventsPath, const QString &startedAtIso);
    void buildTelemetrySeries();
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
};
