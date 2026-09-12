#include "sensors/mjpegaviwriter.h"
#include "sensors/sensorsessionstorage.h"
#include "sessions/replaydata.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QtEndian>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x << '\n'; return 1; } } while (false)
static bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
static quint32 word(const QByteArray &bytes, int offset) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + offset));
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    SensorSessionStorage storage;
    CHECK(!storage.writeManifest({}));
    CHECK(storage.open(temp.path()) == SensorSessionStorage::OpenResult::Ready);
    CHECK(storage.writeFrame("rgb/test.jpg", "frame"));
    CHECK(storage.append("{\"elapsedMs\":1}\n"));
    storage.flush();
    storage.close();
    CHECK(storage.writeManifest({{"recorded", true}}));
    QFile timelineCheck(temp.filePath("sensor/timeline.ndjson"));
    CHECK(timelineCheck.open(QIODevice::ReadOnly));
    CHECK(timelineCheck.readAll().trimmed() == "{\"elapsedMs\":1}");
    timelineCheck.close();
    storage.clearDirectory();
    CHECK(!storage.writeManifest({}));
    const QString blocked = temp.filePath("blocked");
    CHECK(QDir().mkpath(blocked));
    CHECK(writeFile(QDir(blocked).filePath("sensor"), "not a directory"));
    CHECK(storage.open(blocked) == SensorSessionStorage::OpenResult::DirectoryFailed);
    CHECK(storage.directory().isEmpty());
    const QString blockedTimeline = temp.filePath("blockedTimeline");
    CHECK(QDir().mkpath(QDir(blockedTimeline).filePath("sensor/timeline.ndjson")));
    CHECK(storage.open(blockedTimeline) == SensorSessionStorage::OpenResult::TimelineFailed);
    CHECK(storage.directory().isEmpty());
    MjpegAviWriter avi;
    CHECK(!avi.finish());
    CHECK(!avi.open(temp.filePath("missing/video.avi"), 2, 2));
    const QString video = temp.filePath("video.avi");
    CHECK(avi.open(video, 2, 2));
    avi.append("bad-size", 3, 2);
    CHECK(!avi.hasFrames());
    avi.append("abc", 2, 2);
    avi.append("abcd", 2, 2);
    CHECK(avi.finish());
    CHECK(!avi.finish());
    QFile file(video); CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(bytes.startsWith("RIFF"));
    CHECK(bytes.mid(8, 4) == "AVI ");
    CHECK(word(bytes, 4) == bytes.size() - 8);
    const int avih = bytes.indexOf("avih");
    CHECK(avih >= 0 && word(bytes, avih + 24) == 2);
    const int movi = bytes.indexOf("movi"), idx = bytes.indexOf("idx1");
    CHECK(movi >= 0 && idx > movi && word(bytes, idx + 4) == 32);
    CHECK(bytes.mid(movi + 4, 4) == "00dc");
    CHECK(word(bytes, movi + 8) == 3 && bytes.at(movi + 15) == '\0');
    CHECK(word(bytes, idx + 16) == 0 && word(bytes, idx + 20) == 3);
    CHECK(word(bytes, idx + 32) == 12 && word(bytes, idx + 36) == 4);
    CHECK(avi.open(temp.filePath("empty.avi"), 2, 2));
    CHECK(!avi.hasFrames() && !avi.finish());

    CHECK(QDir().mkpath(temp.filePath("sensor")));
    CHECK(writeFile(temp.filePath("sensor/manifest.json"),
        "{\"format\":\"FlightBenchSensorSession\",\"formatVersion\":2,"
        "\"kind\":\"depth-camera\"}\n"));
    const QString timeline = temp.filePath("sensor/timeline.ndjson");
    CHECK(writeFile(timeline,
        "{\"type\":\"camera\",\"elapsedMs\":0,\"stream\":\"rgb\",\"file\":\"frame.jpg\",\"jpegBase64\":\"YWJj\"}\n"
        "{broken}\n"
        "{\"type\":\"camera\",\"elapsedMs\":20,\"stream\":\"depth\",\"depthMillimetresBase64\":\"AAAA\"}\n"
        "{\"type\":\"scan\",\"elapsedMs\":30,\"points\":[1,2]}\n"
        "{\"type\":\"gimbal-state\",\"elapsedMs\":40,\"actual\":false,\"yaw\":30}\n"
        "{\"type\":\"gimbal-state\",\"elapsedMs\":50,\"actual\":true,\"yaw\":5}\n"
        "{\"type\":\"scan\",\"elapsedMs\":-1}\n"));
    ReplayData::SensorTimeline index;
    index.load(temp.path());
    CHECK(index.available && index.rgbSamples.size() == 1 && index.depthSamples.size() == 1);
    CHECK(index.lidarSamples.size() == 1 && index.gimbalTargetSamples.size() == 1);
    CHECK(index.gimbalFeedbackSamples.size() == 1);
    CHECK(!index.rgbSamples[0].metadata.contains("jpegBase64"));
    CHECK(!index.depthSamples[0].metadata.contains("depthMillimetresBase64"));
    CHECK(!index.lidarSamples[0].metadata.contains("points"));
    CHECK(index.rgbSamples[0].metadata.value("file").toString() == temp.filePath("sensor/frame.jpg"));
    CHECK(index.read(index.depthSamples[0]).value("depthMillimetresBase64").toString() == "AAAA");
    CHECK(index.read(index.gimbalFeedbackSamples[0]).value("yaw").toInt() == 5);
    CHECK(writeFile(temp.filePath("sensor/manifest.json"),
        "{\"format\":\"FlightBenchSensorSession\",\"formatVersion\":1,"
        "\"kind\":\"depth-camera\"}\n"));
    index.load(temp.path());
    CHECK(!index.available && index.rgbSamples.isEmpty() && index.depthSamples.isEmpty());
    CHECK(writeFile(temp.filePath("sensor/manifest.json"),
        "{\"format\":\"FlightBenchSensorSession\",\"formatVersion\":2,"
        "\"kind\":\"depth-camera\"}\n"));
    CHECK(writeFile(timeline, "{\"type\":\"gimbal-state\",\"elapsedMs\":0,\"actual\":true}\n"));
    index.load(temp.path());
    CHECK(!index.available && index.rgbSamples.isEmpty() && index.gimbalFeedbackSamples.size() == 1);
    index.load(QString());
    CHECK(!index.available && index.gimbalFeedbackSamples.isEmpty());

    const QString csv = temp.filePath("telemetry.csv");
    CHECK(writeFile(csv, "elapsed_ms,local_position_available,scene_x_east_m,scene_y_up_m,scene_z_south_m,heading_deg,roll_deg,pitch_deg,yaw_deg,altitude_m,horizontal_speed_m_s,vertical_speed_m_s\n"
        "0,1,1,2,3,0,0,0,0,2,1,0\n100,1,4,5,6,0,0,0,0,2,1,0\n"
        "100,1,7,8,9,0,0,0,0,2,1,0\n90,1,0,0,0,0,0,0,0,2,1,0\n"
        "200,1,nan,1,2,0,0,0,0,2,1,0\n"));
    const auto samples = ReplayData::loadSamples(csv);
    CHECK(samples.size() == 2 && samples[1].position == QVector3D(7,8,9));
    QString error;
    CHECK(ReplayData::loadSamples(temp.filePath("missing.csv"), &error).isEmpty() && !error.isEmpty());
    std::cout << "AVI layout, replay index and telemetry parsing checks passed\n";
}
