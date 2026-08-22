#pragma once

#include <QVariantMap>

class PreflightEvaluator final
{
public:
    [[nodiscard]] static QVariantMap evaluate(const QVariantMap &plan,
                                              const QVariantMap &route,
                                              const QVariantMap &worldMap,
                                              bool dockerAvailable,
                                              bool imageAvailable,
                                              bool modelAvailable,
                                              bool worldAvailable,
                                              bool previewAvailable,
                                              bool containerNameValid);
};
