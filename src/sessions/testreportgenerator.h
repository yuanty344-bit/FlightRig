#pragma once

#include <QString>
#include <QVariantMap>

class TestReportGenerator final
{
public:
    [[nodiscard]] static QString generate(const QVariantMap &session,
                                          const QString &outputDirectory,
                                          QString *errorMessage = nullptr);
};
