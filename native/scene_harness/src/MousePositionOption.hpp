#pragma once

#include <QtCore/QString>

namespace yakkai::harness
{

struct MousePositionOptionResult
{
    bool valid = false;
    bool hasPosition = false;
    double x = 0.5;
    double y = 0.5;
    QString normalized;
    QString error;
};

MousePositionOptionResult validateMousePositionOption(const QString& value);

}
