#pragma once

#include <string>
#include <string_view>

namespace wallpaper::policy {

enum class SceneScriptRuntimeGapKind {
    Visible,
    Harmless,
    MediaRuntimeOnly,
    Unknown,
};

struct SceneScriptRuntimeGap {
    SceneScriptRuntimeGapKind kind { SceneScriptRuntimeGapKind::Unknown };
    std::string api { "unknown" };
    std::string reason { "unclassified-runtime-gap" };
};

std::string sanitizeSceneScriptModule(std::string_view script);
std::string sceneScriptRuntimeStubSource();
std::string sceneScriptRuntimeGapKindText(SceneScriptRuntimeGapKind kind);
SceneScriptRuntimeGap classifySceneScriptRuntimeGap(std::string_view message,
                                                    std::string_view stack);

} // namespace wallpaper::policy
