#include "Policy/VideoTexturePolicy.hpp"

namespace wallpaper::policy {

VideoTextureDecision decideVideoTexturePolicy(const VideoTextureInput& input)
{
    VideoTextureDecision decision;
    const bool sizeMismatch = input.sourceSize != input.expectedRawSize &&
                              input.sourceSize > input.expectedRawSize;
    decision.shouldAttemptDecode = sizeMismatch || input.hasVideoMagic;
    decision.enablePlayback = decision.shouldAttemptDecode && input.decodedWidth >= 1920;
    return decision;
}

} // namespace wallpaper::policy
