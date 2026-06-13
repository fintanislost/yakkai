#pragma once

#include <QtCore/QString>

namespace yakkai::harness
{

struct LayerVisibilityOverrideOptionResult
{
    bool valid = false;
    QString normalized;
    QString error;
};

LayerVisibilityOverrideOptionResult validateLayerVisibilityOverrideOption(const QString& value);

}
