#pragma once
#include "mapping/depthvoxel.h"
#include <QIODevice>
#include <QString>
#include <functional>

namespace DepthMapReader {
// Reads from an already-open device. Emits one cell at a time, in file order.
// The caller owns map clearing and chunk indexing.
bool readPoints(QIODevice &file, double resolution,
    const std::function<void(int, int, int, const DepthVoxel &)> &accept,
    QString *errorMessage);
}
