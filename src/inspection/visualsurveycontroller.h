#pragma once

#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

class VisualSurveyController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY surveyChanged)
    Q_PROPERTY(bool historical READ historical NOTIFY stateChanged)
    Q_PROPERTY(int keyframeCount READ keyframeCount NOTIFY surveyChanged)
    Q_PROPERTY(int goodKeyframeCount READ goodKeyframeCount NOTIFY surveyChanged)
    Q_PROPERTY(int warningKeyframeCount READ warningKeyframeCount NOTIFY surveyChanged)
    Q_PROPERTY(int rejectedCount READ rejectedCount NOTIFY surveyChanged)
    Q_PROPERTY(int duplicateCount READ duplicateCount NOTIFY surveyChanged)
    Q_PROPERTY(double coveragePercent READ coveragePercent NOTIFY surveyChanged)
    Q_PROPERTY(double spatialCoveragePercent READ spatialCoveragePercent NOTIFY surveyChanged)
    Q_PROPERTY(double qualityPassPercent READ qualityPassPercent NOTIFY surveyChanged)
    Q_PROPERTY(int observedViewCount READ observedViewCount NOTIFY surveyChanged)
    Q_PROPERTY(int capturedViewCount READ capturedViewCount NOTIFY surveyChanged)
    Q_PROPERTY(int visitedAreaCount READ visitedAreaCount NOTIFY surveyChanged)
    Q_PROPERTY(QString coverageDefinition READ coverageDefinition NOTIFY surveyChanged)
    Q_PROPERTY(QVariantList keyframes READ keyframes NOTIFY surveyChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(QVariantMap selectedKeyframe READ selectedKeyframe NOTIFY selectedIndexChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)

public:
    explicit VisualSurveyController(QObject *parent = nullptr);

    bool active() const { return m_active; }
    bool available() const { return !m_keyframes.isEmpty(); }
    bool historical() const { return m_historical; }
    int keyframeCount() const { return m_keyframes.size(); }
    int goodKeyframeCount() const { return m_goodCount; }
    int warningKeyframeCount() const { return m_warningCount; }
    int rejectedCount() const { return m_rejectedCount; }
    int duplicateCount() const { return m_duplicateCount; }
    double coveragePercent() const;
    double spatialCoveragePercent() const { return coveragePercent(); }
    double qualityPassPercent() const;
    int observedViewCount() const
    {
        return m_observedCells.isEmpty() ? m_loadedObservedViewCount : m_observedCells.size();
    }
    int capturedViewCount() const
    {
        return m_observedCells.isEmpty() ? m_loadedCapturedViewCount : m_capturedCells.size();
    }
    int visitedAreaCount() const
    {
        return m_observedCells.isEmpty() ? m_loadedVisitedAreaCount : m_visitedAreas.size();
    }
    QString coverageDefinition() const
    {
        return m_historical && m_loadedCoverageEstimated
                   ? QStringLiteral("legacy-candidate-frame-estimate")
                   : QStringLiteral("candidate-view-grid");
    }
    QVariantList keyframes() const { return m_keyframes; }
    int selectedIndex() const { return m_selectedIndex; }
    QVariantMap selectedKeyframe() const;
    QString statusMessage() const { return m_statusMessage; }

    void prepareSession(const QString &sessionFolder, bool canRecord);
    void ingestFrame(const QByteArray &jpeg, int width, int height,
                     qint64 timestampMs, const QVector3D &position,
                     double headingDegrees, double pitchDegrees, bool poseValid);
    QVariantMap finishSession();
    bool loadFromSession(const QString &sessionFolder);
    void clear();
    static QVariantMap inspectFrameQuality(const QByteArray &jpeg);

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setSelectedIndex(int index);
    Q_INVOKABLE bool updateSelected(const QString &notes, bool anomaly);
    Q_INVOKABLE void selectNearest(double eastMeters, double northMeters);

signals:
    void stateChanged();
    void surveyChanged();
    void selectedIndexChanged();

private:
    struct ImageQuality {
        double brightness{0.0};
        double sharpness{0.0};
        double edgeRatio{0.0};
        quint64 hash{0};
        int score{0};
        bool hardReject{false};
        QString label;
        QString reason;
    };
    static double normalizedAngleDifference(double a, double b);
    static int hammingDistance(quint64 a, quint64 b);
    static ImageQuality analyze(const QByteArray &jpeg);
    bool writeManifest(bool completed);
    QVariantMap summary() const;
    void rebuildCounts();
    void setStatus(const QString &message);
    QString surveyDirectory() const;

    bool m_active{false};
    bool m_historical{false};
    bool m_canRecord{false};
    QString m_sessionFolder;
    QVariantList m_keyframes;
    int m_selectedIndex{-1};
    int m_goodCount{0};
    int m_warningCount{0};
    int m_rejectedCount{0};
    int m_duplicateCount{0};
    int m_candidateCount{0};
    qint64 m_startedMs{0};
    qint64 m_lastCandidateMs{0};
    qint64 m_lastAcceptedMs{0};
    QVector3D m_lastAcceptedPosition;
    double m_lastAcceptedHeading{0.0};
    quint64 m_lastAcceptedHash{0};
    QSet<QString> m_observedCells;
    QSet<QString> m_capturedCells;
    QSet<QString> m_visitedAreas;
    double m_loadedCoveragePercent{0.0};
    double m_loadedQualityPassPercent{0.0};
    int m_loadedObservedViewCount{0};
    int m_loadedCapturedViewCount{0};
    int m_loadedVisitedAreaCount{0};
    bool m_loadedCoverageEstimated{false};
    QString m_statusMessage{QStringLiteral("尚未开始视觉巡检")};
};
