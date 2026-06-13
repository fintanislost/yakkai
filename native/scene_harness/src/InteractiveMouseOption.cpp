#include "InteractiveMouseOption.hpp"

namespace yakkai::harness
{

InteractiveMouseOptionResult validateInteractiveMouseOption(bool interactiveMouseRequested,
                                                            bool debugMousePositionRequested,
                                                            bool debugMouseTimelineRequested)
{
    if (!interactiveMouseRequested) {
        return InteractiveMouseOptionResult{true, QString()};
    }
    if (debugMousePositionRequested) {
        return InteractiveMouseOptionResult{
            false,
            QStringLiteral("--interactive-mouse cannot be combined with --debug-mouse-position")
        };
    }
    if (debugMouseTimelineRequested) {
        return InteractiveMouseOptionResult{
            false,
            QStringLiteral("--interactive-mouse cannot be combined with --debug-mouse-timeline")
        };
    }
    return InteractiveMouseOptionResult{true, QString()};
}

}
