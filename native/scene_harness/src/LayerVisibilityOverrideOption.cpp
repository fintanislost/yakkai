#include "LayerVisibilityOverrideOption.hpp"

#include <QtCore/QStringList>

#include <algorithm>
#include <vector>

namespace yakkai::harness
{

namespace
{

struct OverrideRule
{
    int layerId = 0;
    bool visible = false;
};

bool parsePositiveLayerId(const QString& value, int* layerId)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed <= 0) {
        return false;
    }
    *layerId = parsed;
    return true;
}

bool parseBoolValue(const QString& value, bool* visible)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("true")) {
        *visible = true;
        return true;
    }
    if (normalized == QStringLiteral("false")) {
        *visible = false;
        return true;
    }
    return false;
}

}

LayerVisibilityOverrideOptionResult validateLayerVisibilityOverrideOption(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return LayerVisibilityOverrideOptionResult{true, QString(), QString()};
    }

    std::vector<OverrideRule> rules;
    const QStringList parts = trimmed.split(',', Qt::KeepEmptyParts);
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed();
        const QStringList pair = part.split(':', Qt::KeepEmptyParts);
        if (pair.size() != 2) {
            return LayerVisibilityOverrideOptionResult{
                false,
                QString(),
                QStringLiteral("--debug-layer-visibility-overrides must use layerId:true|false entries")
            };
        }

        int layerId = 0;
        bool visible = false;
        if (!parsePositiveLayerId(pair.at(0).trimmed(), &layerId) ||
            !parseBoolValue(pair.at(1), &visible)) {
            return LayerVisibilityOverrideOptionResult{
                false,
                QString(),
                QStringLiteral("--debug-layer-visibility-overrides must use positive numeric layer ids and true|false values")
            };
        }

        auto existing = std::find_if(rules.begin(), rules.end(), [layerId](const OverrideRule& rule) {
            return rule.layerId == layerId;
        });
        if (existing == rules.end()) {
            rules.push_back(OverrideRule{layerId, visible});
        } else {
            existing->visible = visible;
        }
    }

    QStringList normalized;
    for (const OverrideRule& rule : rules) {
        normalized.push_back(QStringLiteral("%1:%2")
            .arg(rule.layerId)
            .arg(rule.visible ? QStringLiteral("true") : QStringLiteral("false")));
    }

    return LayerVisibilityOverrideOptionResult{true, normalized.join(QLatin1Char(',')), QString()};
}

}
