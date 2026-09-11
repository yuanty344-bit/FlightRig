#pragma once

#include <QVariantMap>

class AcceptanceEvaluator final
{
public:
    [[nodiscard]] static QVariantMap evaluate(const QVariantMap &session);
};
