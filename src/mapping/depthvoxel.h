#pragma once
#include <QVector3D>

// Fusion cell data shared with serialization, without rendering dependencies.
struct DepthVoxel {
        QVector3D center;
        float tsdf{1.0f};
        float weight{0.0f};
        float occupancy{0.0f};
        quint16 observations{0};
        quint16 freeObservations{0};
        int lastSeenFrame{0};
    };
