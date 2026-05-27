#pragma once
#include "Debug/EffectCaptureDebug.hpp"
#include "Interface/ISceneParser.h"
#include <random>
#include <string>
#include <utility>

namespace wallpaper
{

class WPSceneParser : public ISceneParser {
public:
    WPSceneParser()  = default;
    ~WPSceneParser() = default;
    void SetScenePropertiesJson(std::string value) { m_scene_properties_json = std::move(value); }
    void SetDebugEffectCaptureConfig(wallpaper::debug::EffectCaptureConfig value)
    {
        m_debug_effect_captures = std::move(value);
    }
    std::shared_ptr<Scene> Parse(std::string_view scene_id, const std::string&, fs::VFS&, audio::SoundManager&) override;

private:
    std::string m_scene_properties_json;
    wallpaper::debug::EffectCaptureConfig m_debug_effect_captures;
};
} // namespace wallpaper
