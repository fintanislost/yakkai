#include "WPImageObject.h"
#include "Utils/Logging.h"
#include "Fs/VFS.h"
#include "Core/StringHelper.hpp"

using namespace wallpaper::wpscene;

namespace
{
bool StringContainsAnyToken(std::string_view value,
                            std::initializer_list<std::string_view> tokens) {
    for (std::string_view token : tokens) {
        if (value.find(token) != std::string_view::npos) return true;
    }
    return false;
}

bool IsUtilityUiImagePath(std::string_view path) {
    return path == "models/util/solidlayer.json" || path == "models/util/projectlayer.json" ||
           path == "models/util/fullscreenlayer.json" || wallpaper::sstart_with(path, "models/workshop/");
}

bool PropertyRequiresUnsupportedMediaIntegrationRuntime(const nlohmann::json& value) {
    if (! value.is_object()) return false;

    auto scriptIt = value.find("script");
    if (scriptIt != value.end() && scriptIt->is_string()) {
        const auto& script = scriptIt->get_ref<const std::string&>();
        if (StringContainsAnyToken(script,
                                   {
                                       "shared.mi",
                                       "mediaintegration",
                                       "MediaPlaybackEvent",
                                       "mediaThumbnailChanged",
                                       "mediaTimelineChanged",
                                       "mediaPlaybackChanged",
                                       "cursorClick",
                                       "localStorage",
                                       "getTextureAnimation(",
                                   })) {
            return true;
        }
    }

    auto userIt = value.find("user");
    if (userIt != value.end() && userIt->is_string()) {
        const auto& user = userIt->get_ref<const std::string&>();
        if (wallpaper::sstart_with(user, "mediaintegration")) return true;
    }

    return false;
}

bool JsonRequiresUnsupportedMediaIntegrationRuntime(const nlohmann::json& value) {
    if (PropertyRequiresUnsupportedMediaIntegrationRuntime(value)) {
        return true;
    }

    if (value.is_object()) {
        for (const auto& item : value.items()) {
            if (JsonRequiresUnsupportedMediaIntegrationRuntime(item.value())) {
                return true;
            }
        }
        return false;
    }

    if (value.is_array()) {
        for (const auto& item : value) {
            if (JsonRequiresUnsupportedMediaIntegrationRuntime(item)) {
                return true;
            }
        }
        return false;
    }

    if (value.is_string()) {
        const auto& str = value.get_ref<const std::string&>();
        return StringContainsAnyToken(str,
                                      {
                                          "$mediaThumbnail",
                                          "$mediaPreviousThumbnail",
                                          "MediaPlaybackEvent",
                                          "mediaThumbnailChanged",
                                          "mediaTimelineChanged",
                                          "mediaPlaybackChanged",
                                          "shared.mi",
                                      });
    }

    return false;
}

bool RequiresUnsupportedMediaIntegrationRuntime(const nlohmann::json& json, std::string_view imagePath) {
    if (! IsUtilityUiImagePath(imagePath)) return false;

    return JsonRequiresUnsupportedMediaIntegrationRuntime(json);
}
} // namespace


bool WPEffectCommand::FromJson(const nlohmann::json& json) {
    GET_JSON_NAME_VALUE(json, "command", command);
    GET_JSON_NAME_VALUE(json, "target", target);
    GET_JSON_NAME_VALUE(json, "source", source);
    return true;
}

bool WPEffectFbo::FromJson(const nlohmann::json& json) {
    GET_JSON_NAME_VALUE(json, "name", name);
    GET_JSON_NAME_VALUE(json, "format", format);

    GET_JSON_NAME_VALUE(json, "scale", scale);
    if(scale == 0) { 
        LOG_ERROR("fbo scale can't be 0");
        scale = 1;
    }
    return true;
}

// Define and initialize the static property
const std::unordered_set<std::string> WPImageEffect::BLACKLISTED_WORKSHOP_EFFECTS = 
{
    "2799421411" // Audio Responsive Oscilloscope   --  causes vulcan deadlock
};


bool WPImageEffect::IsEffectBlacklisted(const std::string& filePath) {
    
    std::filesystem::path path(filePath);
    // Check if the path has a parent path
    if (path.has_parent_path()) {
        path = path.parent_path();
        if(path.has_parent_path()) {
            std::string effectId = path.parent_path().filename().string();
            std::string parentPath = path.parent_path().string();
            return WPImageEffect::BLACKLISTED_WORKSHOP_EFFECTS.find(effectId) != WPImageEffect::BLACKLISTED_WORKSHOP_EFFECTS.end();
        }
    }
    return false;
}
    
bool WPImageEffect::FromJson(const nlohmann::json& json, fs::VFS& vfs) {
    std::string filePath;
    GET_JSON_NAME_VALUE(json, "file", filePath);
    GET_JSON_NAME_VALUE_NOWARN(json, "visible", visible);
    if(this->IsEffectBlacklisted(filePath)) {
        //hide blacklisted effects
        visible = false;
    }
	GET_JSON_NAME_VALUE_NOWARN(json, "id", id);
    nlohmann::json jEffect;
    if(!PARSE_JSON(fs::GetFileContent(vfs, "/assets/" + filePath), jEffect))
        return false;
    if(!FromFileJson(jEffect, vfs))
        return false;

    if(json.contains("passes")) {
        const auto& jPasses = json.at("passes");
        if(jPasses.size() > passes.size()) {
            LOG_ERROR("passes is not injective");
            return false;
        }
        int32_t i = 0;
        for(const auto& jP:jPasses) {
            WPMaterialPass pass;
            pass.FromJson(jP);
            passes[i++].Update(pass); 
        }
    }
    return true;
}

bool WPImageEffect::FromFileJson(const nlohmann::json& json, fs::VFS& vfs) {
	GET_JSON_NAME_VALUE_NOWARN(json, "version", version);
    GET_JSON_NAME_VALUE(json, "name", name);
    if(json.contains("fbos")) {
        for(auto& jF:json.at("fbos")) {
            WPEffectFbo fbo;
            fbo.FromJson(jF);
            fbos.push_back(std::move(fbo));
        }
    }
    if(json.contains("passes")) {
        const auto& jEPasses = json.at("passes");
        bool compose {false};
        for(const auto& jP:jEPasses) {
            if(!jP.contains("material")) {
                if(jP.contains("command")) {
                    WPEffectCommand cmd;
                    cmd.FromJson(jP);
                    cmd.afterpos = passes.size();
                    commands.push_back(cmd);
                    continue;
                }
                LOG_ERROR("no material in effect pass");
                return false;
            }
            std::string matPath;
            GET_JSON_NAME_VALUE(jP, "material", matPath);
            nlohmann::json jMat;
            if(!PARSE_JSON(fs::GetFileContent(vfs, "/assets/" + matPath), jMat))
                return false;
            WPMaterial material;
            material.FromJson(jMat);
            materials.push_back(std::move(material));
            WPMaterialPass pass;
            pass.FromJson(jP);
            passes.push_back(std::move(pass));
            if(jP.contains("compose"))
	            GET_JSON_NAME_VALUE(jP, "compose", compose);
        }
        if(compose) {
            if(passes.size() != 2) {
                LOG_ERROR("effect compose option error");
                return false;
            }
            WPEffectFbo fbo; {fbo.name = "_rt_FullCompoBuffer1"; fbo.scale = 1;}
            fbos.push_back(fbo);
            passes.at(0).bind.push_back({ "previous", 0});
            passes.at(0).target = "_rt_FullCompoBuffer1";
            passes.at(1).bind.push_back({"_rt_FullCompoBuffer1", 0});
        }
    } else {
        LOG_ERROR("no passes in effect file");
        return false;
    }
    return true;
}

bool WPImageObject::FromJson(const nlohmann::json& json, fs::VFS& vfs) {
    GET_JSON_NAME_VALUE(json, "image", image);
    GET_JSON_NAME_VALUE_NOWARN(json, "visible", visible);
    GET_JSON_NAME_VALUE_NOWARN(json, "alignment", alignment);
    nlohmann::json jImage;
    if(!PARSE_JSON(fs::GetFileContent(vfs, "/assets/" + image), jImage)) {
        LOG_ERROR("Can't load image json: %s", image.c_str());
        return false;
    }
    GET_JSON_NAME_VALUE_NOWARN(jImage, "fullscreen", fullscreen);
	GET_JSON_NAME_VALUE_NOWARN(json, "name", name);
	GET_JSON_NAME_VALUE_NOWARN(json, "id", id);
	GET_JSON_NAME_VALUE_NOWARN(json, "parent", parent);
	GET_JSON_NAME_VALUE_NOWARN(json, "colorBlendMode", colorBlendMode);
    if (RequiresUnsupportedMediaIntegrationRuntime(json, image)) {
        visible = false;
        LOG_INFO("suppressing unsupported media integration image layer: name=%s id=%d image=%s",
                 name.c_str(),
                 id,
                 image.c_str());
        return true;
    }
	if(!fullscreen) {
		GET_JSON_NAME_VALUE(json, "origin", origin);	
		GET_JSON_NAME_VALUE(json, "angles", angles);	
		GET_JSON_NAME_VALUE(json, "scale", scale);	
		GET_JSON_NAME_VALUE_NOWARN(json, "parallaxDepth", parallaxDepth);
		if(jImage.contains("width")) {
			int32_t w,h;
			GET_JSON_NAME_VALUE(jImage, "width", w);	
			GET_JSON_NAME_VALUE(jImage, "height", h);	
			size = {(float)w, (float)h};
		} else if(json.contains("size")) {
			GET_JSON_NAME_VALUE(json, "size", size);	
		} else {
			size = {origin.at(0)*2, origin.at(1)*2};
		}
    }
    GET_JSON_NAME_VALUE_NOWARN(jImage, "nopadding", nopadding);
    GET_JSON_NAME_VALUE_NOWARN(json, "color", color);
    GET_JSON_NAME_VALUE_NOWARN(json, "alpha", alpha);
    GET_JSON_NAME_VALUE_NOWARN(json, "brightness", brightness);

	GET_JSON_NAME_VALUE_NOWARN(jImage, "puppet", puppet);	
    if(jImage.contains("material")) {
        std::string matPath;
		GET_JSON_NAME_VALUE(jImage, "material", matPath);	
        nlohmann::json jMat;
        if(!PARSE_JSON(fs::GetFileContent(vfs, "/assets/" + matPath), jMat)) {
            LOG_ERROR("Can't load material json: %s", matPath.c_str());
            return false;
        }
        material.FromJson(jMat);
    } else {
        LOG_INFO("image object no material");
        return false;
    }
    if(json.contains("effects")) {
        for(const auto& jE:json.at("effects")) {
            WPImageEffect wpeff;
            wpeff.FromJson(jE, vfs);
            effects.push_back(std::move(wpeff));
        }
    }
    if(json.contains("animationlayers")) {
        for(const auto& jLayer:json.at("animationlayers")) {
             WPPuppetLayer::AnimationLayer layer;
             GET_JSON_NAME_VALUE(jLayer, "animation", layer.id);
             GET_JSON_NAME_VALUE_NOWARN(jLayer, "name", layer.name);
             GET_JSON_NAME_VALUE(jLayer, "blend", layer.blend);
             GET_JSON_NAME_VALUE(jLayer, "rate", layer.rate);
             GET_JSON_NAME_VALUE_NOWARN(jLayer, "visible", layer.visible);
             puppet_layers.push_back(layer);
        }
    }
    if(json.contains("config")) {
        const auto& jConf = json.at("config");
        GET_JSON_NAME_VALUE_NOWARN(jConf, "passthrough", config.passthrough);
    }
    return true;
}
