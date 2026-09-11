#include "sensors/sensorsessionstorage.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

SensorSessionStorage::OpenResult SensorSessionStorage::open(const QString &sessionFolder)
{
    m_directory = QDir(sessionFolder).filePath(QStringLiteral("sensor"));
    QDir directory(m_directory);
    if (!directory.mkpath(QStringLiteral("rgb")) || !directory.mkpath(QStringLiteral("depth"))) {
        m_directory.clear();
        return OpenResult::DirectoryFailed;
    }
    m_timeline.setFileName(directory.filePath(QStringLiteral("timeline.ndjson")));
    if (!m_timeline.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        m_directory.clear();
        return OpenResult::TimelineFailed;
    }
    return OpenResult::Ready;
}

bool SensorSessionStorage::writeFrame(const QString &relativePath, const QByteArray &jpeg)
{
    QFile frame(QDir(m_directory).filePath(relativePath));
    if (!frame.open(QIODevice::WriteOnly) || frame.write(jpeg) != jpeg.size()) return false;
    frame.close();
    return true;
}

bool SensorSessionStorage::append(const QByteArray &line)
{
    return m_timeline.write(line) == line.size();
}

void SensorSessionStorage::flush()
{
    if (m_timeline.isOpen()) m_timeline.flush();
}

void SensorSessionStorage::close()
{
    if (m_timeline.isOpen()) {
        m_timeline.flush();
        m_timeline.close();
    }
}

bool SensorSessionStorage::writeManifest(const QVariantMap &summary)
{
    if (m_directory.isEmpty()) return false;
    QSaveFile file(QDir(m_directory).filePath(QStringLiteral("manifest.json")));
    const QByteArray contents = QJsonDocument(QJsonObject::fromVariantMap(summary))
                                    .toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(contents) == contents.size() && file.commit();
}
