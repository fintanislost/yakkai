#pragma once

#include <QtCore/QString>

namespace yakkai::harness
{

struct ScenePropertiesJsonOptionResult
{
    bool valid = false;
    QString normalized;
    QString error;
};

ScenePropertiesJsonOptionResult validateScenePropertiesJsonOption(const QString& value);

}
