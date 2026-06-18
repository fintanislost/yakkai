#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

namespace wallpaper::policy {

enum class MediaIntegrationSupportKind {
    None,
    SupportedWidget,
    DeferredRuntime,
};

struct MediaIntegrationSupport {
    MediaIntegrationSupportKind kind { MediaIntegrationSupportKind::None };
    std::string reason;
    bool timelineDrivenSolidLayer { false };
};

MediaIntegrationSupport ClassifyMediaIntegrationSupport(const nlohmann::json& objectJson,
                                                        std::string_view imagePath);

} // namespace wallpaper::policy
