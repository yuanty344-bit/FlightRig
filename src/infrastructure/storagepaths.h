#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace StoragePaths {

inline QString rootDirectory()
{
    const QString overridePath = qEnvironmentVariable("FLIGHTBENCH_DATA_ROOT").trimmed();
    if (!overridePath.isEmpty()) return QDir::cleanPath(overridePath);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("FlightRig"));
}

inline QString directory(const QString &name)
{
    return QDir(rootDirectory()).filePath(name);
}

} // namespace StoragePaths
