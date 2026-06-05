#pragma once

#include <QtCore/QString>

namespace yakkai::harness
{

struct PuppetSimulationOptionResult
{
    bool valid = false;
    QString normalized;
    QString error;
};

PuppetSimulationOptionResult validatePuppetSimulationOption(const QString& value);

}
