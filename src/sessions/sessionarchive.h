#pragma once
#include <QString>
#include <QVariantMap>
#include <QVariantList>

// On-disk catalog for the current session format. No active flight/timers/signals.
namespace SessionArchive {
bool saveMetadata(QVariantMap &metadata, const QString &folderPath);
QVariantList loadCompleted(const QString &directory);
QVariantList loadTrashed(const QString &directory);
}
