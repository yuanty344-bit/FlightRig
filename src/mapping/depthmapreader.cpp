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
    if (ix < 0 || iy < 0 || iz < 0 || io < 0 || iw < 0 || it < 0
        || ioc < 0 || ifree < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("地图 CSV 不是当前 DepthFusion 格式");
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
        if (parts.size() <= std::max({io, iw, it, ioc, ifree})) continue;
        const int savedObservations = parts[io].toInt();
        voxel.observations = static_cast<quint16>(
            std::clamp(std::max(4, savedObservations), 4, 65535));
        const float savedWeight = parts[iw].toFloat();
        voxel.weight = std::max(6.0f, savedWeight);
        voxel.tsdf = parts[it].toFloat();
        voxel.occupancy = std::max(2.0f, parts[ioc].toFloat());
        voxel.freeObservations = static_cast<quint16>(
            std::clamp(parts[ifree].toInt(), 0, 65535));
        accept(x, y, z, voxel);
    }
    return true;
}
