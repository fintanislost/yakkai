#pragma once

#include <cstdint>

namespace wallpaper::policy {

struct VideoTextureInput {
    std::int64_t sourceSize { 0 };
    std::int64_t expectedRawSize { 0 };
    bool hasVideoMagic { false };
    int decodedWidth { 0 };
};

struct VideoTextureDecision {
    bool shouldAttemptDecode { false };
    bool enablePlayback { false };
};

VideoTextureDecision decideVideoTexturePolicy(const VideoTextureInput& input);

} // namespace wallpaper::policy
