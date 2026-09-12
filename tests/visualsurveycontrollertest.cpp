#include "inspection/visualsurveycontroller.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>

namespace {
QByteArray testJpeg()
{
    QImage image(320, 180, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, ((x / 12 + y / 12) % 2) ? QColor("#E5EDF5")
                                                                  : QColor("#263746"));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 88);
    return bytes;
}

QByteArray lowTextureCityJpeg()
{
    // A sharp white-model scene contains large flat regions by design. It must
    // not be classified as blurred merely because it has little texture.
    QImage image(320, 180, QImage::Format_RGB32);
    image.fill(QColor("#E8EEF3"));
    for (int y = 82; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor("#C8CEC8"));
    for (int y = 55; y < 120; ++y)
        for (int x = 95; x < 245; ++x)
            image.setPixelColor(x, y, QColor("#858B90"));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 88);
    return bytes;
}

QByteArray structurelessJpeg()
{
    QImage image(320, 180, QImage::Format_RGB32);
    image.fill(QColor("#808080"));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 88);
    return bytes;
}

int fail(const char *message)
{
    qCritical("%s", message);
    return 1;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return fail("temporary directory unavailable");
    const qint64 start = QDateTime::currentMSecsSinceEpoch();
    QFile metadata(directory.filePath(QStringLiteral("session.json")));
    if (!metadata.open(QIODevice::WriteOnly)) return fail("session metadata unavailable");
    metadata.write(QJsonDocument(QJsonObject{{QStringLiteral("startedAt"),
        QDateTime::fromMSecsSinceEpoch(start).toString(Qt::ISODate)}}).toJson());
    metadata.close();

    VisualSurveyController survey;
    survey.prepareSession(directory.path(), true);
    if (!survey.start()) return fail("survey did not start");
    const QByteArray jpeg = testJpeg();
    survey.ingestFrame(jpeg, 320, 180, start + 1000,
                       QVector3D(0, 5, 0), 0, 0, true);
    if (survey.keyframeCount() != 1) return fail("first keyframe was not accepted");
    survey.ingestFrame(jpeg, 320, 180, start + 2200,
                       QVector3D(0.2f, 5, 0), 1, 0, true);
    if (survey.keyframeCount() != 1) return fail("duplicate keyframe was accepted");
    survey.ingestFrame(jpeg, 320, 180, start + 3400,
                       QVector3D(5.0f, 5, 0), 5, 0, true);
    if (survey.keyframeCount() != 2) return fail("moved keyframe was not accepted");
    const QByteArray cityJpeg = lowTextureCityJpeg();
    survey.ingestFrame(cityJpeg, 320, 180, start + 4600,
                       QVector3D(10.0f, 5, 0), 10, 0, true);
    if (survey.keyframeCount() != 3) return fail("low-texture city keyframe was rejected");
    survey.setSelectedIndex(2);
    if (survey.selectedKeyframe().value(QStringLiteral("qualityLabel")).toString()
        != QStringLiteral("清晰")) return fail("sharp white-model scene was marked unclear");
    survey.ingestFrame(structurelessJpeg(), 320, 180, start + 5800,
                       QVector3D(15.0f, 5, 0), 15, 0, true);
    if (survey.keyframeCount() != 3)
        return fail("structureless frame was retained as a keyframe");
    if (std::abs(survey.spatialCoveragePercent() - 75.0) > 0.1)
        return fail("capture coverage was confused with clear-frame percentage");
    if (survey.qualityPassPercent() < 99.9)
        return fail("quality pass percentage is incorrect");
    if (!survey.updateSelected(QStringLiteral("外墙待复核"), true))
        return fail("keyframe annotation was not saved");
    survey.stop();
    if (!QFile::exists(directory.filePath(QStringLiteral(
            "sensor/visual-survey/visual-survey.json"))))
        return fail("survey manifest missing");

    VisualSurveyController loaded;
    if (!loaded.loadFromSession(directory.path()) || loaded.keyframeCount() != 3)
        return fail("saved survey did not load");
    if (std::abs(loaded.spatialCoveragePercent() - 75.0) > 0.1
        || loaded.qualityPassPercent() < 99.9)
        return fail("saved survey metrics did not load");
    loaded.setSelectedIndex(2);
    if (!loaded.selectedKeyframe().value(QStringLiteral("anomaly")).toBool())
        return fail("saved annotation missing");
    QFile obsoleteManifest(directory.filePath(
        QStringLiteral("sensor/visual-survey/visual-survey.json")));
    if (!obsoleteManifest.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail("could not prepare obsolete survey manifest");
    obsoleteManifest.write("{\"formatVersion\":1,\"qualityMetricVersion\":1,\"keyframes\":[]}");
    obsoleteManifest.close();
    VisualSurveyController obsolete;
    if (obsolete.loadFromSession(directory.path()))
        return fail("obsolete survey format was accepted");
    return 0;
}
