#pragma once
#include <QPointF>
#include <QVector>
namespace LidarMapTypes {
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
}
