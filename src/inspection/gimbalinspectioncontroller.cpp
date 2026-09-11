#include "inspection/gimbalinspectioncontroller.h"
#include "inspection/visualsurveycontroller.h"
#include "mapping/worldmapcatalog.h"
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStorageInfo>
#include <QUrl>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
double wrapped(double angle) { return std::remainder(angle, 360.0); }
double azimuth(const QVector3D &v) { return qRadiansToDegrees(std::atan2(double(v.x()), double(-v.z()))); }
double elevation(const QVector3D &v) { return qRadiansToDegrees(std::atan2(double(v.y()), std::hypot(double(v.x()), double(v.z())))); }
QVector3D fluToScene(const QVector3D &v) { return {-v.y(), v.z(), -v.x()}; }
QQuaternion yawRotation(double yaw) { return QQuaternion::fromAxisAndAngle(0, 0, 1, float(-yaw)); }
QQuaternion pitchRotation(double pitch) { return QQuaternion::fromAxisAndAngle(0, 1, 0, float(pitch)); }
QQuaternion mountRotation() { return QQuaternion::fromAxisAndAngle(0, 0, 1, float(qRadiansToDegrees(3.14))); }
QVariantList vectorJson(const QVector3D &v) { return {v.x(), v.y(), v.z()}; }
QVector3D fromJson(const QVariant &v) {
    const auto a = v.toList();
    return a.size() == 3 ? QVector3D(a[0].toFloat(), a[1].toFloat(), a[2].toFloat()) : QVector3D();
}
}

GimbalInspectionController::GimbalInspectionController(QObject *parent) : QObject(parent) { resetViews(); }

QQuaternion GimbalInspectionController::aircraftOrientation(double heading, double pitch, double roll)
{
    return QQuaternion::fromAxisAndAngle(0, 1, 0, float(-heading))
        * QQuaternion::fromAxisAndAngle(1, 0, 0, float(pitch))
        * QQuaternion::fromAxisAndAngle(0, 0, -1, float(roll));
}

QVector3D GimbalInspectionController::cameraOrigin(double yaw, double pitch)
{
    // x500_gimbal mount and the local gimbal/model.sdf joint pivots, in metres.
    const QVector3D yawPivot(-0.02552f, 0, 0), pitchPivot(-0.0412f, 0, -0.162f);
    const QVector3D sensor(-0.085f, 0, -0.162f);
    const auto local = yawPivot + yawRotation(yaw).rotatedVector(
        pitchPivot - yawPivot + pitchRotation(pitch).rotatedVector(sensor - pitchPivot));
    return fluToScene(QVector3D(0, 0, 0.26f) + mountRotation().rotatedVector(local));
}

QVector3D GimbalInspectionController::cameraDirection(double yaw, double pitch)
{
    const auto optical = mountRotation().rotatedVector(QVector3D(1, 0, 0));
    return fluToScene(mountRotation().rotatedVector(
        yawRotation(yaw).rotatedVector(pitchRotation(pitch).rotatedVector(optical)))).normalized();
}

QPointF GimbalInspectionController::aimAngles(const QVector3D &bodyTarget)
{
    double yaw = azimuth(bodyTarget), pitch = elevation(bodyTarget);
    // Correct the small 3.14-vs-pi mounting offset and moving optical origin.
    for (int i = 0; i < 6; ++i) {
        const auto desired = (bodyTarget - cameraOrigin(yaw, pitch)).normalized();
        const auto actual = cameraDirection(yaw, pitch);
        yaw = wrapped(yaw + wrapped(azimuth(desired) - azimuth(actual)));
        pitch += elevation(desired) - elevation(actual);
    }
    return {yaw, pitch};
}

void GimbalInspectionController::resetViews()
{
    m_views.clear();
    for (const auto &label : {QStringLiteral("正面"), QStringLiteral("左斜视"),
                              QStringLiteral("右斜视"), QStringLiteral("较高视角")})
        m_views.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("complete"), false}});
}

void GimbalInspectionController::clear()
{
    m_active = m_picking = m_historical = m_hasTarget = m_canRecord = m_ready = m_aligned = m_capture = false;
    m_folder.clear(); m_world.clear(); m_epochMs = 0; m_sequence = 0; m_bytes = 0;
    m_selected = m_guide = 0; m_replayMs = -1; m_distance = 0; m_alignmentError = -1;
    m_diagnosticTimes.clear(); m_diagnosticBytes = 0; m_diagnosticFailed = false;
    m_frameBlock.clear(); m_frameBlockClock.invalidate();
    m_stable.invalidate(); m_candidate.invalidate(); m_poseClock.invalidate();
    resetViews(); setStatus(QStringLiteral("在三维视图选择建筑表面目标")); emit changed();
}

void GimbalInspectionController::prepareSession(const QString &folder, const QString &world, bool record, qint64 epochMs)
{
    clear(); m_folder = folder; m_world = world; m_canRecord = record; m_epochMs = epochMs; emit changed();
}

void GimbalInspectionController::setStatus(const QString &text)
{
    if (m_status == text) return;
    m_status = text;
    diagnose(text);
    emit changed();
}
void GimbalInspectionController::diagnose(const QString &reason)
{
    if (!m_historical && m_active && (reason.startsWith(QStringLiteral("候选"))
        || reason.startsWith(QStringLiteral("目标区域需复核")) || reason.startsWith(QStringLiteral("无法解码")))) {
        m_frameBlock = reason; m_frameBlockClock.restart();
    }
    if (m_historical || !m_hasTarget || !m_canRecord || m_folder.isEmpty()) return;
    const qint64 elapsed = std::max(qint64(0), QDateTime::currentMSecsSinceEpoch() - m_epochMs);
    const QString key = QString::number(m_selected) + reason;
    if (m_diagnosticTimes.contains(key) && elapsed - m_diagnosticTimes.value(key) < 5000) return;
    m_diagnosticTimes.insert(key, elapsed);
    auto view = m_views[m_selected].toMap();
    if (m_active) {
        view[QStringLiteral("lastDiagnostic")] = reason;
        view[QStringLiteral("lastDiagnosticMs")] = elapsed;
    }
    m_views[m_selected] = view;
    if (m_diagnosticBytes >= 4 * 1024 * 1024) return; // Bounded journal; never grow with flight duration indefinitely.
    const auto observer = m_position + m_orientation.rotatedVector(cameraOrigin(m_yaw, m_tilt)) - m_target;
    const QVariantMap event{{"elapsedMs", elapsed}, {"selectedView", m_selected}, {"guideView", m_guide}, {"automatic", m_automatic},
        {"reason", reason}, {"distanceM", observer.length()},
        {"relativeAzimuthDegrees", wrapped(azimuth(observer) - m_referenceAzimuth)},
        {"relativeElevationDegrees", elevation(observer) - m_referenceElevation}, {"aligned", m_aligned},
        {"alignmentErrorDegrees", m_alignmentError}, {"stableMs", m_stable.isValid() ? m_stable.elapsed() : 0}};
    const auto bytes = QJsonDocument(QJsonObject::fromVariantMap(event)).toJson(QJsonDocument::Compact) + '\n';
    const QString directory = QDir(m_folder).filePath(QStringLiteral("sensor/gimbal-inspection"));
    QFile file(QDir(directory).filePath(QStringLiteral("diagnostics.jsonl")));
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly | QIODevice::Append)
        || file.write(bytes) != bytes.size() || !file.flush()) m_diagnosticFailed = true;
    else m_diagnosticBytes += bytes.size();
}
void GimbalInspectionController::setPicking(bool value)
{
    if (m_historical) return;
    if (value && (!m_ready || !m_views[0].toMap().value(QStringLiteral("image")).toString().isEmpty()
                  || savedCount() > 0)) {
        setStatus(m_ready ? QStringLiteral("本次已采集目标；更换目标请开始新会话")
                          : QStringLiteral("请先启动云台仿真，等待位置与角度反馈")); return;
    }
    pause(); m_picking = value; emit changed();
}
void GimbalInspectionController::setAutomatic(bool value) {
    if (!m_historical) {
        m_automatic = value; m_capture = false;
        if (!value && m_selected != m_guide) { m_selected = m_guide; m_stable.invalidate(); }
        diagnose(value ? QStringLiteral("启用自动匹配与采集") : QStringLiteral("改为手动选择与采集"));
        emit changed();
    }
}
void GimbalInspectionController::setName(const QString &value)
{
    if (m_historical) return;
    m_name = value.trimmed().left(80); if (m_name.isEmpty()) m_name = QStringLiteral("巡检目标");
    if (m_hasTarget && m_canRecord) save(); emit changed();
}
void GimbalInspectionController::setSelectedView(int value)
{
    if (value < 0 || value >= 4) return;
    m_guide = value;
    if ((!m_automatic || m_historical) && m_selected != value) {
        m_selected = value; m_stable.invalidate(); m_capture = false;
    }
    emit changed();
    diagnose(QStringLiteral("用户选择视角卡片"));
}
void GimbalInspectionController::pause()
{
    m_active = false; m_aligned = false; m_capture = false; m_stable.invalidate();
    if (m_hasTarget && !m_historical) setStatus(QStringLiteral("锁定已暂停；可以手动调整云台"));
    emit changed();
}
bool GimbalInspectionController::start()
{
    if (m_historical || !m_hasTarget || !m_ready) { setStatus(QStringLiteral("需要有效目标、实时位置及新鲜云台反馈")); return false; }
    m_active = true; m_picking = false; m_stable.invalidate();
    setStatus(QStringLiteral("正在对准目标；无人机仍由你控制")); emit changed(); return true;
}
bool GimbalInspectionController::selectRay(const QVector3D &origin, const QVector3D &direction)
{
    if (!m_picking || !m_ready || m_historical) return false;
    const auto hit = WorldMapCatalog::raycastBuilding(m_world, origin, direction);
    if (!hit.value(QStringLiteral("hit")).toBool() || hit.value(QStringLiteral("distance")).toDouble() < 0.1) {
        setStatus(QStringLiteral("没有选中建筑表面，请点击墙面或屋顶")); return false;
    }
    m_target = hit.value(QStringLiteral("position")).value<QVector3D>();
    m_normal = hit.value(QStringLiteral("normal")).value<QVector3D>();
    const auto camera = m_position + m_orientation.rotatedVector(cameraOrigin(m_yaw, m_tilt));
    const auto directionToObserver = camera - m_target;
    m_referenceAzimuth = azimuth(directionToObserver);
    m_referenceElevation = elevation(directionToObserver);
    m_hasTarget = true; m_picking = false; resetViews();
    if (m_canRecord && !save()) return false;
    setStatus(QStringLiteral("目标已选定；选择时的观察方向作为正面基准")); emit changed(); return true;
}

void GimbalInspectionController::updatePose(const QVector3D &position, double heading, double pitch, double roll,
    bool ready, bool feedbackFresh, double yaw, double tilt, double gimbalRoll)
{
    if (m_historical) return;
    m_ready = ready && feedbackFresh && std::isfinite(position.lengthSquared())
        && std::isfinite(heading) && std::isfinite(pitch) && std::isfinite(roll)
        && std::isfinite(yaw) && std::isfinite(tilt) && std::isfinite(gimbalRoll);
    m_position = position; m_orientation = aircraftOrientation(heading, pitch, roll);
    m_yaw = yaw; m_tilt = tilt; m_poseClock.restart();
    if (!m_active) return;
    m_aligned = false;
    m_alignmentError = -1;
    if (!m_ready) { m_stable.invalidate(); setStatus(QStringLiteral("数据过期或断连：暂停指令更新与采集，等待恢复")); return; }
    if (std::abs(gimbalRoll) > 2.0) { m_stable.invalidate(); setStatus(QStringLiteral("等待云台横滚回正")); emit commandRequested(yaw, tilt); return; }
    const auto angles = aimAngles(m_orientation.conjugated().rotatedVector(m_target - position));
    if (!std::isfinite(angles.x()) || !std::isfinite(angles.y()) || angles.y() < -135.0 || angles.y() > 45.0) {
        m_stable.invalidate(); setStatus(QStringLiteral("目标超出云台俯仰范围；请移动无人机")); return;
    }
    // The existing bridge accepts bounded absolute yaw, not an unwrapped joint
    // target. Do not send a ~360 degree step when the desired ray crosses pi.
    if (std::abs(angles.x() - yaw) > 180.0) {
        m_stable.invalidate(); setStatus(QStringLiteral("目标跨越云台水平边界；请调整机头方向后继续")); return;
    }
    const auto camera = position + m_orientation.rotatedVector(cameraOrigin(yaw, tilt));
    const auto toTarget = m_target - camera;
    m_distance = toTarget.length();
    matchUnfinishedView(camera);
    emit changed(); // Quantitative guidance follows pose, independently of status text changes.
    if (m_distance < 3.0) {
        m_stable.invalidate(); setStatus(QStringLiteral("距离目标不足 3 m：暂停锁定更新，请后退")); return;
    }
    const auto blocking = WorldMapCatalog::raycastBuilding(m_world, camera, toTarget, std::max(0.01, m_distance - 0.15));
    if (!blocking.value(QStringLiteral("available")).toBool() || blocking.value(QStringLiteral("hit")).toBool()) {
        m_stable.invalidate(); setStatus(QStringLiteral("目标被已知建筑遮挡：暂停锁定更新与采集")); return;
    }
    emit commandRequested(angles.x(), angles.y());
    const auto forward = m_orientation.rotatedVector(cameraDirection(yaw, tilt));
    const double error = qRadiansToDegrees(std::acos(std::clamp(double(QVector3D::dotProduct(forward, toTarget.normalized())), -1.0, 1.0)));
    m_alignmentError = error;
    m_aligned = error <= 1.5;
    if (!m_aligned) { m_stable.invalidate(); setStatus(QStringLiteral("正在对准目标")); return; }
    if (!m_stable.isValid()) m_stable.start();
    if (m_distance < 3.0 || m_distance > 40.0) setStatus(QStringLiteral("已锁定；采集距离需要 3—40 m"));
    else if (!viewMatches(camera)) setStatus(QStringLiteral("已锁定；当前角度不满足采集要求，请按下方参考移动"));
    else if (!m_canRecord) setStatus(QStringLiteral("已锁定（仅预览）；保存照片需在方案中选择预览并记录"));
    else if (m_views[m_selected].toMap().value(QStringLiteral("complete")).toBool()) setStatus(completedCount() == 4
        ? QStringLiteral("四个计划视角均已完成") : QStringLiteral("当前位置视角已完成；请绕目标移动，自动模式会匹配未完成视角"));
    else if (m_views[m_selected].toMap().value(QStringLiteral("saved")).toBool())
        setStatus(QStringLiteral("本视角已保存，质量需复核；可继续其他视角，清晰新画面会自动替换"));
    else setStatus(QStringLiteral("已锁定；等待稳定的新画面"));
}

bool GimbalInspectionController::viewMatches(const QVector3D &camera) const
{
    return viewMatches(camera, m_selected);
}
bool GimbalInspectionController::viewMatches(const QVector3D &camera, int index) const
{
    const auto observer = camera - m_target;
    const double offsets[4]{0, 35, -35, 0};
    return std::abs(wrapped(azimuth(observer) - m_referenceAzimuth - offsets[index])) <= 12.0
        && std::abs(elevation(observer) - m_referenceElevation - (index == 3 ? 25.0 : 0.0)) <= 10.0
        && QVector3D::dotProduct(observer.normalized(), m_normal) > 0.1f;
}
void GimbalInspectionController::matchUnfinishedView(const QVector3D &camera)
{
    if (!m_automatic || (camera - m_target).length() < 3 || (camera - m_target).length() > 40) return;
    for (int i = 0; i < 4; ++i) {
        if (!m_views[i].toMap().value(QStringLiteral("complete")).toBool() && viewMatches(camera, i)) {
            if (m_selected != i) {
                m_selected = i; m_stable.invalidate(); m_capture = false;
                diagnose(QStringLiteral("自动匹配未完成视角"));
            }
            return;
        }
    }
}
QString GimbalInspectionController::guidance() const
{
    if (m_diagnosticFailed) return QStringLiteral("诊断日志写入失败，请检查磁盘；本次原因记录可能不完整");
    if (!m_hasTarget) return {};
    if (m_historical) return m_views[m_selected].toMap().value(QStringLiteral("lastDiagnostic"),
        QStringLiteral("旧记录未保存采集阻碍，无法推断未完成原因")).toString();
    if (!m_ready) return QStringLiteral("等待新鲜的位置与云台反馈，暂不提供移动参考");
    const int reference = m_guide;
    const auto observer = m_position + m_orientation.rotatedVector(cameraOrigin(m_yaw, m_tilt)) - m_target;
    const double a = wrapped(azimuth(observer) - m_referenceAzimuth);
    const double e = elevation(observer) - m_referenceElevation;
    const double offsets[4]{0, 35, -35, 0};
    const double wanted = offsets[reference], high = reference == 3 ? 25 : 0;
    return QStringLiteral("%1参考：距离 %2 m（要求 3—40 m）；相对正面方位 %3°（要求 %4°至 %5°，正值为左斜视）；相对选点仰角 %6°（要求 %7°至 %8°）。%9")
        .arg(m_views[reference].toMap().value(QStringLiteral("label")).toString())
        .arg(observer.length(), 0, 'f', 1).arg(a, 0, 'f', 1).arg(wanted - 12).arg(wanted + 12)
        .arg(e, 0, 'f', 1).arg(high - 10).arg(high + 10)
        .arg(e > high + 10 ? QStringLiteral("视角过高：拉开水平距离或降低高度；请自行避障。")
            : e < high - 10 ? QStringLiteral("视角偏低：适当升高；请自行避障。")
            : QStringLiteral("绕目标调整观察方位；这不是自动规划的安全航线。"))
        + QStringLiteral(" 方位还差 %1°、仰角还差 %2°才进入范围。")
            .arg(std::max(0.0, std::abs(wrapped(a-wanted))-12), 0, 'f', 1)
            .arg(std::max(0.0, std::abs(e-high)-10), 0, 'f', 1)
        + (m_active && m_frameBlockClock.isValid() && m_frameBlockClock.elapsed() < 2000
            ? QStringLiteral(" 采集提示：") + m_frameBlock : QString());
}

QVariantMap GimbalInspectionController::observation() const
{
    if (!m_hasTarget || m_historical || !m_ready) return {{"available", false}};
    const auto camera = m_position + m_orientation.rotatedVector(cameraOrigin(m_yaw, m_tilt));
    const auto observer = camera - m_target;
    const double angle = qDegreesToRadians(wrapped(azimuth(observer) - m_referenceAzimuth));
    const double radius = std::hypot(double(observer.x()), double(observer.z()));
    const double offsets[4]{0,35,-35,0};
    const double a = qDegreesToRadians(m_referenceAzimuth + offsets[m_guide]);
    const double e = qDegreesToRadians(m_referenceElevation + (m_guide == 3 ? 25 : 0));
    // Reference only, not a flight path or a collision-free waypoint.
    const QVector3D suggested = m_target + QVector3D(float(20*std::cos(e)*std::sin(a)),
        float(20*std::sin(e)), float(-20*std::cos(e)*std::cos(a)));
    return {{"available", true}, {"x", -radius*std::sin(angle)}, {"y", radius*std::cos(angle)},
        {"deltaEast", suggested.x()-camera.x()}, {"deltaNorth", camera.z()-suggested.z()},
        {"deltaUp", suggested.y()-camera.y()}, {"altitude", suggested.y()},
        {"alignmentError", m_alignmentError}};
}

void GimbalInspectionController::capture() { if (!m_historical) { m_capture = true; diagnose(QStringLiteral("用户请求采集")); emit changed(); } }
void GimbalInspectionController::retake()
{
    if (m_historical) return;
    m_selected = m_guide;
    auto view = m_views[m_selected].toMap(); view[QStringLiteral("complete")] = false;
    view[QStringLiteral("retakePending")] = true;
    m_views[m_selected] = view; m_stable.invalidate();
    if (m_canRecord) save(); emit changed();
}

void GimbalInspectionController::ingestFrame(const QByteArray &jpeg, quint64 sequence, qint64 epochMs,
    const QVector3D &position, const QQuaternion &orientation, bool validPose)
{
    if (!sequence || sequence == m_sequence) return;
    m_sequence = sequence;
    if (!m_active) return;
    if (!m_ready || !validPose || !m_poseClock.isValid() || m_poseClock.elapsed() > 250) {
        diagnose(QStringLiteral("候选帧缺少新鲜有效位姿")); return;
    }
    if (!m_canRecord) { diagnose(QStringLiteral("仅预览，未启用照片记录")); return; }
    if (!m_aligned || !m_stable.isValid() || m_stable.elapsed() < 1000) {
        diagnose(QStringLiteral("候选帧未满足对准并稳定一秒")); return;
    }
    if (!m_automatic && !m_capture) { diagnose(QStringLiteral("等待手动采集")); return; }
    if (jpeg.isEmpty() || std::abs(QDateTime::currentMSecsSinceEpoch() - epochMs) > 400) {
        diagnose(QStringLiteral("候选画面为空或超过400毫秒")); return;
    }
    if (m_candidate.isValid() && m_candidate.elapsed() < 1000) return;
    auto view = m_views[m_selected].toMap();
    if (view.value(QStringLiteral("complete")).toBool()) return;
    const auto camera = position + orientation.rotatedVector(cameraOrigin(m_yaw, m_tilt));
    const auto toTarget = m_target - camera;
    if (toTarget.length() < 3.0f || toTarget.length() > 40.0f || !viewMatches(camera)) {
        diagnose(QStringLiteral("候选帧位置不满足当前视角距离或角度")); return;
    }
    const auto forward = orientation.rotatedVector(cameraDirection(m_yaw, m_tilt));
    if (QVector3D::dotProduct(forward, toTarget.normalized()) < std::cos(qDegreesToRadians(1.5))) {
        diagnose(QStringLiteral("候选帧中心指向误差超过1.5度")); return;
    }
    const auto hit = WorldMapCatalog::raycastBuilding(m_world, camera, toTarget, double(toTarget.length()) - 0.15);
    if (!hit.value(QStringLiteral("available")).toBool() || hit.value(QStringLiteral("hit")).toBool()) {
        diagnose(QStringLiteral("候选帧目标被遮挡或缺少建筑几何")); return;
    }
    m_candidate.restart();
    const QImage image = QImage::fromData(jpeg, "JPEG");
    if (image.isNull()) { setStatus(QStringLiteral("无法解码候选画面，不计入完成")); return; }
    // Inspect the central target patch, not unrelated sharp background features.
    const QImage patch = image.copy(image.width() / 3, image.height() / 3, image.width() / 3, image.height() / 3);
    QByteArray patchBytes; QBuffer buffer(&patchBytes); buffer.open(QIODevice::WriteOnly); patch.save(&buffer, "JPEG");
    const auto quality = VisualSurveyController::inspectFrameQuality(patchBytes);
    const bool accepted = quality.value(QStringLiteral("accepted")).toBool();
    // Flat grey walls are valid evidence but cannot establish image quality.
    // Keep their review photo separately from a qualified completion. Still
    // reject black/white unusable exposures rather than pretending they help.
    const double brightness = quality.value(QStringLiteral("brightness")).toDouble();
    if (brightness < 10 || brightness > 250) {
        setStatus(QStringLiteral("目标区域需复核：严重欠曝或过曝，未保存；请调整视角")); return;
    }
    if (!accepted && view.value(QStringLiteral("saved")).toBool()
        && !view.value(QStringLiteral("retakePending")).toBool()) {
        setStatus(QStringLiteral("已保留需复核照片：%1；可继续其他视角").arg(quality.value(QStringLiteral("reason")).toString())); return;
    }
    const QString directory = QDir(m_folder).filePath(QStringLiteral("sensor/gimbal-inspection"));
    QStorageInfo disk(m_folder); disk.refresh();
    if (!disk.isValid() || disk.bytesAvailable() < 256 * 1024 * 1024 || m_bytes + jpeg.size() > 100 * 1024 * 1024) {
        pause(); setStatus(QStringLiteral("存储空间不足或巡检达到 100 MB 上限；已停止采集")); return;
    }
    if (!QDir().mkpath(directory)) { pause(); setStatus(QStringLiteral("无法创建巡检目录")); return; }
    const QString file = QStringLiteral("view-%1-%2.jpg").arg(m_selected).arg(sequence);
    QSaveFile output(QDir(directory).filePath(file));
    if (!output.open(QIODevice::WriteOnly) || output.write(jpeg) != jpeg.size() || !output.commit()) {
        pause(); setStatus(QStringLiteral("照片写入失败；本视角未完成")); return;
    }
    const auto previous = view;
    view[QStringLiteral("image")] = file;
    view[QStringLiteral("saved")] = true;
    view[QStringLiteral("complete")] = accepted;
    view[QStringLiteral("retakePending")] = false;
    view[QStringLiteral("elapsedMs")] = std::max(qint64(0), epochMs - m_epochMs);
    view[QStringLiteral("cameraPosition")] = vectorJson(camera);
    view[QStringLiteral("yawDegrees")] = m_yaw; view[QStringLiteral("pitchDegrees")] = m_tilt;
    view[QStringLiteral("distanceM")] = toTarget.length(); view[QStringLiteral("quality")] = quality;
    view[QStringLiteral("qualityStatus")] = accepted ? QStringLiteral("qualified") : QStringLiteral("review");
    view[QStringLiteral("lastDiagnostic")] = QStringLiteral("照片已成功采集");
    m_views[m_selected] = view; m_bytes += jpeg.size();
    if (!save()) { m_views[m_selected] = previous; emit changed(); return; }
    m_capture = false;
    m_frameBlock.clear(); m_frameBlockClock.invalidate();
    if (m_automatic && m_guide == m_selected) {
        for (int i=0; i<4; ++i) {
            if (!m_views[i].toMap().value(QStringLiteral("saved")).toBool()) { m_guide=i; break; }
        }
    }
    setStatus(QStringLiteral("已保存%1（%2）；已拍 %3 / 4，质量合格 %4 / 4")
        .arg(view.value(QStringLiteral("label")).toString(), accepted ? QStringLiteral("质量合格") : QStringLiteral("需复核"))
        .arg(savedCount()).arg(completedCount()));
    emit changed();
}

QString GimbalInspectionController::imagePath(const QVariantMap &view) const
{
    const QString file = view.value(QStringLiteral("image")).toString();
    if (file.isEmpty() || QFileInfo(file).fileName() != file || file.contains('/') || file.contains('\\')) return {};
    const QString path = QDir(m_folder).filePath(QStringLiteral("sensor/gimbal-inspection/") + file);
    const QString canonical = QFileInfo(path).canonicalFilePath();
    const QString root = QFileInfo(QDir(m_folder).filePath(QStringLiteral("sensor/gimbal-inspection"))).canonicalFilePath();
    return !root.isEmpty() && canonical.startsWith(root + '/', Qt::CaseInsensitive) ? canonical : QString();
}
QVariantList GimbalInspectionController::views() const
{
    QVariantList result;
    for (const auto &entry : m_views) {
        auto view = entry.toMap();
        const QString path = imagePath(view);
        view[QStringLiteral("source")] = path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
        view[QStringLiteral("availableAtTime")] = !path.isEmpty() && (m_replayMs < 0 || view.value(QStringLiteral("elapsedMs")).toLongLong() <= m_replayMs);
        result.append(view);
    }
    return result;
}
int GimbalInspectionController::completedCount() const
{
    int count = 0; for (const auto &v : m_views) if (v.toMap().value(QStringLiteral("complete")).toBool()) ++count;
    return count;
}
int GimbalInspectionController::savedCount() const
{
    int count = 0;
    for (const auto &v : m_views) {
        const auto view = v.toMap();
        if (view.value(QStringLiteral("saved"), view.value(QStringLiteral("complete"))).toBool()) ++count;
    }
    return count;
}
QVariantMap GimbalInspectionController::summary() const
{
    return {{QStringLiteral("formatVersion"), 1}, {QStringLiteral("available"), m_hasTarget},
        {QStringLiteral("name"), m_name}, {QStringLiteral("worldId"), m_world},
        {QStringLiteral("target"), vectorJson(m_target)}, {QStringLiteral("normal"), vectorJson(m_normal)},
        {QStringLiteral("referenceAzimuth"), m_referenceAzimuth}, {QStringLiteral("referenceElevation"), m_referenceElevation},
        {QStringLiteral("definition"), QStringLiteral("planned-view-completion-not-surface-coverage")},
        {QStringLiteral("completedCount"), completedCount()}, {QStringLiteral("plannedCount"), 4},
        {QStringLiteral("savedCount"), savedCount()}, {QStringLiteral("qualityQualifiedCount"), completedCount()},
        {QStringLiteral("automatic"), m_automatic}, {QStringLiteral("selectedView"), m_selected},
        {QStringLiteral("diagnosticsFile"), QStringLiteral("diagnostics.jsonl")},
        {QStringLiteral("diagnosticsWriteFailed"), m_diagnosticFailed},
        {QStringLiteral("diagnosticsTruncated"), m_diagnosticBytes >= 4 * 1024 * 1024},
        {QStringLiteral("sizeBytes"), m_bytes + m_diagnosticBytes}, {QStringLiteral("views"), m_views}};
}
bool GimbalInspectionController::save()
{
    if (!m_canRecord || m_historical || m_folder.isEmpty()) return false;
    const QString folder = QDir(m_folder).filePath(QStringLiteral("sensor/gimbal-inspection"));
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(summary())).toJson();
    QSaveFile file(QDir(folder).filePath(QStringLiteral("manifest.json")));
    if (!QDir().mkpath(folder) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        pause(); setStatus(QStringLiteral("巡检清单保存失败，请检查磁盘；未计入新的完成项")); return false;
    }
    return true;
}
QVariantMap GimbalInspectionController::finishSession()
{
    pause(); auto result = summary();
    if (!m_historical && m_hasTarget && m_canRecord && !save()) result[QStringLiteral("writeFailed")] = true;
    return result;
}
bool GimbalInspectionController::loadSession(const QString &folder)
{
    clear(); m_historical = true; m_folder = folder;
    QFile file(QDir(folder).filePath(QStringLiteral("sensor/gimbal-inspection/manifest.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) { emit changed(); return false; }
    const auto data = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    if (data.value(QStringLiteral("formatVersion")).toInt() != 1 || data.value(QStringLiteral("views")).toList().size() != 4) return false;
    m_target = fromJson(data.value(QStringLiteral("target"))); m_normal = fromJson(data.value(QStringLiteral("normal")));
    m_name = data.value(QStringLiteral("name")).toString(); m_world = data.value(QStringLiteral("worldId")).toString();
    m_referenceAzimuth = data.value(QStringLiteral("referenceAzimuth")).toDouble();
    m_referenceElevation = data.value(QStringLiteral("referenceElevation")).toDouble();
    m_views = data.value(QStringLiteral("views")).toList();
    for (auto &entry : m_views) {
        auto view = entry.toMap();
        view[QStringLiteral("saved")] = !imagePath(view).isEmpty();
        if (!view.value(QStringLiteral("saved")).toBool()) view[QStringLiteral("complete")] = false;
        entry = view;
    }
    m_hasTarget = data.value(QStringLiteral("available")).toBool();
    setStatus(QStringLiteral("历史巡检：已拍 %1 / 4，质量合格 %2 / 4；点击照片定位回放")
        .arg(savedCount()).arg(completedCount())); emit changed(); return true;
}
void GimbalInspectionController::seek(qint64 elapsedMs) { if (m_historical) { m_replayMs = elapsedMs; emit changed(); } }
