#include "mapping/depthmapreader.h"
#include <QStringList>
#include <QtMath>
#include <algorithm>

bool DepthMapReader::readPoints(QIODevice &file, double resolution,
    const std::function<void(int, int, int, const DepthVoxel &)> &accept,
    QString *errorMessage)
{
    const QStringList header = QString::fromUtf8(file.readLine()).trimmed().split(',');
    const auto column = [&header](const QString &name) { return header.indexOf(name); };
    const int ix = column("east_m"), iy = column("up_m"), iz = column("south_m");
    const int io = column("observations"), iw = column("weight"), it = column("tsdf");
    const int ioc = column("occupancy"), ifree = column("free_observations");
    if (ix < 0 || iy < 0 || iz < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("地图 CSV 缺少坐标列");
        return false;
    }
    while (!file.atEnd()) {
        const QStringList parts = QString::fromUtf8(file.readLine()).trimmed().split(',');
        if (parts.size() <= std::max({ix, iy, iz})) continue;
        bool okx = false, oky = false, okz = false;
        const double px = parts[ix].toDouble(&okx);
        const double py = parts[iy].toDouble(&oky);
        const double pz = parts[iz].toDouble(&okz);
        if (!okx || !oky || !okz) continue;
        const int x = qFloor(px / resolution);
        const int y = qFloor(py / resolution);
        const int z = qFloor(pz / resolution);
        DepthVoxel voxel;
        voxel.center = QVector3D(px, py, pz);
        const int savedObservations = io >= 0 && io < parts.size() ? parts[io].toInt() : 2;
        voxel.observations = static_cast<quint16>(
            std::clamp(std::max(4, savedObservations), 4, 65535));
        const float savedWeight = iw >= 0 && iw < parts.size()
            ? parts[iw].toFloat() : static_cast<float>(voxel.observations);
        voxel.weight = std::max(6.0f, savedWeight);
        voxel.tsdf = it >= 0 && it < parts.size() ? parts[it].toFloat() : 0.0f;
        voxel.occupancy = std::max(2.0f,
            ioc >= 0 && ioc < parts.size() ? parts[ioc].toFloat() : 2.0f);
        voxel.freeObservations = ifree >= 0 && ifree < parts.size()
            ? static_cast<quint16>(std::clamp(parts[ifree].toInt(), 0, 65535)) : 0;
        accept(x, y, z, voxel);
    }
    return true;
}
