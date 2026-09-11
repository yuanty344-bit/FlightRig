#pragma once
#include <QFile>
#include <QVariantMap>

// Synchronous disk sink; sampling, quotas and failure policy stay in the service.
class SensorSessionStorage {
public:
    enum class OpenResult { Ready, DirectoryFailed, TimelineFailed };
    OpenResult open(const QString &sessionFolder);
    bool writeFrame(const QString &relativePath, const QByteArray &jpeg);
    bool append(const QByteArray &line);
    bool writeManifest(const QVariantMap &summary);
    void flush();
    void close();
    void clearDirectory() { m_directory.clear(); }
    QString directory() const { return m_directory; }
private:
    QString m_directory;
    QFile m_timeline;
};
