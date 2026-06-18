#pragma once

#include <QString>

namespace yakkai::harness {

struct MediaStateTimelineOptionResult {
    bool valid = true;
    bool hasTimeline = false;
    QString normalized;
    QString error;
};

MediaStateTimelineOptionResult validateMediaStateTimelineOption(const QString& raw);

} // namespace yakkai::harness
