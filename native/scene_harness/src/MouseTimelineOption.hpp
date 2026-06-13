#pragma once

#include <QString>

namespace yakkai::harness {

struct MouseTimelineOptionResult {
    bool valid = true;
    bool hasTimeline = false;
    QString normalized;
    QString error;
};

MouseTimelineOptionResult validateMouseTimelineOption(const QString& raw);

} // namespace yakkai::harness
