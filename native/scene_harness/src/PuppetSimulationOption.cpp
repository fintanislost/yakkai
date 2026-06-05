#include "PuppetSimulationOption.hpp"

namespace yakkai::harness
{

PuppetSimulationOptionResult validatePuppetSimulationOption(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty() ||
        normalized == QStringLiteral("off") ||
        normalized == QStringLiteral("diagnostic") ||
        normalized == QStringLiteral("runtime")) {
        return PuppetSimulationOptionResult{true, normalized, QString()};
    }

    return PuppetSimulationOptionResult{
        false,
        QString(),
        QStringLiteral("--puppet-simulation must be off, diagnostic, or runtime")
    };
}

}
