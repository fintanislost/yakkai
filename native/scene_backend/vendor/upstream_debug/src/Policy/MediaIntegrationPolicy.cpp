#include "Policy/MediaIntegrationPolicy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace wallpaper::policy {
namespace {

std::string AsciiLower(std::string_view value)
{
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

bool StartsWith(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool IsUtilityUiImagePath(std::string_view path)
{
    return path == "models/util/solidlayer.json" ||
           path == "models/util/projectlayer.json" ||
           path == "models/util/fullscreenlayer.json" ||
           StartsWith(path, "models/workshop/");
}

void CollectStrings(const nlohmann::json& value, std::vector<std::string>& strings)
{
    if (value.is_string()) {
        strings.push_back(value.get<std::string>());
        return;
    }
    if (value.is_object()) {
        for (const auto& item : value.items()) {
            strings.push_back(item.key());
            CollectStrings(item.value(), strings);
        }
        return;
    }
    if (value.is_array()) {
        for (const auto& item : value) {
            CollectStrings(item, strings);
        }
    }
}

bool ContainsAnyToken(const std::vector<std::string>& values,
                      const std::vector<std::string_view>& tokens,
                      std::string* matchedToken)
{
    for (const std::string& value : values) {
        const std::string loweredValue = AsciiLower(value);
        for (std::string_view token : tokens) {
            const std::string loweredToken = AsciiLower(token);
            if (loweredValue.find(loweredToken) != std::string::npos) {
                if (matchedToken) {
                    *matchedToken = std::string(token);
                }
                return true;
            }
        }
    }
    return false;
}

bool FieldContainsAnyToken(const nlohmann::json& objectJson,
                           std::string_view field,
                           const std::vector<std::string_view>& tokens,
                           std::string* matchedToken)
{
    if (!objectJson.is_object()) {
        return false;
    }
    const auto fieldIt = objectJson.find(std::string(field));
    if (fieldIt == objectJson.end()) {
        return false;
    }

    std::vector<std::string> strings;
    CollectStrings(*fieldIt, strings);
    return ContainsAnyToken(strings, tokens, matchedToken);
}

} // namespace

MediaIntegrationSupport ClassifyMediaIntegrationSupport(const nlohmann::json& objectJson,
                                                        std::string_view imagePath)
{
    MediaIntegrationSupport support;
    if (!IsUtilityUiImagePath(imagePath)) {
        return support;
    }

    std::vector<std::string> strings;
    CollectStrings(objectJson, strings);

    static const std::vector<std::string_view> timelineTokens {
        "mediaTimelineChanged",
    };
    const bool isTimelineDrivenSolidLayer =
        imagePath == "models/util/solidlayer.json" &&
        FieldContainsAnyToken(objectJson, "scale", timelineTokens, nullptr);

    static const std::vector<std::string_view> hardDeferredTokens {
        "$audio",
        "audioresponsive",
        "audio_response",
        "getTextureAnimation(",
        "cursorClick",
    };
    std::string matched;
    if (ContainsAnyToken(strings, hardDeferredTokens, &matched)) {
        support.kind = MediaIntegrationSupportKind::DeferredRuntime;
        support.reason = "deferred-runtime-token:" + matched;
        return support;
    }

    static const std::vector<std::string_view> audioBufferTokens {
        "registerAudioBuffers",
    };
    const bool hasAudioBuffer = ContainsAnyToken(strings, audioBufferTokens, nullptr);

    static const std::vector<std::string_view> mediaWidgetTokens {
        "shared.mi",
        "engine.media",
        "mediaintegration",
        "MediaPlaybackEvent",
        "mediaTimelineChanged",
        "mediaPlaybackChanged",
        "mediaThumbnailChanged",
        "$mediaThumbnail",
        "$mediaPreviousThumbnail",
    };
    if (ContainsAnyToken(strings, mediaWidgetTokens, &matched)) {
        support.kind = MediaIntegrationSupportKind::SupportedWidget;
        support.reason = hasAudioBuffer
            ? "supported-media-widget-token:" + matched + "+inert-audio-buffer"
            : "supported-media-widget-token:" + matched;
        support.timelineDrivenSolidLayer = isTimelineDrivenSolidLayer;
        return support;
    }

    if (hasAudioBuffer) {
        support.kind = MediaIntegrationSupportKind::DeferredRuntime;
        support.reason = "deferred-runtime-token:registerAudioBuffers";
        return support;
    }

    return support;
}

} // namespace wallpaper::policy
