#pragma once

#include <array>
#include <string>

#include <nlohmann/json.hpp>

namespace wallpaper {

struct SceneScriptMediaState {
    bool available { false };
    bool playing { false };
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtPath;
    double duration { 0.0 };
    double position { 0.0 };
    double settleSeconds { 0.0 };
    bool hasFixedClock { false };
    double fixedClockEpochMs { 0.0 };
    bool hasThumbnailColors { false };
    std::array<float, 3> textColor { 1.0f, 1.0f, 1.0f };
    std::array<float, 3> primaryColor { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> secondaryColor { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> tertiaryColor { 0.0f, 0.0f, 0.0f };
};

SceneScriptMediaState SceneScriptMediaStateFromSceneProperties(const nlohmann::json& sceneProperties);

} // namespace wallpaper
