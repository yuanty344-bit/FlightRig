#include <QDateTime>
#include <QRegularExpression>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include "infrastructure/applicationpreferences.h"
#include <QStringList>
#include <algorithm>
#include <cmath>

void ApplicationPreferences::persist(const QString &key, const QVariant &value)
{
    if (m_storage) m_storage->setValue(key, value);
    else QSettings().setValue(key, value);
}

ApplicationPreferences::FlightControls ApplicationPreferences::readFlightControls(QSettings &settings)
{
    const bool migrate = !settings.contains(QStringLiteral("flight/controlSpeedMetersPerSecond"));
    static constexpr double legacySpeeds[]{0.5, 1.0, 2.0};
    const int mode = std::clamp(settings.value(QStringLiteral("flight/controlSpeedMode"), 1).toInt(), 0, 2);
    return {
        settings.value(QStringLiteral("flight/takeoffAltitude"), 5.0).toDouble(),
        migrate ? legacySpeeds[mode]
                : settings.value(QStringLiteral("flight/controlSpeedMetersPerSecond"), 1.0).toDouble(),
        settings.value(QStringLiteral("flight/controlVerticalSpeedMetersPerSecond"), 0.5).toDouble(),
        settings.value(QStringLiteral("flight/controlYawRateDegreesPerSecond"), 30.0).toDouble(),
        migrate
    };
}

void ApplicationPreferences::completeLegacySpeedMigration(double validatedSpeed)
{
    saveHorizontalSpeed(validatedSpeed);
    if (m_storage) m_storage->remove(QStringLiteral("flight/controlSpeedMode"));
    else QSettings().remove(QStringLiteral("flight/controlSpeedMode"));
}

void ApplicationPreferences::saveTakeoffAltitude(double validatedValue)
{
    persist(QStringLiteral("flight/takeoffAltitude"), validatedValue);
}
void ApplicationPreferences::saveHorizontalSpeed(double validatedValue)
{
    persist(QStringLiteral("flight/controlSpeedMetersPerSecond"), validatedValue);
}
void ApplicationPreferences::saveVerticalSpeed(double validatedValue)
{
    persist(QStringLiteral("flight/controlVerticalSpeedMetersPerSecond"), validatedValue);
}
void ApplicationPreferences::saveYawRate(double validatedValue)
{
    persist(QStringLiteral("flight/controlYawRateDegreesPerSecond"), validatedValue);
}

bool ApplicationPreferences::validPathColor(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    static const QRegularExpression customColor(QStringLiteral("^#[0-9a-f]{6}$"));
    return normalized == QStringLiteral("orange") || normalized == QStringLiteral("blue")
        || normalized == QStringLiteral("teal") || normalized == QStringLiteral("violet")
        || customColor.match(normalized).hasMatch();
}

void ApplicationPreferences::loadFlightPath(QSettings &settings)
{
    m_flightPathVisible = settings.value(QStringLiteral("flightPath/visible"), true).toBool();
    m_flightPathAutoClear = settings.value(QStringLiteral("flightPath/autoClear"), true).toBool();
    const QString color = settings.value(QStringLiteral("flightPath/colorPreset"),
                                         QStringLiteral("orange")).toString();
    if (validPathColor(color)) m_flightPathColorPreset = color.trimmed().toLower();
}

bool ApplicationPreferences::setFlightPathVisible(bool value)
{
    if (m_flightPathVisible == value) return false;
    m_flightPathVisible = value;
    persist(QStringLiteral("flightPath/visible"), value);
    return true;
}

bool ApplicationPreferences::setFlightPathAutoClear(bool value)
{
    if (m_flightPathAutoClear == value) return false;
    m_flightPathAutoClear = value;
    persist(QStringLiteral("flightPath/autoClear"), value);
    return true;
}

bool ApplicationPreferences::setFlightPathColorPreset(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (!validPathColor(normalized) || m_flightPathColorPreset == normalized) return false;
    m_flightPathColorPreset = normalized;
    persist(QStringLiteral("flightPath/colorPreset"), normalized);
    return true;
}

void ApplicationPreferences::saveFlightPathWidth(double normalizedValue)
{
    persist(QStringLiteral("flightPath/widthMeters"), normalizedValue);
}

void ApplicationPreferences::saveFlightPathLength(int normalizedValue)
{
    persist(QStringLiteral("flightPath/maxVisibleLengthMeters"), normalizedValue);
}
void ApplicationPreferences::load(QSettings &settings)
{
    m_cameraRotationSensitivity = std::clamp(
        settings.value(QStringLiteral("view/rotationSensitivity"), 1.0).toDouble(), 0.5, 2.0);
    m_cameraZoomSensitivity = std::clamp(
        settings.value(QStringLiteral("view/zoomSensitivity"), 1.0).toDouble(), 0.5, 2.0);
    m_invertCameraVertical = settings.value(
        QStringLiteral("view/invertVertical"), false).toBool();
    m_groundMarkerVisible = settings.value(
        QStringLiteral("view/groundMarkerVisible"), true).toBool();
    const QString savedMarkerSize = settings.value(
        QStringLiteral("view/groundMarkerSize"), QStringLiteral("auto")).toString();
    if (QStringList{QStringLiteral("auto"), QStringLiteral("small"),
                    QStringLiteral("medium"), QStringLiteral("large")}.contains(savedMarkerSize))
        m_groundMarkerSizeMode = savedMarkerSize;
    const QString savedCameraMode = settings.value(
        QStringLiteral("view/defaultCameraMode"), QStringLiteral("follow-both")).toString();
    if (QStringList{QStringLiteral("follow"), QStringLiteral("follow-both"),
                    QStringLiteral("free")}.contains(savedCameraMode))
        m_defaultCameraMode = savedCameraMode;
    m_cityBuildingOpacity = std::clamp(
        settings.value(QStringLiteral("view/buildingOpacity"), 1.0).toDouble(), 0.25, 1.0);
    m_worldRoadsVisible = settings.value(QStringLiteral("view/roadsVisible"), true).toBool();
    m_worldGreenAreasVisible = settings.value(QStringLiteral("view/greenAreasVisible"), true).toBool();
    m_worldWaterVisible = settings.value(QStringLiteral("view/waterVisible"), true).toBool();
    m_defaultGridVisible = settings.value(QStringLiteral("view/defaultGridVisible"), true).toBool();
    m_preciseTurnDefaultDegrees = std::clamp(
        settings.value(QStringLiteral("interaction/preciseTurnDegrees"), 45.0).toDouble(),
        5.0, 180.0);
    const int savedRecordingHz = settings.value(
        QStringLiteral("recording/telemetryHz"), 5).toInt();
    m_telemetryRecordingHz = QStringList{QStringLiteral("2"), QStringLiteral("5"),
                                         QStringLiteral("10")}.contains(QString::number(savedRecordingHz))
                                 ? savedRecordingHz : 5;
    m_autoGenerateReports = settings.value(
        QStringLiteral("recording/autoGenerateReports"), false).toBool();
    const int savedRetentionDays = settings.value(
        QStringLiteral("recording/trashRetentionDays"), 30).toInt();
    m_trashRetentionDays = QList<int>{0, 7, 30, 90}.contains(savedRetentionDays)
                               ? savedRetentionDays : 30;
}

bool ApplicationPreferences::setCameraRotationSensitivity(double value)
{
    const double normalized = std::round(std::clamp(value, 0.5, 2.0) * 10.0) / 10.0;
    if (qFuzzyCompare(m_cameraRotationSensitivity, normalized)) return false;
    m_cameraRotationSensitivity = normalized;
    persist(QStringLiteral("view/rotationSensitivity"), normalized);
    return true;
}

bool ApplicationPreferences::setCameraZoomSensitivity(double value)
{
    const double normalized = std::round(std::clamp(value, 0.5, 2.0) * 10.0) / 10.0;
    if (qFuzzyCompare(m_cameraZoomSensitivity, normalized)) return false;
    m_cameraZoomSensitivity = normalized;
    persist(QStringLiteral("view/zoomSensitivity"), normalized);
    return true;
}

bool ApplicationPreferences::setInvertCameraVertical(bool value)
{
    if (m_invertCameraVertical == value) return false;
    m_invertCameraVertical = value;
    persist(QStringLiteral("view/invertVertical"), value);
    return true;
}

bool ApplicationPreferences::setGroundMarkerVisible(bool value)
{
    if (m_groundMarkerVisible == value) return false;
    m_groundMarkerVisible = value;
    persist(QStringLiteral("view/groundMarkerVisible"), value);
    return true;
}

bool ApplicationPreferences::setGroundMarkerSizeMode(const QString &value)
{
    if (!QStringList{QStringLiteral("auto"), QStringLiteral("small"),
                     QStringLiteral("medium"), QStringLiteral("large")}.contains(value)
        || m_groundMarkerSizeMode == value) return false;
    m_groundMarkerSizeMode = value;
    persist(QStringLiteral("view/groundMarkerSize"), value);
    return true;
}

bool ApplicationPreferences::setDefaultCameraMode(const QString &value)
{
    if (!QStringList{QStringLiteral("follow"), QStringLiteral("follow-both"),
                     QStringLiteral("free")}.contains(value)
        || m_defaultCameraMode == value) return false;
    m_defaultCameraMode = value;
    persist(QStringLiteral("view/defaultCameraMode"), value);
    return true;
}

bool ApplicationPreferences::setCityBuildingOpacity(double value)
{
    const double normalized = std::round(std::clamp(value, 0.25, 1.0) * 20.0) / 20.0;
    if (qFuzzyCompare(m_cityBuildingOpacity, normalized)) return false;
    m_cityBuildingOpacity = normalized;
    persist(QStringLiteral("view/buildingOpacity"), normalized);
    return true;
}

bool ApplicationPreferences::setWorldRoadsVisible(bool value)
{
    if (m_worldRoadsVisible == value) return false;
    m_worldRoadsVisible = value;
    persist(QStringLiteral("view/roadsVisible"), value);
    return true;
}

bool ApplicationPreferences::setWorldGreenAreasVisible(bool value)
{
    if (m_worldGreenAreasVisible == value) return false;
    m_worldGreenAreasVisible = value;
    persist(QStringLiteral("view/greenAreasVisible"), value);
    return true;
}

bool ApplicationPreferences::setWorldWaterVisible(bool value)
{
    if (m_worldWaterVisible == value) return false;
    m_worldWaterVisible = value;
    persist(QStringLiteral("view/waterVisible"), value);
    return true;
}

bool ApplicationPreferences::setDefaultGridVisible(bool value)
{
    if (m_defaultGridVisible == value) return false;
    m_defaultGridVisible = value;
    persist(QStringLiteral("view/defaultGridVisible"), value);
    return true;
}

bool ApplicationPreferences::setPreciseTurnDefaultDegrees(double value)
{
    const double normalized = std::round(std::clamp(value, 5.0, 180.0));
    if (qFuzzyCompare(m_preciseTurnDefaultDegrees, normalized)) return false;
    m_preciseTurnDefaultDegrees = normalized;
    persist(QStringLiteral("interaction/preciseTurnDegrees"), normalized);
    return true;
}

bool ApplicationPreferences::setTelemetryRecordingHz(int value)
{
    if (!QList<int>{2, 5, 10}.contains(value) || m_telemetryRecordingHz == value) return false;
    m_telemetryRecordingHz = value;
    persist(QStringLiteral("recording/telemetryHz"), value);
    return true;
}

bool ApplicationPreferences::setAutoGenerateReports(bool value)
{
    if (m_autoGenerateReports == value) return false;
    m_autoGenerateReports = value;
    persist(QStringLiteral("recording/autoGenerateReports"), value);
    return true;
}

bool ApplicationPreferences::setTrashRetentionDays(int value)
{
    if (!QList<int>{0, 7, 30, 90}.contains(value) || m_trashRetentionDays == value) return false;
    m_trashRetentionDays = value;
    persist(QStringLiteral("recording/trashRetentionDays"), value);
    return true;
}


QVariantMap ApplicationPreferences::exportDocument(const QVariantMap &flightPath) const
{
    QSettings defaultSettings;
    QSettings &settings = m_storage ? *m_storage : defaultSettings;
    QVariantMap document;
    document.insert(QStringLiteral("format"), QStringLiteral("FlightBenchSettings"));
    document.insert(QStringLiteral("formatVersion"), 1);
    document.insert(QStringLiteral("exportedAt"),
                    QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    document.insert(QStringLiteral("appearance"), QVariantMap{
        {QStringLiteral("mode"), settings.value(QStringLiteral("Appearance/mode"),
                                                 QStringLiteral("light"))},
        {QStringLiteral("textScalePercent"),
         settings.value(QStringLiteral("Appearance/textScalePercent"), 100)},
        {QStringLiteral("reducedMotion"),
         settings.value(QStringLiteral("Appearance/reducedMotion"), false)}});
    document.insert(QStringLiteral("interaction"), QVariantMap{
        {QStringLiteral("preciseTurnDegrees"), preciseTurnDefaultDegrees()},
        {QStringLiteral("cameraRotationSensitivity"), cameraRotationSensitivity()},
        {QStringLiteral("cameraZoomSensitivity"), cameraZoomSensitivity()},
        {QStringLiteral("invertCameraVertical"), invertCameraVertical()}});
    document.insert(QStringLiteral("view"), QVariantMap{
        {QStringLiteral("groundMarkerVisible"), groundMarkerVisible()},
        {QStringLiteral("groundMarkerSize"), groundMarkerSizeMode()},
        {QStringLiteral("defaultCameraMode"), defaultCameraMode()},
        {QStringLiteral("buildingOpacity"), cityBuildingOpacity()},
        {QStringLiteral("roadsVisible"), worldRoadsVisible()},
        {QStringLiteral("greenAreasVisible"), worldGreenAreasVisible()},
        {QStringLiteral("waterVisible"), worldWaterVisible()},
        {QStringLiteral("defaultGridVisible"), defaultGridVisible()}});
    document.insert(QStringLiteral("flightPath"), flightPath);
    document.insert(QStringLiteral("recording"), QVariantMap{
        {QStringLiteral("telemetryHz"), telemetryRecordingHz()},
        {QStringLiteral("autoGenerateReports"), autoGenerateReports()},
        {QStringLiteral("trashRetentionDays"), trashRetentionDays()}});
    return document;
}

QVariantMap ApplicationPreferences::decodeDocument(const QByteArray &bytes, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    const QVariantMap document = json.object().toVariantMap();
    if (parseError.error != QJsonParseError::NoError
        || document.value(QStringLiteral("format")).toString() != QStringLiteral("FlightBenchSettings")
        || document.value(QStringLiteral("formatVersion")).toInt() != 1) {
        if (error) *error = QStringLiteral("这不是可用的 FlightBench 设置文件");
        return {};
    }
    if (error) error->clear();
    return document;
}

QString ApplicationPreferences::writeDocument(const QVariantMap &document,
                                              const QString &directory, QString *error)
{
    if (!QDir().mkpath(directory)) {
        if (error) *error = QStringLiteral("无法创建设置导出目录");
        return {};
    }
    const QString path = QDir(directory).filePath(QStringLiteral("flightbench-settings.json"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject::fromVariantMap(document))
                          .toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        if (error) *error = QStringLiteral("设置导出失败");
        return {};
    }
    if (error) error->clear();
    return QDir::toNativeSeparators(path);
}
