#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <QQuaternion>
#include <QVariantMap>

// Read-only file parsing. No playback clock, QObject or rendering resources.
namespace ReplayData {
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


struct SensorTimeline {
    struct SensorSample {
        qint64 elapsedMs{0};
        qint64 fileOffset{-1};
        QVariantMap metadata;
    };
    QVector<SensorSample> rgbSamples;
    QVector<SensorSample> depthSamples;
    QVector<SensorSample> lidarSamples;
    QVector<SensorSample> gimbalTargetSamples;
    QVector<SensorSample> gimbalFeedbackSamples;

    bool available{false};
    QString kind;
    QString path;
    void load(const QString &sessionFolder);
    QVariantMap read(const SensorSample &sample) const;
};
QVector<Sample> loadSamples(const QString &csvPath, QString *errorMessage = nullptr);
}
