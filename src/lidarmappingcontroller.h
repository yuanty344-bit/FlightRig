#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class LidarMappingController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY mapChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QString mapDataUrl READ mapDataUrl NOTIFY imageChanged)
    Q_PROPERTY(bool visualizationActive READ visualizationActive WRITE setVisualizationActive
                   NOTIFY visualizationActiveChanged)
    Q_PROPERTY(double resolutionMeters READ resolutionMeters CONSTANT)
    Q_PROPERTY(double exploredAreaSquareMeters READ exploredAreaSquareMeters NOTIFY mapChanged)
    Q_PROPERTY(double widthMeters READ widthMeters NOTIFY mapChanged)
    Q_PROPERTY(double heightMeters READ heightMeters NOTIFY mapChanged)
    Q_PROPERTY(int occupiedCellCount READ occupiedCellCount NOTIFY mapChanged)
    Q_PROPERTY(int processedScanCount READ processedScanCount NOTIFY mapChanged)
    Q_PROPERTY(int skippedScanCount READ skippedScanCount NOTIFY mapChanged)
    Q_PROPERTY(int tiltRejectedScanCount READ tiltRejectedScanCount NOTIFY mapChanged)
    Q_PROPERTY(int keyframeCount READ keyframeCount NOTIFY mapChanged)
    Q_PROPERTY(int scanMatchedCount READ scanMatchedCount NOTIFY mapChanged)
    Q_PROPERTY(int loopClosureCount READ loopClosureCount NOTIFY mapChanged)
    Q_PROPERTY(int poseGraphConstraintCount READ poseGraphConstraintCount NOTIFY mapChanged)
    Q_PROPERTY(int optimizationCount READ optimizationCount NOTIFY mapChanged)
    Q_PROPERTY(double localizationConfidence READ localizationConfidence NOTIFY mapChanged)
    Q_PROPERTY(double correctionDistanceMeters READ correctionDistanceMeters NOTIFY mapChanged)
    Q_PROPERTY(double correctionHeadingDegrees READ correctionHeadingDegrees NOTIFY mapChanged)
    Q_PROPERTY(QString lastSavedPath READ lastSavedPath NOTIFY lastSavedPathChanged)

public:
    explicit LidarMappingController(QObject *parent = nullptr);

    [[nodiscard]] bool available() const { return m_confirmedCellCount > 0; }
    [[nodiscard]] QString statusMessage() const { return m_statusMessage; }
    [[nodiscard]] QString mapDataUrl() const { return m_mapDataUrl; }
    [[nodiscard]] bool visualizationActive() const { return m_visualizationActive; }
    [[nodiscard]] double resolutionMeters() const { return ResolutionMeters; }
    [[nodiscard]] double exploredAreaSquareMeters() const;
    [[nodiscard]] double widthMeters() const;
    [[nodiscard]] double heightMeters() const;
    [[nodiscard]] int occupiedCellCount() const { return m_occupiedCellCount; }
    [[nodiscard]] int processedScanCount() const { return m_processedScanCount; }
    [[nodiscard]] int skippedScanCount() const { return m_skippedScanCount; }
    [[nodiscard]] int tiltRejectedScanCount() const { return m_tiltRejectedScanCount; }
    [[nodiscard]] int keyframeCount() const { return static_cast<int>(m_keyframes.size()); }
    [[nodiscard]] int scanMatchedCount() const { return m_scanMatchedCount; }
    [[nodiscard]] int loopClosureCount() const { return m_loopClosureCount; }
    [[nodiscard]] int poseGraphConstraintCount() const
    {
        return static_cast<int>(m_poseConstraints.size());
    }
    [[nodiscard]] int optimizationCount() const { return m_optimizationCount; }
    [[nodiscard]] double localizationConfidence() const { return m_localizationConfidence; }
    [[nodiscard]] double correctionDistanceMeters() const;
    [[nodiscard]] double correctionHeadingDegrees() const { return m_headingCorrectionDegrees; }
    [[nodiscard]] QString lastSavedPath() const { return m_lastSavedPath; }

    void ingestScan(const QVariantList &points,
                    double eastMeters,
                    double northMeters,
                    double headingDegrees,
                    double rollDegrees,
                    double pitchDegrees,
                    double altitudeMeters,
                    qint64 timestampMs,
                    bool poseValid);
    [[nodiscard]] QVariantMap saveToDirectory(const QString &directoryPath);
    [[nodiscard]] QVariantMap summary() const;

    Q_INVOKABLE void clear();
    Q_INVOKABLE bool saveMap(const QString &directoryPath);
    Q_INVOKABLE void setVisualizationActive(bool active);
    Q_INVOKABLE void refreshVisualization();

signals:
    void mapChanged();
    void statusChanged();
    void imageChanged();
    void visualizationActiveChanged();
    void lastSavedPathChanged();

private:
    static constexpr double ResolutionMeters = 0.25;
    // A free cell also needs repeated observations before it is exported as
    // confirmed free space. This avoids one sparse scan carving a permanent
    // white fan through otherwise unknown map space.
    static constexpr int FreeDelta = -1;
    // A single return is not enough to create a permanent obstacle. This
    // suppresses isolated ground hits while repeated wall returns accumulate.
    static constexpr int OccupiedDelta = 2;
    static constexpr int MinimumLogOdds = -20;
    static constexpr int MaximumLogOdds = 20;
    static constexpr int OccupiedThreshold = 5;
    static constexpr int FreeThreshold = -3;
    static constexpr qint64 MinimumScanIntervalMs = 500;
    static constexpr int ScanMatchingStride = 8;
    static constexpr double MinimumMappingAltitudeMeters = 2.0;
    static constexpr double MaximumMappingTiltDegrees = 3.5;

    [[nodiscard]] static quint64 cellKey(int x, int y);
    static void unpackCellKey(quint64 key, int *x, int *y);
    [[nodiscard]] QPointF worldToCell(double eastMeters, double northMeters) const;
    struct LocalScanPoint {
        double rightMeters{0.0};
        double forwardMeters{0.0};
        double rangeMeters{0.0};
        bool obstacleHit{false};
    };
    struct SlamPose {
        QPointF position;
        double headingDegrees{0.0};
    };
    struct MatchResult {
        SlamPose pose;
        double confidence{0.0};
        int matchedPointCount{0};
        bool accepted{false};
    };
    struct Keyframe {
        SlamPose pose;
        SlamPose rawPose;
        QVector<LocalScanPoint> scan;
        QVector<float> descriptor;
        qint64 timestampMs{0};
    };
    struct PoseConstraint {
        int fromIndex{-1};
        int toIndex{-1};
        QPointF relativePosition;
        double relativeHeadingDegrees{0.0};
        double translationWeight{1.0};
        double headingWeight{1.0};
        bool loopClosure{false};
    };
    [[nodiscard]] static double normalizedHeading(double headingDegrees);
    [[nodiscard]] static double signedAngleDifference(double first, double second);
    [[nodiscard]] QPointF transformPoint(const LocalScanPoint &point,
                                         const SlamPose &pose) const;
    [[nodiscard]] double occupiedSupport(double eastMeters, double northMeters) const;
    [[nodiscard]] double scanMatchScore(const QVector<LocalScanPoint> &points,
                                        const SlamPose &pose,
                                        int *matchedPointCount) const;
    [[nodiscard]] MatchResult matchScan(const QVector<LocalScanPoint> &points,
                                        const SlamPose &predictedPose,
                                        bool loopClosureCandidate) const;
    [[nodiscard]] QVector<float> scanDescriptor(const QVector<LocalScanPoint> &points,
                                                double headingDegrees) const;
    [[nodiscard]] int findLoopClosureCandidate(const QVector<LocalScanPoint> &points,
                                               const SlamPose &predictedPose) const;
    [[nodiscard]] MatchResult matchScanToKeyframeSubmap(
        const QVector<LocalScanPoint> &points,
        const SlamPose &predictedPose,
        int keyframeIndex) const;
    [[nodiscard]] static QPointF relativePosition(const SlamPose &from,
                                                  const SlamPose &to);
    void updateSlamPose(double rawEastMeters,
                        double rawNorthMeters,
                        double rawHeadingDegrees,
                        const QVector<LocalScanPoint> &points);
    void appendKeyframeIfNeeded(const QVector<LocalScanPoint> &points,
                                const SlamPose &rawPose,
                                qint64 timestampMs);
    void optimizePoseGraph();
    void rebuildMapFromKeyframes();
    void integrateScanAtPose(const QVector<LocalScanPoint> &points,
                             const SlamPose &pose,
                             bool updateCurrentHits);
    void resetOccupancyGrid();
    void updateCell(int x, int y, int delta);
    void traceRay(int startX, int startY, int endX, int endY);
    void decayWeakObstacles();
    void scheduleRender();
    void renderMap();
    void setStatusMessage(const QString &message);
    [[nodiscard]] bool writePortableMap(const QString &directoryPath,
                                        QString *pngPath,
                                        QString *errorMessage) const;
    [[nodiscard]] bool writePoseGraph(const QString &directoryPath,
                                      QString *errorMessage) const;

    QHash<quint64, qint8> m_cells;
    QHash<quint64, int> m_lastHitScan;
    QVector<Keyframe> m_keyframes;
    QVector<PoseConstraint> m_poseConstraints;
    QVector<QPointF> m_track;
    QVector<QPointF> m_currentHits;
    QTimer m_renderTimer;
    QString m_statusMessage{QStringLiteral("等待二维雷达和飞行器本地位姿")};
    QString m_mapDataUrl;
    QString m_lastSavedPath;
    qint64 m_lastAcceptedTimestampMs{-1};
    QPointF m_currentPose;
    double m_currentHeadingDegrees{0.0};
    QPointF m_lastRawPose;
    double m_lastRawHeadingDegrees{0.0};
    QPointF m_positionCorrection;
    double m_headingCorrectionDegrees{0.0};
    double m_localizationConfidence{0.0};
    int m_scanMatchedCount{0};
    int m_loopClosureCount{0};
    int m_optimizationCount{0};
    int m_lastLoopClosureKeyframe{-1000};
    int m_lastLoopClosureAttemptScan{-1000};
    int m_pendingLoopClosureIndex{-1};
    SlamPose m_pendingLoopMatchedPose;
    double m_pendingLoopConfidence{0.0};
    bool m_slamPoseInitialized{false};
    int m_stationaryScanCounter{0};
    int m_minCellX{0};
    int m_maxCellX{0};
    int m_minCellY{0};
    int m_maxCellY{0};
    int m_occupiedCellCount{0};
    int m_confirmedCellCount{0};
    int m_processedScanCount{0};
    int m_skippedScanCount{0};
    int m_tiltRejectedScanCount{0};
    bool m_boundsValid{false};
    bool m_visualizationActive{false};
    bool m_mapDirty{false};
};
