#pragma once
#include <QtGlobal>

// Shared signed 21-bit voxel coordinates; used by fusion, drawing and export.
namespace DepthVoxelCoordinates {
constexpr qint64 CoordinateBias = 1 << 20;
constexpr quint64 CoordinateMask = (1ULL << 21) - 1;

inline quint64 packedKey(int x, int y, int z)
{
    return ((static_cast<quint64>(x + CoordinateBias) & CoordinateMask) << 42)
           | ((static_cast<quint64>(y + CoordinateBias) & CoordinateMask) << 21)
           | (static_cast<quint64>(z + CoordinateBias) & CoordinateMask);
}

inline void unpackedKey(quint64 key, int *x, int *y, int *z)
{
    if (x) *x = static_cast<int>((key >> 42) & CoordinateMask) - CoordinateBias;
    if (y) *y = static_cast<int>((key >> 21) & CoordinateMask) - CoordinateBias;
    if (z) *z = static_cast<int>(key & CoordinateMask) - CoordinateBias;
}

}
