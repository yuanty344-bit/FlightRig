#pragma once

#include <QHash>
#include <QObject>
#include <QQuick3DGeometry>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QVector3D>

class DepthSurfaceGeometry final : public QQuick3DGeometry
{
    Q_OBJECT
public:
    explicit DepthSurfaceGeometry(QQuick3DObject *parent = nullptr);
    void setSurfaceCells(const QHash<quint64, QVector3D> &centers, float sizeMeters);
    void setVoxelCenters(const QVector<QVector3D> &centers, float sizeMeters);
    void clearGeometry();
    [[nodiscard]] int triangleCount() const { return m_triangleCount; }
private:
    int m_triangleCount{0};
};

// Persistent, pose-referenced DepthFusion map. Valid rays clear traversed free
// space, a weighted TSDF fuses the surface band, and an 8 m chunk index keeps
// long flights independent from the former 80,000-voxel ceiling.
class DepthMappingController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY mapChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool visualizationActive READ visualizationActive WRITE setVisualizationActive NOTIFY visualizationActiveChanged)
    Q_PROPERTY(QQuick3DGeometry *obstacleGeometry READ obstacleGeometry CONSTANT)
    Q_PROPERTY(QQuick3DGeometry *dangerGeometry READ dangerGeometry CONSTANT)
    Q_PROPERTY(double resolutionMeters READ resolutionMeters CONSTANT)
    Q_PROPERTY(int confirmedVoxelCount READ confirmedVoxelCount NOTIFY mapChanged)
    Q_PROPERTY(int chunkCount READ chunkCount NOTIFY mapChanged)
    Q_PROPERTY(int meshTriangleCount READ meshTriangleCount NOTIFY mapChanged)
    Q_PROPERTY(int clearedVoxelCount READ clearedVoxelCount NOTIFY mapChanged)
    Q_PROPERTY(int noiseVoxelCount READ noiseVoxelCount NOTIFY mapChanged)
    Q_PROPERTY(int processedFrameCount READ processedFrameCount NOTIFY mapChanged)
    Q_PROPERTY(int skippedFrameCount READ skippedFrameCount NOTIFY mapChanged)
    Q_PROPERTY(int filteredGroundPointCount READ filteredGroundPointCount NOTIFY mapChanged)
    Q_PROPERTY(double nearestObstacleMeters READ nearestObstacleMeters NOTIFY mapChanged)
    Q_PROPERTY(double corridorClearanceMeters READ corridorClearanceMeters NOTIFY mapChanged)
    Q_PROPERTY(double minimumObservedObstacleMeters READ minimumObservedObstacleMeters NOTIFY mapChanged)
    Q_PROPERTY(double minimumObservedCorridorMeters READ minimumObservedCorridorMeters NOTIFY mapChanged)
    Q_PROPERTY(double coveragePercent READ coveragePercent NOTIFY mapChanged)
    Q_PROPERTY(double noisePercent READ noisePercent NOTIFY mapChanged)
    Q_PROPERTY(double mapConfidencePercent READ mapConfidencePercent NOTIFY mapChanged)
    Q_PROPERTY(bool groundPlaneAvailable READ groundPlaneAvailable NOTIFY mapChanged)
    Q_PROPERTY(double groundSlopeDegrees READ groundSlopeDegrees NOTIFY mapChanged)
    Q_PROPERTY(bool loadedFromDisk READ loadedFromDisk NOTIFY mapChanged)
    Q_PROPERTY(QString safetyLevel READ safetyLevel NOTIFY mapChanged)
    Q_PROPERTY(QString safetyLabel READ safetyLabel NOTIFY mapChanged)
    Q_PROPERTY(double widthMeters READ widthMeters NOTIFY mapChanged)
    Q_PROPERTY(double depthMeters READ depthMeters NOTIFY mapChanged)
    Q_PROPERTY(double heightMeters READ heightMeters NOTIFY mapChanged)
    Q_PROPERTY(QVector3D mapCenter READ mapCenter NOTIFY mapChanged)
    Q_PROPERTY(QString lastSavedPath READ lastSavedPath NOTIFY lastSavedPathChanged)

public:
    explicit DepthMappingController(QObject *parent = nullptr);
    [[nodiscard]] bool available() const { return m_confirmedVoxelCount > 0; }
    [[nodiscard]] QString statusMessage() const { return m_statusMessage; }
    [[nodiscard]] bool visualizationActive() const { return m_visualizationActive; }
    [[nodiscard]] QQuick3DGeometry *obstacleGeometry() { return &m_obstacleGeometry; }
    [[nodiscard]] QQuick3DGeometry *dangerGeometry() { return &m_dangerGeometry; }
    [[nodiscard]] double resolutionMeters() const { return ResolutionMeters; }
    [[nodiscard]] int confirmedVoxelCount() const { return m_confirmedVoxelCount; }
    [[nodiscard]] int chunkCount() const { return m_chunkMembers.size(); }
    [[nodiscard]] int meshTriangleCount() const { return m_meshTriangleCount; }
    [[nodiscard]] int clearedVoxelCount() const { return m_clearedVoxelCount; }
    [[nodiscard]] int noiseVoxelCount() const { return m_noiseVoxelCount; }
    [[nodiscard]] int processedFrameCount() const { return m_processedFrameCount; }
    [[nodiscard]] int skippedFrameCount() const { return m_skippedFrameCount; }
    [[nodiscard]] int filteredGroundPointCount() const { return m_filteredGroundPointCount; }
    [[nodiscard]] double nearestObstacleMeters() const { return m_nearestObstacleMeters; }
    [[nodiscard]] double corridorClearanceMeters() const { return m_corridorClearanceMeters; }
    [[nodiscard]] double minimumObservedObstacleMeters() const { return m_minimumObservedObstacleMeters; }
    [[nodiscard]] double minimumObservedCorridorMeters() const { return m_minimumObservedCorridorMeters; }
    [[nodiscard]] double coveragePercent() const { return m_coveragePercent; }
    [[nodiscard]] double noisePercent() const { return m_noisePercent; }
    [[nodiscard]] double mapConfidencePercent() const { return m_mapConfidencePercent; }
    [[nodiscard]] bool groundPlaneAvailable() const { return m_groundPlaneAvailable; }
    [[nodiscard]] double groundSlopeDegrees() const { return m_groundSlopeDegrees; }
    [[nodiscard]] bool loadedFromDisk() const { return m_loadedFromDisk; }
    [[nodiscard]] QString safetyLevel() const { return m_safetyLevel; }
    [[nodiscard]] QString safetyLabel() const;
    [[nodiscard]] double widthMeters() const;
    [[nodiscard]] double depthMeters() const;
    [[nodiscard]] double heightMeters() const;
    [[nodiscard]] QVector3D mapCenter() const;
    [[nodiscard]] QString lastSavedPath() const { return m_lastSavedPath; }

    void ingestDepthFrame(const QByteArray &depthMillimetres, int gridWidth, int gridHeight,
                          const QVector3D &position, double headingDegrees,
                          double rollDegrees, double pitchDegrees,
                          qint64 timestampMs, bool poseValid);
    [[nodiscard]] QVariantMap saveToDirectory(const QString &directoryPath);
    [[nodiscard]] QVariantMap summary() const;

    Q_INVOKABLE void clear();
    Q_INVOKABLE bool saveMap(const QString &directoryPath);
    Q_INVOKABLE bool loadMap(const QString &directoryPath);
    Q_INVOKABLE QVariantMap analyzeRoute(const QVariantList &waypoints, double clearanceMeters = 1.0) const;
    Q_INVOKABLE void setVisualizationActive(bool active);
    Q_INVOKABLE void refreshVisualization();

signals:
    void mapChanged();
    void statusChanged();
    void visualizationActiveChanged();
    void lastSavedPathChanged();

private:
    struct Voxel {
        QVector3D center;
        float tsdf{1.0f};
        float weight{0.0f};
        float occupancy{0.0f};
        quint16 observations{0};
        quint16 freeObservations{0};
        int lastSeenFrame{0};
    };
    static constexpr double ResolutionMeters = 0.25;
    static constexpr double TsdfTruncationMeters = 0.50;
    static constexpr double HorizontalFovRadians = 1.204;
    static constexpr double CameraForwardOffsetMeters = 0.13233;
    static constexpr double CameraRightOffsetMeters = 0.0;
    static constexpr double CameraUpOffsetMeters = 0.26078;
    static constexpr qint64 MinimumFrameIntervalMs = 650;
    static constexpr double MinimumDepthMeters = 0.35;
    static constexpr double MaximumDepthMeters = 30.0;
    static constexpr double MaximumMappingTiltDegrees = 24.0;
    static constexpr int RequiredObservations = 2;
    static constexpr int ChunkVoxelSize = 32;
    static constexpr int MaximumRenderedVoxelCount = 12000;

    [[nodiscard]] static quint64 voxelKey(int x, int y, int z);
    static void unpackVoxelKey(quint64 key, int *x, int *y, int *z);
    [[nodiscard]] static quint64 chunkKeyForVoxel(int x, int y, int z);
    [[nodiscard]] static quint16 depthMillimetresAt(const QByteArray &data, int index);
    [[nodiscard]] bool isSurfaceVoxel(const Voxel &voxel) const;
    [[nodiscard]] bool isStableSurfaceVoxel(quint64 key, const Voxel &voxel) const;
    void insertIntoChunk(quint64 key, int x, int y, int z);
    void removeVoxel(quint64 key);
    void integrateSurfaceBand(const QVector3D &origin, const QVector3D &direction,
                              double distanceMeters, QSet<quint64> *observedThisFrame);
    void clearFreeSpace(const QVector3D &origin, const QVector3D &direction, double distanceMeters);
    void estimateGroundPlane(const QVector<QVector3D> &candidates);
    [[nodiscard]] double signedGroundDistance(const QVector3D &point) const;
    void updateBoundsAndMetrics();
    void removeStaleWeakVoxels();
    void scheduleRender();
    void rebuildGeometry();
    void setStatusMessage(const QString &message);
    [[nodiscard]] QHash<quint64, QVector3D> stableSurfaceCenters() const;
    [[nodiscard]] bool writePointCloud(const QString &directoryPath, QString *plyPath,
                                       QString *csvPath, QString *errorMessage) const;
    [[nodiscard]] bool writeMesh(const QString &directoryPath, QString *meshPath,
                                 int *triangleCount, QString *errorMessage) const;
    [[nodiscard]] bool writeTopDownPreview(const QString &directoryPath, QString *previewPath,
                                           QString *errorMessage) const;

    QHash<quint64, Voxel> m_voxels;
    QHash<quint64, QSet<quint64>> m_chunkMembers;
    QVector<QVector3D> m_currentDangerPoints;
    DepthSurfaceGeometry m_obstacleGeometry;
    DepthSurfaceGeometry m_dangerGeometry;
    QTimer m_renderTimer;
    QString m_statusMessage{QStringLiteral("等待深度图和飞行器本地位姿")};
    QString m_lastSavedPath;
    qint64 m_lastAcceptedTimestampMs{-1};
    int m_confirmedVoxelCount{0};
    int m_meshTriangleCount{0};
    int m_clearedVoxelCount{0};
    int m_noiseVoxelCount{0};
    int m_processedFrameCount{0};
    int m_skippedFrameCount{0};
    int m_filteredGroundPointCount{0};
    double m_nearestObstacleMeters{0.0};
    double m_corridorClearanceMeters{0.0};
    double m_minimumObservedObstacleMeters{0.0};
    double m_minimumObservedCorridorMeters{0.0};
    double m_coveragePercent{0.0};
    double m_noisePercent{0.0};
    double m_mapConfidencePercent{0.0};
    QVector3D m_groundNormal{0.0f, 1.0f, 0.0f};
    double m_groundOffset{0.0};
    bool m_groundPlaneAvailable{false};
    double m_groundSlopeDegrees{0.0};
    QString m_safetyLevel{QStringLiteral("unknown")};
    QVector3D m_minBounds;
    QVector3D m_maxBounds;
    bool m_boundsValid{false};
    bool m_visualizationActive{false};
    bool m_geometryDirty{false};
    bool m_loadedFromDisk{false};
};
