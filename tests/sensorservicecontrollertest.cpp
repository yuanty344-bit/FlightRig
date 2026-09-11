#include "sensors/sensorservicecontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QTemporaryDir>
#include <QThread>
#include <QDateTime>
#include <QJsonDocument>
#include <QDebug>

namespace {
QJsonObject cameraMessage(const QString &stream, double validRatio = 0.0)
{
    QJsonObject message{
        {QStringLiteral("type"), QStringLiteral("camera")},
        {QStringLiteral("stream"), stream},
        {QStringLiteral("width"), 2},
        {QStringLiteral("height"), 2},
        {QStringLiteral("hz"), 10.0},
        {QStringLiteral("jpegBase64"),
         QString::fromLatin1(QByteArray("test-jpeg").toBase64())},
    };
    if (stream == QStringLiteral("depth")) {
        message.insert(QStringLiteral("validRatio"), validRatio);
        message.insert(QStringLiteral("centerDistance"), 4.0);
        message.insert(QStringLiteral("nearestDistance"), 2.5);
        message.insert(QStringLiteral("gridWidth"), 1);
        message.insert(QStringLiteral("gridHeight"), 1);
        message.insert(QStringLiteral("depthMillimetresBase64"),
                       QString::fromLatin1(QByteArray::fromHex("c409").toBase64()));
    }
    return message;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    SensorClockSync clock;
    if (clock.latency(1000, 2000) != -1) return 20;
    // Different monotonic origins, 20 ms RTT, 4 ms server processing.
    if (!clock.observe(1000, 1010 + 9000000, 1014 + 9000000, 1024)
        || clock.latency(1040 + 9000000, 1070) != 30
        || clock.uncertainty(1070) != 11) return 21;
    // Reject congestion and malformed exchanges, do not replace good baseline.
    if (clock.observe(1100, 1110 + 9000000, 1114 + 9000000, 1500)
        || clock.observe(1100, 1110, 1100, 1120)
        || clock.latency(1140 + 9000000, 1170) != 30
        || clock.latency(2000 + 9000000, 1170) != -1
        || clock.latency(10000 + 9000000, 10010) != -1) return 22;
    clock.reset();
    if (clock.latency(1140 + 9000000, 1170) != -1) return 23;
    QTemporaryDir root;
    if (!root.isValid()) return 1;

    // Old gateway epoch timestamps, including huge clock offsets, must not
    // become measured zero/60-second delays or enter the report average.
    SensorServiceController calibrated;
    calibrated.setSensorKindForTesting(QStringLiteral("mono-camera"));
    const QString clockSession = root.filePath(QStringLiteral("clock"));
    const bool clockFolderCreated = QDir().mkpath(clockSession);
    if (!clockFolderCreated
        || !calibrated.beginSessionRecording(clockSession, QStringLiteral("record"),
                                         QStringLiteral("standard"))) {
        qCritical() << "Clock regression recording setup:" << clockSession
                    << clockFolderCreated << QFileInfo(clockSession).isDir()
                    << calibrated.statusMessage();
        return 24;
    }
    auto frame = cameraMessage(QStringLiteral("rgb"));
    frame.insert(QStringLiteral("sentAtEpochMs"), QDateTime::currentMSecsSinceEpoch() - 900);
    calibrated.injectGatewayMessageForTesting(frame);
    if (calibrated.rgbLatencyMilliseconds() != -1) return 25;
    QThread::msleep(110); // exceed the normal recording sample interval
    frame.insert(QStringLiteral("sentAtEpochMs"), QDateTime::currentMSecsSinceEpoch() + 999999);
    frame.insert(QStringLiteral("sentAtMonotonicMs"),
                 calibrated.synchronizeClockForTesting(9000000) - 50);
    calibrated.injectGatewayMessageForTesting(frame);
    const int measured = calibrated.rgbLatencyMilliseconds();
    const auto clockSummary = calibrated.finishSessionRecording();
    if (measured < 50 || measured > 100
        || !clockSummary.value(QStringLiteral("latencyMeasured")).toBool()
        || clockSummary.value(QStringLiteral("latencySampleCount")).toInt() != 1
        || clockSummary.value(QStringLiteral("averageLatencyMilliseconds")).toInt() != measured)
        return 26;
    QFile clockTimeline(QDir(clockSession).filePath(QStringLiteral("sensor/timeline.ndjson")));
    if (!clockTimeline.open(QIODevice::ReadOnly)) return 27;
    clockTimeline.readLine();
    const auto savedFrame = QJsonDocument::fromJson(clockTimeline.readLine()).object();
    if (savedFrame.value(QStringLiteral("latencyMilliseconds")).toInt(-1) != measured
        || savedFrame.value(QStringLiteral("latencyMethod")).toString()
               != QStringLiteral("monotonic-sync-v1")) return 28;

    SensorServiceController recorded;
    recorded.setSensorKindForTesting(QStringLiteral("depth-camera"));
    const QString recordedSession = QDir(root.path()).filePath(QStringLiteral("recorded"));
    if (!QDir().mkpath(recordedSession)) return 2;
    if (!recorded.beginSessionRecording(recordedSession, QStringLiteral("record"),
                                        QStringLiteral("standard"))) return 2;
    // ROS discovery time before the first frame is startup latency, not a
    // stream interruption and must not reduce the active data availability.
    QThread::msleep(120);
    recorded.injectGatewayMessageForTesting(cameraMessage(QStringLiteral("rgb")));
    recorded.injectGatewayMessageForTesting(cameraMessage(QStringLiteral("depth"), 0.75));
    const QVariantMap recordedSummary = recorded.finishSessionRecording();
    if (!recordedSummary.value(QStringLiteral("available")).toBool()
        || !recordedSummary.value(QStringLiteral("replayable")).toBool()
        || !recordedSummary.value(QStringLiteral("recordingComplete")).toBool()
        || recordedSummary.value(QStringLiteral("rgbFrameCount")).toInt() != 1
        || recordedSummary.value(QStringLiteral("depthFrameCount")).toInt() != 1
        || recordedSummary.value(QStringLiteral("depthValidRatio")).toDouble() < 0.7
        || recordedSummary.value(QStringLiteral("primaryStartedOffsetMs")).toLongLong() < 100
        || recordedSummary.value(QStringLiteral("maximumDataGapSeconds")).toDouble() > 0.05
        || recordedSummary.value(QStringLiteral("nearestObstacleMeters")).toDouble() < 2.4
        || recordedSummary.value(QStringLiteral("latencyMeasured")).toBool()) return 3;

    SensorServiceController preview;
    preview.setSensorKindForTesting(QStringLiteral("mono-camera"));
    const QString previewSession = QDir(root.path()).filePath(QStringLiteral("preview"));
    if (!QDir().mkpath(previewSession)
        || !preview.beginSessionRecording(previewSession,
                                       QStringLiteral("preview"),
                                       QStringLiteral("standard"))) return 4;
    preview.injectGatewayMessageForTesting(cameraMessage(QStringLiteral("rgb")));
    const QVariantMap previewSummary = preview.finishSessionRecording();
    if (!previewSummary.value(QStringLiteral("available")).toBool()
        || previewSummary.value(QStringLiteral("recorded")).toBool()
        || previewSummary.value(QStringLiteral("replayable")).toBool()
        || previewSummary.value(QStringLiteral("usage")).toString()
               != QStringLiteral("preview")) return 5;

    const QList<QPair<QString, int>> performanceModes{
        {QStringLiteral("eco"), 5},
        {QStringLiteral("standard"), 10},
        {QStringLiteral("smooth"), 15},
        // Auto starts conservatively at Standard and changes only after live
        // continuity/latency observations from the monitor timer.
        {QStringLiteral("auto"), 10},
    };
    int modeIndex = 0;
    for (const auto &[mode, expectedFps] : performanceModes) {
        SensorServiceController modeController;
        modeController.setSensorKindForTesting(QStringLiteral("mono-camera"));
        const QString folder = QDir(root.path()).filePath(
            QStringLiteral("mode-%1").arg(modeIndex++));
        if (!QDir().mkpath(folder)
            || !modeController.beginSessionRecording(folder,
                                                     QStringLiteral("preview"), mode)
            || modeController.performanceMode() != mode
            || modeController.targetFramesPerSecond() != expectedFps) return 10;
        modeController.finishSessionRecording();
    }

    SensorServiceController disabled;
    disabled.setSensorKindForTesting(QStringLiteral("mono-camera"));
    if (!disabled.beginSessionRecording(root.path(), QStringLiteral("off"),
                                        QStringLiteral("standard"))
        || !disabled.finishSessionRecording().isEmpty()) return 11;

    SensorServiceController limited;
    limited.setSensorKindForTesting(QStringLiteral("mono-camera"));
    const QString limitedSession = QDir(root.path()).filePath(QStringLiteral("limited"));
    if (!QDir().mkpath(limitedSession)
        || !limited.beginSessionRecording(limitedSession,
                                       QStringLiteral("record"),
                                       QStringLiteral("standard"))) return 6;
    limited.setMaximumRecordedBytesForTesting(1);
    limited.injectGatewayMessageForTesting(cameraMessage(QStringLiteral("rgb")));
    limited.injectGatewayMessageForTesting(cameraMessage(QStringLiteral("rgb")));
    const QVariantMap limitedSummary = limited.finishSessionRecording();
    if (!limitedSummary.value(QStringLiteral("storageLimitReached")).toBool()
        || limitedSummary.value(QStringLiteral("recordingComplete")).toBool()) return 7;

    SensorServiceController gimbal;
    gimbal.setSensorKindForTesting(QStringLiteral("gimbal-camera"));
    gimbal.injectGatewayMessageForTesting(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("gimbal-state")},
        {QStringLiteral("actual"), false},
        {QStringLiteral("targetYaw"), 180.0},
        {QStringLiteral("targetPitch"), -20.0},
    });
    if (gimbal.gimbalFeedbackAvailable()) return 8;
    gimbal.injectGatewayMessageForTesting(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("gimbal-state")},
        {QStringLiteral("actual"), true},
        {QStringLiteral("yaw"), -180.0},
        {QStringLiteral("pitch"), -20.0},
        {QStringLiteral("roll"), 0.0},
    });
    if (!gimbal.gimbalFeedbackAvailable() || gimbal.gimbalMoving()) return 9;
    return 0;
}
