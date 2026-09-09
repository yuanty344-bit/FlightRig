#include "lidarmappingcontroller.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>
#include <utility>

namespace {
double normalizedAngleDifference(double first, double second)
{
    return std::abs(std::remainder(first - second, 360.0));
}

void removeIsolatedRasterCells(QImage *image,
                               QRgb obstacleColor,
                               QRgb freeColor,
                               QRgb unknownColor)
{
    if (!image || image->format() != QImage::Format_RGB32
        || image->width() < 3 || image->height() < 3) {
        return;
    }
    const QImage source = image->copy();
    for (int y = 1; y < source.height() - 1; ++y) {
        const QRgb *sourceLine = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        QRgb *targetLine = reinterpret_cast<QRgb *>(image->scanLine(y));
        for (int x = 1; x < source.width() - 1; ++x) {
            const QRgb color = sourceLine[x];
            if (color != obstacleColor && color != freeColor) continue;
            int support = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const QRgb *neighborLine = reinterpret_cast<const QRgb *>(
                    source.constScanLine(y + dy));
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx != 0 || dy != 0) && neighborLine[x + dx] == color) ++support;
                }
            }
            // Keep walls and filled free regions, but remove single-cell
            // speckles produced by one marginal ground return or rasterized
            // ray. This is presentation/export cleanup only; SLAM evidence is
            // retained internally for later observations.
            if (support < 2) targetLine[x] = unknownColor;
        }
    }
}
}

LidarMappingController::LidarMappingController(QObject *parent)
    : QObject(parent)
{
    m_renderTimer.setSingleShot(true);
    // Rendering and PNG encoding are intentionally much slower than the ROS
    // scan stream. Mapping stays useful while leaving the UI and sensor reader
    // enough time to process incoming data.
    m_renderTimer.setInterval(1500);
    connect(&m_renderTimer, &QTimer::timeout, this, &LidarMappingController::renderMap);
}

double LidarMappingController::exploredAreaSquareMeters() const
{
    return static_cast<double>(m_confirmedCellCount)
           * ResolutionMeters * ResolutionMeters;
}

double LidarMappingController::widthMeters() const
{
    return m_boundsValid ? (m_maxCellX - m_minCellX + 1) * ResolutionMeters : 0.0;
}

double LidarMappingController::heightMeters() const
{
    return m_boundsValid ? (m_maxCellY - m_minCellY + 1) * ResolutionMeters : 0.0;
}

double LidarMappingController::correctionDistanceMeters() const
{
    return std::hypot(m_positionCorrection.x(), m_positionCorrection.y());
}

double LidarMappingController::normalizedHeading(double headingDegrees)
{
    double result = std::fmod(headingDegrees, 360.0);
    if (result < 0.0) result += 360.0;
    return result;
}

double LidarMappingController::signedAngleDifference(double first, double second)
{
    return std::remainder(first - second, 360.0);
}

QPointF LidarMappingController::transformPoint(const LocalScanPoint &point,
                                                const SlamPose &pose) const
{
    const double headingRadians = pose.headingDegrees * std::numbers::pi / 180.0;
    const double sinHeading = std::sin(headingRadians);
    const double cosHeading = std::cos(headingRadians);
    return QPointF(pose.position.x()
                       + point.forwardMeters * sinHeading
                       + point.rightMeters * cosHeading,
                   pose.position.y()
                       + point.forwardMeters * cosHeading
                       - point.rightMeters * sinHeading);
}

double LidarMappingController::occupiedSupport(double eastMeters, double northMeters) const
{
    const QPointF center = worldToCell(eastMeters, northMeters);
    const int centerX = qRound(center.x());
    const int centerY = qRound(center.y());
    double support = 0.0;
    // A three-cell distance kernel makes matching tolerant of one-cell wall
    // thickness and small PX4/scan timestamp differences without blurring the
    // occupancy map itself. Keeping this kernel small is important because it
    // runs for every scan-matching candidate.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (m_cells.value(cellKey(centerX + dx, centerY + dy), 0)
                < OccupiedThreshold) {
                continue;
            }
            const double distanceCells = std::hypot(static_cast<double>(dx),
                                                    static_cast<double>(dy));
            support = std::max(support, std::max(0.0, 1.0 - distanceCells * 0.35));
        }
    }
    return support;
}

double LidarMappingController::scanMatchScore(const QVector<LocalScanPoint> &points,
                                               const SlamPose &pose,
                                               int *matchedPointCount) const
{
    int considered = 0;
    int matched = 0;
    double supportSum = 0.0;
    const qsizetype step = std::max<qsizetype>(1, points.size() / 48);
    for (qsizetype index = 0; index < points.size(); index += step) {
        const LocalScanPoint &point = points.at(index);
        if (!point.obstacleHit) continue;
        ++considered;
        const QPointF world = transformPoint(point, pose);
        const double support = occupiedSupport(world.x(), world.y());
        supportSum += support;
        if (support > 0.0) ++matched;
    }
    if (matchedPointCount) *matchedPointCount = matched;
    return considered > 0 ? supportSum / static_cast<double>(considered) : 0.0;
}

LidarMappingController::MatchResult LidarMappingController::matchScan(
    const QVector<LocalScanPoint> &points,
    const SlamPose &predictedPose,
    bool loopClosureCandidate) const
{
    MatchResult result;
    result.pose = predictedPose;
    if (m_occupiedCellCount < 24) return result;

    int hitCount = 0;
    for (const LocalScanPoint &point : points) hitCount += point.obstacleHit ? 1 : 0;
    if (hitCount < 8) return result;

    int baselineMatches = 0;
    const double baselineScore = scanMatchScore(points, predictedPose, &baselineMatches);
    double bestObjective = baselineScore;
    double bestScore = baselineScore;
    int bestMatches = baselineMatches;
    SlamPose bestPose = predictedPose;

    const double translationRadius = loopClosureCandidate ? 1.0 : 0.5;
    const double headingRadius = loopClosureCandidate ? 5.0 : 3.0;
    constexpr double TranslationStep = 0.5;
    constexpr double HeadingStep = 1.5;
    for (double headingOffset = -headingRadius;
         headingOffset <= headingRadius + 0.01; headingOffset += HeadingStep) {
        for (double northOffset = -translationRadius;
             northOffset <= translationRadius + 0.01; northOffset += TranslationStep) {
            for (double eastOffset = -translationRadius;
                 eastOffset <= translationRadius + 0.01; eastOffset += TranslationStep) {
                SlamPose candidate{
                    predictedPose.position + QPointF(eastOffset, northOffset),
                    normalizedHeading(predictedPose.headingDegrees + headingOffset)};
                int matches = 0;
                const double score = scanMatchScore(points, candidate, &matches);
                // Keep the odometry prior unless the scan endpoints provide a
                // materially better wall alignment.
                const double objective = score
                    - 0.008 * std::hypot(eastOffset, northOffset)
                    - 0.0008 * std::abs(headingOffset);
                if (objective > bestObjective) {
                    bestObjective = objective;
                    bestScore = score;
                    bestMatches = matches;
                    bestPose = candidate;
                }
            }
        }
    }

    // Refine the best coarse cell without repeating the full wide search.
    const SlamPose coarsePose = bestPose;
    for (double headingOffset = -0.75; headingOffset <= 0.76; headingOffset += 0.375) {
        for (double northOffset = -0.2; northOffset <= 0.201; northOffset += 0.1) {
            for (double eastOffset = -0.2; eastOffset <= 0.201; eastOffset += 0.1) {
                SlamPose candidate{
                    coarsePose.position + QPointF(eastOffset, northOffset),
                    normalizedHeading(coarsePose.headingDegrees + headingOffset)};
                int matches = 0;
                const double score = scanMatchScore(points, candidate, &matches);
                const double distanceFromPrediction = std::hypot(
                    candidate.position.x() - predictedPose.position.x(),
                    candidate.position.y() - predictedPose.position.y());
                const double headingFromPrediction = std::abs(signedAngleDifference(
                    candidate.headingDegrees, predictedPose.headingDegrees));
                const double objective = score - 0.008 * distanceFromPrediction
                                         - 0.0008 * headingFromPrediction;
                if (objective > bestObjective) {
                    bestObjective = objective;
                    bestScore = score;
                    bestMatches = matches;
                    bestPose = candidate;
                }
            }
        }
    }

    const int minimumMatches = std::max(8, std::min(14, hitCount / 10));
    const double scoreGain = bestScore - baselineScore;
    result.confidence = std::clamp(bestScore, 0.0, 1.0);
    result.matchedPointCount = bestMatches;
    result.accepted = bestMatches >= minimumMatches && bestScore >= 0.18
                      && (scoreGain >= 0.015 || baselineScore >= 0.22);
    if (result.accepted) result.pose = bestPose;
    return result;
}

QVector<float> LidarMappingController::scanDescriptor(
    const QVector<LocalScanPoint> &points,
    double headingDegrees) const
{
    constexpr int BinCount = 72;
    QVector<float> descriptor(BinCount, 0.0F);
    for (const LocalScanPoint &point : points) {
        if (!point.obstacleHit) continue;
        double worldAngle = std::atan2(point.rightMeters, point.forwardMeters)
                            * 180.0 / std::numbers::pi + headingDegrees;
        worldAngle = normalizedHeading(worldAngle);
        const int bin = std::clamp(static_cast<int>(worldAngle / 360.0 * BinCount),
                                   0, BinCount - 1);
        const float range = static_cast<float>(point.rangeMeters);
        if (descriptor[bin] <= 0.0F || range < descriptor[bin]) descriptor[bin] = range;
    }
    return descriptor;
}

int LidarMappingController::findLoopClosureCandidate(
    const QVector<LocalScanPoint> &points,
    const SlamPose &predictedPose) const
{
    constexpr int MinimumKeyframeSeparation = 18;
    if (m_keyframes.size() <= MinimumKeyframeSeparation
        || static_cast<int>(m_keyframes.size()) - m_lastLoopClosureKeyframe < 20
        || m_processedScanCount - m_lastLoopClosureAttemptScan < 30) {
        return -1;
    }
    const QVector<float> current = scanDescriptor(points, predictedPose.headingDegrees);
    int bestIndex = -1;
    double bestScore = 0.0;
    QVector<QPair<int, double>> candidateScores;
    const int newestCandidate = static_cast<int>(m_keyframes.size())
                                - MinimumKeyframeSeparation;
    for (int index = 0; index < newestCandidate; ++index) {
        const Keyframe &candidate = m_keyframes.at(index);
        const QPointF delta = predictedPose.position - candidate.pose.position;
        const double distance = std::hypot(delta.x(), delta.y());
        if (distance > 4.0 || candidate.descriptor.size() != current.size()) continue;
        int overlap = 0;
        double similarity = 0.0;
        for (qsizetype bin = 0; bin < current.size(); ++bin) {
            if (current.at(bin) <= 0.0F || candidate.descriptor.at(bin) <= 0.0F) continue;
            ++overlap;
            similarity += std::max(0.0, 1.0
                - std::abs(current.at(bin) - candidate.descriptor.at(bin)) / 12.0);
        }
        if (overlap < 16) continue;
        similarity /= static_cast<double>(overlap);
        const double score = similarity - distance * 0.035;
        if (similarity >= 0.72) {
            candidateScores.append(qMakePair(index, score));
            if (score > bestScore) {
                bestScore = score;
                bestIndex = index;
            }
        }
    }
    double secondBestScore = 0.0;
    for (const auto &candidate : std::as_const(candidateScores)) {
        // Neighboring keyframes describe one physical candidate region and
        // must not be treated as competing ambiguous places.
        if (std::abs(candidate.first - bestIndex) > 4)
            secondBestScore = std::max(secondBestScore, candidate.second);
    }
    // Repeated city facades often look alike. Reject an ambiguous descriptor
    // instead of forcing a visually damaging false loop closure.
    return bestIndex >= 0 && bestScore - secondBestScore >= 0.045 ? bestIndex : -1;
}

LidarMappingController::MatchResult LidarMappingController::matchScanToKeyframeSubmap(
    const QVector<LocalScanPoint> &points,
    const SlamPose &predictedPose,
    int keyframeIndex) const
{
    MatchResult result;
    result.pose = predictedPose;
    if (keyframeIndex < 0 || keyframeIndex >= m_keyframes.size()) return result;

    QSet<quint64> occupied;
    const int first = std::max(0, keyframeIndex - 3);
    const int last = std::min(static_cast<int>(m_keyframes.size()) - 1,
                              keyframeIndex + 3);
    for (int index = first; index <= last; ++index) {
        const Keyframe &keyframe = m_keyframes.at(index);
        for (const LocalScanPoint &point : keyframe.scan) {
            if (!point.obstacleHit) continue;
            const QPointF world = transformPoint(point, keyframe.pose);
            const QPointF cell = worldToCell(world.x(), world.y());
            occupied.insert(cellKey(qRound(cell.x()), qRound(cell.y())));
        }
    }
    if (occupied.size() < 16) return result;

    const auto scorePose = [&](const SlamPose &pose, int *matchedCount) {
        int considered = 0;
        int matched = 0;
        double supportSum = 0.0;
        const qsizetype step = std::max<qsizetype>(1, points.size() / 60);
        for (qsizetype pointIndex = 0; pointIndex < points.size(); pointIndex += step) {
            const LocalScanPoint &point = points.at(pointIndex);
            if (!point.obstacleHit) continue;
            ++considered;
            const QPointF world = transformPoint(point, pose);
            const QPointF cell = worldToCell(world.x(), world.y());
            const int cellX = qRound(cell.x());
            const int cellY = qRound(cell.y());
            double support = 0.0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!occupied.contains(cellKey(cellX + dx, cellY + dy))) continue;
                    support = std::max(support,
                        std::max(0.0, 1.0 - std::hypot(dx, dy) * 0.35));
                }
            }
            supportSum += support;
            if (support > 0.0) ++matched;
        }
        if (matchedCount) *matchedCount = matched;
        return considered > 0 ? supportSum / considered : 0.0;
    };

    int baselineMatches = 0;
    const double baselineScore = scorePose(predictedPose, &baselineMatches);
    double bestObjective = baselineScore;
    double bestScore = baselineScore;
    int bestMatches = 0;
    SlamPose bestPose = predictedPose;
    for (double headingOffset = -6.0; headingOffset <= 6.01; headingOffset += 3.0) {
        for (double northOffset = -2.0; northOffset <= 2.01; northOffset += 1.0) {
            for (double eastOffset = -2.0; eastOffset <= 2.01; eastOffset += 1.0) {
                SlamPose candidate{
                    predictedPose.position + QPointF(eastOffset, northOffset),
                    normalizedHeading(predictedPose.headingDegrees + headingOffset)};
                int matches = 0;
                const double score = scorePose(candidate, &matches);
                const double objective = score - 0.004 * std::hypot(eastOffset, northOffset)
                                         - 0.0004 * std::abs(headingOffset);
                if (objective > bestObjective) {
                    bestObjective = objective;
                    bestScore = score;
                    bestMatches = matches;
                    bestPose = candidate;
                }
            }
        }
    }
    const SlamPose coarsePose = bestPose;
    for (double headingOffset = -1.0; headingOffset <= 1.01; headingOffset += 0.5) {
        for (double northOffset = -0.4; northOffset <= 0.401; northOffset += 0.2) {
            for (double eastOffset = -0.4; eastOffset <= 0.401; eastOffset += 0.2) {
                SlamPose candidate{
                    coarsePose.position + QPointF(eastOffset, northOffset),
                    normalizedHeading(coarsePose.headingDegrees + headingOffset)};
                int matches = 0;
                const double score = scorePose(candidate, &matches);
                const double distanceFromPrediction = std::hypot(
                    candidate.position.x() - predictedPose.position.x(),
                    candidate.position.y() - predictedPose.position.y());
                const double headingFromPrediction = std::abs(signedAngleDifference(
                    candidate.headingDegrees, predictedPose.headingDegrees));
                const double objective = score - 0.004 * distanceFromPrediction
                                         - 0.0004 * headingFromPrediction;
                if (objective > bestObjective) {
                    bestObjective = objective;
                    bestScore = score;
                    bestMatches = matches;
                    bestPose = candidate;
                }
            }
        }
    }
    result.pose = bestPose;
    result.confidence = std::clamp(bestScore, 0.0, 1.0);
    result.matchedPointCount = bestMatches;
    const double correctionDistance = std::hypot(
        bestPose.position.x() - predictedPose.position.x(),
        bestPose.position.y() - predictedPose.position.y());
    const double correctionHeading = std::abs(signedAngleDifference(
        bestPose.headingDegrees, predictedPose.headingDegrees));
    result.accepted = bestMatches >= 16 && result.confidence >= 0.38
                      && bestScore - baselineScore >= 0.08
                      && correctionDistance <= 1.5 && correctionHeading <= 5.0;
    return result;
}

QPointF LidarMappingController::relativePosition(const SlamPose &from,
                                                  const SlamPose &to)
{
    const QPointF delta = to.position - from.position;
    const double heading = from.headingDegrees * std::numbers::pi / 180.0;
    const double sinHeading = std::sin(heading);
    const double cosHeading = std::cos(heading);
    return QPointF(delta.x() * cosHeading - delta.y() * sinHeading,
                   delta.x() * sinHeading + delta.y() * cosHeading);
}

void LidarMappingController::updateSlamPose(double rawEastMeters,
                                             double rawNorthMeters,
                                             double rawHeadingDegrees,
                                             const QVector<LocalScanPoint> &points)
{
    const QPointF rawPose(rawEastMeters, rawNorthMeters);
    if (!m_slamPoseInitialized) {
        m_currentPose = rawPose;
        m_currentHeadingDegrees = normalizedHeading(rawHeadingDegrees);
        m_lastRawPose = rawPose;
        m_lastRawHeadingDegrees = normalizedHeading(rawHeadingDegrees);
        m_positionCorrection = {};
        m_headingCorrectionDegrees = 0.0;
        m_localizationConfidence = 0.0;
        m_slamPoseInitialized = true;
        return;
    }

    const QPointF rawDelta = rawPose - m_lastRawPose;
    SlamPose predicted{
        m_currentPose + rawDelta,
        normalizedHeading(m_currentHeadingDegrees
                          + signedAngleDifference(rawHeadingDegrees,
                                                  m_lastRawHeadingDegrees))};
    const bool loopSearchDue = m_processedScanCount - m_lastLoopClosureAttemptScan >= 30;
    m_pendingLoopClosureIndex = loopSearchDue
        ? findLoopClosureCandidate(points, predicted) : -1;
    if (loopSearchDue) m_lastLoopClosureAttemptScan = m_processedScanCount;
    const bool loopCandidate = m_pendingLoopClosureIndex >= 0;
    const bool shouldMatch = loopCandidate
                             || m_processedScanCount % ScanMatchingStride == 0;
    if (!shouldMatch) {
        m_currentPose = predicted.position;
        m_currentHeadingDegrees = predicted.headingDegrees;
        m_lastRawPose = rawPose;
        m_lastRawHeadingDegrees = normalizedHeading(rawHeadingDegrees);
        m_positionCorrection = m_currentPose - rawPose;
        m_headingCorrectionDegrees = signedAngleDifference(m_currentHeadingDegrees,
                                                           rawHeadingDegrees);
        return;
    }
    const MatchResult match = loopCandidate
        ? matchScanToKeyframeSubmap(points, predicted, m_pendingLoopClosureIndex)
        : matchScan(points, predicted, false);
    if (match.accepted) {
        const QPointF matchTranslation = match.pose.position - predicted.position;
        const double matchHeading = signedAngleDifference(match.pose.headingDegrees,
                                                          predicted.headingDegrees);
        const double translationCorrection = std::hypot(matchTranslation.x(),
                                                         matchTranslation.y());
        const double headingCorrection = std::abs(matchHeading);
        const double correctionGain = loopCandidate ? 0.55 : 0.35;
        m_currentPose = predicted.position + matchTranslation * correctionGain;
        m_currentHeadingDegrees = normalizedHeading(
            predicted.headingDegrees + matchHeading * correctionGain);
        ++m_scanMatchedCount;
        if (loopCandidate && (translationCorrection >= 0.15
                              || headingCorrection >= 0.6)) {
            m_pendingLoopMatchedPose = match.pose;
            m_pendingLoopConfidence = match.confidence;
        } else if (loopCandidate) {
            m_pendingLoopClosureIndex = -1;
        }
        m_localizationConfidence = m_scanMatchedCount == 1
            ? match.confidence
            : m_localizationConfidence * 0.78 + match.confidence * 0.22;
    } else {
        m_pendingLoopClosureIndex = -1;
        m_currentPose = predicted.position;
        m_currentHeadingDegrees = predicted.headingDegrees;
        m_localizationConfidence *= 0.985;
    }
    m_lastRawPose = rawPose;
    m_lastRawHeadingDegrees = normalizedHeading(rawHeadingDegrees);
    m_positionCorrection = m_currentPose - rawPose;
    m_headingCorrectionDegrees = signedAngleDifference(m_currentHeadingDegrees,
                                                       rawHeadingDegrees);
}

void LidarMappingController::appendKeyframeIfNeeded(
    const QVector<LocalScanPoint> &points,
    const SlamPose &rawPose,
    qint64 timestampMs)
{
    bool append = m_keyframes.isEmpty() || m_pendingLoopClosureIndex >= 0;
    if (!append) {
        const Keyframe &last = m_keyframes.constLast();
        const QPointF delta = m_currentPose - last.pose.position;
        const bool moved = std::hypot(delta.x(), delta.y()) >= 1.5;
        const bool turned = normalizedAngleDifference(m_currentHeadingDegrees,
                                                       last.pose.headingDegrees) >= 18.0;
        const bool elapsed = timestampMs - last.timestampMs >= 6000;
        append = moved || turned || elapsed;
    }
    if (!append) return;

    QVector<LocalScanPoint> storedScan;
    const qsizetype pointStep = std::max<qsizetype>(1, points.size() / 120);
    storedScan.reserve((points.size() + pointStep - 1) / pointStep);
    for (qsizetype index = 0; index < points.size(); index += pointStep)
        storedScan.append(points.at(index));

    const SlamPose pose{m_currentPose, m_currentHeadingDegrees};
    const int newIndex = static_cast<int>(m_keyframes.size());
    m_keyframes.append({pose, rawPose, storedScan,
                        scanDescriptor(storedScan, pose.headingDegrees), timestampMs});
    if (newIndex > 0) {
        const Keyframe &previous = m_keyframes.at(newIndex - 1);
        m_poseConstraints.append({newIndex - 1, newIndex,
                                  relativePosition(previous.rawPose, rawPose),
                                  signedAngleDifference(rawPose.headingDegrees,
                                                        previous.rawPose.headingDegrees),
                                  0.65, 0.55, false});
    }
    if (m_pendingLoopClosureIndex >= 0
        && m_pendingLoopClosureIndex < newIndex) {
        const Keyframe &candidate = m_keyframes.at(m_pendingLoopClosureIndex);
        m_poseConstraints.append({m_pendingLoopClosureIndex, newIndex,
                                  relativePosition(candidate.pose,
                                                   m_pendingLoopMatchedPose),
                                  signedAngleDifference(
                                      m_pendingLoopMatchedPose.headingDegrees,
                                      candidate.pose.headingDegrees),
                                  std::clamp(0.65 + m_pendingLoopConfidence * 0.45,
                                             0.65, 1.10),
                                  std::clamp(0.60 + m_pendingLoopConfidence * 0.40,
                                             0.60, 1.00),
                                  true});
        ++m_loopClosureCount;
        m_lastLoopClosureKeyframe = newIndex;
        optimizePoseGraph();
        rebuildMapFromKeyframes();
    }
    m_pendingLoopClosureIndex = -1;
    m_pendingLoopConfidence = 0.0;
}

void LidarMappingController::optimizePoseGraph()
{
    if (m_keyframes.size() < 2 || m_poseConstraints.isEmpty()) return;
    constexpr int Iterations = 35;
    for (int iteration = 0; iteration < Iterations; ++iteration) {
        const double annealing = 1.0 - iteration / static_cast<double>(Iterations) * 0.55;
        for (const PoseConstraint &constraint : std::as_const(m_poseConstraints)) {
            if (constraint.fromIndex < 0 || constraint.toIndex <= constraint.fromIndex
                || constraint.toIndex >= m_keyframes.size()) {
                continue;
            }
            Keyframe &from = m_keyframes[constraint.fromIndex];
            Keyframe &to = m_keyframes[constraint.toIndex];
            const double heading = from.pose.headingDegrees * std::numbers::pi / 180.0;
            const double sinHeading = std::sin(heading);
            const double cosHeading = std::cos(heading);
            const QPointF expectedPosition(
                from.pose.position.x()
                    + constraint.relativePosition.x() * cosHeading
                    + constraint.relativePosition.y() * sinHeading,
                from.pose.position.y()
                    - constraint.relativePosition.x() * sinHeading
                    + constraint.relativePosition.y() * cosHeading);
            const QPointF positionError = expectedPosition - to.pose.position;
            const double expectedHeading = normalizedHeading(
                from.pose.headingDegrees + constraint.relativeHeadingDegrees);
            const double headingError = signedAngleDifference(expectedHeading,
                                                              to.pose.headingDegrees);
            const double baseGain = constraint.loopClosure ? 0.20 : 0.045;
            // Loop matches use a robust kernel. Even after the stricter
            // descriptor and local-submap checks, repeated building facades
            // can still produce an imperfect constraint; one such edge must
            // not drag a whole city block across the map.
            const double robustWeight = constraint.loopClosure
                ? 1.0 / (1.0 + std::hypot(positionError.x(), positionError.y()) / 0.75
                         + std::abs(headingError) / 4.0)
                : 1.0;
            const double translationGain = baseGain * constraint.translationWeight
                                           * annealing * robustWeight;
            const double headingGain = baseGain * constraint.headingWeight
                                       * annealing * robustWeight;
            if (constraint.fromIndex == 0) {
                to.pose.position += positionError * translationGain;
                to.pose.headingDegrees = normalizedHeading(
                    to.pose.headingDegrees + headingError * headingGain);
            } else {
                from.pose.position -= positionError * (translationGain * 0.5);
                to.pose.position += positionError * (translationGain * 0.5);
                from.pose.headingDegrees = normalizedHeading(
                    from.pose.headingDegrees - headingError * headingGain * 0.5);
                to.pose.headingDegrees = normalizedHeading(
                    to.pose.headingDegrees + headingError * headingGain * 0.5);
            }
        }
    }
    ++m_optimizationCount;
    const Keyframe &latest = m_keyframes.constLast();
    m_currentPose = latest.pose.position;
    m_currentHeadingDegrees = latest.pose.headingDegrees;
    m_positionCorrection = m_currentPose - latest.rawPose.position;
    m_headingCorrectionDegrees = signedAngleDifference(
        m_currentHeadingDegrees, latest.rawPose.headingDegrees);
}

void LidarMappingController::resetOccupancyGrid()
{
    m_cells.clear();
    m_lastHitScan.clear();
    m_track.clear();
    m_currentHits.clear();
    m_boundsValid = false;
    m_minCellX = m_maxCellX = m_minCellY = m_maxCellY = 0;
    m_occupiedCellCount = 0;
    m_confirmedCellCount = 0;
}

void LidarMappingController::integrateScanAtPose(
    const QVector<LocalScanPoint> &points,
    const SlamPose &pose,
    bool updateCurrentHits)
{
    const QPointF startCell = worldToCell(pose.position.x(), pose.position.y());
    if (updateCurrentHits) m_currentHits.clear();
    const qsizetype pointStep = std::max<qsizetype>(1, points.size() / 120);
    for (qsizetype pointIndex = 0; pointIndex < points.size(); pointIndex += pointStep) {
        const LocalScanPoint &point = points.at(pointIndex);
        const QPointF hit = transformPoint(point, pose);
        const QPointF endCell = worldToCell(hit.x(), hit.y());
        traceRay(qRound(startCell.x()), qRound(startCell.y()),
                 qRound(endCell.x()), qRound(endCell.y()));
        if (!point.obstacleHit) continue;
        const int endX = qRound(endCell.x());
        const int endY = qRound(endCell.y());
        updateCell(endX, endY, OccupiedDelta);
        m_lastHitScan.insert(cellKey(endX, endY), m_processedScanCount);
        if (updateCurrentHits) m_currentHits.append(hit);
    }
}

void LidarMappingController::rebuildMapFromKeyframes()
{
    resetOccupancyGrid();
    for (const Keyframe &keyframe : std::as_const(m_keyframes)) {
        integrateScanAtPose(keyframe.scan, keyframe.pose, false);
        m_track.append(keyframe.pose.position);
    }
    if (!m_keyframes.isEmpty()) {
        const Keyframe &latest = m_keyframes.constLast();
        m_currentHits.clear();
        for (const LocalScanPoint &point : latest.scan) {
            if (point.obstacleHit) m_currentHits.append(transformPoint(point, latest.pose));
        }
    }
    m_mapDirty = true;
}

quint64 LidarMappingController::cellKey(int x, int y)
{
    return (static_cast<quint64>(static_cast<quint32>(x)) << 32)
           | static_cast<quint32>(y);
}

void LidarMappingController::unpackCellKey(quint64 key, int *x, int *y)
{
    *x = static_cast<qint32>(key >> 32);
    *y = static_cast<qint32>(key & 0xffffffffU);
}

QPointF LidarMappingController::worldToCell(double eastMeters, double northMeters) const
{
    return QPointF(std::floor(eastMeters / ResolutionMeters),
                   std::floor(northMeters / ResolutionMeters));
}

void LidarMappingController::updateCell(int x, int y, int delta)
{
    const quint64 key = cellKey(x, y);
    const int previous = m_cells.value(key, 0);
    const int updated = std::clamp(previous + delta, MinimumLogOdds, MaximumLogOdds);
    const bool wasOccupied = previous >= OccupiedThreshold;
    const bool isOccupied = updated >= OccupiedThreshold;
    const bool wasConfirmed = wasOccupied || previous <= FreeThreshold;
    const bool isConfirmed = isOccupied || updated <= FreeThreshold;
    if (wasOccupied != isOccupied) m_occupiedCellCount += isOccupied ? 1 : -1;
    if (wasConfirmed != isConfirmed) m_confirmedCellCount += isConfirmed ? 1 : -1;
    m_cells.insert(key, static_cast<qint8>(updated));
    if (!m_boundsValid) {
        m_minCellX = m_maxCellX = x;
        m_minCellY = m_maxCellY = y;
        m_boundsValid = true;
    } else {
        m_minCellX = std::min(m_minCellX, x);
        m_maxCellX = std::max(m_maxCellX, x);
        m_minCellY = std::min(m_minCellY, y);
        m_maxCellY = std::max(m_maxCellY, y);
    }
}

void LidarMappingController::traceRay(int startX, int startY, int endX, int endY)
{
    int x = startX;
    int y = startY;
    const int dx = std::abs(endX - startX);
    const int sx = startX < endX ? 1 : -1;
    const int dy = -std::abs(endY - startY);
    const int sy = startY < endY ? 1 : -1;
    int error = dx + dy;
    while (x != endX || y != endY) {
        updateCell(x, y, FreeDelta);
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y += sy;
        }
    }
}

void LidarMappingController::decayWeakObstacles()
{
    constexpr int WeakObstacleMaximum = OccupiedThreshold + 1;
    constexpr int MaximumUnseenScans = 18;
    for (auto it = m_lastHitScan.begin(); it != m_lastHitScan.end();) {
        auto cell = m_cells.find(it.key());
        if (cell == m_cells.end() || cell.value() <= 0) {
            it = m_lastHitScan.erase(it);
            continue;
        }
        if (m_processedScanCount - it.value() > MaximumUnseenScans
            && cell.value() <= WeakObstacleMaximum) {
            if (cell.value() >= OccupiedThreshold) {
                --m_occupiedCellCount;
                --m_confirmedCellCount;
            }
            cell.value() = 0;
            it = m_lastHitScan.erase(it);
            continue;
        }
        ++it;
    }
}

void LidarMappingController::ingestScan(const QVariantList &points,
                                        double eastMeters,
                                        double northMeters,
                                        double headingDegrees,
                                        double rollDegrees,
                                        double pitchDegrees,
                                        double altitudeMeters,
                                        qint64 timestampMs,
                                        bool poseValid)
{
    if (points.isEmpty()) return;
    if (!poseValid || !std::isfinite(eastMeters) || !std::isfinite(northMeters)
        || !std::isfinite(headingDegrees) || !std::isfinite(rollDegrees)
        || !std::isfinite(pitchDegrees) || !std::isfinite(altitudeMeters)) {
        ++m_skippedScanCount;
        setStatusMessage(QStringLiteral("雷达已有数据，正在等待可靠的本地位置与航向"));
        emit mapChanged();
        return;
    }
    if (altitudeMeters < MinimumMappingAltitudeMeters) {
        ++m_skippedScanCount;
        setStatusMessage(QStringLiteral("起飞并达到 2 米高度后开始累计建图"));
        return;
    }
    if (std::abs(rollDegrees) > MaximumMappingTiltDegrees
        || std::abs(pitchDegrees) > MaximumMappingTiltDegrees) {
        ++m_skippedScanCount;
        ++m_tiltRejectedScanCount;
        setStatusMessage(QStringLiteral("机身倾斜较大，已忽略可能扫到地面的扫描"));
        return;
    }
    if (m_lastAcceptedTimestampMs >= 0
        && timestampMs - m_lastAcceptedTimestampMs < MinimumScanIntervalMs) {
        ++m_skippedScanCount;
        return;
    }

    QVector<LocalScanPoint> localPoints;
    localPoints.reserve(points.size());
    for (const QVariant &value : points) {
        const QVariantMap point = value.toMap();
        const double right = point.value(QStringLiteral("x")).toDouble();
        const double forward = -point.value(QStringLiteral("y")).toDouble();
        if (!std::isfinite(right) || !std::isfinite(forward)) continue;
        const double range = std::hypot(right, forward);
        if (range < 0.35 || range > 30.5) continue;
        // New recordings carry an explicit return flag. Older recordings did
        // not, so values close to the 30 m sensor limit are conservatively
        // treated as free-space rays rather than artificial obstacle arcs.
        const bool obstacleHit = point.contains(QStringLiteral("hit"))
            ? point.value(QStringLiteral("hit")).toBool()
            : range < 29.5;
        localPoints.append({right, forward, range, obstacleHit});
    }
    if (localPoints.isEmpty()) {
        ++m_skippedScanCount;
        return;
    }

    // A single isolated laser return is commonly produced by rotor/ground
    // grazing or a transient simulation sample. It must not become a lasting
    // point or radial streak in the global map. Keep real wall edges and small
    // objects when at least one neighbouring beam supports the same surface.
    // Maximum-range free-space rays remain untouched.
    if (localPoints.size() >= 12) {
        QVector<qsizetype> angularOrder(localPoints.size());
        std::iota(angularOrder.begin(), angularOrder.end(), qsizetype{0});
        std::sort(angularOrder.begin(), angularOrder.end(),
                  [&localPoints](qsizetype first, qsizetype second) {
            const LocalScanPoint &a = localPoints.at(first);
            const LocalScanPoint &b = localPoints.at(second);
            return std::atan2(a.rightMeters, a.forwardMeters)
                   < std::atan2(b.rightMeters, b.forwardMeters);
        });
        QVector<bool> keep(localPoints.size(), true);
        const auto isSupportingNeighbour = [&localPoints](qsizetype first,
                                                           qsizetype second) {
            const LocalScanPoint &a = localPoints.at(first);
            const LocalScanPoint &b = localPoints.at(second);
            if (!a.obstacleHit || !b.obstacleHit) return false;
            const double firstAngle = std::atan2(a.rightMeters, a.forwardMeters);
            const double secondAngle = std::atan2(b.rightMeters, b.forwardMeters);
            const double angleGap = std::abs(std::remainder(firstAngle - secondAngle,
                                                            2.0 * std::numbers::pi));
            if (angleGap > 3.2 * std::numbers::pi / 180.0) return false;
            const double spatialGap = std::hypot(a.rightMeters - b.rightMeters,
                                                 a.forwardMeters - b.forwardMeters);
            const double allowedGap = std::clamp(
                std::min(a.rangeMeters, b.rangeMeters) * 0.06 + 0.15, 0.35, 1.3);
            return spatialGap <= allowedGap;
        };
        for (qsizetype orderIndex = 0; orderIndex < angularOrder.size(); ++orderIndex) {
            const qsizetype pointIndex = angularOrder.at(orderIndex);
            if (!localPoints.at(pointIndex).obstacleHit) continue;
            const qsizetype previous = angularOrder.at(
                (orderIndex + angularOrder.size() - 1) % angularOrder.size());
            const qsizetype next = angularOrder.at(
                (orderIndex + 1) % angularOrder.size());
            keep[pointIndex] = isSupportingNeighbour(pointIndex, previous)
                               || isSupportingNeighbour(pointIndex, next);
        }
        QVector<LocalScanPoint> filteredPoints;
        filteredPoints.reserve(localPoints.size());
        for (qsizetype index = 0; index < localPoints.size(); ++index) {
            if (keep.at(index)) filteredPoints.append(localPoints.at(index));
        }
        localPoints = std::move(filteredPoints);
        if (localPoints.isEmpty()) {
            ++m_skippedScanCount;
            return;
        }
    }

    const QPointF rawPose(eastMeters, northMeters);
    const double moved = !m_slamPoseInitialized
        ? std::numeric_limits<double>::infinity()
        : std::hypot(rawPose.x() - m_lastRawPose.x(), rawPose.y() - m_lastRawPose.y());
    const double turned = !m_slamPoseInitialized
        ? std::numeric_limits<double>::infinity()
        : normalizedAngleDifference(headingDegrees, m_lastRawHeadingDegrees);
    // Do not thin stationary scans until at least one obstacle has actually
    // reached the confirmation threshold. Free-space cells become confirmed
    // earlier than obstacle endpoints; using available() here would skip the
    // second observation and make a real wall require unnecessarily long to
    // appear.
    if (m_slamPoseInitialized && m_occupiedCellCount > 0
        && moved < 0.12 && turned < 1.5) {
        ++m_stationaryScanCounter;
        // A second observation is required to confirm obstacles, so keep every
        // other stationary scan instead of waiting several seconds.
        if (m_stationaryScanCounter % 2 != 0) {
            ++m_skippedScanCount;
            return;
        }
    } else {
        m_stationaryScanCounter = 0;
    }

    updateSlamPose(eastMeters, northMeters, headingDegrees, localPoints);
    const SlamPose slamPose{m_currentPose, m_currentHeadingDegrees};
    integrateScanAtPose(localPoints, slamPose, true);

    if (m_track.isEmpty() || std::hypot(m_currentPose.x() - m_track.constLast().x(),
                                        m_currentPose.y() - m_track.constLast().y()) >= 0.35) {
        m_track.append(m_currentPose);
    }
    m_lastAcceptedTimestampMs = timestampMs;
    ++m_processedScanCount;
    appendKeyframeIfNeeded(localPoints,
                           SlamPose{rawPose, normalizedHeading(headingDegrees)},
                           timestampMs);
    if (m_processedScanCount % 12 == 0) decayWeakObstacles();
    if (m_scanMatchedCount > 0) {
        setStatusMessage(QStringLiteral("位姿图SLAM建图中 · 匹配置信度 %1% · 已优化 %2 次")
                             .arg(qRound(m_localizationConfidence * 100.0))
                             .arg(m_optimizationCount));
    } else {
        setStatusMessage(QStringLiteral("正在建立初始地图，等待足够墙面用于扫描匹配"));
    }
    m_mapDirty = true;
    if (m_processedScanCount == 1) emit mapChanged();
    scheduleRender();
}

void LidarMappingController::scheduleRender()
{
    if (!m_visualizationActive) return;
    if (!m_renderTimer.isActive()) m_renderTimer.start();
}

void LidarMappingController::setVisualizationActive(bool active)
{
    if (m_visualizationActive == active) return;
    m_visualizationActive = active;
    emit visualizationActiveChanged();
    if (active && (m_mapDirty || m_mapDataUrl.isEmpty())) {
        m_renderTimer.start(0);
    } else if (!active) {
        m_renderTimer.stop();
    }
}

void LidarMappingController::refreshVisualization()
{
    if (!m_visualizationActive || !available()) return;
    m_renderTimer.start(0);
}

void LidarMappingController::renderMap()
{
    if (!m_boundsValid) {
        if (!m_mapDataUrl.isEmpty()) {
            m_mapDataUrl.clear();
            emit imageChanged();
        }
        return;
    }
    constexpr int ImageWidth = 1000;
    constexpr int ImageHeight = 700;
    constexpr double PaddingMeters = 4.0;
    const double minEast = m_minCellX * ResolutionMeters - PaddingMeters;
    const double maxEast = (m_maxCellX + 1) * ResolutionMeters + PaddingMeters;
    const double minNorth = m_minCellY * ResolutionMeters - PaddingMeters;
    const double maxNorth = (m_maxCellY + 1) * ResolutionMeters + PaddingMeters;
    const double scale = std::min(ImageWidth / std::max(1.0, maxEast - minEast),
                                  ImageHeight / std::max(1.0, maxNorth - minNorth));
    const auto screenPoint = [=](double east, double north) {
        return QPointF((east - minEast) * scale,
                       ImageHeight - (north - minNorth) * scale);
    };

    QImage image(ImageWidth, ImageHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QStringLiteral("#e9edf2")));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Build a one-pixel-per-cell raster first, then scale it once. This avoids
    // tens of thousands of QPainter fill calls on every display refresh.
    const int mapWidthCells = m_maxCellX - m_minCellX + 1;
    const int mapHeightCells = m_maxCellY - m_minCellY + 1;
    constexpr int MaximumRasterSide = 4096;
    const double rasterReduction = std::max(
        1.0, std::max(mapWidthCells, mapHeightCells) / static_cast<double>(MaximumRasterSide));
    const int rasterWidth = std::max(1, static_cast<int>(std::ceil(mapWidthCells / rasterReduction)));
    const int rasterHeight = std::max(1, static_cast<int>(std::ceil(mapHeightCells / rasterReduction)));
    const QRgb unknownColor = QColor(QStringLiteral("#e9edf2")).rgb();
    const QRgb freeColor = QColor(QStringLiteral("#f2f4f7")).rgb();
    const QRgb obstacleColor = QColor(QStringLiteral("#23374d")).rgb();
    QImage occupancyRaster(rasterWidth, rasterHeight, QImage::Format_RGB32);
    occupancyRaster.fill(unknownColor);
    for (auto it = m_cells.cbegin(); it != m_cells.cend(); ++it) {
        int x = 0;
        int y = 0;
        unpackCellKey(it.key(), &x, &y);
        const int px = std::clamp(static_cast<int>((x - m_minCellX) / rasterReduction),
                                  0, rasterWidth - 1);
        const int py = std::clamp(static_cast<int>((m_maxCellY - y) / rasterReduction),
                                  0, rasterHeight - 1);
        QRgb *pixel = reinterpret_cast<QRgb *>(occupancyRaster.scanLine(py)) + px;
        if (it.value() >= OccupiedThreshold) *pixel = obstacleColor;
        else if (it.value() <= FreeThreshold && *pixel != obstacleColor) *pixel = freeColor;
    }
    removeIsolatedRasterCells(&occupancyRaster, obstacleColor, freeColor, unknownColor);
    const QPointF mapTopLeft = screenPoint(m_minCellX * ResolutionMeters,
                                            (m_maxCellY + 1) * ResolutionMeters);
    const QPointF mapBottomRight = screenPoint((m_maxCellX + 1) * ResolutionMeters,
                                                m_minCellY * ResolutionMeters);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRectF(mapTopLeft, mapBottomRight), occupancyRaster);

    // A restrained metric grid makes orientation and scale easier to read.
    const double gridStepMeters = 10.0;
    painter.setPen(QPen(QColor(148, 163, 184, 115), 1.0));
    const double firstGridEast = std::ceil(minEast / gridStepMeters) * gridStepMeters;
    const double firstGridNorth = std::ceil(minNorth / gridStepMeters) * gridStepMeters;
    for (double east = firstGridEast; east <= maxEast; east += gridStepMeters)
        painter.drawLine(screenPoint(east, maxNorth), screenPoint(east, minNorth));
    for (double north = firstGridNorth; north <= maxNorth; north += gridStepMeters)
        painter.drawLine(screenPoint(minEast, north), screenPoint(maxEast, north));
    if (m_track.size() > 1) {
        QPainterPath path(screenPoint(m_track.constFirst().x(), m_track.constFirst().y()));
        for (qsizetype i = 1; i < m_track.size(); ++i)
            path.lineTo(screenPoint(m_track.at(i).x(), m_track.at(i).y()));
        painter.setPen(QPen(QColor(QStringLiteral("#2474e5")), 2.5,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }
    painter.setPen(QPen(Qt::white, 1.0));
    painter.setBrush(QColor(QStringLiteral("#2474e5")));
    for (const Keyframe &keyframe : std::as_const(m_keyframes))
        painter.drawEllipse(screenPoint(keyframe.pose.position.x(),
                                        keyframe.pose.position.y()), 3.0, 3.0);
    painter.setPen(QPen(QColor(QStringLiteral("#8b5cf6")), 1.5, Qt::DashLine));
    for (const PoseConstraint &constraint : std::as_const(m_poseConstraints)) {
        if (!constraint.loopClosure || constraint.fromIndex < 0 || constraint.toIndex < 0
            || constraint.fromIndex >= m_keyframes.size()
            || constraint.toIndex >= m_keyframes.size()) {
            continue;
        }
        const QPointF from = m_keyframes.at(constraint.fromIndex).pose.position;
        const QPointF to = m_keyframes.at(constraint.toIndex).pose.position;
        painter.drawLine(screenPoint(from.x(), from.y()), screenPoint(to.x(), to.y()));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#f59e0b")));
    for (const QPointF &hit : std::as_const(m_currentHits))
        painter.drawEllipse(screenPoint(hit.x(), hit.y()), 2.3, 2.3);

    const QPointF drone = screenPoint(m_currentPose.x(), m_currentPose.y());
    painter.save();
    painter.translate(drone);
    painter.rotate(m_currentHeadingDegrees);
    QPainterPath arrow;
    arrow.moveTo(0.0, -11.0);
    arrow.lineTo(7.0, 8.0);
    arrow.lineTo(0.0, 4.0);
    arrow.lineTo(-7.0, 8.0);
    arrow.closeSubpath();
    painter.setBrush(QColor(QStringLiteral("#1268e8")));
    painter.setPen(QPen(Qt::white, 2.0));
    painter.drawPath(arrow);
    painter.restore();

    // North arrow and scale bar remain visible at every map extent.
    painter.setPen(QPen(QColor(QStringLiteral("#334155")), 2.0));
    painter.setBrush(QColor(QStringLiteral("#334155")));
    painter.drawLine(QPointF(34, 54), QPointF(34, 24));
    QPainterPath northArrow;
    northArrow.moveTo(34, 17);
    northArrow.lineTo(28, 28);
    northArrow.lineTo(40, 28);
    northArrow.closeSubpath();
    painter.drawPath(northArrow);
    painter.drawText(QPointF(28, 70), QStringLiteral("N"));
    const double scaleBarMeters = scale >= 10.0 ? 10.0 : 20.0;
    const double scaleBarPixels = scaleBarMeters * scale;
    const QPointF scaleStart(28, ImageHeight - 30);
    painter.drawLine(scaleStart, QPointF(scaleStart.x() + scaleBarPixels, scaleStart.y()));
    painter.drawLine(QPointF(scaleStart.x(), scaleStart.y() - 5),
                     QPointF(scaleStart.x(), scaleStart.y() + 5));
    painter.drawLine(QPointF(scaleStart.x() + scaleBarPixels, scaleStart.y() - 5),
                     QPointF(scaleStart.x() + scaleBarPixels, scaleStart.y() + 5));
    painter.drawText(QPointF(scaleStart.x(), scaleStart.y() - 8),
                     QStringLiteral("%1 m").arg(scaleBarMeters, 0, 'f', 0));
    painter.end();

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return;
    m_mapDataUrl = QStringLiteral("data:image/png;base64,%1")
                       .arg(QString::fromLatin1(bytes.toBase64()));
    m_mapDirty = false;
    emit imageChanged();
    emit mapChanged();
}

void LidarMappingController::clear()
{
    resetOccupancyGrid();
    m_mapDataUrl.clear();
    m_lastAcceptedTimestampMs = -1;
    m_stationaryScanCounter = 0;
    m_processedScanCount = 0;
    m_skippedScanCount = 0;
    m_tiltRejectedScanCount = 0;
    m_keyframes.clear();
    m_poseConstraints.clear();
    m_scanMatchedCount = 0;
    m_loopClosureCount = 0;
    m_optimizationCount = 0;
    m_lastLoopClosureKeyframe = -1000;
    m_lastLoopClosureAttemptScan = -1000;
    m_pendingLoopClosureIndex = -1;
    m_pendingLoopConfidence = 0.0;
    m_pendingLoopMatchedPose = {};
    m_localizationConfidence = 0.0;
    m_positionCorrection = {};
    m_headingCorrectionDegrees = 0.0;
    m_slamPoseInitialized = false;
    m_lastRawPose = {};
    m_lastRawHeadingDegrees = 0.0;
    m_currentPose = {};
    m_currentHeadingDegrees = 0.0;
    m_mapDirty = false;
    setStatusMessage(QStringLiteral("等待二维雷达和飞行器本地位姿"));
    emit mapChanged();
    emit imageChanged();
}

void LidarMappingController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) return;
    m_statusMessage = message;
    emit statusChanged();
}

QVariantMap LidarMappingController::summary() const
{
    return {
        {QStringLiteral("available"), available()},
        {QStringLiteral("format"), QStringLiteral("FlightBenchLidarOccupancyMap")},
        {QStringLiteral("formatVersion"), 4},
        {QStringLiteral("mappingAlgorithm"), QStringLiteral("pose-graph-slam")},
        {QStringLiteral("mappingProfile"), QStringLiteral("robust-clean")},
        {QStringLiteral("poseSource"), QStringLiteral("time-aligned-lidar-slam-with-px4-odometry-prior")},
        {QStringLiteral("resolutionMeters"), ResolutionMeters},
        {QStringLiteral("minimumMappingAltitudeMeters"), MinimumMappingAltitudeMeters},
        {QStringLiteral("maximumMappingTiltDegrees"), MaximumMappingTiltDegrees},
        {QStringLiteral("occupiedConfirmationObservations"), 3},
        {QStringLiteral("freeConfirmationObservations"), 3},
        {QStringLiteral("exploredAreaSquareMeters"), exploredAreaSquareMeters()},
        {QStringLiteral("widthMeters"), widthMeters()},
        {QStringLiteral("heightMeters"), heightMeters()},
        {QStringLiteral("occupiedCellCount"), m_occupiedCellCount},
        {QStringLiteral("processedScanCount"), m_processedScanCount},
        {QStringLiteral("skippedScanCount"), m_skippedScanCount},
        {QStringLiteral("tiltRejectedScanCount"), m_tiltRejectedScanCount},
        {QStringLiteral("keyframeCount"), m_keyframes.size()},
        {QStringLiteral("scanMatchedCount"), m_scanMatchedCount},
        {QStringLiteral("loopClosureCount"), m_loopClosureCount},
        {QStringLiteral("poseGraphConstraintCount"), m_poseConstraints.size()},
        {QStringLiteral("optimizationCount"), m_optimizationCount},
        {QStringLiteral("localizationConfidence"), m_localizationConfidence},
        {QStringLiteral("correctionEastMeters"), m_positionCorrection.x()},
        {QStringLiteral("correctionNorthMeters"), m_positionCorrection.y()},
        {QStringLiteral("correctionHeadingDegrees"), m_headingCorrectionDegrees},
        {QStringLiteral("pngFile"), available() ? QStringLiteral("sensor/lidar-map.png") : QString()},
        {QStringLiteral("pgmFile"), available() ? QStringLiteral("sensor/lidar-map.pgm") : QString()},
        {QStringLiteral("yamlFile"), available() ? QStringLiteral("sensor/lidar-map.yaml") : QString()},
        {QStringLiteral("poseGraphFile"), available()
             ? QStringLiteral("sensor/lidar-pose-graph.json") : QString()},
        {QStringLiteral("metadataFile"), available() ? QStringLiteral("sensor/lidar-map.json") : QString()},
    };
}

bool LidarMappingController::writePortableMap(const QString &directoryPath,
                                              QString *pngPath,
                                              QString *errorMessage) const
{
    if (!available() || !m_boundsValid) {
        if (errorMessage) *errorMessage = QStringLiteral("当前还没有可保存的累计地图");
        return false;
    }
    if (!QDir().mkpath(directoryPath)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建地图保存目录");
        return false;
    }
    const int width = m_maxCellX - m_minCellX + 1;
    const int height = m_maxCellY - m_minCellY + 1;
    if (width <= 0 || height <= 0 || static_cast<qint64>(width) * height > 100000000LL) {
        if (errorMessage) *errorMessage = QStringLiteral("地图范围过大，已阻止生成超大文件");
        return false;
    }
    const QRgb unknownGray = qRgb(205, 205, 205);
    const QRgb freeGray = qRgb(254, 254, 254);
    const QRgb occupiedGray = qRgb(0, 0, 0);
    QImage occupancy(width, height, QImage::Format_RGB32);
    occupancy.fill(unknownGray);
    for (auto it = m_cells.cbegin(); it != m_cells.cend(); ++it) {
        int x = 0;
        int y = 0;
        unpackCellKey(it.key(), &x, &y);
        const int px = x - m_minCellX;
        const int py = m_maxCellY - y;
        QRgb *pixel = reinterpret_cast<QRgb *>(occupancy.scanLine(py)) + px;
        if (it.value() >= OccupiedThreshold)
            *pixel = occupiedGray;
        else if (it.value() <= FreeThreshold && *pixel != occupiedGray)
            *pixel = freeGray;
    }
    removeIsolatedRasterCells(&occupancy, occupiedGray, freeGray, unknownGray);

    // The PNG is the human-facing preview, while PGM remains a standards-
    // compatible occupancy map. A softer free-space color keeps valid scan
    // coverage from visually overwhelming building outlines.
    QImage preview(width, height, QImage::Format_RGB32);
    for (int row = 0; row < height; ++row) {
        const QRgb *sourceLine = reinterpret_cast<const QRgb *>(occupancy.constScanLine(row));
        QRgb *previewLine = reinterpret_cast<QRgb *>(preview.scanLine(row));
        for (int column = 0; column < width; ++column) {
            previewLine[column] = sourceLine[column] == occupiedGray
                ? qRgb(35, 55, 77)
                : sourceLine[column] == freeGray
                    ? qRgb(242, 244, 247)
                    : qRgb(233, 237, 242);
        }
    }
    const QString actualPngPath = QDir(directoryPath).filePath(QStringLiteral("lidar-map.png"));
    QImageWriter pngWriter(actualPngPath, QByteArrayLiteral("png"));
    if (!pngWriter.write(preview)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法写入地图 PNG：%1（%2 × %3）")
                                .arg(pngWriter.errorString())
                                .arg(width)
                                .arg(height);
        }
        return false;
    }
    const QString pgmPath = QDir(directoryPath).filePath(QStringLiteral("lidar-map.pgm"));
    QSaveFile pgm(pgmPath);
    if (!pgm.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法写入地图 PGM");
        return false;
    }
    const QByteArray header = QByteArray("P5\n") + QByteArray::number(width) + ' '
                              + QByteArray::number(height) + "\n255\n";
    if (pgm.write(header) != header.size()) return false;
    for (int row = 0; row < height; ++row) {
        QByteArray scanLine(width, static_cast<char>(205));
        const QRgb *sourceLine = reinterpret_cast<const QRgb *>(occupancy.constScanLine(row));
        for (int column = 0; column < width; ++column) {
            scanLine[column] = sourceLine[column] == occupiedGray
                ? static_cast<char>(0)
                : sourceLine[column] == freeGray
                    ? static_cast<char>(254)
                    : static_cast<char>(205);
        }
        if (pgm.write(scanLine) != scanLine.size()) return false;
    }
    if (!pgm.commit()) return false;

    const double originEast = m_minCellX * ResolutionMeters;
    const double originNorth = m_minCellY * ResolutionMeters;
    QSaveFile yaml(QDir(directoryPath).filePath(QStringLiteral("lidar-map.yaml")));
    const QByteArray yamlContents = QStringLiteral(
        "image: lidar-map.pgm\nresolution: %1\norigin: [%2, %3, 0.0]\nnegate: 0\noccupied_thresh: 0.65\nfree_thresh: 0.25\n")
        .arg(ResolutionMeters, 0, 'f', 3)
        .arg(originEast, 0, 'f', 3)
        .arg(originNorth, 0, 'f', 3).toUtf8();
    if (!yaml.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || yaml.write(yamlContents) != yamlContents.size() || !yaml.commit()) return false;

    QJsonObject metadata = QJsonObject::fromVariantMap(summary());
    metadata.insert(QStringLiteral("originEastMeters"), originEast);
    metadata.insert(QStringLiteral("originNorthMeters"), originNorth);
    metadata.insert(QStringLiteral("widthCells"), width);
    metadata.insert(QStringLiteral("heightCells"), height);
    QSaveFile json(QDir(directoryPath).filePath(QStringLiteral("lidar-map.json")));
    const QByteArray jsonContents = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    if (!json.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || json.write(jsonContents) != jsonContents.size() || !json.commit()) return false;
    if (!writePoseGraph(directoryPath, errorMessage)) return false;
    if (pngPath) *pngPath = actualPngPath;
    return true;
}

bool LidarMappingController::writePoseGraph(const QString &directoryPath,
                                             QString *errorMessage) const
{
    QJsonArray nodes;
    for (qsizetype index = 0; index < m_keyframes.size(); ++index) {
        const Keyframe &keyframe = m_keyframes.at(index);
        QJsonArray scan;
        for (const LocalScanPoint &point : keyframe.scan) {
            scan.append(QJsonArray{point.rightMeters, point.forwardMeters,
                                   point.rangeMeters, point.obstacleHit});
        }
        nodes.append(QJsonObject{
            {QStringLiteral("id"), static_cast<int>(index)},
            {QStringLiteral("timestampMs"), keyframe.timestampMs},
            {QStringLiteral("eastMeters"), keyframe.pose.position.x()},
            {QStringLiteral("northMeters"), keyframe.pose.position.y()},
            {QStringLiteral("headingDegrees"), keyframe.pose.headingDegrees},
            {QStringLiteral("rawEastMeters"), keyframe.rawPose.position.x()},
            {QStringLiteral("rawNorthMeters"), keyframe.rawPose.position.y()},
            {QStringLiteral("rawHeadingDegrees"), keyframe.rawPose.headingDegrees},
            {QStringLiteral("scan"), scan},
        });
    }
    QJsonArray constraints;
    for (const PoseConstraint &constraint : m_poseConstraints) {
        constraints.append(QJsonObject{
            {QStringLiteral("from"), constraint.fromIndex},
            {QStringLiteral("to"), constraint.toIndex},
            {QStringLiteral("rightMeters"), constraint.relativePosition.x()},
            {QStringLiteral("forwardMeters"), constraint.relativePosition.y()},
            {QStringLiteral("headingDegrees"), constraint.relativeHeadingDegrees},
            {QStringLiteral("translationWeight"), constraint.translationWeight},
            {QStringLiteral("headingWeight"), constraint.headingWeight},
            {QStringLiteral("type"), constraint.loopClosure
                 ? QStringLiteral("loop-closure") : QStringLiteral("odometry")},
        });
    }
    const QJsonObject graph{
        {QStringLiteral("format"), QStringLiteral("FlightBenchLidarPoseGraph")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("coordinateFrame"), QStringLiteral("local-east-north")},
        {QStringLiteral("nodeCount"), nodes.size()},
        {QStringLiteral("constraintCount"), constraints.size()},
        {QStringLiteral("loopClosureCount"), m_loopClosureCount},
        {QStringLiteral("optimizationCount"), m_optimizationCount},
        {QStringLiteral("nodes"), nodes},
        {QStringLiteral("constraints"), constraints},
    };
    QSaveFile file(QDir(directoryPath).filePath(QStringLiteral("lidar-pose-graph.json")));
    const QByteArray contents = QJsonDocument(graph).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(contents) != contents.size() || !file.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("无法写入 SLAM 位姿图");
        return false;
    }
    return true;
}

QVariantMap LidarMappingController::saveToDirectory(const QString &directoryPath)
{
    QVariantMap result = summary();
    QString path;
    QString error;
    const bool saved = writePortableMap(directoryPath, &path, &error);
    result.insert(QStringLiteral("saved"), saved);
    result.insert(QStringLiteral("error"), error);
    if (saved) {
        qint64 sizeBytes = 0;
        for (const QString &file : {QStringLiteral("lidar-map.png"),
                                    QStringLiteral("lidar-map.pgm"),
                                    QStringLiteral("lidar-map.yaml"),
                                    QStringLiteral("lidar-map.json"),
                                    QStringLiteral("lidar-pose-graph.json")}) {
            sizeBytes += QFileInfo(QDir(directoryPath).filePath(file)).size();
        }
        result.insert(QStringLiteral("sizeBytes"), sizeBytes);
        m_lastSavedPath = path;
        emit lastSavedPathChanged();
    }
    return result;
}

bool LidarMappingController::saveMap(const QString &directoryPath)
{
    return saveToDirectory(directoryPath).value(QStringLiteral("saved")).toBool();
}
