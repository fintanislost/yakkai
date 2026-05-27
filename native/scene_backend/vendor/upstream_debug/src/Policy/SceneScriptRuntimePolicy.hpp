#pragma once

#include <string>
#include <string_view>

namespace wallpaper::policy {

std::string sanitizeSceneScriptModule(std::string_view script);
std::string sceneScriptRuntimeStubSource();

} // namespace wallpaper::policy
