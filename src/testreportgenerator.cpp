#include "testreportgenerator.h"

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
    if (checksHtml.isEmpty()) checksHtml = QStringLiteral("<tr><td colspan=3>旧会话没有验收明细</td></tr>");

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
<style>:root{color-scheme:light}*{box-sizing:border-box}body{font-family:"Microsoft YaHei UI","Microsoft YaHei","Segoe UI",sans-serif;color:#17202a;background:#eef2f6;margin:0}.page{max-width:920px;margin:28px auto;background:#fff;padding:38px;border-radius:14px;box-shadow:0 8px 30px #23314218}h1{font-size:28px;margin:0 0 6px;overflow-wrap:anywhere}h2{font-size:18px;margin:30px 0 12px}.muted{color:#6c7784}.result{border-left:5px solid %1;padding:15px 18px;background:#f6f8fa;border-radius:8px;margin:22px 0}.result p{margin:8px 0}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}.metric{padding:13px;background:#f6f8fa;border-radius:8px;min-width:0}.metric small{display:block;color:#6c7784}.metric b{display:block;margin-top:5px;overflow-wrap:anywhere}table{width:100%;border-collapse:collapse;table-layout:auto}th,td{text-align:left;padding:10px;border-bottom:1px solid #e1e6eb;font-size:14px;vertical-align:top;overflow-wrap:anywhere}th[scope=row]{width:172px;color:#6c7784;font-weight:500}.pill{display:inline-block;white-space:nowrap;padding:3px 8px;border-radius:12px;background:#eef2f6}.pill.pass{color:#16836d;background:#e8f5f1}.pill.warning{color:#b86812;background:#fbf0db}.pill.fail{color:#c44242;background:#fbe9ea}.note{white-space:pre-wrap;overflow-wrap:anywhere;padding:14px 16px;background:#f6f8fa;border-radius:8px;color:#495563}svg{display:block;width:100%;height:auto}li{margin:7px 0;font-size:13px;color:#495563;overflow-wrap:anywhere}a{color:#1769e0}@media(max-width:700px){.page{margin:0;padding:22px;border-radius:0}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}th[scope=row]{width:124px}}@media(max-width:430px){.grid{grid-template-columns:1fr}th,td{display:block;width:100%}th[scope=row]{padding-bottom:3px;border-bottom:0}}@media print{*{-webkit-print-color-adjust:exact;print-color-adjust:exact}body{background:#fff}.page{max-width:none;margin:0;padding:0;box-shadow:none}h2{break-after:avoid}table,svg,.result,.metric{break-inside:avoid}}</style></head>
<body><main class="page"><h1>%2</h1><p class="muted">%3</p>
<section class="result"><strong style="color:%1;font-size:20px">%4</strong><p>%5</p><small>%6 项通过 · %7 项提醒 · %8 项未通过</small></section>
<section class="grid"><div class="metric"><small>持续时间</small><b>%9</b></div><div class="metric"><small>最大高度</small><b>%10 m</b></div><div class="metric"><small>最大水平速度</small><b>%11 m/s</b></div><div class="metric"><small>最大垂直速度</small><b>%12 m/s</b></div><div class="metric"><small>最大航线偏差</small><b>%13</b></div><div class="metric"><small>最长遥测中断</small><b>%14</b></div></section>
<h2>测试信息</h2><table>%15</table>%16
<h2>验收结果</h2><table><thead><tr><th>检查项</th><th>结果</th><th>说明</th></tr></thead><tbody>%17</tbody></table>
<h2>高度与速度</h2>%18%19%20
<h2>软件与仿真环境</h2><table>%21</table>
<h2>事件记录</h2><ul>%22</ul><p class="muted">报告由 FlightBench 在本机生成；原始数据以 session.json、events.log 和 telemetry.csv 为准。城市世界地图数据：<a href="https://www.openstreetmap.org/copyright">© OpenStreetMap contributors</a>。</p></main></body></html>)HTML")
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
             telemetryChart(samples), trackHtml, routeMetricsHtml, environmentRows, eventsHtml);
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
