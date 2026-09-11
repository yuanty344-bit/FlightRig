#include "sessions/replaydata.h"
#include <QFile>
#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix3x3>
#include <QStringConverter>
#include <QTextStream>
#include <QtMath>
#include <cmath>

namespace {
QStringList splitCsvLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool quoted = false;
    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (quoted) {
            if (character == QLatin1Char('"')) {
                if (index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"')) {
                    current.append(QLatin1Char('"'));
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                current.append(character);
            }
        } else if (character == QLatin1Char('"')) {
            quoted = true;
        } else if (character == QLatin1Char(',')) {
            fields.append(current);
            current.clear();
        } else {
            current.append(character);
        }
    }
    fields.append(current);
    return fields;
}

QQuaternion sceneRotation(double rollDegrees, double pitchDegrees, double yawDegrees)
{
    const double roll = qDegreesToRadians(rollDegrees);
    const double pitch = qDegreesToRadians(pitchDegrees);
    const double yaw = qDegreesToRadians(yawDegrees);
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    QMatrix3x3 bodyToNed;
    bodyToNed(0, 0) = cy * cp;
    bodyToNed(0, 1) = cy * sp * sr - sy * cr;
    bodyToNed(0, 2) = cy * sp * cr + sy * sr;
    bodyToNed(1, 0) = sy * cp;
    bodyToNed(1, 1) = sy * sp * sr + cy * cr;
    bodyToNed(1, 2) = sy * sp * cr - cy * sr;
    bodyToNed(2, 0) = -sp;
    bodyToNed(2, 1) = cp * sr;
    bodyToNed(2, 2) = cp * cr;
    QMatrix3x3 nedToScene;
    nedToScene(0, 0) = 0;  nedToScene(0, 1) = 1;  nedToScene(0, 2) = 0;
    nedToScene(1, 0) = 0;  nedToScene(1, 1) = 0;  nedToScene(1, 2) = -1;
    nedToScene(2, 0) = -1; nedToScene(2, 1) = 0;  nedToScene(2, 2) = 0;
    QMatrix3x3 modelToBody;
    modelToBody(0, 0) = 0; modelToBody(0, 1) = 0;  modelToBody(0, 2) = -1;
    modelToBody(1, 0) = 1; modelToBody(1, 1) = 0;  modelToBody(1, 2) = 0;
    modelToBody(2, 0) = 0; modelToBody(2, 1) = -1; modelToBody(2, 2) = 0;
    return QQuaternion::fromRotationMatrix(nedToScene * bodyToNed * modelToBody).normalized();
}

bool parseFinite(const QStringList &fields, int index, double *value)
{
    if (index < 0 || index >= fields.size()) return false;
    bool ok = false;
    const double parsed = fields.at(index).toDouble(&ok);
    if (!ok || !std::isfinite(parsed)) return false;
    *value = parsed;
    return true;
}
}

QVector<ReplayData::Sample> ReplayData::loadSamples(const QString &csvPath,
                                                                QString *errorMessage)
{
    QVector<Sample> samples;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法读取遥测文件");
        return samples;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd()) return samples;
    const QStringList header = splitCsvLine(stream.readLine());
    QHash<QString, int> columns;
    for (int index = 0; index < header.size(); ++index)
        columns.insert(header.at(index).trimmed(), index);
    const QStringList required{QStringLiteral("elapsed_ms"), QStringLiteral("local_position_available"),
        QStringLiteral("scene_x_east_m"), QStringLiteral("scene_y_up_m"),
        QStringLiteral("scene_z_south_m")};
    for (const QString &name : required) {
        if (!columns.contains(name)) {
            if (errorMessage) *errorMessage = QStringLiteral("遥测文件缺少位置列：%1").arg(name);
            return {};
        }
    }
    const auto column = [&columns](const char *name) { return columns.value(QLatin1String(name), -1); };
    const int elapsedColumn = column("elapsed_ms");
    const int availableColumn = column("local_position_available");
    const int eastColumn = column("scene_x_east_m");
    const int upColumn = column("scene_y_up_m");
    const int southColumn = column("scene_z_south_m");
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty()) continue;
        const QStringList fields = splitCsvLine(line);
        if (availableColumn >= fields.size() || fields.at(availableColumn).trimmed() != QLatin1String("1"))
            continue;
        bool elapsedOk = false;
        const qint64 elapsedMs = elapsedColumn < fields.size()
                                     ? fields.at(elapsedColumn).toLongLong(&elapsedOk) : -1;
        double east = 0, up = 0, south = 0;
        if (!elapsedOk || elapsedMs < 0 || !parseFinite(fields, eastColumn, &east)
            || !parseFinite(fields, upColumn, &up) || !parseFinite(fields, southColumn, &south))
            continue;
        Sample sample;
        sample.elapsedMs = elapsedMs;
        sample.position = QVector3D(static_cast<float>(east), static_cast<float>(up), static_cast<float>(south));
        parseFinite(fields, column("heading_deg"), &sample.headingDegrees);
        parseFinite(fields, column("roll_deg"), &sample.rollDegrees);
        parseFinite(fields, column("pitch_deg"), &sample.pitchDegrees);
        parseFinite(fields, column("yaw_deg"), &sample.yawDegrees);
        parseFinite(fields, column("altitude_m"), &sample.altitudeMeters);
        parseFinite(fields, column("horizontal_speed_m_s"), &sample.horizontalSpeedMetersPerSecond);
        parseFinite(fields, column("vertical_speed_m_s"), &sample.verticalSpeedMetersPerSecond);
        sample.rotation = sceneRotation(sample.rollDegrees, sample.pitchDegrees, sample.yawDegrees);
        if (!samples.isEmpty() && sample.elapsedMs < samples.constLast().elapsedMs) continue;
        if (!samples.isEmpty() && sample.elapsedMs == samples.constLast().elapsedMs) samples.last() = sample;
        else samples.append(sample);
    }
    if (samples.size() < 2 && errorMessage)
        *errorMessage = QStringLiteral("该会话没有足够的本地位置数据");
    return samples;
}


void ReplayData::SensorTimeline::load(const QString &sessionFolder)
{
    rgbSamples.clear();
    depthSamples.clear();
    lidarSamples.clear();
    gimbalTargetSamples.clear();
    gimbalFeedbackSamples.clear();
    available = false;
    kind.clear();
    if (sessionFolder.isEmpty()) return;
    const QDir sensorDirectory(QDir(sessionFolder).filePath(QStringLiteral("sensor")));
    QFile manifestFile(sensorDirectory.filePath(QStringLiteral("manifest.json")));
    if (manifestFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument manifest = QJsonDocument::fromJson(manifestFile.readAll());
        if (manifest.isObject())
            kind = manifest.object().value(QStringLiteral("kind")).toString();
    }
    path = sensorDirectory.filePath(QStringLiteral("timeline.ndjson"));
    QFile timeline(path);
    if (!timeline.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    while (!timeline.atEnd()) {
        const qint64 fileOffset = timeline.pos();
        const QJsonDocument document = QJsonDocument::fromJson(timeline.readLine().trimmed());
        if (!document.isObject()) continue;
        QVariantMap data = document.object().toVariantMap();
        bool ok = false;
        const qint64 elapsedMs = data.value(QStringLiteral("elapsedMs")).toLongLong(&ok);
        if (!ok || elapsedMs < 0) continue;
        const QString relativeFile = data.value(QStringLiteral("file")).toString();
        if (!relativeFile.isEmpty())
            data.insert(QStringLiteral("file"), sensorDirectory.filePath(relativeFile));
        QVariantMap metadata = data;
        metadata.remove(QStringLiteral("jpegBase64"));
        metadata.remove(QStringLiteral("ranges"));
        metadata.remove(QStringLiteral("points"));
        metadata.remove(QStringLiteral("depthMillimetresBase64"));
        SensorSample sample{elapsedMs, fileOffset, metadata};
        const QString type = data.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("scan")) lidarSamples.append(sample);
        else if (type == QStringLiteral("gimbal-state")) {
            if (data.value(QStringLiteral("actual")).toBool())
                gimbalFeedbackSamples.append(sample);
            else
                gimbalTargetSamples.append(sample);
        }
        else if (type == QStringLiteral("camera")) {
            if (data.value(QStringLiteral("stream")).toString() == QStringLiteral("depth"))
                depthSamples.append(sample);
            else
                rgbSamples.append(sample);
        }
    }
    // A sensor-view entry is useful only when it has something visual to show.
    // Gimbal feedback alone is still replayed internally, but no longer opens
    // an empty camera page.
    available = !rgbSamples.isEmpty() || !depthSamples.isEmpty()
                        || !lidarSamples.isEmpty();
    if (kind.isEmpty()) {
        if (!lidarSamples.isEmpty()) kind = QStringLiteral("lidar-2d");
        else if (!depthSamples.isEmpty()) kind = QStringLiteral("depth-camera");
        else if (!rgbSamples.isEmpty()
                 && (!gimbalTargetSamples.isEmpty() || !gimbalFeedbackSamples.isEmpty()))
            kind = QStringLiteral("gimbal-camera");
        else if (!rgbSamples.isEmpty()) kind = QStringLiteral("mono-camera");
    }
}

QVariantMap ReplayData::SensorTimeline::read(const SensorSample &sample) const
{
    if (sample.fileOffset < 0 || path.isEmpty()) return sample.metadata;
    QFile timeline(path);
    if (!timeline.open(QIODevice::ReadOnly | QIODevice::Text)
        || !timeline.seek(sample.fileOffset)) return sample.metadata;
    const QJsonDocument document = QJsonDocument::fromJson(timeline.readLine().trimmed());
    return document.isObject() ? document.object().toVariantMap() : sample.metadata;
}
