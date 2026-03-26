#pragma once
#include "Interface/ISceneParser.h"
#include <random>
#include <string>

namespace wallpaper
{

class WPSceneParser : public ISceneParser {
public:
    WPSceneParser()  = default;
    ~WPSceneParser() = default;
    void SetScenePropertiesJson(std::string value) { m_scene_properties_json = std::move(value); }
    std::shared_ptr<Scene> Parse(std::string_view scene_id, const std::string&, fs::VFS&, audio::SoundManager&) override;

private:
    std::string m_scene_properties_json;
};
} // namespace wallpaper
