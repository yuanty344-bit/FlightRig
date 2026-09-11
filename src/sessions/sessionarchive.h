#pragma once
#include <QString>
#include <QVariantMap>
#include <QVariantList>

// On-disk catalog and legacy metadata repair. No active flight/timers/signals.
// Loading preserves the existing repair writes; it is not a read-only API.
namespace SessionArchive {
bool saveMetadata(QVariantMap &metadata, const QString &folderPath);
QVariantList loadCompleted(const QString &directory);
QVariantList loadTrashed(const QString &directory);
}
