#include "appcontroller.h"

#include <QGuiApplication>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QSvgRenderer>
#include <QTimer>
#include <QtQml/qqml.h>

#include <memory>
#include <cstdio>

namespace {
bool runtimeUiWarningSeen = false;
QtMessageHandler previousMessageHandler = nullptr;

void uiSmokeMessageHandler(QtMsgType type,
                           const QMessageLogContext &context,
                           const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg) {
        const QString category = QString::fromUtf8(context.category ? context.category : "");
        if (category.startsWith(QStringLiteral("qt.qml"))
            || category.startsWith(QStringLiteral("qt.svg"))
            || message.startsWith(QStringLiteral("QQml"))
            || message.startsWith(QStringLiteral("qrc:/qt/qml/"))
            || message.contains(QStringLiteral("Context2D:"))) {
            runtimeUiWarningSeen = true;
        }
    }
    if (previousMessageHandler)
        previousMessageHandler(type, context, message);
    else
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
}
}

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    application.setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/flightbench-app.svg")));
    QCoreApplication::setApplicationName(QStringLiteral("FlightBench"));
    QCoreApplication::setOrganizationName(QStringLiteral("FlightBench"));
#ifdef FLIGHTBENCH_VERSION
    QCoreApplication::setApplicationVersion(QStringLiteral(FLIGHTBENCH_VERSION));
#endif

    const QStringList arguments = application.arguments();
    const bool connectionTest = arguments.contains(QStringLiteral("--connection-test"));
    const bool smokeTest = arguments.contains(QStringLiteral("--smoke-test"));
    if (smokeTest) {
        previousMessageHandler = qInstallMessageHandler(uiSmokeMessageHandler);
    }
    const bool isolatedTestRuntime = connectionTest || smokeTest;
    std::unique_ptr<QTemporaryDir> isolatedTestData;
    if (isolatedTestRuntime) {
        QStandardPaths::setTestModeEnabled(true);
        isolatedTestData = std::make_unique<QTemporaryDir>(
            QDir::tempPath() + (connectionTest
                                    ? QStringLiteral("/FlightBench-ConnectionTest-XXXXXX")
                                    : QStringLiteral("/FlightBench-UiSmoke-XXXXXX")));
        if (!isolatedTestData->isValid()) {
            qCritical("TEST_RUNTIME_FAILED: unable to create isolated data directory");
            return 3;
        }
        qputenv("FLIGHTBENCH_DATA_ROOT", QFile::encodeName(isolatedTestData->path()));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           isolatedTestData->path());
    }
    const qsizetype exportIconIndex = arguments.indexOf(QStringLiteral("--export-icon"));
    if (exportIconIndex >= 0 && exportIconIndex + 1 < arguments.size()) {
        QSvgRenderer renderer(QStringLiteral(":/icons/flightbench-app.svg"));
        QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter);
        painter.end();
        return renderer.isValid() && image.save(arguments.at(exportIconIndex + 1)) ? 0 : 4;
    }

    std::unique_ptr<AppController> controllerOwner(createAppController());
    AppController &controller = *controllerOwner;

    const qsizetype modelIndex = arguments.indexOf(QStringLiteral("--model"));
    if (modelIndex >= 0 && modelIndex + 1 < arguments.size()) {
        controller.setSelectedModelId(arguments.at(modelIndex + 1));
    }
    const qsizetype worldIndex = arguments.indexOf(QStringLiteral("--world"));
    if (worldIndex >= 0 && worldIndex + 1 < arguments.size()) {
        controller.setSelectedWorldId(arguments.at(worldIndex + 1));
    }
    if (connectionTest) {
        controller.setContainerName(QStringLiteral("flightbench-test-connection-%1")
                                        .arg(QCoreApplication::applicationPid()));
        const QVariantMap savedPlan = controller.saveCurrentTestPlan({
            {QStringLiteral("name"), QStringLiteral("连接诊断")},
            {QStringLiteral("description"), QStringLiteral("自动诊断专用临时方案")},
            {QStringLiteral("type"), QStringLiteral("free-flight")},
            {QStringLiteral("modelId"), controller.selectedModelId()},
            {QStringLiteral("worldId"), controller.selectedWorldId()},
            {QStringLiteral("takeoffAltitudeMeters"), 5.0},
            {QStringLiteral("endAction"), QStringLiteral("hold")},
            {QStringLiteral("acceptancePreset"), QStringLiteral("standard")}});
        if (!savedPlan.value(QStringLiteral("success")).toBool()) {
            qCritical("CONNECTION_TEST_FAILED: unable to create isolated test plan: %s",
                      qPrintable(savedPlan.value(QStringLiteral("message")).toString()));
            return 3;
        }
    }
    const bool dynamicScenePreview = arguments.contains(QStringLiteral("--scene-preview-dynamic"));
    if (arguments.contains(QStringLiteral("--scene-preview")) && !dynamicScenePreview) {
        controller.prepareFlightScenePreview();
    }
    const qsizetype replayIndex = arguments.indexOf(QStringLiteral("--replay-session"));
    if (replayIndex >= 0 && replayIndex + 1 < arguments.size()
        && !controller.startSessionReplay(arguments.at(replayIndex + 1))) {
        qWarning("Unable to start requested replay session: %s",
                 qPrintable(arguments.at(replayIndex + 1)));
    }

    if (connectionTest) {
        bool startAttempted = false;
        bool cleanupRequested = false;
        int connectionExitCode = 2;
        QElapsedTimer startClock;
        QTimer connectionDeadline;
        connectionDeadline.setSingleShot(true);

        auto finishWhenStopped = [&] {
            if (!cleanupRequested || controller.simulationRunning() || controller.busy()) return;
            application.exit(connectionExitCode);
        };
        auto requestCleanup = [&](int exitCode) {
            if (cleanupRequested) return;
            cleanupRequested = true;
            connectionExitCode = exitCode;
            connectionDeadline.stop();
            if (controller.simulationRunning()) controller.stopSimulation();
            finishWhenStopped();
            QTimer::singleShot(15000, &application, [&] {
                if (!cleanupRequested) return;
                if (controller.simulationRunning() || controller.busy()) {
                    qCritical("CONNECTION_TEST_CLEANUP_FAILED: temporary container did not stop; container=%s",
                              qPrintable(controller.containerName()));
                    application.exit(connectionExitCode == 0 ? 3 : connectionExitCode);
                }
            });
        };
        QObject::connect(&controller, &AppController::vehicleConnectedChanged, &application, [&] {
            if (controller.vehicleConnected() && !cleanupRequested) {
                qInfo("CONNECTION_TEST_OK: PX4 vehicle connected through MAVSDK");
                requestCleanup(0);
            }
        });
        auto requestStart = [&] {
            if (startAttempted || cleanupRequested || !controller.dockerAvailable()
                || !controller.imageAvailable() || controller.simulationRunning()
                || controller.busy()) {
                return;
            }
            startAttempted = true;
            startClock.start();
            if (!controller.startSimulation()) {
                qCritical("CONNECTION_TEST_FAILED: start rejected; status=%s; guidance=%s",
                          qPrintable(controller.statusMessage()),
                          qPrintable(controller.testRunGuidance()));
                requestCleanup(2);
            }
        };
        auto detectRejectedStart = [&] {
            if (!startAttempted || cleanupRequested || controller.simulationRunning()
                || controller.busy() || !startClock.isValid() || startClock.elapsed() < 750) {
                return;
            }
            qCritical("CONNECTION_TEST_FAILED: Docker rejected or ended the start request; status=%s; guidance=%s",
                      qPrintable(controller.statusMessage()),
                      qPrintable(controller.testRunGuidance()));
            requestCleanup(2);
        };
        QObject::connect(&controller, &AppController::dockerAvailableChanged,
                         &application, requestStart);
        QObject::connect(&controller, &AppController::imageAvailableChanged,
                         &application, requestStart);
        QObject::connect(&controller, &AppController::busyChanged,
                         &application, requestStart);
        QObject::connect(&controller, &AppController::busyChanged,
                         &application, detectRejectedStart);
        QObject::connect(&controller, &AppController::busyChanged,
                         &application, finishWhenStopped);
        QObject::connect(&controller, &AppController::simulationRunningChanged,
                         &application, finishWhenStopped);
        QTimer::singleShot(0, &application, requestStart);
        QObject::connect(&connectionDeadline, &QTimer::timeout, &application, [&] {
            qCritical("CONNECTION_TEST_FAILED: timed out waiting for PX4; status=%s; guidance=%s",
                      qPrintable(controller.statusMessage()),
                      qPrintable(controller.testRunGuidance()));
            requestCleanup(2);
        });
        connectionDeadline.start(60000);
        return application.exec();
    }

    qmlRegisterSingletonInstance("FlightBench", 1, 0, "App", &controller);

    QFile diagnosticFile;
    QQmlApplicationEngine engine;
    bool qmlWarningSeen = false;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                     [&qmlWarningSeen](const QList<QQmlError> &warnings) {
                         if (!warnings.isEmpty()) qmlWarningSeen = true;
                     });
    const qsizetype diagnosticIndex = arguments.indexOf(QStringLiteral("--diagnostic-log"));
    if (diagnosticIndex >= 0 && diagnosticIndex + 1 < arguments.size()) {
        diagnosticFile.setFileName(arguments.at(diagnosticIndex + 1));
        if (!diagnosticFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            qWarning("Unable to open QML diagnostic log: %s", qPrintable(diagnosticFile.errorString()));
        }
        // The diagnostic file must outlive the QML engine. Use the engine as the
        // connection context as well, so the callback is disconnected before the
        // file is destroyed during normal application shutdown.
        QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                         [&diagnosticFile](const QList<QQmlError> &warnings) {
                             QTextStream stream(&diagnosticFile);
                             for (const QQmlError &warning : warnings) {
                                 stream << warning.toString() << Qt::endl;
                             }
                             diagnosticFile.flush();
                         });
    }
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("FlightBench", "Main");

    if (dynamicScenePreview) {
        // Regression check for live QQuick3DGeometry updates: the View3D is
        // already constructed before the path buffer changes.
        QTimer::singleShot(900, &controller, &AppController::prepareFlightScenePreview);
    }

    const qsizetype pageIndex = arguments.indexOf(QStringLiteral("--page"));
    if (pageIndex >= 0 && pageIndex + 1 < arguments.size() && !engine.rootObjects().isEmpty()) {
        bool validPage = false;
        const int requestedPage = arguments.at(pageIndex + 1).toInt(&validPage);
        if (validPage && requestedPage >= 0 && requestedPage <= 2) {
            engine.rootObjects().constFirst()->setProperty("currentPage", requestedPage);
        }
    }

    // Visual regression and acceptance runs can open a specific flight view
    // without synthesizing mouse input. 0 = instruments, 1 = 3D, 2 = mission.
    const qsizetype flightViewIndex = arguments.indexOf(QStringLiteral("--flight-view"));
    if (flightViewIndex >= 0 && flightViewIndex + 1 < arguments.size()
        && !engine.rootObjects().isEmpty()) {
        bool validFlightView = false;
        const int requestedFlightView = arguments.at(flightViewIndex + 1).toInt(&validFlightView);
        if (validFlightView && requestedFlightView >= 0 && requestedFlightView <= 2) {
            engine.rootObjects().constFirst()->setProperty("flightViewIndex", requestedFlightView);
        }
    }

    const qsizetype drawerIndex = arguments.indexOf(QStringLiteral("--drawer"));
    if (drawerIndex >= 0 && drawerIndex + 1 < arguments.size() && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().constFirst()->setProperty("drawerRequest", arguments.at(drawerIndex + 1));
    }

    const qsizetype themeIndex = arguments.indexOf(QStringLiteral("--theme"));
    if (themeIndex >= 0 && themeIndex + 1 < arguments.size() && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().constFirst()->setProperty("themeRequest", arguments.at(themeIndex + 1));
    }

    const qsizetype textScaleIndex = arguments.indexOf(QStringLiteral("--text-scale"));
    if (textScaleIndex >= 0 && textScaleIndex + 1 < arguments.size()
        && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().constFirst()->setProperty(
            "textScaleRequest", arguments.at(textScaleIndex + 1).toInt());
    }

    if (!engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const qsizetype widthIndex = arguments.indexOf(QStringLiteral("--window-width"));
        const qsizetype heightIndex = arguments.indexOf(QStringLiteral("--window-height"));
        if (window && widthIndex >= 0 && widthIndex + 1 < arguments.size()) {
            window->setWidth(arguments.at(widthIndex + 1).toInt());
        }
        if (window && heightIndex >= 0 && heightIndex + 1 < arguments.size()) {
            window->setHeight(arguments.at(heightIndex + 1).toInt());
        }
    }

    const qsizetype screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        const QString screenshotPath = arguments.at(screenshotIndex + 1);
        QTimer::singleShot(3000, &application, [&application, &engine, screenshotPath] {
            if (engine.rootObjects().isEmpty()) {
                application.exit(3);
                return;
            }
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
            const bool saved = window && window->grabWindow().save(screenshotPath);
            application.exit(saved ? 0 : 3);
        });
    }

    if (smokeTest) {
        QTimer::singleShot(1500, &application, [&application, &qmlWarningSeen] {
            application.exit(qmlWarningSeen || runtimeUiWarningSeen ? 5 : 0);
        });
    }

    return application.exec();
}
