#pragma once

#include <QElapsedTimer>
#include <QQuick3DGeometry>
#include <QVector>
#include <QVector3D>

class FlightPathGeometry final : public QQuick3DGeometry
{
    Q_OBJECT
    Q_PROPERTY(int pointCount READ pointCount NOTIFY pointCountChanged)
    Q_PROPERTY(double lengthMeters READ lengthMeters NOTIFY pointCountChanged)
    Q_PROPERTY(double widthMeters READ widthMeters WRITE setWidthMeters NOTIFY displaySettingsChanged)
    Q_PROPERTY(int maxVisibleLengthMeters READ maxVisibleLengthMeters WRITE setMaxVisibleLengthMeters NOTIFY displaySettingsChanged)

public:
    explicit FlightPathGeometry(QQuick3DObject *parent = nullptr);

    [[nodiscard]] int pointCount() const { return m_points.size(); }
    [[nodiscard]] double lengthMeters() const { return m_lengthMeters; }
    [[nodiscard]] double widthMeters() const { return m_widthMeters; }
    [[nodiscard]] int maxVisibleLengthMeters() const { return m_maxVisibleLengthMeters; }
    void appendPoint(const QVector3D &point);
    void setPoints(const QVector<QVector3D> &points);
    void clearPath();
    void setWidthMeters(double value);
    void setMaxVisibleLengthMeters(int value);
    void setDashed(bool dashed);

signals:
    void pointCountChanged();
    void displaySettingsChanged();

private:
    void rebuildGeometry();

    QVector<QVector3D> m_points;
    QElapsedTimer m_sampleClock;
    double m_lengthMeters{0.0};
    double m_widthMeters{0.06};
    int m_maxVisibleLengthMeters{100};
    bool m_dashed{false};
};
