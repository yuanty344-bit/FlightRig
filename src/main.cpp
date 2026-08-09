#include "appcontroller.h"

#include <QGuiApplication>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QFile>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QTextStream>
#include <QSvgRenderer>
#include <QTimer>
#include <QtQml/qqml.h>

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

    AppController controller;

    const qsizetype modelIndex = arguments.indexOf(QStringLiteral("--model"));
    if (modelIndex >= 0 && modelIndex + 1 < arguments.size()) {
        controller.setSelectedModelId(arguments.at(modelIndex + 1));
    }
    const bool dynamicScenePreview = arguments.contains(QStringLiteral("--scene-preview-dynamic"));
    if (arguments.contains(QStringLiteral("--scene-preview")) && !dynamicScenePreview) {
        controller.prepareFlightScenePreview();
    }

    if (arguments.contains(QStringLiteral("--connection-test"))) {
        QObject::connect(&controller, &AppController::vehicleConnectedChanged, &application, [&] {
            if (controller.vehicleConnected()) {
                qInfo("CONNECTION_TEST_OK: PX4 vehicle connected through MAVSDK");
                QTimer::singleShot(500, &application, [&application] { application.exit(0); });
            }
        });
        QObject::connect(&controller, &AppController::dockerAvailableChanged, &application, [&] {
            if (controller.dockerAvailable() && !controller.simulationRunning()) {
                // Let DockerController finish its container inspection before starting a new process.
                QTimer::singleShot(800, &controller, [&controller] {
                    if (!controller.simulationRunning() && !controller.busy()) {
                        controller.startSimulation();
                    }
                });
            }
        });
        QTimer::singleShot(60000, &application, [&application] {
            qCritical("CONNECTION_TEST_FAILED: timed out waiting for PX4");
            application.exit(2);
        });
        return application.exec();
    }

    qmlRegisterSingletonInstance("FlightBench", 1, 0, "App", &controller);

    QFile diagnosticFile;
    QQmlApplicationEngine engine;
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

    const qsizetype drawerIndex = arguments.indexOf(QStringLiteral("--drawer"));
    if (drawerIndex >= 0 && drawerIndex + 1 < arguments.size() && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().constFirst()->setProperty("drawerRequest", arguments.at(drawerIndex + 1));
    }

    const qsizetype themeIndex = arguments.indexOf(QStringLiteral("--theme"));
    if (themeIndex >= 0 && themeIndex + 1 < arguments.size() && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().constFirst()->setProperty("themeRequest", arguments.at(themeIndex + 1));
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

    if (arguments.contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(1500, &application, [&application] { application.exit(0); });
    }

    return application.exec();
}
