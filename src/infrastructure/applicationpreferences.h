#pragma once
#include <QSettings>
#include <QString>
#include <QVariant>

// Owns preference values and persistence; no timers, flight state or deletion.
class ApplicationPreferences {
public:
    struct FlightControls {
        double takeoffAltitude;
        double horizontalSpeed;
        double verticalSpeed;
        double yawRate;
        bool needsLegacySpeedMigration;
    };
    static FlightControls readFlightControls(QSettings &settings);
    void completeLegacySpeedMigration(double validatedSpeed);
    void saveTakeoffAltitude(double validatedValue);
    void saveHorizontalSpeed(double validatedValue);
    void saveVerticalSpeed(double validatedValue);
    void saveYawRate(double validatedValue);
    explicit ApplicationPreferences(QSettings *storage = nullptr) : m_storage(storage) {}
    void load(QSettings &settings);
    void loadFlightPath(QSettings &settings);
    bool setFlightPathVisible(bool value);
    bool setFlightPathAutoClear(bool value);
    bool setFlightPathColorPreset(const QString &value);
    bool flightPathVisible() const { return m_flightPathVisible; }
    bool flightPathAutoClear() const { return m_flightPathAutoClear; }
    QString flightPathColorPreset() const { return m_flightPathColorPreset; }
    void saveFlightPathWidth(double normalizedValue);
    void saveFlightPathLength(int normalizedValue);
    QVariantMap exportDocument(const QVariantMap &flightPath) const;
    static QVariantMap decodeDocument(const QByteArray &bytes, QString *error);
    static QString writeDocument(const QVariantMap &document, const QString &directory,
                                 QString *error);
    bool setCameraRotationSensitivity(double value);
    double cameraRotationSensitivity() const { return m_cameraRotationSensitivity; }
    bool setCameraZoomSensitivity(double value);
    double cameraZoomSensitivity() const { return m_cameraZoomSensitivity; }
    bool setInvertCameraVertical(bool value);
    bool invertCameraVertical() const { return m_invertCameraVertical; }
    bool setGroundMarkerVisible(bool value);
    bool groundMarkerVisible() const { return m_groundMarkerVisible; }
    bool setGroundMarkerSizeMode(const QString &value);
    QString groundMarkerSizeMode() const { return m_groundMarkerSizeMode; }
    bool setDefaultCameraMode(const QString &value);
    QString defaultCameraMode() const { return m_defaultCameraMode; }
    bool setCityBuildingOpacity(double value);
    double cityBuildingOpacity() const { return m_cityBuildingOpacity; }
    bool setWorldRoadsVisible(bool value);
    bool worldRoadsVisible() const { return m_worldRoadsVisible; }
    bool setWorldGreenAreasVisible(bool value);
    bool worldGreenAreasVisible() const { return m_worldGreenAreasVisible; }
    bool setWorldWaterVisible(bool value);
    bool worldWaterVisible() const { return m_worldWaterVisible; }
    bool setDefaultGridVisible(bool value);
    bool defaultGridVisible() const { return m_defaultGridVisible; }
    bool setPreciseTurnDefaultDegrees(double value);
    double preciseTurnDefaultDegrees() const { return m_preciseTurnDefaultDegrees; }
    bool setTelemetryRecordingHz(int value);
    int telemetryRecordingHz() const { return m_telemetryRecordingHz; }
    bool setAutoGenerateReports(bool value);
    bool autoGenerateReports() const { return m_autoGenerateReports; }
    bool setTrashRetentionDays(int value);
    int trashRetentionDays() const { return m_trashRetentionDays; }
private:
    static bool validPathColor(const QString &value);
    bool m_flightPathVisible{true};
    bool m_flightPathAutoClear{true};
    QString m_flightPathColorPreset{QStringLiteral("orange")};
    void persist(const QString &key, const QVariant &value);
    QSettings *m_storage; // Optional injected store must outlive this object.
    double m_cameraRotationSensitivity{1.0};
    double m_cameraZoomSensitivity{1.0};
    bool m_invertCameraVertical{false};
    bool m_groundMarkerVisible{true};
    QString m_groundMarkerSizeMode{QStringLiteral("auto")};
    QString m_defaultCameraMode{QStringLiteral("follow-both")};
    double m_cityBuildingOpacity{1.0};
    bool m_worldRoadsVisible{true};
    bool m_worldGreenAreasVisible{true};
    bool m_worldWaterVisible{true};
    bool m_defaultGridVisible{true};
    double m_preciseTurnDefaultDegrees{45.0};
    int m_telemetryRecordingHz{5};
    bool m_autoGenerateReports{false};
    int m_trashRetentionDays{30};
};
