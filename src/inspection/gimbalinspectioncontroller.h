#pragma once

#include <QObject>
#include <QPointF>
#include <QElapsedTimer>
#include <QHash>
#include <QQuaternion>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

// Known-world coordinate pointing, not visual tracking. Never emits aircraft commands.
class GimbalInspectionController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool picking READ picking WRITE setPicking NOTIFY changed)
    Q_PROPERTY(bool historical READ historical NOTIFY changed)
    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY changed)
    Q_PROPERTY(bool canRecord READ canRecord NOTIFY changed)
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString worldId READ worldId NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString guidance READ guidance NOTIFY changed)
    Q_PROPERTY(QVector3D target READ target NOTIFY changed)
    Q_PROPERTY(QVariantList views READ views NOTIFY changed)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY changed)
    Q_PROPERTY(int savedCount READ savedCount NOTIFY changed)
    Q_PROPERTY(QVariantMap observation READ observation NOTIFY changed)
    Q_PROPERTY(int selectedView READ selectedView WRITE setSelectedView NOTIFY changed)
    Q_PROPERTY(int guideView READ guideView NOTIFY changed)
    Q_PROPERTY(double distance READ distance NOTIFY changed)
public:
    explicit GimbalInspectionController(QObject *parent = nullptr);
    bool active() const { return m_active; }
    bool picking() const { return m_picking; }
    bool historical() const { return m_historical; }
    bool hasTarget() const { return m_hasTarget; }
    bool canRecord() const { return m_canRecord; }
    bool automatic() const { return m_automatic; }
    QString name() const { return m_name; }
    QString worldId() const { return m_world; }
    QString status() const { return m_status; }
    QString guidance() const;
    QVector3D target() const { return m_target; }
    QVariantList views() const;
    int completedCount() const;
    int savedCount() const;
    QVariantMap observation() const;
    int selectedView() const { return m_selected; }
    int guideView() const { return m_guide; }
    double distance() const { return m_distance; }
    void prepareSession(const QString &folder, const QString &world, bool record, qint64 epochMs);
    void updatePose(const QVector3D &position, double heading, double pitch, double roll,
                    bool ready, bool feedbackFresh, double yaw, double tilt, double gimbalRoll);
    void ingestFrame(const QByteArray &jpeg, quint64 sequence, qint64 epochMs,
                     const QVector3D &position, const QQuaternion &orientation, bool validPose);
    QVariantMap finishSession();
    bool loadSession(const QString &folder);
    void clear();
    void seek(qint64 elapsedMs);
    static QQuaternion aircraftOrientation(double heading, double pitch, double roll);
    static QVector3D cameraOrigin(double yaw, double pitch);
    static QVector3D cameraDirection(double yaw, double pitch);
    static QPointF aimAngles(const QVector3D &bodyTarget);
    Q_INVOKABLE bool selectRay(const QVector3D &origin, const QVector3D &direction);
    Q_INVOKABLE void setPicking(bool value);
    Q_INVOKABLE void setAutomatic(bool value);
    Q_INVOKABLE void setName(const QString &value);
    Q_INVOKABLE void setSelectedView(int value);
    Q_INVOKABLE bool start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void retake();
    Q_INVOKABLE void capture();
signals:
    void changed();
    void commandRequested(double yaw, double pitch);
private:
    void setStatus(const QString &text);
    bool save();
    QVariantMap summary() const;
    bool viewMatches(const QVector3D &camera) const;
    bool viewMatches(const QVector3D &camera, int index) const;
    void matchUnfinishedView(const QVector3D &camera);
    void diagnose(const QString &reason);
    QString imagePath(const QVariantMap &view) const;
    void resetViews();
    QString m_folder, m_world, m_name{QStringLiteral("巡检目标")};
    QString m_status{QStringLiteral("在三维视图选择建筑表面目标")};
    QString m_frameBlock;
    bool m_active{false}, m_picking{false}, m_historical{false}, m_hasTarget{false};
    bool m_canRecord{false}, m_automatic{true}, m_ready{false}, m_aligned{false}, m_capture{false};
    QVector3D m_target, m_normal, m_position;
    QQuaternion m_orientation;
    double m_referenceAzimuth{0}, m_referenceElevation{0}, m_yaw{0}, m_tilt{0};
    double m_distance{0};
    double m_alignmentError{-1};
    int m_selected{0}, m_guide{0};
    qint64 m_epochMs{0}, m_replayMs{-1}, m_bytes{0};
    quint64 m_sequence{0};
    QElapsedTimer m_stable, m_candidate, m_poseClock;
    QElapsedTimer m_frameBlockClock;
    QVariantList m_views;
    QHash<QString, qint64> m_diagnosticTimes;
    qint64 m_diagnosticBytes{0};
    bool m_diagnosticFailed{false};
};
