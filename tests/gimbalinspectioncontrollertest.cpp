#include "inspection/gimbalinspectioncontroller.h"
#include "mapping/worldmapcatalog.h"
#include "sessions/testreportgenerator.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        const auto bytes = message.toUtf8();
        std::fprintf(stderr, "%s\n", bytes.constData());
    });
    bool pass = true;
    const auto expect = [&](bool good, const char *message) { if (!good) { qCritical() << message; pass = false; } };
    const QString world = QStringLiteral("gimbal-test-world");
    const auto wall = WorldMapCatalog::raycastBuilding(world, {0,5,0}, {0,0,-1});
    expect(wall.value("hit").toBool() && std::abs(wall.value("distance").toDouble() - 10) < .001, "wall raycast");
    expect(wall.value("normal").value<QVector3D>().z() > .99f, "wall outward normal");
    const auto roof = WorldMapCatalog::raycastBuilding(world, {-5,30,-20}, {0,-1,0});
    expect(roof.value("hit").toBool() && std::abs(roof.value("distance").toDouble()-10) < .001, "roof / reversed winding");
    expect(!WorldMapCatalog::raycastBuilding(world, {0,5,0}, {0,0,-1},9.85).value("hit").toBool(), "target endpoint is not occlusion");
    expect(WorldMapCatalog::raycastBuilding(world, {0,5,0}, {0,0,-1},15).value("hit").toBool(), "foreground occlusion");
    expect(!WorldMapCatalog::raycastBuilding(world, {20,5,0}, {0,0,-1}).value("hit").toBool(), "parallel outside ray");
    expect(WorldMapCatalog::raycastBuilding(world, {0,5,-20}, {0,0,1}).value("distance").toDouble() == 0, "inside origin is blocked");
    for (double heading : {0.0, 90.0, 179.9, 270.0}) {
        const auto orientation = GimbalInspectionController::aircraftOrientation(heading, 10, -8);
        for (const auto target : {QVector3D(8,3,-15), QVector3D(-5,-4,-10), QVector3D(1,0,20),
                                  QVector3D(0,-20,0), QVector3D(0,20,0), QVector3D(10,-15,0)}) {
            const auto body = orientation.conjugated().rotatedVector(target);
            const auto angles = GimbalInspectionController::aimAngles(body);
            const auto desired = (body - GimbalInspectionController::cameraOrigin(angles.x(), angles.y())).normalized();
            expect(QVector3D::dotProduct(desired, GimbalInspectionController::cameraDirection(angles.x(), angles.y())) > .9999f,
                   "aiming must include mount offset, roll, pitch and yaw wrap");
        }
    }
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary session");
    const qint64 epoch = QDateTime::currentMSecsSinceEpoch();
    GimbalInspectionController controller;
    controller.prepareSession(temporary.path(), world, true, epoch);
    controller.updatePose({0,5,0},0,0,0,true,true,0,0,0);
    controller.setPicking(true);
    expect(controller.selectRay({0,5,0},{0,0,-1}), "select real surface");
    expect(controller.start(), "start lock");
    const auto angles = GimbalInspectionController::aimAngles(controller.target()-QVector3D(0,5,0));
    int commands = 0;
    QObject::connect(&controller, &GimbalInspectionController::commandRequested, [&](double,double) { ++commands; });
    controller.updatePose({0,5,0},0,0,0,true,false,0,0,0);
    expect(commands == 0, "stale feedback must not command");
    controller.updatePose({0,5,0},0,0,0,true,true,angles.x(),angles.y(),0);
    QThread::msleep(1050);
    controller.updatePose({0,5,0},0,0,0,true,true,angles.x(),angles.y(),0);
    QImage image(320,180,QImage::Format_RGB32);
    for (int y=0;y<180;++y) for (int x=0;x<320;++x) image.setPixelColor(x,y,(x/12+y/12)%2 ? QColor("#E5EDF5") : QColor("#263746"));
    QByteArray jpeg; QBuffer buffer(&jpeg); buffer.open(QIODevice::WriteOnly); image.save(&buffer,"JPEG");
    controller.ingestFrame(jpeg,1,QDateTime::currentMSecsSinceEpoch(),{0,5,0},QQuaternion(),true);
    expect(controller.completedCount()==1, "fresh aligned clear frame completes exactly one view");
    controller.ingestFrame(jpeg,1,QDateTime::currentMSecsSinceEpoch(),{0,5,0},QQuaternion(),true);
    expect(controller.completedCount()==1, "duplicate frame must not create another capture");
    controller.pause();
    const int previousCommands=commands;
    controller.updatePose({0,5,0},0,0,0,true,true,0,0,0);
    expect(commands==previousCommands,"manual pause stops commands");
    const auto summary=controller.finishSession();
    expect(summary.value("completedCount").toInt()==1,"final summary survives stop");
    QFile csv(temporary.path()+"/telemetry.csv");
    expect(csv.open(QIODevice::WriteOnly), "report CSV fixture");
    csv.write("elapsed_ms,altitude_m,horizontal_speed_m_s\n0,5,0\n"); csv.close();
    QVariantMap reportSession{{"id", "gimbal-test"}, {"folderPath", temporary.path()}, {"csvPath", csv.fileName()},
        {"sensorRecording", QVariantMap{{"kind", "gimbal-camera"}, {"usage", "record"},
                                       {"gimbalInspection", summary}}}};
    const auto report = TestReportGenerator::generate(reportSession, temporary.path()+"/report");
    QFile html(report);
    expect(html.open(QIODevice::ReadOnly), "gimbal HTML report generated");
    const auto contents = html.readAll();
    expect(contents.contains("data:image/jpeg;base64,") && contents.contains("质量合格 1 / 4")
        && contents.contains("不代表建筑表面覆盖率"), "report contains actual photos and honest completion definition");
    GimbalInspectionController history;
    expect(history.loadSession(temporary.path()) && history.completedCount()==1,"saved session loads");
    expect(!history.start(),"historical session cannot control gimbal");
    history.seek(0);
    expect(!history.views()[0].toMap().value("availableAtTime").toBool(),"history before capture is not a current photo");
    history.seek(5000);
    expect(history.views()[0].toMap().value("availableAtTime").toBool(),"history after capture has photo");
    controller.setSelectedView(0);
    controller.retake();
    expect(controller.completedCount()==0,"retake invalidates previous completion");
    expect(controller.start(), "resume for multi-view captures");
    controller.setSelectedView(3);
    expect(controller.guideView()==3 && controller.selectedView()==0,"auto reference selection does not interrupt capture role");
    const QVector3D observers[4]{{0,5,0},{-5.736f,5,-1.808f},{5.736f,5,-1.808f},{0,9.226f,-0.937f}};
    for (int i=0;i<4;++i) {
        const auto aim=GimbalInspectionController::aimAngles(controller.target()-observers[i]);
        controller.updatePose(observers[i],0,0,0,true,true,aim.x(),aim.y(),0);
        expect(controller.selectedView()==i,"automatic mode matches unfinished role without clicking cards");
        QThread::msleep(1050);
        controller.updatePose(observers[i],0,0,0,true,true,aim.x(),aim.y(),0);
        controller.ingestFrame(jpeg,quint64(i+2),QDateTime::currentMSecsSinceEpoch(),observers[i],QQuaternion(),true);
        expect(controller.completedCount()==i+1,"each distinct observation role can be completed");
    }
    expect(controller.finishSession().value("completedCount").toInt()==4,"four-view summary");
    expect(history.loadSession(temporary.path()) && history.completedCount()==4,"all four photos reload");
    QFile journal(temporary.path()+"/sensor/gimbal-inspection/diagnostics.jsonl");
    expect(journal.open(QIODevice::ReadOnly),"diagnostic journal exists");
    const auto diagnostics=journal.readAll();
    expect(diagnostics.contains("自动匹配未完成视角") && diagnostics.contains("relativeElevationDegrees"),"journal records match and observation geometry");
    controller.setAutomatic(false);
    controller.setSelectedView(3);
    const QVector3D overhead(0,29,-9);
    const auto highAim=GimbalInspectionController::aimAngles(controller.target()-overhead);
    controller.updatePose(overhead,0,0,0,true,true,highAim.x(),highAim.y(),0);
    expect(controller.guidance().contains("视角过高"),"overhead observation explains excessive elevation");
    controller.setSelectedView(0);
    controller.updatePose(observers[1],0,0,0,true,true,0,0,0);
    expect(controller.selectedView()==0,"manual mode preserves selected view");
    controller.setSelectedView(0); controller.retake();
    controller.updatePose({0,5,0},0,0,0,true,true,angles.x(),angles.y(),0);
    expect(controller.start(),"resume for occlusion checks");
    const int beforeBlocked=commands;
    controller.updatePose({0,5,-40},0,0,0,true,true,0,0,0);
    expect(commands==beforeBlocked,"far-side building occlusion emits no new target");
    controller.updatePose({0,5,0},179.8,0,0,true,true,179.8,0,0);
    expect(commands==beforeBlocked,"yaw boundary must not cause a full spin");
    GimbalInspectionController preview;
    preview.prepareSession(temporary.path()+"/preview",world,false,epoch);
    preview.updatePose({0,5,0},0,0,0,true,true,0,0,0);
    preview.setPicking(true); expect(preview.selectRay({0,5,0},{0,0,-1}) && preview.start(),"preview supports pointing");
    expect(!preview.canRecord(),"preview cannot falsely claim saving");
    GimbalInspectionController flat;
    const QString flatFolder=temporary.path()+"/flat";
    QDir().mkpath(flatFolder);
    flat.prepareSession(flatFolder,world,true,epoch);
    flat.updatePose({0,5,0},0,0,0,true,true,0,0,0);
    flat.setPicking(true); expect(flat.selectRay({0,5,0},{0,0,-1}) && flat.start(),"flat wall fixture");
    flat.updatePose({0,5,0},0,0,0,true,true,angles.x(),angles.y(),0);
    QThread::msleep(1050);
    flat.updatePose({0,5,0},0,0,0,true,true,angles.x(),angles.y(),0);
    QImage plain(320,180,QImage::Format_RGB32); plain.fill(QColor("#888888"));
    QByteArray plainJpeg; QBuffer plainBuffer(&plainJpeg); plainBuffer.open(QIODevice::WriteOnly); plain.save(&plainBuffer,"JPEG");
    flat.ingestFrame(plainJpeg,1,QDateTime::currentMSecsSinceEpoch(),{0,5,0},QQuaternion(),true);
    expect(flat.savedCount()==1 && flat.completedCount()==0,"low texture saved as review, never qualified");
    expect(flat.observation().value("available").toBool(),"observation guidance exposed");
    auto flatSummary=flat.finishSession();
    expect(history.loadSession(flatFolder) && history.savedCount()==1 && history.completedCount()==0,"review photos reload honestly");
    reportSession["folderPath"]=flatFolder;
    reportSession["sensorRecording"]=QVariantMap{{"kind","gimbal-camera"},{"usage","record"},{"gimbalInspection",flatSummary}};
    QFile reviewReport(TestReportGenerator::generate(reportSession,flatFolder+"/report"));
    expect(reviewReport.open(QIODevice::ReadOnly),"review report opens");
    expect(reviewReport.readAll().contains("已拍 1 / 4，质量合格 0 / 4"),"report separates saved and qualified");
    expect(flat.start(),"resume review replacement");
    flat.updatePose({0,5,0},0,0,0,true,true,angles.x(),angles.y(),0);
    QThread::msleep(1050);
    flat.updatePose({0,5,0},0,0,0,true,true,angles.x(),angles.y(),0);
    flat.ingestFrame(plainJpeg,2,QDateTime::currentMSecsSinceEpoch(),{0,5,0},QQuaternion(),true);
    expect(QDir(flatFolder+"/sensor/gimbal-inspection").entryList({"*.jpg"},QDir::Files).size()==1,"review frames do not flood disk");
    QThread::msleep(1050);
    flat.updatePose({0,5,0},0,0,0,true,true,angles.x(),angles.y(),0);
    flat.ingestFrame(jpeg,3,QDateTime::currentMSecsSinceEpoch(),{0,5,0},QQuaternion(),true);
    expect(flat.savedCount()==1 && flat.completedCount()==1,"clear evidence upgrades review without double count");
    flat.setPicking(true); expect(!flat.picking(),"cannot change target after review/capture");
    qInfo() << "Gimbal inspection regression:" << (pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
