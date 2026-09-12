#include "mapping/depthmapexport.h"
#include "mapping/depthvoxelcoordinates.h"
#include <QDir>
#include <QSaveFile>
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <array>

bool DepthMapExport::writePointCloud(const QHash<quint64, QVector3D> &centers,
    const QHash<quint64, DepthVoxel> &voxels, const QString &directoryPath, QString *plyPath,
                                             QString *csvPath, QString *errorMessage)
{
    if (!QDir().mkpath(directoryPath)) { if (errorMessage) *errorMessage = QStringLiteral("无法创建地图目录"); return false; }
    const QString ply = QDir(directoryPath).filePath(QStringLiteral("depth-obstacles.ply"));
    const QString csv = QDir(directoryPath).filePath(QStringLiteral("depth-obstacles.csv"));
    QSaveFile plyFile(ply), csvFile(csv);
    if (!plyFile.open(QIODevice::WriteOnly | QIODevice::Text) || !csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建障碍点云文件：%1 %2")
            .arg(plyFile.errorString(), csvFile.errorString()); return false;
    }
    QByteArray header = "ply\nformat ascii 1.0\ncomment FlightRig DepthFusion surface\nelement vertex ";
    header += QByteArray::number(centers.size()) + "\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n";
    plyFile.write(header);
    csvFile.write("east_m,up_m,south_m,observations,weight,tsdf,occupancy,free_observations\n");
    for (auto it = centers.cbegin(); it != centers.cend(); ++it) {
        const DepthVoxel &voxel = voxels.value(it.key());
        const QByteArray xyz = QByteArray::number(voxel.center.x(), 'f', 3) + " " + QByteArray::number(voxel.center.y(), 'f', 3) + " " + QByteArray::number(voxel.center.z(), 'f', 3);
        plyFile.write(xyz + " 35 139 184\n");
        csvFile.write(QByteArray::number(voxel.center.x(), 'f', 3) + "," + QByteArray::number(voxel.center.y(), 'f', 3) + "," + QByteArray::number(voxel.center.z(), 'f', 3) + "," + QByteArray::number(voxel.observations) + "," + QByteArray::number(voxel.weight, 'f', 3) + "," + QByteArray::number(voxel.tsdf, 'f', 5) + "," + QByteArray::number(voxel.occupancy, 'f', 3) + "," + QByteArray::number(voxel.freeObservations) + "\n");
    }
    if (!plyFile.commit() || !csvFile.commit()) { if (errorMessage) *errorMessage = QStringLiteral("写入障碍点云文件失败"); return false; }
    if (plyPath) *plyPath = ply; if (csvPath) *csvPath = csv; return true;
}
bool DepthMapExport::writeMesh(const QHash<quint64, QVector3D> &centers, float resolution,
    const QString &directoryPath, QString *meshPath,
                                       int *triangleCount, QString *errorMessage)
{
    const QString path = QDir(directoryPath).filePath(QStringLiteral("depth-surface.obj"));
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { if (errorMessage) *errorMessage = QStringLiteral("无法创建三角网格文件"); return false; }
    file.write("# FlightRig DepthFusion exposed-face mesh\n");
    int vertexBase = 1, triangles = 0; const float h = resolution * 0.46f;
    const std::array<std::array<int, 3>, 6> neighbours{{{{0,0,-1}},{{0,0,1}},{{-1,0,0}},{{1,0,0}},{{0,1,0}},{{0,-1,0}}}};
    for (auto it = centers.cbegin(); it != centers.cend(); ++it) {
        int x=0,y=0,z=0; DepthVoxelCoordinates::unpackedKey(it.key(), &x,&y,&z); const QVector3D c=it.value();
        const QVector3D p[8] = {c+QVector3D(-h,-h,-h),c+QVector3D(-h,-h,h),c+QVector3D(-h,h,-h),c+QVector3D(-h,h,h),c+QVector3D(h,-h,-h),c+QVector3D(h,-h,h),c+QVector3D(h,h,-h),c+QVector3D(h,h,h)};
        const int faces[6][4]={{0,4,6,2},{5,1,3,7},{1,0,2,3},{4,5,7,6},{2,6,7,3},{1,5,4,0}};
        for (int face=0; face<6; ++face) {
            const auto n=neighbours[face]; if (centers.contains(DepthVoxelCoordinates::packedKey(x+n[0],y+n[1],z+n[2]))) continue;
            for (int k=0;k<4;++k) { const QVector3D v=p[faces[face][k]]; file.write("v "+QByteArray::number(v.x(),'f',4)+" "+QByteArray::number(v.y(),'f',4)+" "+QByteArray::number(v.z(),'f',4)+"\n"); }
            file.write("f "+QByteArray::number(vertexBase)+" "+QByteArray::number(vertexBase+1)+" "+QByteArray::number(vertexBase+2)+"\n");
            file.write("f "+QByteArray::number(vertexBase)+" "+QByteArray::number(vertexBase+2)+" "+QByteArray::number(vertexBase+3)+"\n");
            vertexBase += 4; triangles += 2;
        }
    }
    if (!file.commit()) { if (errorMessage) *errorMessage = QStringLiteral("写入三角网格失败"); return false; }
    if (meshPath) *meshPath=path; if (triangleCount) *triangleCount=triangles; return true;
}

bool DepthMapExport::writeTopDownPreview(const QHash<quint64, QVector3D> &centers,
    bool boundsValid, double width, double depth, const QVector3D &center,
    const QString &directoryPath, QString *previewPath,
                                                 QString *errorMessage)
{
    if (!boundsValid) return false; constexpr int size=900; QImage image(size,size,QImage::Format_RGB32); image.fill(QColor("#F3F6FA"));
    QPainter painter(&image); const double scale=820.0/std::max({1.0,width,depth});
    for (auto it=centers.cbegin();it!=centers.cend();++it) {
        const QVector3D &point=it.value();
        const int px=qRound(450+(point.x()-center.x())*scale);
        const int py=qRound(450+(point.z()-center.z())*scale);
        const double h=std::clamp(static_cast<double>(point.y())/20.0,0.0,1.0);
        painter.fillRect(QRect(px-2,py-2,5,5),
                         QColor::fromHsvF(static_cast<float>(0.56-h*0.10),
                                          0.70f, 0.86f));
    }
    // Keep export independent from a GUI application: the mapping unit test
    // intentionally uses QCoreApplication, and drawing text would initialize
    // the platform font database. The JSON metadata carries the full caption.
    painter.end();
    const QString path=QDir(directoryPath).filePath(QStringLiteral("depth-obstacles.png")); if(!image.save(path,"PNG")){if(errorMessage)*errorMessage=QStringLiteral("保存地图预览失败");return false;} if(previewPath)*previewPath=path;return true;
}
