#include "mapping/depthmapreader.h"
#include "mapping/depthmapexport.h"
#include "mapping/depthmappingcontroller.h"
#include "mapping/depthvoxelcoordinates.h"
using DepthVoxelCoordinates::packedKey;
using DepthVoxelCoordinates::unpackedKey;

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQuaternion>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace {
int floorDivide(int value, int divisor)
{
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

bool solvePlane(const QVector<QVector3D> &points, double *a, double *b, double *c)
{
    if (points.size() < 12) return false;
    double sx = 0, sz = 0, sy = 0, sxx = 0, szz = 0, sxz = 0, sxy = 0, szy = 0;
    for (const QVector3D &p : points) {
        sx += p.x(); sz += p.z(); sy += p.y();
        sxx += p.x() * p.x(); szz += p.z() * p.z(); sxz += p.x() * p.z();
        sxy += p.x() * p.y(); szy += p.z() * p.y();
    }
    double matrix[3][4] = {{sxx, sxz, sx, sxy},
                           {sxz, szz, sz, szy},
                           {sx, sz, static_cast<double>(points.size()), sy}};
    for (int pivot = 0; pivot < 3; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < 3; ++row)
            if (std::abs(matrix[row][pivot]) > std::abs(matrix[best][pivot])) best = row;
        if (std::abs(matrix[best][pivot]) < 1e-8) return false;
        if (best != pivot)
            for (int column = pivot; column < 4; ++column)
                std::swap(matrix[pivot][column], matrix[best][column]);
        const double scale = matrix[pivot][pivot];
        for (int column = pivot; column < 4; ++column) matrix[pivot][column] /= scale;
        for (int row = 0; row < 3; ++row) {
            if (row == pivot) continue;
            const double factor = matrix[row][pivot];
            for (int column = pivot; column < 4; ++column)
                matrix[row][column] -= factor * matrix[pivot][column];
        }
    }
    *a = matrix[0][3]; *b = matrix[1][3]; *c = matrix[2][3];
    return true;
}
}

DepthMappingController::DepthMappingController(QObject *parent) : QObject(parent)
{
    m_renderTimer.setSingleShot(true); m_renderTimer.setInterval(1200);
    connect(&m_renderTimer, &QTimer::timeout, this, &DepthMappingController::rebuildGeometry);
}

QString DepthMappingController::safetyLabel() const
{
    if (m_safetyLevel == QStringLiteral("danger")) return QStringLiteral("前方危险");
    if (m_safetyLevel == QStringLiteral("caution")) return QStringLiteral("注意前方障碍");
    if (m_safetyLevel == QStringLiteral("clear")) return QStringLiteral("当前走廊清晰");
    return QStringLiteral("等待有效深度");
}

double DepthMappingController::widthMeters() const { return m_boundsValid ? m_maxBounds.x() - m_minBounds.x() + ResolutionMeters : 0.0; }
double DepthMappingController::depthMeters() const { return m_boundsValid ? m_maxBounds.z() - m_minBounds.z() + ResolutionMeters : 0.0; }
double DepthMappingController::heightMeters() const { return m_boundsValid ? m_maxBounds.y() - m_minBounds.y() + ResolutionMeters : 0.0; }
QVector3D DepthMappingController::mapCenter() const { return m_boundsValid ? (m_minBounds + m_maxBounds) * 0.5f : QVector3D(); }
quint64 DepthMappingController::voxelKey(int x, int y, int z) { return packedKey(x, y, z); }
void DepthMappingController::unpackVoxelKey(quint64 key, int *x, int *y, int *z) { unpackedKey(key, x, y, z); }
quint64 DepthMappingController::chunkKeyForVoxel(int x, int y, int z)
{
    return voxelKey(floorDivide(x, ChunkVoxelSize), floorDivide(y, ChunkVoxelSize),
                    floorDivide(z, ChunkVoxelSize));
}

quint16 DepthMappingController::depthMillimetresAt(const QByteArray &data, int index)
{
    const qsizetype offset = static_cast<qsizetype>(index) * 2;
    if (offset < 0 || offset + 1 >= data.size()) return 0;
    return static_cast<quint16>(static_cast<quint8>(data.at(offset)))
           | (static_cast<quint16>(static_cast<quint8>(data.at(offset + 1))) << 8);
}

bool DepthMappingController::isSurfaceVoxel(const Voxel &voxel) const
{
    return voxel.observations >= RequiredObservations && voxel.weight >= 1.8f
           && voxel.occupancy > 0.4f && std::abs(voxel.tsdf) <= 0.48f;
}

bool DepthMappingController::isStableSurfaceVoxel(quint64 key, const Voxel &voxel) const
{
    if (!isSurfaceVoxel(voxel)) return false;
    if (voxel.observations >= 4 || voxel.weight >= 6.0f) return true;
    int x = 0, y = 0, z = 0; unpackVoxelKey(key, &x, &y, &z);
    int neighbours = 0;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                const auto it = m_voxels.constFind(voxelKey(x + dx, y + dy, z + dz));
                if (it != m_voxels.cend() && isSurfaceVoxel(it.value()) && ++neighbours >= 2)
                    return true;
            }
    return false;
}

void DepthMappingController::insertIntoChunk(quint64 key, int x, int y, int z)
{
    m_chunkMembers[chunkKeyForVoxel(x, y, z)].insert(key);
}

void DepthMappingController::removeVoxel(quint64 key)
{
    const auto it = m_voxels.constFind(key); if (it == m_voxels.cend()) return;
    int x = 0, y = 0, z = 0; unpackVoxelKey(key, &x, &y, &z);
    const quint64 chunkKey = chunkKeyForVoxel(x, y, z);
    auto chunk = m_chunkMembers.find(chunkKey);
    if (chunk != m_chunkMembers.end()) {
        chunk->remove(key); if (chunk->isEmpty()) m_chunkMembers.erase(chunk);
    }
    m_voxels.remove(key);
}

void DepthMappingController::clearFreeSpace(const QVector3D &origin, const QVector3D &direction,
                                            double distanceMeters)
{
    const double stop = std::max(0.0, distanceMeters - TsdfTruncationMeters);
    QSet<quint64> visited;
    for (double along = 0.5; along < stop; along += ResolutionMeters * 1.6) {
        const QVector3D point = origin + direction * static_cast<float>(along);
        const quint64 key = voxelKey(qFloor(point.x() / ResolutionMeters),
                                     qFloor(point.y() / ResolutionMeters),
                                     qFloor(point.z() / ResolutionMeters));
        if (visited.contains(key)) continue; visited.insert(key);
        auto it = m_voxels.find(key); if (it == m_voxels.end()) continue;
        it->occupancy -= 0.72f;
        it->freeObservations = static_cast<quint16>(std::min<int>(65535, it->freeObservations + 1));
        if (it->occupancy < -1.8f || (it->freeObservations > it->observations + 3 && it->occupancy < 0.1f)) {
            removeVoxel(key); ++m_clearedVoxelCount;
        }
    }
}

void DepthMappingController::integrateSurfaceBand(const QVector3D &origin,
                                                  const QVector3D &direction,
                                                  double distanceMeters,
                                                  QSet<quint64> *observedThisFrame)
{
    for (double offset = -TsdfTruncationMeters; offset <= TsdfTruncationMeters + 0.001;
         offset += ResolutionMeters) {
        const double along = distanceMeters + offset; if (along <= MinimumDepthMeters) continue;
        const QVector3D point = origin + direction * static_cast<float>(along);
        const int x = qFloor(point.x() / ResolutionMeters);
        const int y = qFloor(point.y() / ResolutionMeters);
        const int z = qFloor(point.z() / ResolutionMeters);
        const quint64 key = voxelKey(x, y, z);
        if (observedThisFrame && observedThisFrame->contains(key) && std::abs(offset) < 0.20)
            continue;
        const bool wasNew = !m_voxels.contains(key);
        Voxel &voxel = m_voxels[key];
        if (wasNew) {
            voxel.center = QVector3D((x + 0.5) * ResolutionMeters,
                                     (y + 0.5) * ResolutionMeters,
                                     (z + 0.5) * ResolutionMeters);
            insertIntoChunk(key, x, y, z);
        }
        const float sampleTsdf = static_cast<float>(std::clamp(-offset / TsdfTruncationMeters, -1.0, 1.0));
        const float sampleWeight = std::abs(offset) <= ResolutionMeters ? 1.0f : 0.35f;
        voxel.tsdf = (voxel.tsdf * voxel.weight + sampleTsdf * sampleWeight)
                     / std::max(0.001f, voxel.weight + sampleWeight);
        voxel.weight = std::min(64.0f, voxel.weight + sampleWeight);
        if (std::abs(offset) <= ResolutionMeters * 0.65) {
            voxel.occupancy = std::min(12.0f, voxel.occupancy + 1.0f);
            voxel.observations = static_cast<quint16>(std::min<int>(65535, voxel.observations + 1));
            voxel.lastSeenFrame = m_processedFrameCount;
            if (observedThisFrame) observedThisFrame->insert(key);
        }
    }
}

void DepthMappingController::estimateGroundPlane(const QVector<QVector3D> &candidates)
{
    if (candidates.size() < 30) return;
    QHash<quint64, QVector3D> lowest;
    for (const QVector3D &point : candidates) {
        const int gx = qFloor(point.x()); const int gz = qFloor(point.z());
        const quint64 key = voxelKey(gx, 0, gz);
        auto it = lowest.find(key);
        if (it == lowest.end() || point.y() < it->y()) lowest.insert(key, point);
    }
    QVector<QVector3D> points = lowest.values();
    double a = 0, b = 0, c = 0;
    if (!solvePlane(points, &a, &b, &c)) return;
    QVector<QVector3D> inliers; inliers.reserve(points.size());
    for (const QVector3D &point : std::as_const(points))
        if (std::abs(point.y() - (a * point.x() + b * point.z() + c)) < 0.22)
            inliers.append(point);
    if (inliers.size() < 20 || inliers.size() < points.size() / 3
        || !solvePlane(inliers, &a, &b, &c)) return;
    QVector3D normal(static_cast<float>(-a), 1.0f, static_cast<float>(-b)); normal.normalize();
    const double slope = std::acos(std::clamp(static_cast<double>(normal.y()), 0.0, 1.0))
                         * 180.0 / std::numbers::pi;
    if (slope > 18.0) return;
    if (m_groundPlaneAvailable) {
        m_groundNormal = (m_groundNormal * 0.82f + normal * 0.18f).normalized();
        m_groundOffset = m_groundOffset * 0.82 + (-c * normal.y()) * 0.18;
    } else {
        m_groundNormal = normal; m_groundOffset = -c * normal.y();
    }
    m_groundPlaneAvailable = true;
    m_groundSlopeDegrees = std::acos(std::clamp(static_cast<double>(m_groundNormal.y()), 0.0, 1.0))
                           * 180.0 / std::numbers::pi;
}

double DepthMappingController::signedGroundDistance(const QVector3D &point) const
{
    return QVector3D::dotProduct(m_groundNormal, point) + m_groundOffset;
}

void DepthMappingController::ingestDepthFrame(const QByteArray &data, int gridWidth, int gridHeight,
                                               const QVector3D &position, double headingDegrees,
                                               double rollDegrees, double pitchDegrees,
                                               qint64 timestampMs, bool poseValid)
{
    if (!poseValid || gridWidth < 8 || gridHeight < 6
        || data.size() < static_cast<qsizetype>(gridWidth * gridHeight * 2)) {
        ++m_skippedFrameCount; setStatusMessage(QStringLiteral("等待有效深度图和同步位姿"));
        emit mapChanged(); return;
    }
    if (std::abs(rollDegrees) > MaximumMappingTiltDegrees
        || std::abs(pitchDegrees) > MaximumMappingTiltDegrees) {
        ++m_skippedFrameCount; setStatusMessage(QStringLiteral("机身倾斜较大，已暂停融合以避免地面杂点"));
        emit mapChanged(); return;
    }
    if (m_lastAcceptedTimestampMs >= 0 && timestampMs - m_lastAcceptedTimestampMs < MinimumFrameIntervalMs)
        return;
    m_lastAcceptedTimestampMs = timestampMs; ++m_processedFrameCount; m_loadedFromDisk = false;
    const double fx = (gridWidth * 0.5) / std::tan(HorizontalFovRadians * 0.5);
    const double fy = fx; const double cx = (gridWidth - 1) * 0.5; const double cy = (gridHeight - 1) * 0.5;
    const int stride = std::max(2, gridWidth / 48);
    const QQuaternion orientation = QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), -headingDegrees)
        * QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), pitchDegrees)
        * QQuaternion::fromAxisAndAngle(QVector3D(0, 0, -1), rollDegrees);
    const QVector3D cameraOrigin = position + orientation.rotatedVector(
        QVector3D(static_cast<float>(CameraRightOffsetMeters),
                  static_cast<float>(CameraUpOffsetMeters),
                  static_cast<float>(-CameraForwardOffsetMeters)));
    struct Sample { QVector3D world; QVector3D direction; double distance; double right; double down; };
    QVector<Sample> samples; QVector<QVector3D> groundCandidates;
    double nearest = std::numeric_limits<double>::max();
    double corridor = std::numeric_limits<double>::max();
    int validPointCount = 0;
    for (int py = stride; py < gridHeight - stride; py += stride) {
        for (int px = stride; px < gridWidth - stride; px += stride) {
            const int index = py * gridWidth + px;
            const double forward = depthMillimetresAt(data, index) / 1000.0;
            if (forward < MinimumDepthMeters || forward > MaximumDepthMeters) continue;
            bool supported = false; const int neighbours[] = {index - 1, index + 1, index - gridWidth, index + gridWidth};
            const double tolerance = std::max(0.20, forward * 0.045);
            for (int neighbour : neighbours) {
                const double value = depthMillimetresAt(data, neighbour) / 1000.0;
                if (value >= MinimumDepthMeters && std::abs(value - forward) <= tolerance) { supported = true; break; }
            }
            if (!supported) continue;
            const double right = (px - cx) * forward / fx;
            const double down = (py - cy) * forward / fy;
            const QVector3D local(right, -down, -forward);
            const QVector3D ray = orientation.rotatedVector(local);
            const double rayLength = ray.length(); if (rayLength <= 0.001) continue;
            const QVector3D world = cameraOrigin + ray;
            if (py > gridHeight * 0.54 && world.y() < cameraOrigin.y() + 0.6f)
                groundCandidates.append(world);
            samples.append({world, ray / rayLength, rayLength, right, down});
        }
    }
    estimateGroundPlane(groundCandidates);
    QSet<quint64> observedThisFrame; QVector<QVector3D> dangerPoints;
    for (const Sample &sample : std::as_const(samples)) {
        const double height = m_groundPlaneAvailable ? signedGroundDistance(sample.world) : sample.world.y();
        if (height < 0.28) { ++m_filteredGroundPointCount; continue; }
        ++validPointCount; nearest = std::min(nearest, sample.distance);
        if (std::abs(sample.right) <= 0.9 + sample.distance * 0.03 && std::abs(sample.down) <= 0.75) {
            corridor = std::min(corridor, sample.distance);
            if (sample.distance < 5.0) dangerPoints.append(sample.world);
        }
        clearFreeSpace(cameraOrigin, sample.direction, sample.distance);
        integrateSurfaceBand(cameraOrigin, sample.direction, sample.distance, &observedThisFrame);
    }
    m_nearestObstacleMeters = std::isfinite(nearest) ? nearest : 0.0;
    m_corridorClearanceMeters = std::isfinite(corridor) ? corridor : 0.0;
    if (m_nearestObstacleMeters > 0 && (m_minimumObservedObstacleMeters <= 0 || m_nearestObstacleMeters < m_minimumObservedObstacleMeters))
        m_minimumObservedObstacleMeters = m_nearestObstacleMeters;
    if (m_corridorClearanceMeters > 0 && (m_minimumObservedCorridorMeters <= 0 || m_corridorClearanceMeters < m_minimumObservedCorridorMeters))
        m_minimumObservedCorridorMeters = m_corridorClearanceMeters;
    m_safetyLevel = validPointCount == 0 ? QStringLiteral("unknown")
                    : !std::isfinite(corridor) ? QStringLiteral("clear")
                    : corridor < 2.5 ? QStringLiteral("danger")
                    : corridor < 5.0 ? QStringLiteral("caution") : QStringLiteral("clear");
    m_currentDangerPoints = std::move(dangerPoints);
    removeStaleWeakVoxels();
    // A full confidence/noise pass visits neighbouring voxels. Run it often
    // enough for a responsive UI, but not for every accepted depth frame, so
    // mapping cannot steal the camera decoder's target frame rate on large maps.
    if (!available() || m_processedFrameCount % 4 == 0) updateBoundsAndMetrics();
    setStatusMessage(validPointCount == 0 ? QStringLiteral("当前深度帧没有有效距离，不能判断前方是否安全")
                     : m_confirmedVoxelCount > 0 ? QStringLiteral("DepthFusion 正在清除自由空间并融合连续三维表面")
                     : QStringLiteral("正在确认稳定障碍，需要连续观测"));
    m_geometryDirty = true; scheduleRender(); emit mapChanged();
}

void DepthMappingController::removeStaleWeakVoxels()
{
    QVector<quint64> remove;
    for (auto it = m_voxels.cbegin(); it != m_voxels.cend(); ++it) {
        if (it->observations < RequiredObservations && it->weight < 1.4f
            && m_processedFrameCount - it->lastSeenFrame > 22) remove.append(it.key());
        else if (isSurfaceVoxel(it.value()) && it->observations < 4
                 && m_processedFrameCount - it->lastSeenFrame > 30
                 && !isStableSurfaceVoxel(it.key(), it.value())) remove.append(it.key());
    }
    for (quint64 key : std::as_const(remove)) removeVoxel(key);
}

QHash<quint64, QVector3D> DepthMappingController::stableSurfaceCenters() const
{
    QHash<quint64, QVector3D> result; result.reserve(m_confirmedVoxelCount);
    for (auto it = m_voxels.cbegin(); it != m_voxels.cend(); ++it)
        if (isStableSurfaceVoxel(it.key(), it.value())) result.insert(it.key(), it->center);
    return result;
}

void DepthMappingController::updateBoundsAndMetrics()
{
    m_boundsValid = false; m_confirmedVoxelCount = 0; m_noiseVoxelCount = 0;
    int candidateCount = 0; double confidenceTotal = 0;
    for (auto it = m_voxels.cbegin(); it != m_voxels.cend(); ++it) {
        if (!isSurfaceVoxel(it.value())) continue; ++candidateCount;
        if (!isStableSurfaceVoxel(it.key(), it.value())) { ++m_noiseVoxelCount; continue; }
        const QVector3D center = it->center;
        if (!m_boundsValid) { m_minBounds = m_maxBounds = center; m_boundsValid = true; }
        else {
            m_minBounds.setX(std::min(m_minBounds.x(), center.x())); m_maxBounds.setX(std::max(m_maxBounds.x(), center.x()));
            m_minBounds.setY(std::min(m_minBounds.y(), center.y())); m_maxBounds.setY(std::max(m_maxBounds.y(), center.y()));
            m_minBounds.setZ(std::min(m_minBounds.z(), center.z())); m_maxBounds.setZ(std::max(m_maxBounds.z(), center.z()));
        }
        confidenceTotal += std::min(1.0, static_cast<double>(it->weight) / 8.0)
                           * std::clamp(static_cast<double>(it->occupancy) / 4.0, 0.25, 1.0);
        ++m_confirmedVoxelCount;
    }
    m_coveragePercent = candidateCount > 0 ? 100.0 * m_confirmedVoxelCount / candidateCount : 0.0;
    m_noisePercent = candidateCount > 0 ? 100.0 * m_noiseVoxelCount / candidateCount : 0.0;
    m_mapConfidencePercent = m_confirmedVoxelCount > 0 ? 100.0 * confidenceTotal / m_confirmedVoxelCount : 0.0;
}

void DepthMappingController::scheduleRender() { if (m_visualizationActive && !m_renderTimer.isActive()) m_renderTimer.start(); }

void DepthMappingController::rebuildGeometry()
{
    if (!m_visualizationActive || !m_geometryDirty) return;
    QHash<quint64, QVector3D> centers = stableSurfaceCenters();
    if (centers.size() > MaximumRenderedVoxelCount) {
        QList<quint64> keys = centers.keys(); std::sort(keys.begin(), keys.end());
        QHash<quint64, QVector3D> sampled; sampled.reserve(MaximumRenderedVoxelCount);
        const double step = static_cast<double>(keys.size()) / MaximumRenderedVoxelCount;
        for (int i = 0; i < MaximumRenderedVoxelCount; ++i) {
            const int source = std::min(static_cast<int>(keys.size()) - 1,
                                        static_cast<int>(std::floor(i * step)));
            sampled.insert(keys.at(source), centers.value(keys.at(source)));
        }
        centers = std::move(sampled);
    }
    m_obstacleGeometry.setSurfaceCells(centers, static_cast<float>(ResolutionMeters * 0.92));
    if (m_currentDangerPoints.size() > 500) m_currentDangerPoints.resize(500);
    m_dangerGeometry.setVoxelCenters(m_currentDangerPoints, 0.18f);
    m_meshTriangleCount = m_obstacleGeometry.triangleCount(); m_geometryDirty = false;
}

void DepthMappingController::setVisualizationActive(bool active)
{
    if (m_visualizationActive == active) return; m_visualizationActive = active;
    emit visualizationActiveChanged(); if (active) { m_geometryDirty = true; rebuildGeometry(); }
}
void DepthMappingController::refreshVisualization() { m_geometryDirty = true; rebuildGeometry(); }

void DepthMappingController::clear()
{
    m_voxels.clear(); m_chunkMembers.clear(); m_currentDangerPoints.clear(); m_renderTimer.stop();
    m_obstacleGeometry.clearGeometry(); m_dangerGeometry.clearGeometry(); m_lastAcceptedTimestampMs = -1;
    m_confirmedVoxelCount = m_meshTriangleCount = m_clearedVoxelCount = m_noiseVoxelCount = 0;
    m_processedFrameCount = m_skippedFrameCount = m_filteredGroundPointCount = 0;
    m_nearestObstacleMeters = m_corridorClearanceMeters = 0;
    m_minimumObservedObstacleMeters = m_minimumObservedCorridorMeters = 0;
    m_coveragePercent = m_noisePercent = m_mapConfidencePercent = 0;
    m_groundNormal = QVector3D(0, 1, 0); m_groundOffset = 0; m_groundPlaneAvailable = false; m_groundSlopeDegrees = 0;
    m_safetyLevel = QStringLiteral("unknown"); m_boundsValid = false; m_geometryDirty = false; m_loadedFromDisk = false;
    setStatusMessage(QStringLiteral("等待深度图和飞行器本地位姿")); emit mapChanged();
}
void DepthMappingController::setStatusMessage(const QString &message) { if (m_statusMessage != message) { m_statusMessage = message; emit statusChanged(); } }

bool DepthMappingController::writePointCloud(const QString &directoryPath,
    QString *plyPath, QString *csvPath, QString *errorMessage) const
{
    return DepthMapExport::writePointCloud(stableSurfaceCenters(), m_voxels,
        directoryPath, plyPath, csvPath, errorMessage);
}

bool DepthMappingController::writeMesh(const QString &directoryPath, QString *meshPath,
    int *triangleCount, QString *errorMessage) const
{
    return DepthMapExport::writeMesh(stableSurfaceCenters(), ResolutionMeters,
        directoryPath, meshPath, triangleCount, errorMessage);
}

bool DepthMappingController::writeTopDownPreview(const QString &directoryPath,
    QString *previewPath, QString *errorMessage) const
{
    return DepthMapExport::writeTopDownPreview(stableSurfaceCenters(), m_boundsValid,
        widthMeters(), depthMeters(), mapCenter(), directoryPath, previewPath, errorMessage);
}

QVariantMap DepthMappingController::summary() const
{
    return {{"format","FlightBenchDepthFusionMap"},{"formatVersion",2},{"mapType","chunked-tsdf-surface"},{"coordinateFrame","local ENU-render (east/up/south)"},{"fieldOfViewDegrees",HorizontalFovRadians*180.0/std::numbers::pi},{"resolutionMeters",ResolutionMeters},{"tsdfTruncationMeters",TsdfTruncationMeters},{"confirmedVoxelCount",m_confirmedVoxelCount},{"chunkCount",chunkCount()},{"meshTriangleCount",m_meshTriangleCount},{"clearedVoxelCount",m_clearedVoxelCount},{"noiseVoxelCount",m_noiseVoxelCount},{"processedFrameCount",m_processedFrameCount},{"skippedFrameCount",m_skippedFrameCount},{"filteredGroundPointCount",m_filteredGroundPointCount},{"currentNearestObstacleMeters",m_nearestObstacleMeters},{"currentCorridorClearanceMeters",m_corridorClearanceMeters},{"minimumObservedObstacleMeters",m_minimumObservedObstacleMeters},{"minimumObservedCorridorMeters",m_minimumObservedCorridorMeters},{"nearestObstacleMeters",m_minimumObservedObstacleMeters},{"corridorClearanceMeters",m_minimumObservedCorridorMeters},{"coveragePercent",m_coveragePercent},{"noisePercent",m_noisePercent},{"mapConfidencePercent",m_mapConfidencePercent},{"groundPlaneAvailable",m_groundPlaneAvailable},{"groundSlopeDegrees",m_groundSlopeDegrees},{"safetyLevel",m_safetyLevel},{"widthMeters",widthMeters()},{"depthMeters",depthMeters()},{"heightMeters",heightMeters()},{"unobservedSpaceIsUnknown",true},{"freeSpaceRayClearing",true},{"tsdfWeightedFusion",true}};
}

QVariantMap DepthMappingController::saveToDirectory(const QString &directoryPath)
{
    QVariantMap result=summary(); if(!available()){result.insert("saved",false);result.insert("error",QStringLiteral("尚无稳定三维障碍表面"));return result;}
    QString ply,csv,mesh,preview,error; int triangles=0;
    if(!writePointCloud(directoryPath,&ply,&csv,&error)||!writeMesh(directoryPath,&mesh,&triangles,&error)||!writeTopDownPreview(directoryPath,&preview,&error)){result.insert("saved",false);result.insert("error",error);return result;}
    result.insert("saved",true);result.insert("plyPath",QDir::toNativeSeparators(ply));result.insert("csvPath",QDir::toNativeSeparators(csv));result.insert("meshPath",QDir::toNativeSeparators(mesh));result.insert("previewPath",QDir::toNativeSeparators(preview));result.insert("meshTriangleCount",triangles);result.insert("sizeBytes",QFileInfo(ply).size()+QFileInfo(csv).size()+QFileInfo(mesh).size()+QFileInfo(preview).size());
    QSaveFile metadata(QDir(directoryPath).filePath("depth-map.json")); const QByteArray contents=QJsonDocument(QJsonObject::fromVariantMap(result)).toJson(QJsonDocument::Indented);
    if(!metadata.open(QIODevice::WriteOnly|QIODevice::Truncate)||metadata.write(contents)!=contents.size()||!metadata.commit()){result.insert("saved",false);result.insert("error",QStringLiteral("保存地图元数据失败"));return result;}
    if(m_lastSavedPath!=directoryPath){m_lastSavedPath=QDir::toNativeSeparators(directoryPath);emit lastSavedPathChanged();} return result;
}

bool DepthMappingController::saveMap(const QString &directoryPath){return saveToDirectory(directoryPath).value("saved").toBool();}

bool DepthMappingController::loadMap(const QString &directoryPath)
{
    const QDir directory(directoryPath);
    QFile metadata(directory.filePath("depth-map.json"));
    if (!metadata.open(QIODevice::ReadOnly)) {
        setStatusMessage(QStringLiteral("无法读取三维地图元数据"));
        return false;
    }
    const QVariantMap values = QJsonDocument::fromJson(metadata.readAll()).object().toVariantMap();
    if (values.value("format").toString() != QStringLiteral("FlightBenchDepthFusionMap")
        || values.value("formatVersion").toInt() != 2) {
        setStatusMessage(QStringLiteral("该三维地图不是当前 DepthFusion 格式"));
        return false;
    }
    QString csvPath=directory.filePath("depth-obstacles.csv"); QFile file(csvPath);
    if(!file.open(QIODevice::ReadOnly|QIODevice::Text)){setStatusMessage(QStringLiteral("无法读取已保存的三维地图"));return false;}
    clear();
    QString readError;
    if (!DepthMapReader::readPoints(file, ResolutionMeters,
        [this](int x, int y, int z, const DepthVoxel &voxel) {
            const quint64 key = voxelKey(x, y, z);
            m_voxels[key] = voxel;
            insertIntoChunk(key, x, y, z);
        }, &readError)) {
        setStatusMessage(readError);
        return false;
    }
    m_processedFrameCount=values.value("processedFrameCount").toInt();m_skippedFrameCount=values.value("skippedFrameCount").toInt();m_filteredGroundPointCount=values.value("filteredGroundPointCount").toInt();m_clearedVoxelCount=values.value("clearedVoxelCount").toInt();m_meshTriangleCount=values.value("meshTriangleCount").toInt();m_minimumObservedObstacleMeters=values.value("minimumObservedObstacleMeters").toDouble();m_minimumObservedCorridorMeters=values.value("minimumObservedCorridorMeters").toDouble();m_groundPlaneAvailable=values.value("groundPlaneAvailable").toBool();m_groundSlopeDegrees=values.value("groundSlopeDegrees").toDouble();
    updateBoundsAndMetrics();m_loadedFromDisk=true;m_lastSavedPath=QDir::toNativeSeparators(directoryPath);m_geometryDirty=true;setStatusMessage(QStringLiteral("已直接加载保存的 DepthFusion 地图"));if(m_visualizationActive)rebuildGeometry();emit lastSavedPathChanged();emit mapChanged();return available();
}

QVariantMap DepthMappingController::analyzeRoute(const QVariantList &waypoints, double clearanceMeters) const
{
    QVariantMap result{{"available",false},{"collisionCount",0},{"minimumClearanceMeters",0.0},{"collisionPoints",QVariantList{}}};
    if(!available()||waypoints.size()<2)return result; clearanceMeters=std::clamp(clearanceMeters,0.25,20.0);const int radius=qCeil(clearanceMeters/ResolutionMeters);QVariantList collisions;double minimum=std::numeric_limits<double>::max();QSet<int> collisionSegments;
    for(int segment=1;segment<waypoints.size();++segment){const QVariantMap from=waypoints.at(segment-1).toMap(),to=waypoints.at(segment).toMap();const QVector3D a(from.value("eastMeters").toFloat(),from.value("altitudeMeters").toFloat(),-from.value("northMeters").toFloat());const QVector3D b(to.value("eastMeters").toFloat(),to.value("altitudeMeters").toFloat(),-to.value("northMeters").toFloat());const double length=(b-a).length();const int samples=std::max(1,qCeil(length/(ResolutionMeters*1.5)));double segmentMinimum=std::numeric_limits<double>::max();QVector3D nearestPoint;
        for(int sample=0;sample<=samples;++sample){const QVector3D p=a+(b-a)*(static_cast<float>(sample)/samples);const int vx=qFloor(p.x()/ResolutionMeters),vy=qFloor(p.y()/ResolutionMeters),vz=qFloor(p.z()/ResolutionMeters);for(int dz=-radius;dz<=radius;++dz)for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){const quint64 key=voxelKey(vx+dx,vy+dy,vz+dz);const auto it=m_voxels.constFind(key);if(it==m_voxels.cend()||!isStableSurfaceVoxel(key,it.value()))continue;const double distance=(it->center-p).length();if(distance<segmentMinimum){segmentMinimum=distance;nearestPoint=p;}minimum=std::min(minimum,distance);}}
        if(segmentMinimum<clearanceMeters){collisionSegments.insert(segment-1);collisions.append(QVariantMap{{"segmentIndex",segment-1},{"eastMeters",nearestPoint.x()},{"northMeters",-nearestPoint.z()},{"altitudeMeters",nearestPoint.y()},{"clearanceMeters",segmentMinimum}});}}
    result.insert("available",true);result.insert("collisionCount",collisionSegments.size());result.insert("minimumClearanceMeters",std::isfinite(minimum)?minimum:0.0);result.insert("collisionPoints",collisions);result.insert("requiredClearanceMeters",clearanceMeters);result.insert("safe",collisionSegments.isEmpty());return result;
}
