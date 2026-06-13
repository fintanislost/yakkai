#pragma once

#include <QtCore/QString>

namespace yakkai::harness
{

struct InteractiveMouseOptionResult
{
    bool valid = false;
    QString error;
};

InteractiveMouseOptionResult validateInteractiveMouseOption(bool interactiveMouseRequested,
                                                            bool debugMousePositionRequested,
                                                            bool debugMouseTimelineRequested);

}
