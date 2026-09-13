#include "infrastructure/applicationpreferences.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failed line " << __LINE__ << ": " #condition << '\n'; return 1; } } while (false)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    CHECK(directory.isValid());
    QSettings store(directory.filePath("settings.ini"), QSettings::IniFormat);
    ApplicationPreferences preferences(&store);
    auto controls = ApplicationPreferences::readFlightControls(store);
    CHECK(controls.needsLegacySpeedMigration);
    CHECK(controls.takeoffAltitude == 5.0 && controls.horizontalSpeed == 1.0);
    CHECK(controls.verticalSpeed == 0.5 && controls.yawRate == 30.0);
    CHECK(!store.contains("flight/controlSpeedMetersPerSecond")); // Reading alone does not migrate.
    const double legacySpeeds[] = {0.5, 1.0, 2.0};
    for (int mode = 0; mode < 3; ++mode) {
        store.setValue("flight/controlSpeedMode", mode);
        const auto legacy = ApplicationPreferences::readFlightControls(store);
        CHECK(legacy.needsLegacySpeedMigration);
        CHECK(legacy.horizontalSpeed == legacySpeeds[mode]);
        CHECK(store.contains("flight/controlSpeedMode"));
        CHECK(!store.contains("flight/controlSpeedMetersPerSecond"));
    }
    store.setValue("flight/controlSpeedMode", 99);
    CHECK(ApplicationPreferences::readFlightControls(store).horizontalSpeed == 2.0);
    store.setValue("flight/controlSpeedMode", -5);
    CHECK(ApplicationPreferences::readFlightControls(store).horizontalSpeed == 0.5);
    preferences.completeLegacySpeedMigration(0.5);
    CHECK(!store.contains("flight/controlSpeedMode"));
    CHECK(!ApplicationPreferences::readFlightControls(store).needsLegacySpeedMigration);
    store.setValue("flight/controlSpeedMode", 2);
    CHECK(ApplicationPreferences::readFlightControls(store).horizontalSpeed == 0.5);
    preferences.saveHorizontalSpeed(3.5);
    preferences.saveVerticalSpeed(1.5);
    preferences.saveYawRate(60);
    preferences.saveTakeoffAltitude(8);
    controls = ApplicationPreferences::readFlightControls(store);
    CHECK(controls.horizontalSpeed == 3.5 && controls.verticalSpeed == 1.5);
    CHECK(controls.takeoffAltitude == 8 && controls.yawRate == 60);
    store.sync();
    CHECK(store.status() == QSettings::NoError);
    {
        QSettings reopened(directory.filePath("settings.ini"), QSettings::IniFormat);
        const auto afterRestart = ApplicationPreferences::readFlightControls(reopened);
        CHECK(!afterRestart.needsLegacySpeedMigration);
        CHECK(afterRestart.horizontalSpeed == 3.5 && afterRestart.verticalSpeed == 1.5);
        CHECK(afterRestart.takeoffAltitude == 8 && afterRestart.yawRate == 60);
    }
    preferences.load(store);
    preferences.loadFlightPath(store);
    CHECK(preferences.flightPathVisible());
    CHECK(preferences.flightPathAutoClear());
    CHECK(preferences.flightPathColorPreset() == "orange");
    CHECK(preferences.setFlightPathVisible(false));
    CHECK(preferences.setFlightPathAutoClear(false));
    CHECK(preferences.setFlightPathColorPreset("  #Ab12Ef  "));
    CHECK(preferences.flightPathColorPreset() == "#ab12ef");
    CHECK(!preferences.setFlightPathColorPreset("#ab12ef"));
    CHECK(!preferences.setFlightPathColorPreset("#123"));
    CHECK(!preferences.setFlightPathColorPreset("red"));
    preferences.saveFlightPathWidth(0.08);
    preferences.saveFlightPathLength(0);
    CHECK(store.value("flightPath/widthMeters").toDouble() == 0.08);
    CHECK(store.value("flightPath/maxVisibleLengthMeters").toInt() == 0);
    CHECK(preferences.cameraRotationSensitivity() == 1.0);
    CHECK(preferences.groundMarkerSizeMode() == "auto");
    CHECK(preferences.telemetryRecordingHz() == 5);
    CHECK(preferences.trashRetentionDays() == 30);
    CHECK(!preferences.setTelemetryRecordingHz(6));
    CHECK(!preferences.setTrashRetentionDays(1));
    CHECK(!preferences.setDefaultCameraMode("invalid"));
    CHECK(!preferences.setGroundMarkerSizeMode("invalid"));
    CHECK(!preferences.setCameraRotationSensitivity(1.0));
    CHECK(preferences.setCameraRotationSensitivity(9));
    CHECK(preferences.cameraRotationSensitivity() == 2);
    CHECK(preferences.setCameraZoomSensitivity(-1));
    CHECK(preferences.cameraZoomSensitivity() == 0.5);
    CHECK(preferences.setCityBuildingOpacity(0));
    CHECK(preferences.cityBuildingOpacity() == 0.25);
    CHECK(preferences.setPreciseTurnDefaultDegrees(400));
    CHECK(preferences.preciseTurnDefaultDegrees() == 180);
    CHECK(preferences.setInvertCameraVertical(true));
    CHECK(preferences.setGroundMarkerVisible(false));
    CHECK(preferences.setGroundMarkerSizeMode("large"));
    CHECK(preferences.setDefaultCameraMode("free"));
    CHECK(preferences.setWorldRoadsVisible(false));
    CHECK(preferences.setWorldGreenAreasVisible(false));
    CHECK(preferences.setWorldWaterVisible(false));
    CHECK(preferences.setDefaultGridVisible(false));
    CHECK(preferences.setTelemetryRecordingHz(10));
    CHECK(preferences.setAutoGenerateReports(true));
    CHECK(preferences.setTrashRetentionDays(0));
    CHECK(store.value("view/rotationSensitivity").toDouble() == 2);
    CHECK(store.value("interaction/preciseTurnDegrees").toDouble() == 180);
    CHECK(store.value("recording/trashRetentionDays").toInt() == 0);
    store.setValue("Appearance/mode", "dark");
    const QVariantMap flightPath{{"widthMeters", 0.06}, {"visible", false}};
    auto document = preferences.exportDocument(flightPath);
    CHECK(document.value("format").toString() == "FlightBenchSettings");
    CHECK(document.value("formatVersion").toInt() == 1);
    CHECK(document.value("appearance").toMap().value("mode").toString() == "dark");
    CHECK(document.value("flightPath").toMap() == flightPath);
    ApplicationPreferences restored(&store);
    restored.load(store);
    restored.loadFlightPath(store);
    CHECK(!restored.flightPathVisible());
    CHECK(!restored.flightPathAutoClear());
    CHECK(restored.flightPathColorPreset() == "#ab12ef");
    auto reloaded = restored.exportDocument(flightPath);
    document.remove("exportedAt"); reloaded.remove("exportedAt");
    CHECK(document == reloaded);
    QString error;
    CHECK(ApplicationPreferences::decodeDocument("{broken", &error).isEmpty());
    CHECK(!error.isEmpty());
    CHECK(ApplicationPreferences::decodeDocument("[]", &error).isEmpty());
    auto invalidVersion = document;
    invalidVersion.insert("formatVersion", 2);
    CHECK(ApplicationPreferences::decodeDocument(QJsonDocument(QJsonObject::fromVariantMap(invalidVersion)).toJson(), &error).isEmpty());
    const QString path = ApplicationPreferences::writeDocument(document, directory.filePath("exports"), &error);
    CHECK(!path.isEmpty()); CHECK(error.isEmpty());
    CHECK(QFileInfo(path).fileName() == "flightrig-settings.json");
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(ApplicationPreferences::decodeDocument(file.readAll(), &error) == document);
    CHECK(error.isEmpty());
    CHECK(ApplicationPreferences::writeDocument(document, path + "/blocked", &error).isEmpty());
    CHECK(!error.isEmpty());
    CHECK(store.allKeys().filter("simulation").isEmpty());
    std::cout << "Preference defaults, validation, persistence, reload and settings document tests passed\n";
    return 0;
}
