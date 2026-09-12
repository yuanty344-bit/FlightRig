#include "sessions/testreportgenerator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointF>
#include <QSaveFile>
#include <QTextStream>
#include <QVariantList>

#include <algorithm>
#include <cmath>

namespace {
QString h(const QString &value)
{
    return value.toHtmlEscaped();
}

QString h(const QVariant &value)
{
    return h(value.toString());
}

QString resultColor(const QString &result)
{
    if (result == QStringLiteral("passed")) return QStringLiteral("#16836d");
    if (result == QStringLiteral("warning") || result == QStringLiteral("review"))
        return QStringLiteral("#b86812");
    if (result == QStringLiteral("failed") || result == QStringLiteral("incomplete")
        || result == QStringLiteral("aborted"))
        return QStringLiteral("#c44242");
    return QStringLiteral("#5c6876");
}

QString firstSensorPreviewDataUrl(const QVariantMap &session)
{
    const QString folder = session.value(QStringLiteral("folderPath")).toString();
    if (folder.isEmpty()) return {};
    const QDir sensor(QDir(folder).filePath(QStringLiteral("sensor")));
    for (const QString &stream : {QStringLiteral("rgb"), QStringLiteral("depth")}) {
        const QDir directory(sensor.filePath(stream));
        const QStringList frames = directory.entryList(
            {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")},
            QDir::Files, QDir::Name);
        if (frames.isEmpty()) continue;
        QFile file(directory.filePath(frames.constFirst()));
        if (file.open(QIODevice::ReadOnly))
            return QStringLiteral("data:image/jpeg;base64,%1")
                .arg(QString::fromLatin1(file.readAll().toBase64()));
    }
    QFile map(sensor.filePath(QStringLiteral("lidar-map.png")));
    if (map.open(QIODevice::ReadOnly))
        return QStringLiteral("data:image/png;base64,%1")
            .arg(QString::fromLatin1(map.readAll().toBase64()));
    return {};
}

QString textOr(const QVariantMap &session, const QString &key,
               const QString &fallback = QStringLiteral("未记录"))
{
    const QString value = session.value(key).toString().trimmed();
    return h(value.isEmpty() ? fallback : value);
}

struct ChartSample
{
    double time{0.0};
    double altitude{0.0};
    double speed{0.0};
    double east{0.0};
    double north{0.0};
    bool positionAvailable{false};
};

QVector<ChartSample> readSamples(const QString &csvPath)
{
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream stream(&file);
    const QStringList header = stream.readLine().split(QLatin1Char(','));
    auto column = [&header](const QString &name) { return header.indexOf(name); };
    const int timeColumn = column(QStringLiteral("elapsed_ms"));
    const int altitudeColumn = column(QStringLiteral("altitude_m"));
    const int speedColumn = column(QStringLiteral("horizontal_speed_m_s"));
    const int positionColumn = column(QStringLiteral("local_position_available"));
    const int eastColumn = column(QStringLiteral("scene_x_east_m"));
    const int southColumn = column(QStringLiteral("scene_z_south_m"));
    if (timeColumn < 0 || altitudeColumn < 0 || speedColumn < 0) return {};

    QVector<ChartSample> samples;
    while (!stream.atEnd()) {
        const QStringList fields = stream.readLine().split(QLatin1Char(','));
        if (fields.size() <= std::max({timeColumn, altitudeColumn, speedColumn})) continue;
        ChartSample sample;
        sample.time = fields.at(timeColumn).toDouble() / 1000.0;
        sample.altitude = fields.at(altitudeColumn).toDouble();
        sample.speed = fields.at(speedColumn).toDouble();
        if (positionColumn >= 0 && eastColumn >= 0 && southColumn >= 0
            && fields.size() > std::max({positionColumn, eastColumn, southColumn})) {
            sample.positionAvailable = fields.at(positionColumn) == QStringLiteral("1");
            sample.east = fields.at(eastColumn).toDouble();
            sample.north = -fields.at(southColumn).toDouble();
        }
        samples.append(sample);
    }
    return samples;
}

QVector<ChartSample> reducedSamples(const QVector<ChartSample> &samples,
                                    qsizetype maximumCount = 600)
{
    if (samples.size() <= maximumCount || maximumCount < 2) return samples;
    QVector<ChartSample> reduced;
    reduced.reserve(maximumCount);
    const double step = static_cast<double>(samples.size() - 1)
                        / static_cast<double>(maximumCount - 1);
    for (qsizetype index = 0; index < maximumCount; ++index) {
        const qsizetype sampleIndex = std::min(
            samples.size() - 1,
            static_cast<qsizetype>(std::round(index * step)));
        reduced.append(samples.at(sampleIndex));
    }
    return reduced;
}

double maximumValue(const QVector<ChartSample> &samples,
                    double (*value)(const ChartSample &))
{
    double maximum = 0.0;
    for (const ChartSample &sample : samples)
        maximum = std::max(maximum, std::max(0.0, value(sample)));
    return std::max(1.0, maximum);
}

QString polyline(const QVector<ChartSample> &samples,
                 double (*value)(const ChartSample &), double maximum,
                 int width, int height, int leftPadding, int rightPadding,
                 int topPadding, int bottomPadding)
{
    if (samples.size() < 2) return {};
    const double maximumTime = std::max(1.0, samples.constLast().time);
    QStringList points;
    for (const ChartSample &sample : samples) {
        const double x = leftPadding + sample.time / maximumTime
                                           * (width - leftPadding - rightPadding);
        const double y = height - bottomPadding
                         - std::max(0.0, value(sample)) / maximum
                               * (height - topPadding - bottomPadding);
        points.append(QStringLiteral("%1,%2").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
    }
    return points.join(QLatin1Char(' '));
}

QString telemetryChart(const QVector<ChartSample> &samples)
{
    if (samples.size() < 2) return QStringLiteral("<p class=muted>该会话缺少可绘制的遥测数据。</p>");
    const QVector<ChartSample> chartSamples = reducedSamples(samples);
    const auto altitudeValue = [](const ChartSample &s) { return s.altitude; };
    const auto speedValue = [](const ChartSample &s) { return s.speed; };
    const double maxAltitude = maximumValue(chartSamples, altitudeValue);
    const double maxSpeed = maximumValue(chartSamples, speedValue);
    const QString altitude = polyline(chartSamples, altitudeValue, maxAltitude,
                                      760, 230, 44, 44, 32, 30);
    const QString speed = polyline(chartSamples, speedValue, maxSpeed,
                                   760, 230, 44, 44, 32, 30);
    const QString duration = QString::number(chartSamples.constLast().time, 'f', 0);
    return QStringLiteral(
        "<svg viewBox='0 0 760 230' role='img' aria-label='高度和水平速度随时间变化图表'>"
        "<rect width='760' height='230' rx='10' fill='#f6f8fa'/>"
        "<path d='M44 200H716M44 144H716M44 88H716M44 32H716' stroke='#dce3ea' fill='none'/>"
        "<polyline points='%1' fill='none' stroke='#1769e0' stroke-width='2'/>"
        "<polyline points='%2' fill='none' stroke='#c85d00' stroke-width='2'/>"
        "<text x='48' y='22' font-size='12' fill='#1769e0'>高度（左轴，最高 %3 m）</text>"
        "<text x='238' y='22' font-size='12' fill='#c85d00'>水平速度（右轴，最高 %4 m/s）</text>"
        "<text x='8' y='36' font-size='11' fill='#6c7784'>%3</text>"
        "<text x='12' y='204' font-size='11' fill='#6c7784'>0</text>"
        "<text x='720' y='36' font-size='11' fill='#6c7784'>%4</text>"
        "<text x='724' y='204' font-size='11' fill='#6c7784'>0</text>"
        "<text x='44' y='220' font-size='11' fill='#6c7784'>0 s</text>"
        "<text x='680' y='220' font-size='11' fill='#6c7784'>%5 s</text></svg>")
        .arg(altitude, speed,
             QString::number(maxAltitude, 'f', 1),
             QString::number(maxSpeed, 'f', 1), duration);
}

QString trackChart(const QVector<ChartSample> &samples, const QVariantMap &session)
{
    QVector<ChartSample> positioned;
    const double missionStart = session.value(QStringLiteral("missionStartElapsedMs"), -1).toDouble() / 1000.0;
    const double missionEnd = session.value(QStringLiteral("missionEndElapsedMs"), -1).toDouble() / 1000.0;
    for (const ChartSample &sample : samples) {
        if (!sample.positionAvailable) continue;
        if (missionStart >= 0.0 && sample.time < missionStart) continue;
        if (missionStart >= 0.0 && missionEnd >= missionStart && sample.time > missionEnd) continue;
        positioned.append(sample);
    }
    positioned = reducedSamples(positioned);
    if (positioned.size() < 2) return QStringLiteral("<p class=muted>该会话缺少可绘制的实际轨迹。</p>");
    QVector<QPointF> planned;
    const QVariantList waypoints = session.value(QStringLiteral("missionWaypoints")).toList();
    if (!waypoints.isEmpty()) {
        planned.append(QPointF(session.value(QStringLiteral("missionStartEastMeters")).toDouble(),
                               -session.value(QStringLiteral("missionStartSouthMeters")).toDouble()));
        for (const QVariant &entry : waypoints) {
            const QVariantMap point = entry.toMap();
            planned.append(QPointF(point.value(QStringLiteral("eastMeters")).toDouble()
                                       + planned.constFirst().x(),
                                   point.value(QStringLiteral("northMeters")).toDouble()
                                       + planned.constFirst().y()));
        }
    }
    double minEast = positioned.first().east, maxEast = minEast;
    double minNorth = positioned.first().north, maxNorth = minNorth;
    for (const auto &sample : positioned) {
        minEast = std::min(minEast, sample.east); maxEast = std::max(maxEast, sample.east);
        minNorth = std::min(minNorth, sample.north); maxNorth = std::max(maxNorth, sample.north);
    }
    for (const QPointF &point : planned) {
        minEast = std::min(minEast, point.x()); maxEast = std::max(maxEast, point.x());
        minNorth = std::min(minNorth, point.y()); maxNorth = std::max(maxNorth, point.y());
    }
    const double spanEast = std::max(1.0, maxEast - minEast);
    const double spanNorth = std::max(1.0, maxNorth - minNorth);
    const double scale = std::min(672.0 / spanEast, 158.0 / spanNorth);
    const double drawnWidth = spanEast * scale;
    const double drawnHeight = spanNorth * scale;
    const double offsetX = 44.0 + (672.0 - drawnWidth) / 2.0;
    const double offsetY = 190.0 - (158.0 - drawnHeight) / 2.0;
    QStringList points;
    for (const auto &sample : positioned) {
        const double x = offsetX + (sample.east - minEast) * scale;
        const double y = offsetY - (sample.north - minNorth) * scale;
        points.append(QStringLiteral("%1,%2").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
    }
    QStringList plannedPoints;
    for (const QPointF &point : planned) {
        const double x = offsetX + (point.x() - minEast) * scale;
        const double y = offsetY - (point.y() - minNorth) * scale;
        plannedPoints.append(QStringLiteral("%1,%2").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1));
    }
    return QStringLiteral(
        "<svg viewBox='0 0 760 230' role='img' aria-label='计划航线与任务期间实际飞行轨迹'>"
        "<rect width='760' height='230' rx='10' fill='#f6f8fa'/>"
        "<path d='M44 190H716M44 137H716M44 84H716M44 32H716M44 32V190M212 32V190M380 32V190M548 32V190M716 32V190' stroke='#e4e9ef' fill='none'/>"
        "<polyline points='%1' fill='none' stroke='#1769e0' stroke-width='1.6' stroke-dasharray='6 5'/>"
        "<polyline points='%2' fill='none' stroke='#c85d00' stroke-width='2'/>"
        "<circle cx='%3' cy='%4' r='4' fill='#1769e0'/>"
        "<text x='48' y='22' font-size='12' fill='#1769e0'>计划航线</text>"
        "<text x='112' y='22' font-size='12' fill='#c85d00'>任务期间实际轨迹</text>"
        "<path d='M700 48L706 36L712 48Z' fill='#6c7784'/><text x='690' y='62' font-size='10' fill='#6c7784'>北</text>"
        "<text x='44' y='218' font-size='11' fill='#6c7784'>等比例显示 · 东向右 / 北向上</text></svg>")
        .arg(plannedPoints.join(QLatin1Char(' ')), points.join(QLatin1Char(' ')))
        .arg(points.first().section(QLatin1Char(','), 0, 0),
             points.first().section(QLatin1Char(','), 1, 1));
}
}

QString TestReportGenerator::generate(const QVariantMap &session,
                                      const QString &outputDirectory,
                                      QString *errorMessage)
{
    if (session.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("会话数据为空");
        return {};
    }
    if (!QDir().mkpath(outputDirectory)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建报告目录");
        return {};
    }
    const QString id = session.value(QStringLiteral("id"), QStringLiteral("session")).toString();
    const QString path = QDir(outputDirectory).filePath(id + QStringLiteral(".html"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = file.errorString();
        return {};
    }

    const QVector<ChartSample> samples = readSamples(session.value(QStringLiteral("csvPath")).toString());
    QString checksHtml;
    for (const QVariant &entry : session.value(QStringLiteral("acceptanceChecks")).toList()) {
        const QVariantMap item = entry.toMap();
        const QString status = item.value(QStringLiteral("status")).toString();
        const QString statusLabel = status == QStringLiteral("pass") ? QStringLiteral("通过")
                                    : status == QStringLiteral("warning") ? QStringLiteral("提醒")
                                                                          : QStringLiteral("未通过");
        checksHtml += QStringLiteral("<tr><td>%1</td><td><span class='pill %2'>%3</span></td><td>%4</td></tr>")
                          .arg(h(item.value(QStringLiteral("label"))), status,
                               statusLabel, h(item.value(QStringLiteral("detail"))));
    }
    if (checksHtml.isEmpty()) checksHtml = QStringLiteral("<tr><td colspan=3>没有验收明细</td></tr>");

    QString eventsHtml;
    for (const QVariant &event : session.value(QStringLiteral("events")).toList())
        eventsHtml += QStringLiteral("<li>%1</li>").arg(h(event));
    if (eventsHtml.isEmpty()) eventsHtml = QStringLiteral("<li>没有记录事件</li>");

    const QString result = session.value(QStringLiteral("testResult"), QStringLiteral("not-rated")).toString();
    const bool hasMission = !session.value(QStringLiteral("missionWaypoints")).toList().isEmpty()
                            || session.value(QStringLiteral("missionTotalItems")).toInt() > 0;
    const QVariantMap routeAnalysis = session.value(QStringLiteral("routeAnalysis")).toMap();
    const bool hasRouteAnalysis = routeAnalysis.value(QStringLiteral("available")).toBool();
    const QString deviationText = hasMission
        ? QString::number(hasRouteAnalysis
                              ? routeAnalysis.value(QStringLiteral("maxHorizontalDeviationMeters")).toDouble()
                              : session.value(QStringLiteral("missionMaxCrossTrackError")).toDouble(),
                          'f', 1)
              + QStringLiteral(" m")
        : QStringLiteral("不适用");
    const QString telemetryGapText = session.value(QStringLiteral("sampleCount")).toLongLong() > 0
        ? QString::number(session.value(QStringLiteral("maximumTelemetryGapSeconds")).toDouble(), 'f', 1)
              + QStringLiteral(" s")
        : QStringLiteral("无数据");
    const QString notes = session.value(QStringLiteral("notes")).toString().trimmed();
    const QString notesHtml = notes.isEmpty()
        ? QString()
        : QStringLiteral("<h2>会话备注</h2><div class='note'>%1</div>").arg(h(notes));
    const QString trackHtml = hasMission
        ? QStringLiteral("<h2>计划与实际飞行轨迹</h2>%1").arg(trackChart(samples, session))
        : QStringLiteral("<h2>实际飞行轨迹</h2>%1").arg(trackChart(samples, session));

    QString endAction = session.value(QStringLiteral("missionEndAction")).toString();
    if (endAction == QStringLiteral("rtl")) endAction = QStringLiteral("任务完成后返航");
    else if (endAction == QStringLiteral("land")) endAction = QStringLiteral("任务完成后降落");
    else if (endAction == QStringLiteral("hold")) endAction = QStringLiteral("任务完成后悬停");
    else if (endAction.isEmpty()) endAction = QStringLiteral("未记录");

    const auto row = [](const QString &label, const QString &escapedValue) {
        return QStringLiteral("<tr><th scope='row'>%1</th><td>%2</td></tr>")
            .arg(label.toHtmlEscaped(), escapedValue);
    };
    QString basicRows;
    basicRows += row(QStringLiteral("会话编号"), h(id));
    basicRows += row(QStringLiteral("会话状态"), textOr(session, QStringLiteral("status")));
    basicRows += row(QStringLiteral("测试方案"), textOr(session, QStringLiteral("testPlanName"), QStringLiteral("未使用方案")));
    basicRows += row(QStringLiteral("执行方式"), textOr(session, QStringLiteral("testPlanTypeLabel")));
    basicRows += row(QStringLiteral("验收规则"), textOr(session, QStringLiteral("acceptancePresetLabel")));
    const QVariantMap controlSettings = session.value(
        QStringLiteral("testPlanControlSettings")).toMap();
    if (!controlSettings.isEmpty()) {
        basicRows += row(QStringLiteral("键盘控制速度"), h(QStringLiteral("水平 %1 m/s · 垂直 %2 m/s · 转向 %3°/s")
            .arg(controlSettings.value(QStringLiteral("horizontalSpeedMetersPerSecond")).toDouble(), 0, 'f', 1)
            .arg(controlSettings.value(QStringLiteral("verticalSpeedMetersPerSecond")).toDouble(), 0, 'f', 1)
            .arg(controlSettings.value(QStringLiteral("yawRateDegreesPerSecond")).toDouble(), 0, 'f', 0)));
    }
    const QVariantMap acceptanceRules = session.value(
        QStringLiteral("testPlanAcceptanceRules")).toMap();
    if (!acceptanceRules.isEmpty()) {
        basicRows += row(QStringLiteral("验收阈值"), h(QStringLiteral("偏差 ≤ %1 m · 净空 ≥ %2 m · 遥测中断 ≤ %3 s · 高度 ≤ %4 m · 用时 ≤ %5 min")
            .arg(acceptanceRules.value(QStringLiteral("maxHorizontalDeviationMeters")).toDouble(), 0, 'f', 1)
            .arg(acceptanceRules.value(QStringLiteral("minimumObstacleClearanceMeters")).toDouble(), 0, 'f', 1)
            .arg(acceptanceRules.value(QStringLiteral("maximumTelemetryGapSeconds")).toDouble(), 0, 'f', 1)
            .arg(acceptanceRules.value(QStringLiteral("maximumAltitudeMeters")).toDouble(), 0, 'f', 0)
            .arg(acceptanceRules.value(QStringLiteral("maximumDurationSeconds")).toDouble() / 60.0, 0, 'f', 0)));
    }
    basicRows += row(QStringLiteral("开始时间"), textOr(session, QStringLiteral("startedAt")));
    basicRows += row(QStringLiteral("结束时间"), textOr(session, QStringLiteral("endedAt")));
    basicRows += row(QStringLiteral("结束原因"), textOr(session, QStringLiteral("endReason")));
    basicRows += row(QStringLiteral("健康检查"), textOr(session, QStringLiteral("healthResult")));
    basicRows += row(QStringLiteral("起飞结果"), textOr(session, QStringLiteral("takeoffResult")));
    basicRows += row(QStringLiteral("落地结果"), textOr(session, QStringLiteral("landingResult")));
    basicRows += row(QStringLiteral("目标起飞高度"),
                     session.contains(QStringLiteral("takeoffAltitude"))
                         ? QString::number(session.value(QStringLiteral("takeoffAltitude")).toDouble(), 'f', 1)
                               + QStringLiteral(" m")
                         : QStringLiteral("未记录"));

    QString routeMetricsHtml;
    if (hasMission) {
        const int completed = session.value(QStringLiteral("missionCompletedItems")).toInt();
        const int total = session.value(QStringLiteral("missionTotalItems")).toInt();
        QString routeRows;
        routeRows += row(QStringLiteral("航线"), textOr(session, QStringLiteral("missionRouteName"), QStringLiteral("未命名航线")));
        routeRows += row(QStringLiteral("航点完成"),
                         QStringLiteral("%1 / %2").arg(std::min(completed, total)).arg(total));
        routeRows += row(QStringLiteral("结束动作"), h(endAction));
        routeRows += row(QStringLiteral("最大水平偏差"), deviationText);
        if (hasRouteAnalysis) {
            routeRows += row(QStringLiteral("平均水平偏差"),
                             QString::number(routeAnalysis.value(QStringLiteral("meanHorizontalDeviationMeters")).toDouble(), 'f', 2)
                                 + QStringLiteral(" m"));
            routeRows += row(QStringLiteral("95% 水平偏差"),
                             QString::number(routeAnalysis.value(QStringLiteral("p95HorizontalDeviationMeters")).toDouble(), 'f', 2)
                                 + QStringLiteral(" m"));
            routeRows += row(QStringLiteral("容差内轨迹"),
                             QString::number(routeAnalysis.value(QStringLiteral("withinTolerancePercent")).toDouble(), 'f', 1)
                                 + QStringLiteral("%"));
            routeRows += row(QStringLiteral("实际飞行距离"),
                             QString::number(routeAnalysis.value(QStringLiteral("actualDistanceMeters")).toDouble(), 'f', 1)
                                 + QStringLiteral(" m"));
        }
        routeMetricsHtml = QStringLiteral("<h2>航线执行</h2><table>%1</table>").arg(routeRows);
    }

    QString environmentRows;
    environmentRows += row(QStringLiteral("机型"), textOr(session, QStringLiteral("modelName")));
    environmentRows += row(QStringLiteral("仿真世界"), textOr(session, QStringLiteral("worldName")));
    environmentRows += row(QStringLiteral("FlightBench"), textOr(session, QStringLiteral("applicationVersion")));
    environmentRows += row(QStringLiteral("Qt"), textOr(session, QStringLiteral("qtVersion")));
    environmentRows += row(QStringLiteral("MAVSDK"), textOr(session, QStringLiteral("mavsdkVersion")));
    environmentRows += row(QStringLiteral("PX4 镜像"), textOr(session, QStringLiteral("simulationImage")));

    QString sensorHtml;
    const QVariantMap sensorSettings = session.value(
        QStringLiteral("testPlanSensorSettings")).toMap();
    const QVariantMap sensor = session.value(QStringLiteral("sensorRecording")).toMap();
    if (!sensorSettings.isEmpty() || !sensor.isEmpty()) {
        // Report what actually happened. A user may manually enable ROS 2
        // preview even when the saved plan says "off".
        const QString usage = sensor.value(
            QStringLiteral("usage"), sensorSettings.value(QStringLiteral("usage"))).toString();
        const QString usageLabel = usage == QStringLiteral("record")
            ? QStringLiteral("预览并记录")
            : usage == QStringLiteral("preview") ? QStringLiteral("仅预览")
                                                   : QStringLiteral("不使用");
        const QString kind = sensor.value(QStringLiteral("kind")).toString();
        const QString kindLabel = kind == QStringLiteral("lidar-2d")
            ? QStringLiteral("二维激光雷达")
            : kind == QStringLiteral("depth-camera") ? QStringLiteral("RGB 与深度相机")
            : kind == QStringLiteral("gimbal-camera") ? QStringLiteral("云台相机")
            : kind == QStringLiteral("mono-camera") ? QStringLiteral("单目相机")
                                                      : QStringLiteral("未记录");
        QString sensorRows;
        sensorRows += row(QStringLiteral("使用方式"), h(usageLabel));
        sensorRows += row(QStringLiteral("传感器"), h(kindLabel));
        sensorRows += row(QStringLiteral("性能模式"), h(QStringLiteral("%1 · 实际目标 %2 FPS")
            .arg(sensor.value(QStringLiteral("performanceMode"), QStringLiteral("—")).toString())
            .arg(sensor.value(QStringLiteral("targetFramesPerSecond")).toInt())));
        sensorRows += row(QStringLiteral("数据状态"),
                          textOr(sensor, QStringLiteral("statusLabel"), QStringLiteral("未记录")));
        sensorRows += row(QStringLiteral("数据可用率"),
                          QString::number(sensor.value(QStringLiteral("dataAvailabilityPercent")).toDouble(), 'f', 1)
                              + QStringLiteral("%"));
        sensorRows += row(QStringLiteral("平均频率"),
                          QString::number(sensor.value(QStringLiteral("averageFrameRateHz")).toDouble(), 'f', 1)
                              + QStringLiteral(" Hz"));
        sensorRows += row(QStringLiteral("最低频率"),
                          QString::number(sensor.value(QStringLiteral("minimumFrameRateHz")).toDouble(), 'f', 1)
                              + QStringLiteral(" Hz"));
        sensorRows += row(QStringLiteral("最长数据中断"),
                          QString::number(sensor.value(QStringLiteral("maximumDataGapSeconds")).toDouble(), 'f', 1)
                              + QStringLiteral(" s"));
        sensorRows += row(QStringLiteral("平均传输延迟（估计）"),
                          sensor.value(QStringLiteral("latencyMeasured")).toBool()
                              ? QString::number(sensor.value(QStringLiteral("averageLatencyMilliseconds")).toDouble(), 'f', 0)
                                    + QStringLiteral(" ms")
                              : QStringLiteral("未测量（未完成有效时钟校准）"));
        sensorRows += row(QStringLiteral("延迟口径"),
            QStringLiteral("单调时钟往返校准后的单向估计；从网关数据准备完成至应用接收，不含曝光、前置 ROS 处理和画面解码。仅统计有效校准样本。"));
        sensorRows += row(QStringLiteral("有效延迟样本"),
            QString::number(sensor.value(QStringLiteral("latencySampleCount")).toInt()));
        if (kind == QStringLiteral("lidar-2d") || kind == QStringLiteral("depth-camera")) {
            const double nearest = sensor.value(QStringLiteral("nearestObstacleMeters")).toDouble();
            const bool distanceMeasured = sensor.value(
                QStringLiteral("obstacleDistanceMeasured")).toBool();
            sensorRows += row(QStringLiteral("最近障碍"), distanceMeasured && nearest > 0.0
                ? QString::number(nearest, 'f', 2) + QStringLiteral(" m")
                : QStringLiteral("未测量"));
        }
        if (kind == QStringLiteral("depth-camera")) {
            sensorRows += row(QStringLiteral("深度有效区域"),
                              QString::number(sensor.value(QStringLiteral("depthValidRatio")).toDouble()
                                                  * 100.0, 'f', 1)
                                  + QStringLiteral("%"));
        }
        sensorRows += row(QStringLiteral("数据量"), h(QStringLiteral("彩色 %1 帧 · 深度 %2 帧 · 雷达 %3 帧 · 云台 %4 条")
            .arg(sensor.value(QStringLiteral("rgbFrameCount")).toInt())
            .arg(sensor.value(QStringLiteral("depthFrameCount")).toInt())
            .arg(sensor.value(QStringLiteral("lidarScanCount")).toInt())
            .arg(sensor.value(QStringLiteral("gimbalSampleCount")).toInt())));
        const int staleFrames = sensor.value(QStringLiteral("staleFrameDiscardCount")).toInt();
        if (staleFrames > 0) {
            sensorRows += row(QStringLiteral("实时追帧保护"),
                              QStringLiteral("已放弃 %1 个过期排队帧，保留各数据通道的最新帧")
                                  .arg(staleFrames));
        }
        if (sensor.value(QStringLiteral("recorded")).toBool()) {
            sensorRows += row(QStringLiteral("保存位置"),
                              textOr(sensor, QStringLiteral("relativeDirectory"),
                                     QStringLiteral("sensor")));
        }
        if (sensor.value(QStringLiteral("storageLimitReached")).toBool()) {
            sensorRows += row(QStringLiteral("存储保护"),
                              QStringLiteral("已达到本次安全容量或磁盘余量下限，后续数据仅实时预览"));
        }
        if (sensor.value(QStringLiteral("writeFailed")).toBool()
            || sensor.value(QStringLiteral("manifestWriteFailed")).toBool()) {
            sensorRows += row(QStringLiteral("记录完整性"),
                              QStringLiteral("写入失败，当前记录不完整"));
        }
        if (kind == QStringLiteral("lidar-2d")) {
            const QVariantMap lidarMap = sensor.value(QStringLiteral("lidarMap")).toMap();
            if (sensor.value(QStringLiteral("lidarMapAvailable")).toBool()) {
                sensorRows += row(QStringLiteral("二维 SLAM 地图"),
                    QStringLiteral("已生成 · %1 m² · %2 × %3 m · 分辨率 %4 m")
                        .arg(lidarMap.value(QStringLiteral("exploredAreaSquareMeters")).toDouble(), 0, 'f', 1)
                        .arg(lidarMap.value(QStringLiteral("widthMeters")).toDouble(), 0, 'f', 1)
                        .arg(lidarMap.value(QStringLiteral("heightMeters")).toDouble(), 0, 'f', 1)
                        .arg(lidarMap.value(QStringLiteral("resolutionMeters")).toDouble(), 0, 'f', 2));
                sensorRows += row(QStringLiteral("SLAM 定位"),
                        QStringLiteral("%1 个关键帧 · %2 次扫描匹配 · 置信度 %3% · %4 次回环修正")
                            .arg(lidarMap.value(QStringLiteral("keyframeCount")).toInt())
                            .arg(lidarMap.value(QStringLiteral("scanMatchedCount")).toInt())
                            .arg(static_cast<int>(std::lround(
                                lidarMap.value(QStringLiteral("localizationConfidence")).toDouble()
                                * 100.0)))
                            .arg(lidarMap.value(QStringLiteral("loopClosureCount")).toInt()));
                sensorRows += row(QStringLiteral("位姿图优化"),
                            QStringLiteral("%1 条约束 · %2 次全图优化 · 扫描与飞控位姿已按时间对齐")
                                .arg(lidarMap.value(QStringLiteral("poseGraphConstraintCount")).toInt())
                                .arg(lidarMap.value(QStringLiteral("optimizationCount")).toInt()));
                sensorRows += row(QStringLiteral("累计位姿修正"),
                        QStringLiteral("东 %1 m · 北 %2 m · 航向 %3°")
                            .arg(lidarMap.value(QStringLiteral("correctionEastMeters")).toDouble(), 0, 'f', 2)
                            .arg(lidarMap.value(QStringLiteral("correctionNorthMeters")).toDouble(), 0, 'f', 2)
                            .arg(lidarMap.value(QStringLiteral("correctionHeadingDegrees")).toDouble(), 0, 'f', 1));
                sensorRows += row(QStringLiteral("地图文件"),
                    QStringLiteral("地图 PNG/PGM/YAML/JSON + lidar-pose-graph.json"));
            } else {
                sensorRows += row(QStringLiteral("二维 SLAM 地图"),
                                  QStringLiteral("未生成；需要有效本地位姿并飞行扫描"));
            }
        }
        if (kind == QStringLiteral("mono-camera")) {
            const QVariantMap survey = sensor.value(QStringLiteral("visualSurvey")).toMap();
            if (sensor.value(QStringLiteral("visualSurveyAvailable")).toBool()) {
                const QVariantList frames = survey.value(QStringLiteral("keyframes")).toList();
                const int visitedAreaCount = survey.value(QStringLiteral("visitedAreaCount")).toInt();
                sensorRows += row(QStringLiteral("视觉巡检影像地图"),
                    QStringLiteral("已生成 · %1 张关键帧 · %2 张清晰 · %3 张需复核")
                        .arg(survey.value(QStringLiteral("keyframeCount")).toInt())
                        .arg(survey.value(QStringLiteral("goodKeyframeCount")).toInt())
                        .arg(survey.value(QStringLiteral("warningKeyframeCount")).toInt()));
                sensorRows += row(QStringLiteral("自动质量筛选"),
                    QStringLiteral("采集覆盖 %1% · 清晰帧 %2% · 覆盖 %3 个区域格 · 跳过 %4 张（重复 %5 张）")
                        .arg(survey.value(QStringLiteral("spatialCoveragePercent")).toDouble(), 0, 'f', 0)
                        .arg(survey.value(QStringLiteral("qualityPassPercent")).toDouble(), 0, 'f', 0)
                        .arg(visitedAreaCount)
                        .arg(survey.value(QStringLiteral("rejectedCount")).toInt())
                        .arg(survey.value(QStringLiteral("duplicateCount")).toInt()));
                int anomalyCount = 0;
                QStringList surveyNotes;
                for (const QVariant &entry : frames) {
                    const QVariantMap frame = entry.toMap();
                    if (frame.value(QStringLiteral("anomaly")).toBool()) ++anomalyCount;
                    const QString note = frame.value(QStringLiteral("notes")).toString().trimmed();
                    if (!note.isEmpty() && surveyNotes.size() < 5)
                        surveyNotes.append(QStringLiteral("#%1 %2：%3")
                            .arg(frame.value(QStringLiteral("index")).toInt() + 1)
                            .arg(frame.value(QStringLiteral("areaLabel")).toString())
                            .arg(note));
                }
                sensorRows += row(QStringLiteral("人工复核"),
                    QStringLiteral("%1 张已标记异常%2")
                        .arg(anomalyCount)
                        .arg(surveyNotes.isEmpty() ? QString() : QStringLiteral(" · %1").arg(h(surveyNotes.join(QStringLiteral("；"))))));
                sensorRows += row(QStringLiteral("影像地图文件"),
                    QStringLiteral("sensor/visual-survey/visual-survey.json 与 images 原始关键帧"));
            } else {
                sensorRows += row(QStringLiteral("视觉巡检影像地图"),
                    QStringLiteral("未生成；需选择“预览并记录”并在飞行页开始视觉巡检"));
            }
        }
        if (kind == QStringLiteral("depth-camera")) {
            const QVariantMap depthMap = sensor.value(QStringLiteral("depthObstacleMap")).toMap();
            if (sensor.value(QStringLiteral("depthMapAvailable")).toBool()) {
                sensorRows += row(QStringLiteral("三维障碍地图"),
                    QStringLiteral("DepthFusion · %1 个稳定体素 · %2 × %3 × %4 m · 分辨率 %5 m")
                        .arg(depthMap.value(QStringLiteral("confirmedVoxelCount")).toInt())
                        .arg(depthMap.value(QStringLiteral("widthMeters")).toDouble(), 0, 'f', 1)
                        .arg(depthMap.value(QStringLiteral("depthMeters")).toDouble(), 0, 'f', 1)
                        .arg(depthMap.value(QStringLiteral("heightMeters")).toDouble(), 0, 'f', 1)
                        .arg(depthMap.value(QStringLiteral("resolutionMeters")).toDouble(), 0, 'f', 2));
                const double corridor = depthMap.value(
                    QStringLiteral("minimumObservedCorridorMeters")).toDouble();
                sensorRows += row(QStringLiteral("前向安全走廊"),
                    corridor > 0.0
                        ? QStringLiteral("整场最小净空 %1 m · 结束时当前净空 %2 m")
                              .arg(corridor, 0, 'f', 2)
                              .arg(depthMap.value(QStringLiteral("currentCorridorClearanceMeters")).toDouble(), 0, 'f', 2)
                        : QStringLiteral("量程内未发现走廊障碍"));
                sensorRows += row(QStringLiteral("融合质量"),
                    QStringLiteral("%1 帧 · 置信度 %2% · 稳定表面 %3% · 噪声 %4% · 射线清除 %5 个幽灵体素")
                        .arg(depthMap.value(QStringLiteral("processedFrameCount")).toInt())
                        .arg(depthMap.value(QStringLiteral("mapConfidencePercent")).toDouble(), 0, 'f', 0)
                        .arg(depthMap.value(QStringLiteral("coveragePercent")).toDouble(), 0, 'f', 0)
                        .arg(depthMap.value(QStringLiteral("noisePercent")).toDouble(), 0, 'f', 1)
                        .arg(depthMap.value(QStringLiteral("clearedVoxelCount")).toInt()));
                sensorRows += row(QStringLiteral("分块与地面"),
                        QStringLiteral("%1 个空间分块 · 地面坡度 %2° · %3 个三角面")
                            .arg(depthMap.value(QStringLiteral("chunkCount")).toInt())
                            .arg(depthMap.value(QStringLiteral("groundSlopeDegrees")).toDouble(), 0, 'f', 1)
                            .arg(depthMap.value(QStringLiteral("meshTriangleCount")).toInt()));
                sensorRows += row(QStringLiteral("地图文件"),
                    QStringLiteral("PLY 点云、CSV TSDF 表、OBJ 三角网格、俯视 PNG 与 JSON"));
                sensorRows += row(QStringLiteral("地图范围说明"),
                    QStringLiteral("前向深度相机局部表面；未观测区域保持未知，不代表 360° SLAM"));
            } else {
                sensorRows += row(QStringLiteral("三维障碍地图"),
                    QStringLiteral("未生成；需要预览并记录、有效本地位姿和连续深度画面"));
            }
        }
        const QString preview = firstSensorPreviewDataUrl(session);
        const QString previewHtml = preview.isEmpty() ? QString()
            : QStringLiteral("<img class='sensor-preview' alt='传感器画面缩略图' src='%1'>")
                  .arg(preview);
        QString inspectionHtml;
        const auto inspection = sensor.value(QStringLiteral("gimbalInspection")).toMap();
        if (kind == QStringLiteral("gimbal-camera") && inspection.value(QStringLiteral("available")).toBool()) {
            int verifiedCompleted = 0, verifiedSaved = 0;
            const QString inspectionHeading = QStringLiteral("<h3>云台目标锁定与多角度巡检：%1</h3>"
                "<p>已拍 %2 / 4，质量合格 %3 / 4。低纹理照片仅作为需复核证据，不计入质量合格。坐标锁定，不是视觉目标识别；不代表建筑表面覆盖率。"
                "遮挡检查仅使用已知建筑几何，不包含动态障碍。</p>");
            if (inspection.value(QStringLiteral("writeFailed")).toBool())
                inspectionHtml += QStringLiteral("<p>巡检清单写入失败，结果需复核。</p>");
            const QDir directory(QDir(session.value(QStringLiteral("folderPath")).toString())
                .filePath(QStringLiteral("sensor/gimbal-inspection")));
            const auto views = inspection.value(QStringLiteral("views")).toList();
            for (qsizetype i = 0; i < std::min<qsizetype>(4, views.size()); ++i) {
                const auto view = views[i].toMap();
                const auto name = view.value(QStringLiteral("image")).toString();
                const QString photoPath = QFileInfo(directory.filePath(name)).canonicalFilePath();
                const QString root = QFileInfo(directory.path()).canonicalFilePath();
                QFile image(photoPath);
                const bool readable = !name.isEmpty() && QFileInfo(name).fileName() == name
                    && !root.isEmpty() && photoPath.startsWith(root + '/', Qt::CaseInsensitive)
                    && image.size() > 0 && image.size() <= 8 * 1024 * 1024 && image.open(QIODevice::ReadOnly);
                if (readable && view.value(QStringLiteral("complete")).toBool()) ++verifiedCompleted;
                if (readable) ++verifiedSaved;
                inspectionHtml += QStringLiteral("<h4>%1 · %2</h4>")
                    .arg(h(view.value(QStringLiteral("label"))),
                         readable && view.value(QStringLiteral("complete")).toBool()
                             ? QStringLiteral("已拍 · 质量合格") : readable ? QStringLiteral("已拍 · 需复核") : QStringLiteral("待采集或文件缺失"));
                const auto quality = view.value(QStringLiteral("quality")).toMap();
                inspectionHtml += QStringLiteral("<p>%1</p>").arg(h(readable ? quality.value(QStringLiteral("reason"))
                    : view.value(QStringLiteral("lastDiagnostic"), QStringLiteral("未记录原因"))));
                if (readable) {
                    inspectionHtml += QStringLiteral("<p>会话第 %1 秒 · 距离 %2 m · 实际云台水平 %3° / 俯仰 %4°</p>"
                        "<img class='sensor-preview' alt='巡检观察方向照片' src='data:image/jpeg;base64,%5'>")
                        .arg(view.value(QStringLiteral("elapsedMs")).toLongLong() / 1000.0, 0, 'f', 1)
                        .arg(view.value(QStringLiteral("distanceM")).toDouble(), 0, 'f', 1)
                        .arg(view.value(QStringLiteral("yawDegrees")).toDouble(), 0, 'f', 1)
                        .arg(view.value(QStringLiteral("pitchDegrees")).toDouble(), 0, 'f', 1)
                        .arg(QString::fromLatin1(image.readAll().toBase64()));
                }
            }
            inspectionHtml += QStringLiteral("<p class='muted'>原始文件：sensor/gimbal-inspection/manifest.json 与同目录照片。</p>");
            inspectionHtml.prepend(inspectionHeading.arg(h(inspection.value(QStringLiteral("name"))),
                                                         QString::number(verifiedSaved), QString::number(verifiedCompleted)));
        }
        sensorHtml = QStringLiteral("<h2>传感器数据</h2><table>%1</table>%2%3")
                         .arg(sensorRows, previewHtml, inspectionHtml);
    }

    QString titleValue = session.value(QStringLiteral("displayName")).toString().trimmed();
    if (titleValue.isEmpty())
        titleValue = session.value(QStringLiteral("testPlanName")).toString().trimmed();
    if (titleValue.isEmpty())
        titleValue = session.value(QStringLiteral("missionRouteName")).toString().trimmed();
    if (titleValue.isEmpty()) {
        const QString modelName = session.value(QStringLiteral("modelName")).toString().trimmed();
        titleValue = modelName.isEmpty() ? QStringLiteral("飞行测试")
                                         : modelName + QStringLiteral(" 飞行测试");
    }
    const QString headerInfo = QStringLiteral("%1 · %2 · %3")
                                   .arg(textOr(session, QStringLiteral("startedLabel")),
                                        textOr(session, QStringLiteral("modelName")),
                                        textOr(session, QStringLiteral("worldName")));
    const QString html = QStringLiteral(R"HTML(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>FlightBench 测试报告</title>
<style>:root{color-scheme:light}*{box-sizing:border-box}body{font-family:"Microsoft YaHei UI","Microsoft YaHei","Segoe UI",sans-serif;color:#17202a;background:#eef2f6;margin:0}.page{max-width:920px;margin:28px auto;background:#fff;padding:38px;border-radius:14px;box-shadow:0 8px 30px #23314218}h1{font-size:28px;margin:0 0 6px;overflow-wrap:anywhere}h2{font-size:18px;margin:30px 0 12px}.muted{color:#6c7784}.result{border-left:5px solid %1;padding:15px 18px;background:#f6f8fa;border-radius:8px;margin:22px 0}.result p{margin:8px 0}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}.metric{padding:13px;background:#f6f8fa;border-radius:8px;min-width:0}.metric small{display:block;color:#6c7784}.metric b{display:block;margin-top:5px;overflow-wrap:anywhere}table{width:100%;border-collapse:collapse;table-layout:auto}th,td{text-align:left;padding:10px;border-bottom:1px solid #e1e6eb;font-size:14px;vertical-align:top;overflow-wrap:anywhere}th[scope=row]{width:172px;color:#6c7784;font-weight:500}.pill{display:inline-block;white-space:nowrap;padding:3px 8px;border-radius:12px;background:#eef2f6}.pill.pass{color:#16836d;background:#e8f5f1}.pill.warning{color:#b86812;background:#fbf0db}.pill.fail{color:#c44242;background:#fbe9ea}.note{white-space:pre-wrap;overflow-wrap:anywhere;padding:14px 16px;background:#f6f8fa;border-radius:8px;color:#495563}.sensor-preview{display:block;max-width:100%;max-height:360px;margin:14px auto;border:1px solid #e1e6eb;border-radius:10px}svg{display:block;width:100%;height:auto}li{margin:7px 0;font-size:13px;color:#495563;overflow-wrap:anywhere}a{color:#1769e0}@media(max-width:700px){.page{margin:0;padding:22px;border-radius:0}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}th[scope=row]{width:124px}}@media(max-width:430px){.grid{grid-template-columns:1fr}th,td{display:block;width:100%}th[scope=row]{padding-bottom:3px;border-bottom:0}}@media print{*{-webkit-print-color-adjust:exact;print-color-adjust:exact}body{background:#fff}.page{max-width:none;margin:0;padding:0;box-shadow:none}h2{break-after:avoid}table,svg,.result,.metric,.sensor-preview{break-inside:avoid}}</style></head>
<body><main class="page"><h1>%2</h1><p class="muted">%3</p>
<section class="result"><strong style="color:%1;font-size:20px">%4</strong><p>%5</p><small>%6 项通过 · %7 项提醒 · %8 项未通过</small></section>
<section class="grid"><div class="metric"><small>持续时间</small><b>%9</b></div><div class="metric"><small>最大高度</small><b>%10 m</b></div><div class="metric"><small>最大水平速度</small><b>%11 m/s</b></div><div class="metric"><small>最大垂直速度</small><b>%12 m/s</b></div><div class="metric"><small>最大航线偏差</small><b>%13</b></div><div class="metric"><small>最长遥测中断</small><b>%14</b></div></section>
<h2>测试信息</h2><table>%15</table>%16
<h2>验收结果</h2><table><thead><tr><th>检查项</th><th>结果</th><th>说明</th></tr></thead><tbody>%17</tbody></table>
<h2>高度与速度</h2>%18%19%20%21
<h2>软件与仿真环境</h2><table>%22</table>
<h2>事件记录</h2><ul>%23</ul><p class="muted">报告由 FlightBench 在本机生成；原始数据以 session.json、events.log 和 telemetry.csv 为准。城市世界地图数据：<a href="https://www.openstreetmap.org/copyright">© OpenStreetMap contributors</a>。</p></main></body></html>)HTML")
        .arg(resultColor(result), h(titleValue), headerInfo,
             textOr(session, QStringLiteral("testResultLabel"), QStringLiteral("未评估")),
             textOr(session, QStringLiteral("testResultReason"), QStringLiteral("没有结论说明")),
             QString::number(session.value(QStringLiteral("acceptancePassedCount")).toInt()),
             QString::number(session.value(QStringLiteral("acceptanceWarningCount")).toInt()),
             QString::number(session.value(QStringLiteral("acceptanceFailedCount")).toInt()),
             textOr(session, QStringLiteral("durationLabel"), QStringLiteral("00:00")),
             QString::number(session.value(QStringLiteral("maxAltitude")).toDouble(), 'f', 1),
             QString::number(session.value(QStringLiteral("maxHorizontalSpeed")).toDouble(), 'f', 1),
             QString::number(session.value(QStringLiteral("maxVerticalSpeed")).toDouble(), 'f', 1),
             deviationText, telemetryGapText, basicRows, notesHtml, checksHtml,
             telemetryChart(samples), trackHtml, routeMetricsHtml, sensorHtml,
             environmentRows, eventsHtml);
    const QByteArray encoded = html.toUtf8();
    if (file.write(encoded) != encoded.size()) {
        if (errorMessage) *errorMessage = file.errorString();
        file.cancelWriting();
        return {};
    }
    if (!file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return {};
    }
    return QDir::toNativeSeparators(path);
}
