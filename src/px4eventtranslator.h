#pragma once

#include <QString>
#include <QVariantMap>

class Px4EventTranslator final
{
public:
    [[nodiscard]] static QVariantMap explain(const QString &issueCode,
                                             const QString &technicalMessage,
                                             const QVariantMap &health);
};
