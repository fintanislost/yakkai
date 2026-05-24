#include "WPSceneParser.hpp"
#include "WPJson.hpp"
#include "WPSceneScript.hpp"
#include <sstream>

#include "Utils/String.h"
#include "Utils/Logging.h"
#include "Utils/Algorism.h"
#include "Core/Visitors.hpp"
#include "Core/StringHelper.hpp"
#include "Core/ArrayHelper.hpp"
#include "SpecTexs.hpp"

#include "WPShaderParser.hpp"
#include "WPTexImageParser.hpp"
#include "WPParticleParser.hpp"
#include "WPSoundParser.hpp"
#include "WPMdlParser.hpp"

#include "Particle/WPParticleRawGener.h"
#include "Particle/ParticleSystem.h"

#include "WPShaderValueUpdater.hpp"
#include "wpscene/WPImageObject.h"
#include "wpscene/WPParticleObject.h"
#include "wpscene/WPSoundObject.h"
#include "wpscene/WPLightObject.hpp"
#include "wpscene/WPModelObject.h"
#include "wpscene/WPScene.h"

#include "Fs/VFS.h"
#include "Utils/Eigen.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <cmath>
#include <functional>
#include <regex>
#include <variant>
#include <Eigen/Dense>

using namespace wallpaper;
using namespace Eigen;

std::string getAddr(void* p) { return std::to_string(reinterpret_cast<intptr_t>(p)); }

struct CameraPose {
    std::array<float, 3> eye { 0.0f, 0.0f, 1.0f };
    std::array<float, 3> center { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> up { 0.0f, 1.0f, 0.0f };
};

struct ParseContext {
    std::shared_ptr<Scene> scene;
    WPShaderValueUpdater*  shader_updater;
    i32                    ortho_w;
    i32                    ortho_h;
    fs::VFS*               vfs;
    std::unordered_map<int32_t, std::shared_ptr<SceneNode>> object_nodes;
    std::unordered_map<int32_t, std::vector<std::shared_ptr<SceneNode>>> deferred_children;
    std::unordered_map<std::string, std::unordered_set<std::string>> paused_puppet_animations;

    // Scene type detection — determines which rendering pipeline is used.
    // Detected during object scan before rendering begins.
    enum class SceneType { Standard, Puppet, Video };
    SceneType scene_type { SceneType::Standard };
    bool has_puppet_objects { false };
    bool has_video_textures { false };
    int  puppet_parse_successes { 0 };

    ShaderValueMap             global_base_uniforms;
    std::shared_ptr<SceneNode> effect_camera_node;
    std::shared_ptr<SceneNode> global_camera_node;
    std::shared_ptr<SceneNode> global_perspective_camera_node;
    CameraPose                scene_perspective_pose;
    bool                      has_scene_perspective_pose { false };

    // Color tint overlays resolved from scene properties.
    // Populated from project.json colour/opacity pairs during init.
    struct ColorTintOverlay {
        std::array<float, 3> color;
        float                alpha;
    };
    std::vector<ColorTintOverlay> pending_tint_overlays;

    // Composite tint color derived from the overlay properties.
    // Applied to all non-solid layers' g_Color4 to simulate WE's
    // script-driven color tinting of grayscale textures.
    std::array<float, 3> composite_tint { 1.0f, 1.0f, 1.0f };

    // Script-resolved color/alpha bindings per object ID.
    // Populated by pre-scanning scene JSON for thisLayer.color/alpha scripts.
    struct ScriptColorBinding {
        std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
        float                alpha { 1.0f };
        std::array<float, 3> origin { 0.0f, 0.0f, 0.0f };
        bool                 has_color { false };
        bool                 has_alpha { false };
        bool                 has_origin { false };
    };
    std::unordered_map<int32_t, ScriptColorBinding> script_color_bindings;

    // Container objects (no image/particle) whose conditional visibility is false.
    // Child objects with a parent in this set should be hidden.
    std::unordered_set<int32_t> hidden_containers;
    // All container object IDs (visible or not) — used to reparent orphaned children.
    std::unordered_set<int32_t> all_containers;
};

struct WPSolidAnchorObject {
    bool FromJson(const nlohmann::json& json, fs::VFS&) {
        GET_JSON_NAME_VALUE_NOWARN(json, "id", id);
        GET_JSON_NAME_VALUE_NOWARN(json, "parent", parent);
        GET_JSON_NAME_VALUE_NOWARN(json, "name", name);
        GET_JSON_NAME_VALUE_NOWARN(json, "origin", origin);
        GET_JSON_NAME_VALUE_NOWARN(json, "angles", angles);
        GET_JSON_NAME_VALUE_NOWARN(json, "scale", scale);
        GET_JSON_NAME_VALUE_NOWARN(json, "visible", visible);
        return true;
    }

    int32_t              id { 0 };
    int32_t              parent { 0 };
    std::string          name;
    std::array<float, 3> origin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> scale { 1.0f, 1.0f, 1.0f };
    std::array<float, 3> angles { 0.0f, 0.0f, 0.0f };
    bool                 visible { true };
};

using WPObjectVar = std::variant<wpscene::WPImageObject, wpscene::WPParticleObject,
                                 wpscene::WPSoundObject, wpscene::WPLightObject,
                                 wpscene::WPModelObject, WPSolidAnchorObject>;

namespace
{
nlohmann::json LoadScenePropertiesFromProjectJsonSource(const std::string& projectJsonSource,
                                                        const char*        sourceLabel) {
    nlohmann::json projectJson;
    if (! PARSE_JSON(projectJsonSource, projectJson) || ! projectJson.is_object()) {
        return nlohmann::json::object();
    }

    const auto generalIt = projectJson.find("general");
    if (generalIt == projectJson.end() || ! generalIt->is_object()) {
        return nlohmann::json::object();
    }

    const auto propertiesIt = generalIt->find("properties");
    if (propertiesIt == generalIt->end() || ! propertiesIt->is_object()) {
        return nlohmann::json::object();
    }

    LOG_INFO("scene property defaults loaded from %s: count=%zu",
             sourceLabel,
             propertiesIt->size());
    return *propertiesIt;
}

nlohmann::json NormalizeScenePropertiesForNativeFallback(nlohmann::json properties) {
    if (! properties.is_object()) {
        return nlohmann::json::object();
    }

    auto it = properties.find("loadingintro");
    if (it != properties.end() && it->is_object() && it->contains("value")) {
        const auto& value = it->at("value");
        const bool enabled = (value.is_boolean() && value.get<bool>()) ||
                             (value.is_number_integer() && value.get<int64_t>() != 0) ||
                             (value.is_number_unsigned() && value.get<uint64_t>() != 0) ||
                             (value.is_string() &&
                              (value.get<std::string>() == "1" ||
                               value.get<std::string>() == "true"));
        if (enabled) {
            (*it)["value"] = false;
            LOG_INFO("forcing loadingintro disabled for native scene fallback");
        }
    }

    return properties;
}

nlohmann::json ResolveSceneProperties(fs::VFS& vfs, const std::string& scenePropertiesJson) {
    if (! scenePropertiesJson.empty()) {
        nlohmann::json parsedProperties;
        if (PARSE_JSON(scenePropertiesJson, parsedProperties) && parsedProperties.is_object() &&
            ! parsedProperties.empty()) {
            return NormalizeScenePropertiesForNativeFallback(std::move(parsedProperties));
        }
    }

    if (vfs.Contains("/assets/project.json")) {
        return NormalizeScenePropertiesForNativeFallback(LoadScenePropertiesFromProjectJsonSource(
            fs::GetFileContent(vfs, "/assets/project.json"), "mounted project.json"));
    }

    return nlohmann::json::object();
}

void RegisterPuppetPauseDirectives(ParseContext& context, const nlohmann::json& value) {
    static const std::regex pauseRegex(
        R"REGEX(thisScene\.getLayer\("([^"]+)"\)\.getAnimationLayer\("([^"]+)"\))REGEX");

    if (value.is_object()) {
        auto scriptIt = value.find("script");
        if (scriptIt != value.end() && scriptIt->is_string()) {
            const auto& script = scriptIt->get_ref<const std::string&>();
            if (script.find(".pause()") != std::string::npos) {
                for (auto it = std::sregex_iterator(script.begin(), script.end(), pauseRegex);
                     it != std::sregex_iterator();
                     ++it) {
                    const std::string targetLayer = (*it)[1].str();
                    const std::string animationLayer = (*it)[2].str();
                    if (targetLayer.empty() || animationLayer.empty()) {
                        continue;
                    }
                    context.paused_puppet_animations[targetLayer].insert(animationLayer);
                    LOG_INFO("discovered native puppet pause directive: layer=%s animation=%s",
                             targetLayer.c_str(),
                             animationLayer.c_str());
                }
            }
        }

        for (const auto& item : value.items()) {
            RegisterPuppetPauseDirectives(context, item.value());
        }
        return;
    }

    if (value.is_array()) {
        for (const auto& item : value) {
            RegisterPuppetPauseDirectives(context, item);
        }
    }
}

bool HasDrawablePayload(const nlohmann::json& obj) {
    auto hasNonNull = [&obj](const char* key) {
        return obj.contains(key) && ! obj.at(key).is_null();
    };
    return hasNonNull("image") || hasNonNull("particle") || hasNonNull("sound") ||
           hasNonNull("light") || hasNonNull("model");
}

bool IsTransformAnchorObject(const nlohmann::json& obj) {
    if (HasDrawablePayload(obj)) {
        return false;
    }

    if (obj.contains("text") || obj.contains("font")) {
        return false;
    }

    if (! obj.contains("id")) {
        return false;
    }

    return obj.contains("origin") || obj.contains("angles") || obj.contains("scale") ||
           obj.contains("parent") || obj.contains("parallaxDepth") || obj.contains("solid");
}

float ClampUnit(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

std::string LowercaseCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool MaterialComboEnabled(const wpscene::WPMaterial& material, std::string_view comboName) {
    const std::string target = LowercaseCopy(std::string(comboName));
    for (const auto& combo : material.combos) {
        if (LowercaseCopy(combo.first) == target) {
            return combo.second != 0;
        }
    }
    return false;
}

bool IsUnsupportedWorkshopBokehParticle(std::string_view particlePath) {
    return particlePath == "particles/workshop/2820123291/bokeh_Hex.json" ||
           particlePath == "particles/workshop/2820123291/Bokeh_Cir.json";
}

bool DebugSkipLayerByName(std::string_view name) {
    const char* raw = std::getenv("YAKKAI_SCENE_DEBUG_SKIP_LAYERS");
    if (raw == nullptr || *raw == '\0' || name.empty()) {
        return false;
    }

    std::string_view haystack(raw);
    usize            start = 0;
    while (start < haystack.size()) {
        usize end = haystack.find(',', start);
        if (end == std::string_view::npos) {
            end = haystack.size();
        }

        std::string_view token = haystack.substr(start, end - start);
        while (! token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) {
            token.remove_prefix(1);
        }
        while (! token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) {
            token.remove_suffix(1);
        }

        if (! token.empty() && token == name) {
            return true;
        }
        start = end + 1;
    }

    return false;
}

std::string ResolveStaticFallbackDiffuseTexture(fs::VFS& vfs, std::string_view baseName) {
    static constexpr std::array<std::string_view, 5> rasterExts {
        ".png",
        ".tga",
        ".jpg",
        ".jpeg",
        ".bmp",
    };

    for (const auto ext : rasterExts) {
        const std::string candidate = std::string(baseName) + std::string(ext);
        if (vfs.Open("/assets/materials/" + candidate)) {
            LOG_INFO("model fallback using raster diffuse sidecar: base=%s resolved=%s",
                     std::string(baseName).c_str(),
                     candidate.c_str());
            return candidate;
        }
    }

    return std::string(baseName);
}

std::string DerivePuppetChannelMapMaterialPath(std::string_view imagePath) {
    const usize slash = imagePath.find_last_of('/');
    const usize stemStart = slash == std::string_view::npos ? 0 : slash + 1;
    const usize dot = imagePath.find_last_of('.');
    const usize stemEnd = dot == std::string_view::npos || dot <= stemStart ? imagePath.size() : dot;
    if (stemStart >= stemEnd) {
        return {};
    }

    return "materials/" + std::string(imagePath.substr(stemStart, stemEnd - stemStart)) +
           "_channelmap.json";
}

std::string InjectPuppetChannelMapSkinning(std::string src) {
    if (src.find("g_Bones[") != std::string::npos || src.find("a_BlendWeights") != std::string::npos) {
        return src;
    }

    constexpr std::string_view declBlock = "#if SKINNING\n"
                                           "uniform mat4x3 g_Bones[BONECOUNT];\n"
                                           "attribute vec4 a_BlendWeights;\n"
                                           "#endif\n";

    const std::regex blendIndexLine(R"((^[ \t]*attribute\s+uvec4\s+a_BlendIndices\s*;\s*$))",
                                    std::regex::ECMAScript | std::regex::multiline);
    if (std::regex_search(src, blendIndexLine)) {
        src = std::regex_replace(src,
                                 blendIndexLine,
                                 std::string("$1\n") + std::string(declBlock),
                                 std::regex_constants::format_first_only);
    } else if (const usize mainPos = src.find("void main("); mainPos != std::string::npos) {
        src.insert(mainPos, std::string(declBlock));
    }

    const std::regex glPosLine(
        R"((^[ \t]*)gl_Position\s*=\s*mul\s*\(\s*vec4\s*\(\s*a_Position\s*,\s*1\.0\s*\)\s*,\s*g_ModelViewProjectionMatrix\s*\)\s*;\s*$)",
        std::regex::ECMAScript | std::regex::multiline);
    if (std::regex_search(src, glPosLine)) {
        src = std::regex_replace(
            src,
            glPosLine,
            "$1#if SKINNING\n"
            "$1vec3 localPos = mul(vec4(a_Position, 1.0), g_Bones[a_BlendIndices.x] * a_BlendWeights.x +\n"
            "$1\t\t\t\t\tg_Bones[a_BlendIndices.y] * a_BlendWeights.y +\n"
            "$1\t\t\t\t\tg_Bones[a_BlendIndices.z] * a_BlendWeights.z +\n"
            "$1\t\t\t\t\tg_Bones[a_BlendIndices.w] * a_BlendWeights.w);\n"
            "$1#else\n"
            "$1vec3 localPos = a_Position;\n"
            "$1#endif\n"
            "$1gl_Position = mul(vec4(localPos, 1.0), g_ModelViewProjectionMatrix);",
            std::regex_constants::format_first_only);
    }

    return src;
}

std::string InjectGenericImage4ChannelMapAlphaMask(std::string src) {
    if (src.find("paperChannelMapMaskAlpha") != std::string::npos ||
        src.find("paperBaseMaskAlpha") != std::string::npos ||
        src.find("paperBaseVisibleAlpha") != std::string::npos ||
        src.find("paperStandaloneBaseAlpha") != std::string::npos) {
        return src;
    }

    constexpr std::string_view declBlock = "#if YAKKAI_CHANNELMAP_ALPHA_MASK || YAKKAI_CHANNELMAP_BASE_EXCLUDE || YAKKAI_BASE_ALPHA_MASK\n"
                                           "uniform sampler2D g_Texture1;\n"
                                           "#endif\n"
                                            "#if YAKKAI_CHANNELMAP_ALPHA_MASK\n"
                                            "uniform sampler2D g_Texture2;\n"
                                            "#endif\n";

    const std::regex tex0Line(R"((^[ \t]*uniform\s+sampler2D\s+g_Texture0\s*;[^\n]*$))",
                              std::regex::ECMAScript | std::regex::multiline);
    if (std::regex_search(src, tex0Line)) {
        src = std::regex_replace(src,
                                 tex0Line,
                                 std::string("$1\n") + std::string(declBlock),
                                 std::regex_constants::format_first_only);
    }

    const std::regex colorWriteLine(R"((^[ \t]*)gl_FragColor\s*=\s*color\s*;\s*$)",
                                    std::regex::ECMAScript | std::regex::multiline);
    if (std::regex_search(src, colorWriteLine)) {
        src = std::regex_replace(
            src,
            colorWriteLine,
            "$1#if YAKKAI_CHANNELMAP_ALPHA_MASK || YAKKAI_CHANNELMAP_BASE_EXCLUDE || YAKKAI_BASE_ALPHA_MASK\n"
            "$1float paperChannelMapMaskAlpha = texSample2D(g_Texture1, v_TexCoord.xy).a;\n"
            "$1#endif\n"
            "$1#if YAKKAI_CHANNELMAP_ALPHA_MASK\n"
            "$1float paperBaseMaskAlpha = texSample2D(g_Texture2, v_TexCoord.xy).a;\n"
            "$1float paperOverlayMaskAlpha = paperChannelMapMaskAlpha * paperBaseMaskAlpha;\n"
            "$1color.rgb *= paperOverlayMaskAlpha;\n"
            "$1color.a *= paperOverlayMaskAlpha;\n"
            "$1#endif\n"
            "$1#if YAKKAI_BASE_ALPHA_MASK\n"
            "$1float paperStandaloneBaseAlpha = paperChannelMapMaskAlpha;\n"
            "$1color.rgb *= paperStandaloneBaseAlpha;\n"
            "$1color.a *= paperStandaloneBaseAlpha;\n"
            "$1#endif\n"
            "$1#if YAKKAI_CHANNELMAP_BASE_EXCLUDE\n"
            "$1float paperBaseVisibleAlpha = 1.0 - paperChannelMapMaskAlpha;\n"
            "$1color.rgb *= paperBaseVisibleAlpha;\n"
            "$1color.a *= paperBaseVisibleAlpha;\n"
            "$1#endif\n"
            "$1gl_FragColor = color;",
            std::regex_constants::format_first_only);
    }

    return src;
}

uint32_t ResolvePuppetChannelMapRowCount(const WPMdl& mdl, uint32_t minimumRowCount) {
    uint32_t maxBlendIndex = 0;
    for (const auto& vertex : mdl.vertexs) {
        maxBlendIndex = std::max(maxBlendIndex, vertex.blend_indices[0]);
    }
    return std::max(minimumRowCount, maxBlendIndex / 4u + 1u);
}

bool PuppetBoneFramesHaveMeaningfulDelta(const WPPuppet::Animation::BoneFrames& bframes) {
    if (bframes.frames.size() <= 1) {
        return false;
    }

    const auto& base = bframes.frames.front();
    constexpr float kPosEps2 = 1.0e-6f;
    constexpr float kAngEps2 = 1.0e-6f;
    constexpr float kSclEps2 = 1.0e-6f;

    for (usize frameIndex = 1; frameIndex < bframes.frames.size(); ++frameIndex) {
        const auto& frame = bframes.frames[frameIndex];
        if ((frame.position - base.position).squaredNorm() > kPosEps2 ||
            (frame.angle - base.angle).squaredNorm() > kAngEps2 ||
            (frame.scale - base.scale).squaredNorm() > kSclEps2) {
            return true;
        }
    }

    return false;
}

void LogPuppetChannelMapBlendIndexCoverage(const wpscene::WPImageObject& imageObject,
                                           const WPMdl&                  mdl,
                                           std::span<const float>        blendMap) {
    std::array<size_t, 64> vertexCounts {};
    std::array<size_t, 64> triangleCounts {};

    for (const auto& vertex : mdl.vertexs) {
        const size_t index = std::min<size_t>(vertex.blend_indices[0], vertexCounts.size() - 1);
        vertexCounts[index]++;
    }

    for (const auto& tri : mdl.indices) {
        size_t triIndex = vertexCounts.size() - 1;
        if (tri[0] < mdl.vertexs.size() && tri[1] < mdl.vertexs.size() && tri[2] < mdl.vertexs.size()) {
            const auto i0 = static_cast<size_t>(mdl.vertexs[tri[0]].blend_indices[0]);
            const auto i1 = static_cast<size_t>(mdl.vertexs[tri[1]].blend_indices[0]);
            const auto i2 = static_cast<size_t>(mdl.vertexs[tri[2]].blend_indices[0]);
            if (i0 == i1 && i1 == i2) {
                triIndex = std::min(i0, triangleCounts.size() - 1);
            }
        }
        triangleCounts[triIndex]++;
    }

    std::ostringstream activeStream;
    bool first = true;
    for (size_t i = 0; i < blendMap.size(); ++i) {
        if (blendMap[i] <= 1.0e-6f) {
            continue;
        }
        if (!first) {
            activeStream << "; ";
        }
        first = false;
        activeStream << "slot=" << i
                     << " weight=" << blendMap[i]
                     << " verts=" << vertexCounts[std::min(i, vertexCounts.size() - 1)]
                     << " tris=" << triangleCounts[std::min(i, triangleCounts.size() - 1)];
    }
    if (first) {
        activeStream << "none";
    }

    std::ostringstream topStream;
    bool firstTop = true;
    for (size_t i = 0; i < vertexCounts.size(); ++i) {
        if (vertexCounts[i] == 0 && triangleCounts[i] == 0) {
            continue;
        }
        if (!firstTop) {
            topStream << "; ";
        }
        firstTop = false;
        topStream << "slot=" << i
                  << " verts=" << vertexCounts[i]
                  << " tris=" << triangleCounts[i];
    }

    LOG_INFO("native puppet channelmap blend coverage: image=%s active=[%s] all=[%s]",
             imageObject.name.c_str(),
             activeStream.str().c_str(),
             topStream.str().c_str());
}

size_t SeedPuppetChannelMapBlendMapFromVisibleLayers(const wpscene::WPImageObject& imageObject,
                                                     const WPMdl&                  mdl,
                                                     std::vector<float>&           blendMap,
                                                     std::vector<uint32_t>*        activeIndicesOut) {
    constexpr float kInactiveChannelMapBlendValue = 0.0f;
    constexpr float kActiveChannelMapBlendValue = 1.0f;

    std::fill(blendMap.begin(), blendMap.end(), kInactiveChannelMapBlendValue);

    if (! mdl.puppet) {
        return 0;
    }

    size_t              activeBlendSlots = 0;
    std::vector<uint32_t> activeIndices;
    activeIndices.reserve(blendMap.size());

    for (const auto& layer : imageObject.puppet_layers) {
        if (! layer.visible || layer.blend <= 1.0e-6) {
            continue;
        }

        const auto animIt = std::find_if(mdl.puppet->anims.begin(),
                                         mdl.puppet->anims.end(),
                                         [&layer](const auto& anim) { return anim.id == layer.id; });
        if (animIt == mdl.puppet->anims.end()) {
            continue;
        }

        const float layerBlendValue = std::clamp(static_cast<float>(layer.blend),
                                                 kInactiveChannelMapBlendValue,
                                                 kActiveChannelMapBlendValue);

        const usize boneCount = std::min<usize>(animIt->bframes_array.size(), blendMap.size());
        for (usize boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            if (! PuppetBoneFramesHaveMeaningfulDelta(animIt->bframes_array[boneIndex])) {
                continue;
            }

            if (blendMap[boneIndex] <= kInactiveChannelMapBlendValue) {
                activeIndices.push_back(static_cast<uint32_t>(boneIndex));
                activeBlendSlots++;
            }
            blendMap[boneIndex] = std::max(blendMap[boneIndex], layerBlendValue);
        }
    }

    std::ostringstream activeIndicesStream;
    for (usize i = 0; i < activeIndices.size(); ++i) {
        if (i != 0) activeIndicesStream << ", ";
        activeIndicesStream << activeIndices[i];
    }
    LOG_INFO("native puppet channelmap blend map seeded from visible animation layers: image=%s activeBlendSlots=%zu indices=[%s]",
             imageObject.name.c_str(),
             activeBlendSlots,
             activeIndicesStream.str().c_str());

    if (activeIndicesOut) {
        *activeIndicesOut = activeIndices;
    }

    return activeBlendSlots;
}

std::string ResolveEffectPingPongFinalTarget(const std::string& pingpongA,
                                             const std::string& pingpongB,
                                             int32_t            authoredEffectCount) {
    if (authoredEffectCount <= 0) {
        return pingpongA;
    }
    return (authoredEffectCount % 2 == 0) ? pingpongA : pingpongB;
}

bool TryPreparePuppetChannelMapPrepass(const wpscene::WPImageObject& imageObject,
                                       const WPMdl&                  puppet,
                                       fs::VFS&                      vfs,
                                       wpscene::WPMaterial&          material,
                                       ShaderValueMap&               baseUniforms,
                                       std::vector<uint32_t>*        activeBlendSlotsOut) {
    const bool sourceMaterialExplicitlyRequestsChannelMap =
        imageObject.material.shader == "puppettexturechannels" ||
        std::any_of(imageObject.material.textures.begin(),
                    imageObject.material.textures.end(),
                    [](const std::string& textureName) {
                        return textureName.find("_channelmap") != std::string::npos;
                    });
    const std::string channelMapMaterialPath = DerivePuppetChannelMapMaterialPath(imageObject.image);
    if (channelMapMaterialPath.empty()) {
        return false;
    }

    if (! vfs.Contains("/assets/" + channelMapMaterialPath)) {
        return false; // channelmap is optional — many scenes don't have one
    }

    nlohmann::json materialJson;
    wpscene::WPMaterial parsedMaterial;
    if (! PARSE_JSON(fs::GetFileContent(vfs, "/assets/" + channelMapMaterialPath), materialJson) ||
        ! parsedMaterial.FromJson(materialJson)) {
        LOG_ERROR("failed to load puppet channelmap material: %s", channelMapMaterialPath.c_str());
        return false;
    }

    if (! sourceMaterialExplicitlyRequestsChannelMap) {
        LOG_INFO("native puppet channelmap prepass skipped because source material has no explicit channelmap route: image=%s shader=%s",
                 imageObject.name.c_str(),
                 imageObject.material.shader.c_str());
        return false;
    }

    material = std::move(parsedMaterial);

    if (material.shader != "puppettexturechannels" || imageObject.material.textures.empty()) {
        return false;
    }

    material.textures.resize(std::max<usize>(material.textures.size(), 2));
    material.textures[1] = imageObject.material.textures[0];
    material.combos["DOUBLEBUFFERED"] = 1;

    const uint32_t rowCount =
        ResolvePuppetChannelMapRowCount(puppet,
                                        static_cast<uint32_t>(
                                            std::max(1, material.combos["BLENDROWCOUNT"])));
    material.combos["BLENDROWCOUNT"] = static_cast<int32_t>(rowCount);

    std::vector<float> blendMap(rowCount * 4u, 0.0f);
    const size_t       activeBlendSlots =
        SeedPuppetChannelMapBlendMapFromVisibleLayers(imageObject, puppet, blendMap, activeBlendSlotsOut);
    baseUniforms["g_BlendMap"] = blendMap;
    LogPuppetChannelMapBlendIndexCoverage(imageObject, puppet, blendMap);

    LOG_INFO("native puppet channelmap prepass enabled: image=%s material=%s rowCount=%u activeBlendSlots=%zu baseTexture=%s channelTexture=%s",
             imageObject.name.c_str(),
             channelMapMaterialPath.c_str(),
             rowCount,
             activeBlendSlots,
             material.textures[1].c_str(),
             material.textures[0].c_str());
    return true;
}

Eigen::Vector3f ComputeCameraNodeRotation(const std::array<float, 3>& eye,
                                          const std::array<float, 3>& center,
                                          const std::array<float, 3>& up) {
    const Eigen::Vector3f eyeVec(eye.data());
    const Eigen::Vector3f centerVec(center.data());
    Eigen::Vector3f       dir = centerVec - eyeVec;
    if (dir.squaredNorm() <= 1.0e-12f) {
        return Eigen::Vector3f::Zero();
    }

    dir.normalize();
    Eigen::Vector3f upVec(up.data());
    if (upVec.squaredNorm() <= 1.0e-12f) {
        upVec = Eigen::Vector3f::UnitY();
    } else {
        upVec.normalize();
    }

    Eigen::Vector3f right = dir.cross(upVec);
    if (right.squaredNorm() <= 1.0e-12f) {
        right = dir.cross(Eigen::Vector3f::UnitY());
    }
    if (right.squaredNorm() <= 1.0e-12f) {
        right = Eigen::Vector3f::UnitX();
    } else {
        right.normalize();
    }

    Eigen::Vector3f correctedUp = right.cross(dir);
    if (correctedUp.squaredNorm() <= 1.0e-12f) {
        correctedUp = Eigen::Vector3f::UnitY();
    } else {
        correctedUp.normalize();
    }

    Eigen::Matrix3f rotationMatrix;
    rotationMatrix.col(0) = right;
    rotationMatrix.col(1) = correctedUp;
    rotationMatrix.col(2) = -dir;

    const Eigen::Vector3f zyx = rotationMatrix.eulerAngles(2, 1, 0);
    return Eigen::Vector3f(zyx[2], zyx[1], zyx[0]);
}

float ResolvePerspectiveFov(const wpscene::WPSceneGeneral& general, i32 ortho_h) {
    if (general.fov > 0.0f && general.fov < 180.0f) {
        return general.fov;
    }
    return algorism::CalculatePersperctiveFov(1000.0f, ortho_h);
}

struct PerspectiveCameraFrame {
    bool   valid { false };
    Eigen::Vector3d eye { Eigen::Vector3d::Zero() };
    Eigen::Vector3d target { -Eigen::Vector3d::UnitZ() };
    Eigen::Vector3d forward { -Eigen::Vector3d::UnitZ() };
    Eigen::Vector3d right { Eigen::Vector3d::UnitX() };
    Eigen::Vector3d up { Eigen::Vector3d::UnitY() };
    double          verticalFovRadians { 0.0 };
    double          aspect { 1.0 };
};

struct StaticModelWorldMetrics {
    bool            valid { false };
    Eigen::Vector3d center { Eigen::Vector3d::Zero() };
    double          radius { 0.0 };
};

struct StaticModelProjectedMetrics {
    bool            valid { false };
    size_t          projectedCount { 0 };
    Eigen::Vector2d ndcMin { Eigen::Vector2d::Constant(std::numeric_limits<double>::infinity()) };
    Eigen::Vector2d ndcMax { Eigen::Vector2d::Constant(-std::numeric_limits<double>::infinity()) };
};

struct StaticModelBasisChoice {
    Eigen::Matrix3f linear { Eigen::Matrix3f::Identity() };
    std::string     label { "x=x y=y z=z" };
    double          score { -std::numeric_limits<double>::infinity() };
    double          frontRatio { 0.0 };
    double          insideRatio { 0.0 };
    double          coverage { 0.0 };
    double          centerPenalty { 0.0 };
    double          overflowPerFront { 0.0 };
    double          meanDepth { 0.0 };
    Eigen::Vector2d ndcBoundsCenter { Eigen::Vector2d::Zero() };
    double          backdropPenalty { 0.0 };
    double          backdropFacingBonus { 0.0 };
    bool            valid { false };
};

struct StaticBackdropSubmeshInfo {
    bool            valid { false };
    bool            isBackdrop { false };
    Eigen::Vector3f extents { Eigen::Vector3f::Zero() };
    int             thinAxis { -1 };
    Eigen::Vector3f localNormal { Eigen::Vector3f::Zero() };
};

bool ParseCameraPathAnimation(const nlohmann::json& json, WPCameraPathAnimation& animation) {
    if (! json.is_object() || ! json.contains("paths")) {
        return false;
    }

    const auto& paths = json.at("paths");
    if (! paths.is_array()) {
        return false;
    }

    animation = WPCameraPathAnimation();
    for (const auto& pathValue : paths) {
        if (! pathValue.is_object() || ! pathValue.contains("transforms")) {
            continue;
        }

        const auto& transforms = pathValue.at("transforms");
        if (! transforms.is_array() || transforms.empty()) {
            continue;
        }

        WPCameraPathSegment segment;
        GET_JSON_NAME_VALUE_NOWARN(pathValue, "duration", segment.duration);

        for (const auto& transform : transforms) {
            if (! transform.is_object()) {
                continue;
            }
            WPCameraPathKeyframe keyframe;
            GET_JSON_NAME_VALUE(transform, "eye", keyframe.eye);
            GET_JSON_NAME_VALUE(transform, "center", keyframe.center);
            GET_JSON_NAME_VALUE_NOWARN(transform, "up", keyframe.up);
            GET_JSON_NAME_VALUE_NOWARN(transform, "timestamp", keyframe.timestamp);
            segment.keyframes.push_back(keyframe);
        }

        if (segment.keyframes.empty()) {
            continue;
        }

        std::sort(segment.keyframes.begin(),
                  segment.keyframes.end(),
                  [](const WPCameraPathKeyframe& a, const WPCameraPathKeyframe& b) {
                      return a.timestamp < b.timestamp;
                  });

        if (segment.duration <= 0.0) {
            segment.duration = std::max(0.0, segment.keyframes.back().timestamp);
        }
        if (segment.duration <= 0.0) {
            segment.duration = 1.0;
        }

        animation.totalDuration += segment.duration;
        animation.segments.push_back(std::move(segment));
    }

    animation.valid = ! animation.segments.empty();
    return animation.valid;
}

CameraPose ResolveScenePerspectivePose(const wpscene::WPScene& sc,
                                       fs::VFS&               vfs,
                                       WPCameraPathAnimation* animationOut = nullptr) {
    CameraPose pose { sc.camera.eye, sc.camera.center, sc.camera.up };
    if (animationOut != nullptr) {
        *animationOut = WPCameraPathAnimation();
    }

    for (const auto& path : sc.camera.paths) {
        if (path.empty()) continue;

        nlohmann::json cameraPathJson;
        if (! PARSE_JSON(fs::GetFileContent(vfs, "/assets/" + path), cameraPathJson)) {
            LOG_ERROR("camera path json parse failed: %s", path.c_str());
            continue;
        }

        WPCameraPathAnimation animation;
        if (! ParseCameraPathAnimation(cameraPathJson, animation) || animation.segments.empty() ||
            animation.segments.front().keyframes.empty()) {
            LOG_ERROR("camera path has no usable transforms: %s", path.c_str());
            continue;
        }

        const auto& pathPose = animation.segments.front().keyframes.front();
        if (animationOut != nullptr) {
            *animationOut = animation;
        }

        LOG_INFO("using camera path first transform for perspective camera: %s eye=(%.3f, %.3f, %.3f) center=(%.3f, %.3f, %.3f)",
                 path.c_str(),
                 pathPose.eye[0],
                 pathPose.eye[1],
                 pathPose.eye[2],
                 pathPose.center[0],
                 pathPose.center[1],
                 pathPose.center[2]);
        if (animation.valid) {
            LOG_INFO("camera path animation parsed: %s segments=%zu duration=%.3f",
                     path.c_str(),
                     animation.segments.size(),
                     animation.totalDuration);
        }
        return CameraPose { pathPose.eye, pathPose.center, pathPose.up };
    }

    return pose;
}

Eigen::Affine3d BuildLocalTransform(const Eigen::Vector3f& translate,
                                    const Eigen::Vector3f& scale,
                                    const Eigen::Vector3f& rotation) {
    Eigen::Affine3d trans = Eigen::Affine3d::Identity();
    trans.prescale(scale.cast<double>());
    trans.prerotate(Eigen::AngleAxis<double>(rotation.x(), Eigen::Vector3d::UnitX()));
    trans.prerotate(Eigen::AngleAxis<double>(rotation.y(), Eigen::Vector3d::UnitY()));
    trans.prerotate(Eigen::AngleAxis<double>(rotation.z(), Eigen::Vector3d::UnitZ()));
    trans.pretranslate(translate.cast<double>());
    return trans;
}

Eigen::Affine3d BuildStaticModelFallbackTransform(const Eigen::Vector3f& translate,
                                                  const Eigen::Vector3f& authoredScale,
                                                  const Eigen::Vector3f& rotation,
                                                  const Eigen::Matrix3f& basisLinear,
                                                  float                  fallbackScaleFactor) {
    const Eigen::Affine3d rootTransform = BuildLocalTransform(translate, authoredScale, rotation);

    Eigen::Affine3d basisTransform = Eigen::Affine3d::Identity();
    Eigen::Matrix3d linear = basisLinear.cast<double>();
    if (! linear.allFinite()) {
        linear = Eigen::Matrix3d::Identity();
    }
    linear *= static_cast<double>(fallbackScaleFactor);
    basisTransform.linear() = linear;

    return rootTransform * basisTransform;
}

std::string DescribeStaticBasis(const Eigen::Matrix3f& basis) {
    auto describeComponent = [](char outAxis, Eigen::Vector3f row) {
        int   bestAxis  = 0;
        float bestValue = std::abs(row.x());
        for (int axis = 1; axis < 3; ++axis) {
            const float value = std::abs(row[axis]);
            if (value > bestValue) {
                bestAxis  = axis;
                bestValue = value;
            }
        }

        std::string result;
        result += outAxis;
        result += '=';
        if (row[bestAxis] < 0.0f) {
            result += '-';
        }
        result += "xyz"[bestAxis];
        return result;
    };

    return describeComponent('x', basis.row(0)) + " " +
           describeComponent('y', basis.row(1)) + " " +
           describeComponent('z', basis.row(2));
}

StaticBackdropSubmeshInfo ClassifyStaticBackdropSubmesh(const WPMdl::Submesh& submesh) {
    StaticBackdropSubmeshInfo info;
    if (submesh.vertexs.empty()) {
        return info;
    }

    Eigen::AlignedBox3f submeshBounds;
    submeshBounds.setNull();
    for (const auto& vertex : submesh.vertexs) {
        submeshBounds.extend(Eigen::Vector3f(vertex.position.data()));
    }

    info.valid   = true;
    info.extents = submeshBounds.sizes();

    std::array<float, 3> sortedExtents {
        info.extents.x(),
        info.extents.y(),
        info.extents.z(),
    };
    std::sort(sortedExtents.begin(), sortedExtents.end());

    int smallestAxis = 0;
    if (info.extents.y() < info.extents[smallestAxis]) {
        smallestAxis = 1;
    }
    if (info.extents.z() < info.extents[smallestAxis]) {
        smallestAxis = 2;
    }
    info.thinAxis = smallestAxis;
    info.localNormal[smallestAxis] = 1.0f;

    info.isBackdrop = submesh.vertexs.size() <= 16 &&
                      sortedExtents[2] >= 10.0f &&
                      sortedExtents[1] >= 10.0f &&
                      sortedExtents[0] <= 0.25f;
    return info;
}

StaticModelBasisChoice ScoreStaticModelBasisChoice(const PerspectiveCameraFrame&      cameraFrame,
                                                   const Eigen::Vector3f&             translate,
                                                   const Eigen::Vector3f&             authoredScale,
                                                   const Eigen::Vector3f&             rotation,
                                                   const std::vector<WPMdl::Submesh>& submeshes,
                                                   const Eigen::Matrix3f&             basisLinear) {
    StaticModelBasisChoice choice;
    choice.linear = basisLinear;
    choice.label  = DescribeStaticBasis(basisLinear);

    const double halfVerticalFov = cameraFrame.verticalFovRadians * 0.5;
    const double tanVertical     = std::tan(halfVerticalFov);
    const double tanHorizontal   = tanVertical * cameraFrame.aspect;
    if (! std::isfinite(tanVertical) || tanVertical <= 1.0e-9 || ! std::isfinite(tanHorizontal) || tanHorizontal <= 1.0e-9) {
        return choice;
    }

    if (cameraFrame.right.squaredNorm() <= 1.0e-12 || cameraFrame.up.squaredNorm() <= 1.0e-12) {
        return choice;
    }

    const Eigen::Affine3d modelTransform =
        BuildStaticModelFallbackTransform(translate, authoredScale, rotation, basisLinear, 1.0f);

    size_t         totalCount      = 0;
    size_t         frontCount      = 0;
    size_t         insideCount     = 0;
    size_t         projectedCount  = 0;
    double         overflow        = 0.0;
    double         depthSum        = 0.0;
    Eigen::Vector2d ndcSum         = Eigen::Vector2d::Zero();
    Eigen::Vector2d ndcMin         = Eigen::Vector2d::Constant(std::numeric_limits<double>::infinity());
    Eigen::Vector2d ndcMax         = Eigen::Vector2d::Constant(-std::numeric_limits<double>::infinity());

    for (const auto& submesh : submeshes) {
        for (const auto& vertex : submesh.vertexs) {
            ++totalCount;

            const Eigen::Vector3d worldPoint =
                modelTransform * Eigen::Vector3f(vertex.position.data()).cast<double>();
            const Eigen::Vector3d relative   = worldPoint - cameraFrame.eye;
            const double          depth      = relative.dot(cameraFrame.forward);
            if (! std::isfinite(depth) || depth <= 1.0e-6) {
                continue;
            }

            ++frontCount;

            const double x = relative.dot(cameraFrame.right) / (depth * tanHorizontal);
            const double y = relative.dot(cameraFrame.up) / (depth * tanVertical);
            if (! std::isfinite(x) || ! std::isfinite(y)) {
                continue;
            }

            overflow += std::max(0.0, std::abs(x) - 1.0) + std::max(0.0, std::abs(y) - 1.0);

            if (std::abs(x) <= 1.05 && std::abs(y) <= 1.05) {
                ++insideCount;
            }

            if (std::abs(x) <= 4.0 && std::abs(y) <= 4.0) {
                ++projectedCount;
                depthSum += depth;
                ndcSum += Eigen::Vector2d(x, y);
                ndcMin = ndcMin.cwiseMin(Eigen::Vector2d(x, y));
                ndcMax = ndcMax.cwiseMax(Eigen::Vector2d(x, y));
            }
        }
    }

    if (totalCount == 0 || frontCount == 0 || projectedCount == 0) {
        return choice;
    }

    choice.valid        = true;
    choice.frontRatio   = static_cast<double>(frontCount) / static_cast<double>(totalCount);
    choice.insideRatio  = static_cast<double>(insideCount) / static_cast<double>(frontCount);
    const Eigen::Vector2d extents = (ndcMax - ndcMin).cwiseMax(Eigen::Vector2d::Zero());
    choice.coverage     = std::min(2.0, extents.x()) * std::min(2.0, extents.y());
    choice.centerPenalty = (ndcSum / static_cast<double>(projectedCount)).norm();
    choice.overflowPerFront = overflow / static_cast<double>(frontCount);
    choice.meanDepth    = depthSum / static_cast<double>(projectedCount);
    choice.ndcBoundsCenter = (ndcMin + ndcMax) * 0.5;

    struct SubmeshEval {
        bool   valid { false };
        bool   isBackdrop { false };
        double depth { 0.0 };
        double ndcX { 0.0 };
        double ndcY { 0.0 };
        double denseWeight { 0.0 };
    };

    std::vector<SubmeshEval> submeshEvals;
    submeshEvals.reserve(submeshes.size());
    double denseDepthWeightedSum = 0.0;
    double denseDepthWeight      = 0.0;

    for (const auto& submesh : submeshes) {
        SubmeshEval eval;
        if (submesh.vertexs.empty()) {
            submeshEvals.push_back(eval);
            continue;
        }

        Eigen::AlignedBox3f submeshBounds;
        submeshBounds.setNull();
        for (const auto& vertex : submesh.vertexs) {
            submeshBounds.extend(Eigen::Vector3f(vertex.position.data()));
        }

        eval.isBackdrop = ClassifyStaticBackdropSubmesh(submesh).isBackdrop;

        const Eigen::Vector3d center =
            modelTransform * submeshBounds.center().cast<double>();
        const Eigen::Vector3d relative = center - cameraFrame.eye;
        const double depth = relative.dot(cameraFrame.forward);
        if (! std::isfinite(depth) || depth <= 1.0e-6) {
            submeshEvals.push_back(eval);
            continue;
        }

        const double ndcX = relative.dot(cameraFrame.right) / (depth * tanHorizontal);
        const double ndcY = relative.dot(cameraFrame.up) / (depth * tanVertical);
        if (! std::isfinite(ndcX) || ! std::isfinite(ndcY)) {
            submeshEvals.push_back(eval);
            continue;
        }

        eval.valid = true;
        eval.depth = depth;
        eval.ndcX  = ndcX;
        eval.ndcY  = ndcY;
        if (submesh.vertexs.size() > 128) {
            eval.denseWeight = std::min(2000.0, static_cast<double>(submesh.vertexs.size()));
            denseDepthWeightedSum += eval.depth * eval.denseWeight;
            denseDepthWeight += eval.denseWeight;
        }
        submeshEvals.push_back(eval);
    }

    if (denseDepthWeight > 1.0e-6) {
        const double denseMeanDepth = denseDepthWeightedSum / denseDepthWeight;
        for (const auto& eval : submeshEvals) {
            if (! eval.valid || ! eval.isBackdrop) {
                continue;
            }

            const double offscreenAmount =
                std::max(0.0, std::abs(eval.ndcX) - 0.75) +
                std::max(0.0, std::abs(eval.ndcY) - 0.75);
            if (offscreenAmount <= 0.0) {
                continue;
            }

            if (eval.depth < denseMeanDepth * 0.85) {
                const double depthRatio = denseMeanDepth / std::max(eval.depth, 1.0e-6);
                choice.backdropPenalty += offscreenAmount * depthRatio * 3.0;
            }
        }
    }

    const double cameraDownwardness =
        std::clamp(-cameraFrame.forward.dot(cameraFrame.up), 0.0, 1.0);
    if (cameraDownwardness > 0.15) {
        double backdropAlignmentSum   = 0.0;
        double backdropAlignmentCount = 0.0;
        for (const auto& submesh : submeshes) {
            const StaticBackdropSubmeshInfo backdropInfo = ClassifyStaticBackdropSubmesh(submesh);
            if (! backdropInfo.isBackdrop || backdropInfo.thinAxis < 0) {
                continue;
            }

            Eigen::Vector3d worldNormal =
                (basisLinear * backdropInfo.localNormal).cast<double>();
            if (worldNormal.squaredNorm() <= 1.0e-12) {
                continue;
            }
            worldNormal.normalize();
            backdropAlignmentSum += std::abs(worldNormal.dot(cameraFrame.up));
            backdropAlignmentCount += 1.0;
        }

        if (backdropAlignmentCount > 0.0) {
            choice.backdropFacingBonus =
                (backdropAlignmentSum / backdropAlignmentCount) * cameraDownwardness * 8.0;
        }
    }

    choice.score = choice.frontRatio * 4.0 +
                   choice.insideRatio * 8.0 +
                   choice.coverage * 2.0 -
                   choice.overflowPerFront * 0.75 -
                   choice.centerPenalty * 1.5 -
                   choice.backdropPenalty +
                   choice.backdropFacingBonus;
    if (insideCount == 0) {
        choice.score -= 4.0;
    }

    return choice;
}

double ComputeBackdropFacingBonus(const PerspectiveCameraFrame&      cameraFrame,
                                  const std::vector<WPMdl::Submesh>& submeshes,
                                  const Eigen::Matrix3f&             basisLinear) {
    double backdropFloorSum   = 0.0;
    double backdropFloorCount = 0.0;
    for (const auto& submesh : submeshes) {
        const StaticBackdropSubmeshInfo backdropInfo = ClassifyStaticBackdropSubmesh(submesh);
        if (! backdropInfo.isBackdrop || backdropInfo.thinAxis < 0) {
            continue;
        }

        Eigen::Vector3d worldNormal = (basisLinear * backdropInfo.localNormal).cast<double>();
        if (worldNormal.squaredNorm() <= 1.0e-12) {
            continue;
        }

        worldNormal.normalize();
        const double floorAlignment  = std::abs(worldNormal.dot(cameraFrame.up));
        const double facingPenalty   = std::abs(worldNormal.dot(cameraFrame.forward));
        backdropFloorSum += std::max(0.0, floorAlignment - facingPenalty * 0.5);
        backdropFloorCount += 1.0;
    }

    if (backdropFloorCount <= 0.0) {
        return 0.0;
    }

    return (backdropFloorSum / backdropFloorCount) * 12.0;
}

bool ComputeStaticModelBounds(const std::vector<WPMdl::Submesh>& submeshes,
                              Eigen::AlignedBox3f&              bounds) {
    bounds.setNull();

    bool hasPoint = false;
    for (const auto& submesh : submeshes) {
        for (const auto& vertex : submesh.vertexs) {
            bounds.extend(Eigen::Vector3f(vertex.position.data()));
            hasPoint = true;
        }
    }
    return hasPoint;
}

bool ComputeStaticModelWorldMetrics(const Eigen::Affine3d&    modelTransform,
                                    const Eigen::AlignedBox3f& bounds,
                                    StaticModelWorldMetrics&  metrics) {
    const auto& mins = bounds.min();
    const auto& maxs = bounds.max();
    metrics.valid    = true;
    metrics.center   = modelTransform * bounds.center().cast<double>();
    metrics.radius   = 0.0;

    for (int ix = 0; ix < 2; ++ix) {
        for (int iy = 0; iy < 2; ++iy) {
            for (int iz = 0; iz < 2; ++iz) {
                const Eigen::Vector3d point(
                    ix == 0 ? mins.x() : maxs.x(),
                    iy == 0 ? mins.y() : maxs.y(),
                    iz == 0 ? mins.z() : maxs.z());
                const Eigen::Vector3d worldPoint = modelTransform * point;
                const double          distance   = (worldPoint - metrics.center).norm();
                if (std::isfinite(distance)) {
                    metrics.radius = std::max(metrics.radius, distance);
                }
            }
        }
    }

    return metrics.valid;
}

bool ComputeStaticModelProjectedMetrics(ParseContext&                    context,
                                        const wpscene::WPModelObject&   model_obj,
                                        const StaticModelBasisChoice&   basisChoice,
                                        float                           staticFallbackScale,
                                        const std::vector<WPMdl::Submesh>& submeshes,
                                        StaticModelProjectedMetrics&    metrics) {
    auto cameraIt = context.scene->cameras.find("global_perspective");
    if (cameraIt == context.scene->cameras.end()) {
        return false;
    }

    const Eigen::Matrix4d viewProjection = cameraIt->second->GetViewProjectionMatrix();
    if (! viewProjection.allFinite()) {
        return false;
    }

    Eigen::Vector3f authoredScale(model_obj.scale.data());
    authoredScale *= staticFallbackScale;
    const Eigen::Affine3d modelTransform =
        BuildLocalTransform(Eigen::Vector3f(model_obj.origin.data()),
                            authoredScale,
                            Eigen::Vector3f(model_obj.angles.data()));

    for (const auto& submesh : submeshes) {
        for (const auto& vertex : submesh.vertexs) {
            const Eigen::Vector3d local =
                (basisChoice.linear * Eigen::Vector3f(vertex.position.data())).cast<double>();
            const Eigen::Vector3d world = modelTransform * local;
            const Eigen::Vector4d clip  =
                viewProjection * Eigen::Vector4d(world.x(), world.y(), world.z(), 1.0);
            if (! clip.allFinite() || clip.w() <= 1.0e-6) {
                continue;
            }

            const Eigen::Vector2d ndc(clip.x() / clip.w(), clip.y() / clip.w());
            if (! ndc.allFinite()) {
                continue;
            }

            ++metrics.projectedCount;
            metrics.ndcMin = metrics.ndcMin.cwiseMin(ndc);
            metrics.ndcMax = metrics.ndcMax.cwiseMax(ndc);
        }
    }

    metrics.valid = metrics.projectedCount > 0;
    return metrics.valid;
}

PerspectiveCameraFrame ResolvePerspectiveCameraFrame(const ParseContext& context) {
    PerspectiveCameraFrame frame;

    auto cameraIt = context.scene->cameras.find("global_perspective");
    if (cameraIt == context.scene->cameras.end()) {
        return frame;
    }

    const auto& camera   = *cameraIt->second;
    Eigen::Vector3d forward = camera.GetDirection();
    if (forward.squaredNorm() <= 1.0e-12) {
        return frame;
    }

    forward.normalize();
    frame.valid              = true;
    frame.eye                = camera.GetPosition();
    frame.forward            = forward;
    frame.target             = frame.eye + frame.forward;
    frame.verticalFovRadians = Radians(camera.Fov());
    frame.aspect             = camera.Aspect();
    if (auto node = camera.GetAttachedNode()) {
        const Eigen::Affine3d local(node->GetLocalTrans());
        frame.right = (local.linear() * Eigen::Vector3d::UnitX()).normalized();
        frame.up    = (local.linear() * Eigen::Vector3d::UnitY()).normalized();
    } else {
        frame.right = frame.forward.cross(Eigen::Vector3d::UnitY());
        if (frame.right.squaredNorm() <= 1.0e-12) {
            frame.right = Eigen::Vector3d::UnitX();
        } else {
            frame.right.normalize();
        }
        frame.up = frame.right.cross(frame.forward);
        if (frame.up.squaredNorm() <= 1.0e-12) {
            frame.up = Eigen::Vector3d::UnitY();
        } else {
            frame.up.normalize();
        }
    }
    return frame;
}

StaticModelBasisChoice ResolveStaticModelBasis(ParseContext&                    context,
                                               const wpscene::WPModelObject&   model_obj,
                                               const std::vector<WPMdl::Submesh>& framingSubmeshes,
                                               const std::vector<WPMdl::Submesh>& allSubmeshes) {
    StaticModelBasisChoice identity;
    const PerspectiveCameraFrame cameraFrame = ResolvePerspectiveCameraFrame(context);
    if (! cameraFrame.valid) {
        return identity;
    }

    const Eigen::Vector3f translate(model_obj.origin.data());
    const Eigen::Vector3f rotation(model_obj.angles.data());
    const Eigen::Vector3f authoredScale(model_obj.scale.data());
    double                cameraDownwardness = 0.0;
    if (context.has_scene_perspective_pose) {
        const Eigen::Map<const Eigen::Vector3f> authoredCenter(context.scene_perspective_pose.center.data());
        const Eigen::Map<const Eigen::Vector3f> authoredEye(context.scene_perspective_pose.eye.data());
        Eigen::Vector3d authoredForward = (authoredCenter - authoredEye).cast<double>();
        if (authoredForward.squaredNorm() > 1.0e-12) {
            authoredForward.normalize();
            cameraDownwardness =
                std::clamp(-authoredForward.dot(Eigen::Vector3d::UnitY()), 0.0, 1.0);
        }
    }
    int backdropThinAxis = -1;
    for (const auto& submesh : allSubmeshes) {
        const StaticBackdropSubmeshInfo backdropInfo = ClassifyStaticBackdropSubmesh(submesh);
        if (backdropInfo.isBackdrop && backdropInfo.thinAxis >= 0) {
            backdropThinAxis = backdropInfo.thinAxis;
            break;
        }
    }
    const bool hasBackdrop = backdropThinAxis >= 0;
    LOG_INFO("static model backdrop heuristic: %s thinAxis=%d cameraDownwardness=%.3f preferFloor=%d",
             model_obj.model.c_str(),
             backdropThinAxis,
             cameraDownwardness,
             hasBackdrop && cameraDownwardness > 0.15 ? 1 : 0);

    identity = ScoreStaticModelBasisChoice(
        cameraFrame, translate, authoredScale, rotation, framingSubmeshes, Eigen::Matrix3f::Identity());
    if (! identity.valid) {
        return identity;
    }
    identity.backdropFacingBonus = ComputeBackdropFacingBonus(cameraFrame, allSubmeshes, identity.linear);
    identity.score += identity.backdropFacingBonus;

    StaticModelBasisChoice best = identity;

    std::array<int, 3> permutation { 0, 1, 2 };
    do {
        for (int signX : { -1, 1 }) {
            for (int signY : { -1, 1 }) {
                for (int signZ : { -1, 1 }) {
                    Eigen::Matrix3f basis = Eigen::Matrix3f::Zero();
                    basis(0, permutation[0]) = static_cast<float>(signX);
                    basis(1, permutation[1]) = static_cast<float>(signY);
                    basis(2, permutation[2]) = static_cast<float>(signZ);
                    if (basis.determinant() <= 0.0f) {
                        continue;
                    }

                    StaticModelBasisChoice candidate =
                        ScoreStaticModelBasisChoice(cameraFrame, translate, authoredScale, rotation, framingSubmeshes, basis);
                    if (candidate.valid) {
                        candidate.backdropFacingBonus = ComputeBackdropFacingBonus(cameraFrame, allSubmeshes, candidate.linear);
                        candidate.score += candidate.backdropFacingBonus;
                    }
                    if (candidate.valid &&
                        (! best.valid || candidate.score > best.score + 1.0e-6)) {
                        best = candidate;
                    }
                }
            }
        }
    } while (std::next_permutation(permutation.begin(), permutation.end()));

    if (! best.valid) {
        return identity;
    }

    if (best.label != identity.label &&
        (best.score > identity.score + 0.25 ||
         best.insideRatio > identity.insideRatio + 0.10 ||
         best.coverage > identity.coverage + 0.25)) {
        Eigen::AlignedBox3f bounds;
        StaticModelWorldMetrics identityMetrics;
        StaticModelWorldMetrics correctedMetrics;
        if (ComputeStaticModelBounds(allSubmeshes, bounds) &&
            ComputeStaticModelWorldMetrics(
                BuildStaticModelFallbackTransform(translate, authoredScale, rotation, identity.linear, 1.0f),
                bounds,
                identityMetrics) &&
            ComputeStaticModelWorldMetrics(
                BuildStaticModelFallbackTransform(translate, authoredScale, rotation, best.linear, 1.0f),
                bounds,
                correctedMetrics)) {
            LOG_INFO("static model basis correction: %s basis=\"%s\" score=%.3f inside=%.3f coverage=%.3f centerBefore=(%.3f, %.3f, %.3f) centerAfter=(%.3f, %.3f, %.3f) target=(%.3f, %.3f, %.3f)",
                     model_obj.model.c_str(),
                     best.label.c_str(),
                     best.score,
                     best.insideRatio,
                     best.coverage,
                     identityMetrics.center.x(),
                     identityMetrics.center.y(),
                     identityMetrics.center.z(),
                     correctedMetrics.center.x(),
                     correctedMetrics.center.y(),
                     correctedMetrics.center.z(),
                     cameraFrame.target.x(),
                     cameraFrame.target.y(),
                     cameraFrame.target.z());
        } else {
            LOG_INFO("static model basis correction: %s basis=\"%s\" score=%.3f inside=%.3f coverage=%.3f",
                     model_obj.model.c_str(),
                     best.label.c_str(),
                     best.score,
                     best.insideRatio,
                     best.coverage);
        }
    }

    return best;
}

bool MaybeReframeStaticModelCamera(ParseContext&                  context,
                                   const wpscene::WPModelObject& model_obj,
                                   const StaticModelBasisChoice& basisChoice) {
    if (! basisChoice.valid || ! context.global_perspective_camera_node ||
        basisChoice.meanDepth <= 1.0e-6 || ! basisChoice.ndcBoundsCenter.allFinite()) {
        return false;
    }

    const Eigen::Vector2d ndcCenter = basisChoice.ndcBoundsCenter;
    if (std::abs(ndcCenter.x()) < 0.20 && std::abs(ndcCenter.y()) < 0.20) {
        return false;
    }

    const PerspectiveCameraFrame cameraFrame = ResolvePerspectiveCameraFrame(context);
    if (! cameraFrame.valid) {
        return false;
    }

    if (cameraFrame.right.squaredNorm() <= 1.0e-12 || cameraFrame.up.squaredNorm() <= 1.0e-12) {
        return false;
    }

    const double halfVerticalFov = cameraFrame.verticalFovRadians * 0.5;
    const double tanVertical     = std::tan(halfVerticalFov);
    const double tanHorizontal   = tanVertical * cameraFrame.aspect;
    if (! std::isfinite(tanVertical) || tanVertical <= 1.0e-9 || ! std::isfinite(tanHorizontal) || tanHorizontal <= 1.0e-9) {
        return false;
    }

    Eigen::Vector3d shift =
        cameraFrame.right * (ndcCenter.x() * basisChoice.meanDepth * tanHorizontal) +
        cameraFrame.up * (ndcCenter.y() * basisChoice.meanDepth * tanVertical);
    if (! shift.allFinite()) {
        return false;
    }

    const double maxShift = std::max(0.5, basisChoice.meanDepth);
    const double shiftNorm = shift.norm();
    if (shiftNorm > maxShift) {
        shift *= maxShift / shiftNorm;
    }

    Eigen::Vector3f translate = context.global_perspective_camera_node->Translate();
    translate += shift.cast<float>();
    context.global_perspective_camera_node->SetTranslate(translate);
    context.scene->cameras.at("global_perspective")->Update();

    LOG_INFO("static model camera reframe: %s ndcCenter=(%.3f, %.3f) meanDepth=%.3f shift=(%.3f, %.3f, %.3f)",
             model_obj.model.c_str(),
             ndcCenter.x(),
             ndcCenter.y(),
             basisChoice.meanDepth,
             shift.x(),
             shift.y(),
             shift.z());
    return true;
}

float ResolveStaticModelAutoFitScale(ParseContext&              context,
                                     const wpscene::WPModelObject& model_obj,
                                     const StaticModelBasisChoice& basisChoice,
                                     const std::vector<WPMdl::Submesh>& submeshes) {
    StaticModelProjectedMetrics projected;
    if (! ComputeStaticModelProjectedMetrics(
            context, model_obj, basisChoice, 1.0f, submeshes, projected)) {
        return 1.0f;
    }

    const Eigen::Vector2d extents =
        (projected.ndcMax - projected.ndcMin).cwiseMax(Eigen::Vector2d::Zero());
    const double maxExtent = std::max(extents.x(), extents.y());
    if (! std::isfinite(maxExtent) || maxExtent <= 1.0e-6) {
        return 1.0f;
    }

    constexpr double kTargetMaxExtent = 1.90;
    if (maxExtent <= kTargetMaxExtent) {
        LOG_INFO("skip auto-fit static model scale: %s projectedWidth=%.3f projectedHeight=%.3f target=%.3f",
                 model_obj.model.c_str(),
                 extents.x(),
                 extents.y(),
                 kTargetMaxExtent);
        return 1.0f;
    }

    const float scale =
        std::clamp(static_cast<float>(kTargetMaxExtent / maxExtent), 0.05f, 1.0f);
    LOG_INFO("auto-fit static model scale: %s scale=%.4f projectedWidth=%.3f projectedHeight=%.3f target=%.3f",
             model_obj.model.c_str(),
             scale,
             extents.x(),
             extents.y(),
             kTargetMaxExtent);
    return scale;
}

void LogStaticModelProjectedBounds(ParseContext&                    context,
                                   const wpscene::WPModelObject&   model_obj,
                                   const StaticModelBasisChoice&   basisChoice,
                                   float                           staticFallbackScale,
                                   const std::vector<WPMdl::Submesh>& submeshes) {
    const PerspectiveCameraFrame cameraFrame = ResolvePerspectiveCameraFrame(context);
    if (! cameraFrame.valid) {
        return;
    }

    const double halfVerticalFov = cameraFrame.verticalFovRadians * 0.5;
    const double tanVertical     = std::tan(halfVerticalFov);
    const double tanHorizontal   = tanVertical * cameraFrame.aspect;
    if (! std::isfinite(tanVertical) || tanVertical <= 1.0e-9 ||
        ! std::isfinite(tanHorizontal) || tanHorizontal <= 1.0e-9) {
        return;
    }

    Eigen::Vector3f authoredScale(model_obj.scale.data());
    authoredScale *= staticFallbackScale;
    const Eigen::Affine3d modelTransform =
        BuildLocalTransform(Eigen::Vector3f(model_obj.origin.data()),
                            authoredScale,
                            Eigen::Vector3f(model_obj.angles.data()));

    for (const auto& submesh : submeshes) {
        size_t frontCount = 0;
        Eigen::Vector2d ndcMin(std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::infinity());
        Eigen::Vector2d ndcMax(-std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity());
        double depthMin = std::numeric_limits<double>::infinity();
        double depthMax = -std::numeric_limits<double>::infinity();
        Eigen::Vector2f uv0Min = Eigen::Vector2f::Constant(std::numeric_limits<float>::infinity());
        Eigen::Vector2f uv0Max =
            Eigen::Vector2f::Constant(-std::numeric_limits<float>::infinity());
        Eigen::Vector2f uv1Min = Eigen::Vector2f::Constant(std::numeric_limits<float>::infinity());
        Eigen::Vector2f uv1Max =
            Eigen::Vector2f::Constant(-std::numeric_limits<float>::infinity());

        for (const auto& vertex : submesh.vertexs) {
            Eigen::Vector3f local = basisChoice.linear * Eigen::Vector3f(vertex.position.data());
            const Eigen::Vector3d world = modelTransform * local.cast<double>();
            const Eigen::Vector3d relative = world - cameraFrame.eye;
            const double depth = relative.dot(cameraFrame.forward);
            if (! std::isfinite(depth) || depth <= 1.0e-6) {
                continue;
            }

            const double x = relative.dot(cameraFrame.right) / (depth * tanHorizontal);
            const double y = relative.dot(cameraFrame.up) / (depth * tanVertical);
            if (! std::isfinite(x) || ! std::isfinite(y)) {
                continue;
            }

            ++frontCount;
            ndcMin = ndcMin.cwiseMin(Eigen::Vector2d(x, y));
            ndcMax = ndcMax.cwiseMax(Eigen::Vector2d(x, y));
            depthMin = std::min(depthMin, depth);
            depthMax = std::max(depthMax, depth);
            uv0Min = uv0Min.cwiseMin(Eigen::Vector2f(vertex.texcoord.data()));
            uv0Max = uv0Max.cwiseMax(Eigen::Vector2f(vertex.texcoord.data()));
            uv1Min = uv1Min.cwiseMin(Eigen::Vector2f(vertex.texcoord2.data()));
            uv1Max = uv1Max.cwiseMax(Eigen::Vector2f(vertex.texcoord2.data()));
        }

        if (frontCount == 0) {
            LOG_INFO("static submesh projected bounds: %s material=%s front=0",
                     model_obj.model.c_str(),
                     submesh.mat_json_file.c_str());
            continue;
        }

        LOG_INFO("static submesh projected bounds: %s material=%s front=%zu ndcMin=(%.3f, %.3f) ndcMax=(%.3f, %.3f) depth=(%.3f, %.3f)",
                 model_obj.model.c_str(),
                 submesh.mat_json_file.c_str(),
                 frontCount,
                 ndcMin.x(),
                 ndcMin.y(),
                 ndcMax.x(),
                 ndcMax.y(),
                 depthMin,
                 depthMax);
        LOG_INFO("static submesh texcoord bounds: %s material=%s uv0Min=(%.3f, %.3f) uv0Max=(%.3f, %.3f) uv1Min=(%.3f, %.3f) uv1Max=(%.3f, %.3f)",
                 model_obj.model.c_str(),
                 submesh.mat_json_file.c_str(),
                 uv0Min.x(),
                 uv0Min.y(),
                 uv0Max.x(),
                 uv0Max.y(),
                 uv1Min.x(),
                 uv1Min.y(),
                 uv1Max.x(),
                 uv1Max.y());
    }
}

// mapRate < 1.0
void GenCardMesh(SceneMesh& mesh, const std::array<uint16_t, 2> size,
                 const std::array<float, 2> mapRate = { 1.0f, 1.0f }) {
    float left   = -(size[0] / 2.0f);
    float right  = size[0] / 2.0f;
    float bottom = -(size[1] / 2.0f);
    float top    = size[1] / 2.0f;
    float z      = 0.0f;

    float tw = mapRate[0], th = mapRate[1];

    // clang-format off
	const std::array pos = {
		left, bottom, z,
		left,  top, z,
		right, bottom, z,
		right,  top, z,
	};
	const std::array texCoord = {
		0.0f, th,
		0.0f, 0.0f,
		tw, th,
		tw, 0.0f,
	};
    // clang-format on

    SceneVertexArray vertex(
        {
            { WE_IN_POSITION.data(), VertexType::FLOAT3 },
            { WE_IN_TEXCOORD.data(), VertexType::FLOAT2 },
        },
        4);
    vertex.SetVertex(WE_IN_POSITION, pos);
    vertex.SetVertex(WE_IN_TEXCOORD, texCoord);
    mesh.AddVertexArray(std::move(vertex));
}

void SetParticleMesh(SceneMesh& mesh, const wpscene::Particle& particle, uint32_t count,
                     bool thick_format) {
    (void)particle;
    std::vector<SceneVertexArray::SceneVertexAttribute> attrs {
        { WE_IN_POSITION.data(), VertexType::FLOAT3 },
        { WE_IN_TEXCOORDVEC4.data(), VertexType::FLOAT4 },
        { WE_IN_COLOR.data(), VertexType::FLOAT4 },
    };
    if (thick_format) {
        attrs.push_back({ WE_IN_TEXCOORDVEC4C1.data(), VertexType::FLOAT4 });
    }
    attrs.push_back({ WE_IN_TEXCOORDC2.data(), VertexType::FLOAT2 });
    mesh.AddVertexArray(SceneVertexArray(attrs, count * 4));
    mesh.AddIndexArray(SceneIndexArray(count));
    mesh.GetVertexArray(0).SetOption(WE_CB_THICK_FORMAT, thick_format);
}

void SetRopeParticleMesh(SceneMesh& mesh, const wpscene::Particle& particle, uint32_t count,
                         bool thick_format) {
    (void)particle;
    std::vector<SceneVertexArray::SceneVertexAttribute> attrs {
        { WE_IN_POSITIONVEC4.data(), VertexType::FLOAT4 },
        { WE_IN_TEXCOORDVEC4.data(), VertexType::FLOAT4 },
        { WE_IN_TEXCOORDVEC4C1.data(), VertexType::FLOAT4 },
    };
    if (thick_format) {
        attrs.push_back({ WE_IN_TEXCOORDVEC4C2.data(), VertexType::FLOAT4 });
        attrs.push_back({ WE_IN_TEXCOORDVEC4C3.data(), VertexType::FLOAT4 });
        attrs.push_back({ WE_IN_TEXCOORDC4.data(), VertexType::FLOAT4 });
    } else {
        attrs.push_back({ WE_IN_TEXCOORDVEC3C2.data(), VertexType::FLOAT4 });
        attrs.push_back({ WE_IN_TEXCOORDC3.data(), VertexType::FLOAT4 });
    }
    attrs.push_back({ WE_IN_COLOR.data(), VertexType::FLOAT4 });
    mesh.AddVertexArray(SceneVertexArray(attrs, count * 4));
    mesh.AddIndexArray(SceneIndexArray(count));
    mesh.GetVertexArray(0).SetOption(WE_PRENDER_ROPE, true);
    mesh.GetVertexArray(0).SetOption(WE_CB_THICK_FORMAT, thick_format);
}

ParticleAnimationMode ToAnimMode(const std::string& str) {
    if (str == "randomframe")
        return ParticleAnimationMode::RANDOMONE;
    else if (str == "sequence")
        return ParticleAnimationMode::SEQUENCE;
    else {
        return ParticleAnimationMode::SEQUENCE;
    }
}

void LoadControlPoint(ParticleSubSystem& pSys, const wpscene::Particle& wp) {
    std::span<ParticleControlpoint> pcs = pSys.Controlpoints();
    usize                           s   = std::min(pcs.size(), wp.controlpoints.size());
    for (usize i = 0; i < s; i++) {
        pcs[i].offset = Eigen::Vector3d { array_cast<double>(wp.controlpoints[i].offset).data() };
        pcs[i].link_mouse =
            wp.controlpoints[i].flags[wpscene::ParticleControlpoint::FlagEnum::link_mouse];
        pcs[i].worldspace =
            wp.controlpoints[i].flags[wpscene::ParticleControlpoint::FlagEnum::worldspace];
    }
}
void LoadInitializer(ParticleSubSystem& pSys, const wpscene::Particle& wp,
                     const wpscene::ParticleInstanceoverride& over) {
    for (const auto& ini : wp.initializers) {
        pSys.AddInitializer(WPParticleParser::genParticleInitOp(ini));
    }
    if (over.enabled) pSys.AddInitializer(WPParticleParser::genOverrideInitOp(over));
}
void LoadOperator(ParticleSubSystem& pSys, const wpscene::Particle& wp,
                  const wpscene::ParticleInstanceoverride& over) {
    for (const auto& op : wp.operators) {
        pSys.AddOperator(WPParticleParser::genParticleOperatorOp(op, over));
    }
}
void LoadEmitter(ParticleSubSystem& pSys, const wpscene::Particle& wp, float count,
                 bool render_rope) {
    bool sort = render_rope;
    for (const auto& em : wp.emitters) {
        auto newEm = em;
        newEm.rate *= count;
        // newEm.origin[2] -= perspectiveZ;
        pSys.AddEmitter(WPParticleParser::genParticleEmittOp(newEm, sort));
    }
}

ParticleSubSystem::SpawnType ParseSpawnType(std::string_view str) {
    using ST = ParticleSubSystem::SpawnType;
    ST type { ST::STATIC };
    if (str == "eventfollow") {
        type = ST::EVENT_FOLLOW;
    } else if (str == "eventspawn") {
        type = ST::EVENT_SPAWN;
    } else if (str == "eventdeath") {
        type = ST::EVENT_DEATH;
    }
    return type;
};

BlendMode ParseBlendMode(std::string_view str) {
    BlendMode bm;
    if (str == "translucent") {
        bm = BlendMode::Translucent;
    } else if (str == "additive") {
        bm = BlendMode::Additive;
    } else if (str == "normal") {
        bm = BlendMode::Normal;
    } else if (str == "disabled") {
        // seems disabled is normal
        bm = BlendMode::Normal;
    } else {
        bm = BlendMode::Normal;
        LOG_ERROR("unknown blending: %s", str.data());
    }
    return bm;
}

std::string ResolveFallbackTextureName(std::string_view name) {
    if (name == "_rt_shadowAtlas") {
        LOG_INFO("substituting unsupported shadow atlas with neutral texture");
        return "util/white";
    }

    if (name == "_alias_lightCookie" || name == "materials/_alias_lightCookie" ||
        send_with(name, "/_alias_lightCookie") || name == "_alias_lightCookie.tex" ||
        name == "materials/_alias_lightCookie.tex" || send_with(name, "/_alias_lightCookie.tex")) {
        LOG_INFO("substituting missing light cookie alias with neutral texture: %s",
                 std::string(name).c_str());
        return "util/white";
    }

    return std::string(name);
}

std::string CanonicalizeRuntimeRenderTargetName(std::string_view baseName,
                                                std::string_view uniqueSuffix) {
    std::string result;
    if (IsSpecTex(baseName)) {
        result = std::string(baseName);
    } else if (! baseName.empty() && baseName.front() == '_') {
        result = std::string(WE_SPEC_PREFIX.substr(0, WE_SPEC_PREFIX.size() - 1)) +
                 std::string(baseName);
    } else {
        result = std::string(WE_SPEC_PREFIX) + std::string(baseName);
    }

    if (! uniqueSuffix.empty()) {
        result += "_";
        result += uniqueSuffix;
    }
    return result;
}

void ParseSpecTexName(std::string& name, const wpscene::WPMaterial& wpmat,
                      const WPShaderInfo& sinfo) {
    if (IsSpecTex(name)) {
        if (name == "_rt_FullFrameBuffer") {
            name = SpecTex_Default;
            if (wpmat.shader == "genericimage2" && ! exists(sinfo.combos, "BLENDMODE")) name = "";
            /*
            if(wpmat.shader == "genericparticle") {
                name = "_rt_ParticleRefract";
            }
            */
        } else if (sstart_with(name, WE_IMAGE_LAYER_COMPOSITE_PREFIX)) {
            LOG_INFO("link tex \"%s\"", name.c_str());
            int         wpid { -1 };
            std::regex  reImgId { R"(_rt_imageLayerComposite_([0-9]+))" };
            std::smatch match;
            if (std::regex_search(name, match, reImgId)) {
                STRTONUM(std::string(match[1]), wpid);
            }
            name = GenLinkTex((u32)wpid);
        } else if (sstart_with(name, WE_MIP_MAPPED_FRAME_BUFFER)) {
        } else if (name == WE_REFLECTION_BUFFER) {
        } else if (sstart_with(name, WE_EFFECT_PPONG_PREFIX)) {
        } else if (sstart_with(name, WE_HALF_COMPO_BUFFER_PREFIX)) {
        } else if (sstart_with(name, WE_QUARTER_COMPO_BUFFER_PREFIX)) {
        } else if (sstart_with(name, WE_FULL_COMPO_BUFFER_PREFIX)) {
        } else {
            // Scene-local effects synthesize additional _rt_* names during parse.
        }
    }
}

void AttachObjectNode(ParseContext&                    context,
                      const std::shared_ptr<SceneNode>& node,
                      int32_t                           id,
                      int32_t                           parentId) {
    if (! node) {
        return;
    }

    if (parentId > 0 && parentId != id) {
        auto parentIt = context.object_nodes.find(parentId);
        if (parentIt != context.object_nodes.end()) {
            parentIt->second->AppendChild(node);
        } else {
            context.deferred_children[parentId].push_back(node);
        }
    } else {
        context.scene->sceneGraph->AppendChild(node);
    }

    if (id <= 0) {
        return;
    }

    context.object_nodes[id] = node;
    auto deferredIt = context.deferred_children.find(id);
    if (deferredIt == context.deferred_children.end()) {
        return;
    }

    for (const auto& child : deferredIt->second) {
        node->AppendChild(child);
    }
    context.deferred_children.erase(deferredIt);
}

bool LoadMaterial(fs::VFS& vfs, const wpscene::WPMaterial& wpmat, Scene* pScene, SceneNode* pNode,
                  SceneMaterial* pMaterial, WPShaderValueData* pSvData,
                  WPShaderInfo* pWPShaderInfo = nullptr) {
    (void)pNode;

    auto& svData   = *pSvData;
    auto& material = *pMaterial;

    std::unique_ptr<WPShaderInfo> upWPShaderInfo(nullptr);
    if (pWPShaderInfo == nullptr) {
        upWPShaderInfo = std::make_unique<WPShaderInfo>();
        pWPShaderInfo  = upWPShaderInfo.get();
    }

    SceneMaterialCustomShader materialShader;

    auto& shader = materialShader.shader;
    shader       = std::make_shared<SceneShader>();
    shader->name = wpmat.shader;

    std::string shaderPath("/assets/shaders/" + wpmat.shader);

    std::array sd_units { WPShaderUnit {
                              .stage           = ShaderType::VERTEX,
                              .src             = fs::GetFileContent(vfs, shaderPath + ".vert"),
                              .preprocess_info = {},
                          },
                          WPShaderUnit {
                              .stage           = ShaderType::FRAGMENT,
                              .src             = fs::GetFileContent(vfs, shaderPath + ".frag"),
                              .preprocess_info = {},
                          } };



    for (const auto& el : wpmat.combos) {
        pWPShaderInfo->combos[el.first] = std::to_string(el.second);
    }

    const bool shaderUsesSkinning =
        exists(pWPShaderInfo->combos, "SKINNING") && pWPShaderInfo->combos.at("SKINNING") != "0";
    const bool shaderUsesChannelMapAlphaMask = exists(pWPShaderInfo->combos, "YAKKAI_CHANNELMAP_ALPHA_MASK") &&
                                               pWPShaderInfo->combos.at("YAKKAI_CHANNELMAP_ALPHA_MASK") != "0";
    const bool shaderUsesChannelMapBaseExclude =
        exists(pWPShaderInfo->combos, "YAKKAI_CHANNELMAP_BASE_EXCLUDE") &&
        pWPShaderInfo->combos.at("YAKKAI_CHANNELMAP_BASE_EXCLUDE") != "0";
    const bool shaderUsesBaseAlphaMask = exists(pWPShaderInfo->combos, "YAKKAI_BASE_ALPHA_MASK") &&
                                         pWPShaderInfo->combos.at("YAKKAI_BASE_ALPHA_MASK") != "0";
    if (wpmat.shader == "puppettexturechannels" && shaderUsesSkinning) {
        sd_units[0].src = InjectPuppetChannelMapSkinning(std::move(sd_units[0].src));
        LOG_INFO("injecting native skinning path into puppettexturechannels vertex shader");
    }
    if (wpmat.shader == "genericimage4" &&
        (shaderUsesChannelMapAlphaMask || shaderUsesChannelMapBaseExclude ||
         shaderUsesBaseAlphaMask)) {
        sd_units[1].src = InjectGenericImage4ChannelMapAlphaMask(std::move(sd_units[1].src));
        LOG_INFO("injecting native channelmap alpha/base mask logic into genericimage4 fragment shader");
    }

    std::vector<WPShaderTexInfo>                 texinfos;
    std::unordered_map<std::string, ImageHeader> texHeaders;
    for (const auto& el : wpmat.textures) {
        const std::string resolvedTexture = ResolveFallbackTextureName(el);
        if (resolvedTexture.empty()) {
            texinfos.push_back({ false });
        } else if (! IsSpecTex(resolvedTexture)) {
            const auto& texh = pScene->imageParser->ParseHeader(resolvedTexture);
            texHeaders[resolvedTexture] = texh;
            if (texh.extraHeader.count("compo1") == 0) {
                texinfos.push_back({ false });
                continue;
            }
            texinfos.push_back({ true,
                                 {
                                     (bool)texh.extraHeader.at("compo1").val,
                                     (bool)texh.extraHeader.at("compo2").val,
                                     (bool)texh.extraHeader.at("compo3").val,
                                 } });
        } else
            texinfos.push_back({ true });
    }

    // Disable shadow mapping and light cookies — we don't render shadow
    // depth maps or cookie textures. LIGHTING itself is kept enabled so
    // PerformLighting_V1 can compute point light contributions.
    if (exists(pWPShaderInfo->combos, "LIGHTS_SHADOW_MAPPING")) {
        pWPShaderInfo->combos["LIGHTS_SHADOW_MAPPING"] = "0";
    }
    if (exists(pWPShaderInfo->combos, "LIGHTS_COOKIE")) {
        pWPShaderInfo->combos["LIGHTS_COOKIE"] = "0";
    }

    for (auto& unit : sd_units) {
        unit.src = WPShaderParser::PreShaderSrc(vfs, unit.src, pWPShaderInfo, texinfos);
    }
    if (wpmat.shader == "genericimage4" && shaderUsesChannelMapAlphaMask) {
        pWPShaderInfo->combos["REFLECTION"] = "0";
        pWPShaderInfo->combos["NORMALMAP"]  = "0";
        pWPShaderInfo->combos["PBRMASKS"]   = "0";
    }

    if (wpmat.shader == "genericimage4") {
        const bool vertexHasBones = sd_units[0].src.find("g_Bones[") != std::string::npos;
        const bool vertexHasBlendWeights = sd_units[0].src.find("a_BlendWeights") != std::string::npos;
        const auto boneCountIt = pWPShaderInfo->combos.find("BONECOUNT");
        LOG_INFO("genericimage4 shader resolved: usePuppet=%d comboSKINNING=%d comboBONECOUNT=%s vertexHasBones=%d vertexHasBlendWeights=%d",
                 wpmat.use_puppet ? 1 : 0,
                 shaderUsesSkinning ? 1 : 0,
                 boneCountIt != pWPShaderInfo->combos.end() ? boneCountIt->second.c_str() : "",
                 vertexHasBones ? 1 : 0,
                 vertexHasBlendWeights ? 1 : 0);
    }

    shader->default_uniforms = pWPShaderInfo->svs;

    auto textures = wpmat.textures;
    if (pWPShaderInfo->defTexs.size() > 0) {
        for (auto& t : pWPShaderInfo->defTexs) {
            if (textures.size() > t.first) {
                if (! textures.at(t.first).empty()) continue;
            } else {
                textures.resize(t.first + 1);
            }
            textures[t.first] = t.second;
        }
    }

    for (auto& texture : textures) {
        texture = ResolveFallbackTextureName(texture);
    }

    for (usize i = 0; i < textures.size(); i++) {
        std::string name = textures.at(i);
        ParseSpecTexName(name, wpmat, *pWPShaderInfo);
        material.textures.push_back(name);
        material.defines.push_back("g_Texture" + std::to_string(i));
        if (name.empty()) {
            continue;
        }

        std::array<i32, 4> resolution {};
        if (IsSpecTex(name)) {
            if (IsSpecLinkTex(name)) {
                svData.renderTargets.push_back({ i, name });
            } else if (pScene->renderTargets.count(name) == 0) {
                LOG_ERROR("%s not found in render targes", name.c_str());
            } else {
                svData.renderTargets.push_back({ i, name });
                const auto& rt = pScene->renderTargets.at(name);
                resolution     = { rt.width, rt.height, rt.width, rt.height };
            }
        } else {
            const ImageHeader& texh = texHeaders.count(name) == 0
                                          ? pScene->imageParser->ParseHeader(name)
                                          : texHeaders.at(name);
            if (i == 0) {
                if (texh.format == TextureFormat::R8)
                    pWPShaderInfo->combos["TEX0FORMAT"] = "FORMAT_R8";
                else if (texh.format == TextureFormat::RG8)
                    pWPShaderInfo->combos["TEX0FORMAT"] = "FORMAT_RG88";
            }
            if (texh.mipmap_larger) {
                resolution = { texh.width, texh.height, texh.mapWidth, texh.mapHeight };
            } else {
                resolution = { texh.mapWidth, texh.mapHeight, texh.mapWidth, texh.mapHeight };
            }

            if (pScene->textures.count(name) == 0) {
                SceneTexture stex;
                stex.sample = texh.sample;
                stex.url    = name;
                if (texh.isSprite) {
                    stex.isSprite   = texh.isSprite;
                    stex.spriteAnim = texh.spriteAnim;
                }
                pScene->textures[name] = stex;
            }
            if ((pScene->textures.at(name)).isSprite) {
                material.hasSprite = true;
const auto& f1     = texh.spriteAnim.GetCurFrame();
                if (wpmat.shader == "genericparticle" || wpmat.shader == "genericropeparticle") {
                    pWPShaderInfo->combos["SPRITESHEET"] = "1";
                    pWPShaderInfo->combos["THICKFORMAT"] = "1";
                    if (algorism::IsPowOfTwo((u32)texh.width) &&
                        algorism::IsPowOfTwo((u32)texh.height)) {
                        pWPShaderInfo->combos["SPRITESHEETBLENDNPOT"] = "1";
                        resolution[2] = resolution[0] - resolution[0] % (int)f1.width;
                        resolution[3] = resolution[1] - resolution[1] % (int)f1.height;
                    }
                    materialShader.constValues["g_RenderVar1"] = std::array {
                        f1.xAxis[0], f1.yAxis[1], (float)(texh.spriteAnim.numFrames()), f1.rate
                    };
                }
            }
        }
        if (! resolution.empty()) {
            const std::string gResolution = WE_GLTEX_RESOLUTION_NAMES[i];

            materialShader.constValues[gResolution] = array_cast<float>(resolution);
        }
    }
    if (! WPShaderParser::CompileToSpv(
            pScene->scene_id, sd_units, shader->codes, vfs, pWPShaderInfo, texinfos)) {
        return false;
    }

    material.blenmode = ParseBlendMode(wpmat.blending);


    for (uint i = 0; i < material.textures.size(); i++) {
        if (! exists(sd_units[1].preprocess_info.active_tex_slots, i)) material.textures[i].clear();
    }

    for (const auto& el : pWPShaderInfo->baseConstSvs) {
        materialShader.constValues[el.first] = el.second;
    }
    material.customShader = materialShader;
    material.name         = wpmat.shader;

    return true;
}

void LoadAlignment(SceneNode& node, std::string_view align, Vector2f size) {
    Vector3f trans = node.Translate();
    size *= 0.5f;
    size.y() *= 1.0f;

    auto contains = [&](std::string_view s) {
        return align.find(s) != std::string::npos;
    };

    // topleft top center ...
    if (contains("top")) trans.y() -= size.y();
    if (contains("left")) trans.x() += size.x();
    if (contains("right")) trans.x() -= size.x();
    if (contains("bottom")) trans.y() += size.y();

    node.SetTranslate(trans);
}

void LoadConstvalue(SceneMaterial& material, const wpscene::WPMaterial& wpmat,
                    const WPShaderInfo& info) {
    // load glname from alias and load to constvalue
    for (const auto& cs : wpmat.constantshadervalues) {
        const auto&               name  = cs.first;
        const std::vector<float>& value = cs.second;
        std::string               glname;
        if (info.alias.count(name) != 0) {
            glname = info.alias.at(name);
        } else {
            for (const auto& el : info.alias) {
                if (el.second.substr(2) == name) {
                    glname = el.second;
                    break;
                }
            }
        }
        if (glname.empty()) {
            LOG_ERROR("ShaderValue: %s not found in glsl", name.c_str());
        } else {
            material.customShader.constValues[glname] = value;
        }
    }
}

// parse

void ParseCamera(ParseContext& context, const wpscene::WPScene& sc, bool useScenePerspectiveCamera) {
    auto& general = sc.general;
    auto& scene = *context.scene;
    // effect camera
    scene.cameras["effect"]    = std::make_shared<SceneCamera>(2, 2, -1.0f, 1.0f);
    context.effect_camera_node = std::make_shared<SceneNode>(); // at 0,0,0
    scene.cameras.at("effect")->AttatchNode(context.effect_camera_node);
    scene.sceneGraph->AppendChild(context.effect_camera_node);

    // global camera
    scene.cameras["global"] = std::make_shared<SceneCamera>((context.ortho_w / (i32)general.zoom),
                                                            (context.ortho_h / (i32)general.zoom),
                                                            -5000.0f,
                                                            5000.0f);
    scene.activeCamera      = scene.cameras.at("global").get();
    Vector3f cori { (float)context.ortho_w / 2.0f, (float)context.ortho_h / 2.0f, 0 },
        cscale { 1.0f, 1.0f, 1.0f }, cangle(Vector3f::Zero());

    context.global_camera_node = std::make_shared<SceneNode>(cori, cscale, cangle);
    scene.activeCamera->AttatchNode(context.global_camera_node);
    scene.sceneGraph->AppendChild(context.global_camera_node);

    scene.cameras["global_perspective"] =
        std::make_shared<SceneCamera>((float)context.ortho_w / (float)context.ortho_h,
                                      general.nearz,
                                      general.farz,
                                      ResolvePerspectiveFov(general, context.ortho_h));

    Vector3f cperori   = cori;
    Vector3f cperangle = cangle;
    WPCameraPathAnimation perspectiveAnimation;
    cperori[2]         = 1000.0f;
    if (useScenePerspectiveCamera) {
        const CameraPose perspectivePose =
            ResolveScenePerspectivePose(sc, *context.vfs, &perspectiveAnimation);
        context.scene_perspective_pose = perspectivePose;
        context.has_scene_perspective_pose = true;
        cperori   = Vector3f(perspectivePose.eye.data());
        cperangle = ComputeCameraNodeRotation(perspectivePose.eye, perspectivePose.center, perspectivePose.up);
    }
    context.global_perspective_camera_node = std::make_shared<SceneNode>(cperori, cscale, cangle);
    context.global_perspective_camera_node->SetRotation(cperangle);
    scene.cameras["global_perspective"]->AttatchNode(context.global_perspective_camera_node);
    scene.sceneGraph->AppendChild(context.global_perspective_camera_node);
    if (perspectiveAnimation.valid) {
        context.shader_updater->SetPerspectiveCameraPath(scene.cameras.at("global_perspective"),
                                                         context.global_perspective_camera_node,
                                                         std::move(perspectiveAnimation));
    }
}

void InitContext(ParseContext& context, fs::VFS& vfs, wpscene::WPScene& sc) {
    context.scene            = std::make_shared<Scene>();
    context.vfs              = &vfs;
    auto& scene              = *context.scene;
    scene.imageParser        = std::make_unique<WPTexImageParser>(&vfs);
    scene.paritileSys->gener = std::make_unique<WPParticleRawGener>();
    scene.shaderValueUpdater = std::make_unique<WPShaderValueUpdater>(&scene);
    GenCardMesh(scene.default_effect_mesh, { 2, 2 });
    context.shader_updater = static_cast<WPShaderValueUpdater*>(scene.shaderValueUpdater.get());

    scene.clearColor = sc.general.clearcolor;

    // Apply detected tint overlays to the clear color. WE scenes with
    // script-driven color overlays use solid layers to tint the background,
    // but our solid layer rendering is z-order dependent. As a fallback,
    // blend the tint colors into the clear color itself.
    // Scene property color overlays: use the most saturated color property
    // to tint the clear color. This provides a colored background that shows
    // through transparent areas of layers. BC3 textures have their own colors
    // baked in, so we don't tint the layer textures — only the background.
    // Blend the clear color with the most saturated scene color property.
    // Textures have their own colors baked in (BC3/DXT5) — the clear color
    // only shows where layers are transparent. A 50/50 blend keeps the
    // atmosphere color visible without crushing the overall brightness.
    if (! context.pending_tint_overlays.empty()) {
        auto& cc = scene.clearColor;
        for (const auto& overlay : context.pending_tint_overlays) {
            float gray = (overlay.color[0] + overlay.color[1] + overlay.color[2]) / 3.0f;
            float sat = std::abs(overlay.color[0] - gray) +
                        std::abs(overlay.color[1] - gray) +
                        std::abs(overlay.color[2] - gray);
            if (sat > 0.05f) {
                // Blend 50/50 between original clear color and the atmosphere
                // property. This provides a medium-dark background that
                // matches the general tone of the BC3 texture colors.
                cc[0] = (cc[0] + overlay.color[0]) * 0.5f;
                cc[1] = (cc[1] + overlay.color[1]) * 0.5f;
                cc[2] = (cc[2] + overlay.color[2]) * 0.5f;
                LOG_INFO("tint-adjusted clear color: (%.3f,%.3f,%.3f)", cc[0], cc[1], cc[2]);
                break;
            }
        }
    }
    scene.ortho[0]   = sc.general.orthogonalprojection.width;
    scene.ortho[1]   = sc.general.orthogonalprojection.height;
    context.ortho_w  = scene.ortho[0];
    context.ortho_h  = scene.ortho[1];

    {
        auto& gb              = context.global_base_uniforms;
        gb["g_ViewUp"]        = std::array { 0.0f, 1.0f, 0.0f };
        gb["g_ViewRight"]     = std::array { 1.0f, 0.0f, 0.0f };
        gb["g_ViewForward"]   = std::array { 0.0f, 0.0f, -1.0f };
        gb["g_EyePosition"]   = std::array { 0.0f, 0.0f, 0.0f };
        gb["g_TexelSize"]     = std::array { 1.0f / 1920.0f, 1.0f / 1080.0f };
        gb["g_TexelSizeHalf"] = std::array { 1.0f / 1920.0f / 2.0f, 1.0f / 1080.0f / 2.0f };

        gb["g_LightAmbientColor"] = sc.general.ambientcolor;
        gb["g_LightSkylightColor"] = sc.general.skylightcolor;
        gb["g_NormalModelMatrix"] = ShaderValue::fromMatrix(Matrix4f::Identity());
    }

    {
        WPCameraParallax cam_para;
        cam_para.enable         = sc.general.cameraparallax;
        cam_para.amount         = sc.general.cameraparallaxamount;
        cam_para.delay          = sc.general.cameraparallaxdelay;
        cam_para.mouseinfluence = sc.general.cameraparallaxmouseinfluence;
        context.shader_updater->SetCameraParallax(cam_para);
    }
}

void ParseImageObj(ParseContext& context, wpscene::WPImageObject& img_obj) {
    auto& wpimgobj = img_obj;
    if (! wpimgobj.visible) return;
    // Hide children of invisible container objects
    if (wpimgobj.parent > 0 && context.hidden_containers.count(wpimgobj.parent)) return;
    if (DebugSkipLayerByName(wpimgobj.name)) {
        LOG_INFO("debug skipping image layer: name=%s id=%d image=%s",
                 wpimgobj.name.c_str(),
                 wpimgobj.id,
                 wpimgobj.image.c_str());
        return;
    }

    auto& vfs = *context.vfs;

    if (const auto pauseIt = context.paused_puppet_animations.find(wpimgobj.name);
        pauseIt != context.paused_puppet_animations.end()) {
        for (auto& layer : wpimgobj.puppet_layers) {
            if (! layer.name.empty() && pauseIt->second.count(layer.name) > 0) {
                layer.paused = true;
                LOG_INFO("pausing puppet animation layer for native fallback: image=%s animation=%s",
                         wpimgobj.name.c_str(),
                         layer.name.c_str());
            }
        }
    }

    // Apply pending tint overlays to solid layers with default black color.
    // WE scenes use scripts to set solid layer colors from scene properties;
    // we statically resolve these by matching solid layers to detected
    // colour/opacity property pairs in order.
    // Only apply to generic solid/color layers, not audio bars or named UI elements
    const bool isSolidColorOverlay =
        wpimgobj.image == "models/util/solidlayer.json" &&
        wpimgobj.color[0] == 0.0f && wpimgobj.color[1] == 0.0f && wpimgobj.color[2] == 0.0f &&
        (wpimgobj.name.find("Solid") != std::string::npos ||
         wpimgobj.name.find("solid") != std::string::npos ||
         wpimgobj.name == "\xe7\xba\xaf\xe8\x89\xb2" /* 纯色 */ ||
         wpimgobj.name.empty());
    if (isSolidColorOverlay && ! context.pending_tint_overlays.empty()) {
        auto overlay = context.pending_tint_overlays.front();
        context.pending_tint_overlays.erase(context.pending_tint_overlays.begin());
        wpimgobj.color = overlay.color;
        wpimgobj.alpha = overlay.alpha;
        LOG_INFO("applied tint overlay to solid layer: name=%s id=%d color=(%.3f,%.3f,%.3f) alpha=%.2f",
                 wpimgobj.name.c_str(), wpimgobj.id,
                 overlay.color[0], overlay.color[1], overlay.color[2], overlay.alpha);
    }

    // coloBlendMode load passthrough manaully
    if (wpimgobj.colorBlendMode != 0) {
        wpscene::WPImageEffect colorEffect;
        wpscene::WPMaterial    colorMat;
        nlohmann::json         json;
        if (! PARSE_JSON(fs::GetFileContent(vfs, "/assets/materials/util/effectpassthrough.json"),
                         json))
            return;
        colorMat.FromJson(json);
        colorMat.combos["BONECOUNT"] = 1;
        colorMat.combos["BLENDMODE"] = wpimgobj.colorBlendMode;
        colorMat.blending            = "disabled";
        colorEffect.materials.push_back(colorMat);
        wpimgobj.effects.push_back(colorEffect);
    }

    int32_t count_eff = 0;
    for (const auto& wpeffobj : wpimgobj.effects) {
        if (wpeffobj.visible) count_eff++;
    }
    bool hasEffect = count_eff > 0;
    std::vector<wpscene::WPImageEffect> effectObjects = wpimgobj.effects;

    bool hasPuppet = ! wpimgobj.puppet.empty();
    (void)hasPuppet;

    bool isCompose = (wpimgobj.image == "models/util/composelayer.json");

    // Skip effectless fullscreen/compose layers — they're no-ops
    if (! hasEffect && wpimgobj.fullscreen) return;
    if (! hasEffect && isCompose) return;

    // Debug: YAKKAI_NO_EFFECTS=1 strips all effects for color debugging
    if (std::getenv("YAKKAI_NO_EFFECTS")) {
        count_eff = 0;
        hasEffect = false;
        effectObjects.clear();
        if (isCompose) return; // skip composelayer entirely
    }

    if (isCompose) {
        // Don't force fullscreen — use the composelayer's own size for render targets.
        LOG_INFO("processing composelayer: name=%s id=%d effects=%d fullscreen=%d size=(%.0f,%.0f)",
                 wpimgobj.name.c_str(), wpimgobj.id, count_eff,
                 wpimgobj.fullscreen ? 1 : 0,
                 wpimgobj.size[0], wpimgobj.size[1]);
    }

    std::unique_ptr<WPMdl> puppet;
    if (! wpimgobj.puppet.empty()) {
        puppet = std::make_unique<WPMdl>();
        if (! WPMdlParser::Parse(wpimgobj.puppet, vfs, *puppet)) {
            LOG_ERROR("parse puppet failed: %s", wpimgobj.puppet.c_str());
            puppet = nullptr;
        }
        else if (puppet->puppet->bones.size() == 0){
            LOG_ERROR("puppet has no bones: %s", wpimgobj.puppet.c_str());
            puppet = nullptr;
        }
        if (puppet) {
            context.puppet_parse_successes++;
        }
    }

    // Apply script-resolved origin binding before creating the scene node.
    {
        auto it = context.script_color_bindings.find(wpimgobj.id);
        if (it != context.script_color_bindings.end() && it->second.has_origin) {
            wpimgobj.origin = it->second.origin;
        }
    }
    auto spImgNode = std::make_shared<SceneNode>(Vector3f(wpimgobj.origin.data()),
                                                 Vector3f(wpimgobj.scale.data()),
                                                 Vector3f(wpimgobj.angles.data()));
    LoadAlignment(*spImgNode, wpimgobj.alignment, { wpimgobj.size[0], wpimgobj.size[1] });
    spImgNode->ID() = wpimgobj.id;

    SceneMaterial     material;
    WPShaderValueData svData;

    ShaderValueMap baseConstSvs = context.global_base_uniforms;
    WPShaderInfo   shaderInfo;
    wpscene::WPMaterial sourceMaterial = wpimgobj.material;
    // =========================================================================
    // PUPPET SCENE EFFECT HANDLING
    // Smoke test: smoke-tests/3228578419-sleeping-arona.png
    //
    // In puppet scenes, strip only KNOWN-PROBLEMATIC effects (LUT color grading
    // on individual non-fullscreen layers) that produce washed-out output.
    // Keep SAFE effects: waterflow, waterwaves, opacity, shine, iris, shake,
    // pulse, blur — these are distortion/animation effects that render correctly.
    //
    // Flare/lens layers with alpha=0 get alpha forced to 1.0 (script-controlled
    // visibility workaround).
    // =========================================================================
    if (context.has_puppet_objects && hasEffect) {
        bool hasColorkey = false;
        bool hasAudioEffect = false;
        for (const auto& eff : effectObjects) {
            if (eff.name.find("colorkey") != std::string::npos) { hasColorkey = true; }
            if (eff.name.find("audio") != std::string::npos) { hasAudioEffect = true; }
            for (const auto& mat : eff.materials) {
                if (mat.shader.find("colorkey") != std::string::npos) { hasColorkey = true; }
                if (mat.shader.find("audio") != std::string::npos) { hasAudioEffect = true; }
            }
        }
        // Preserve effects for layers that need their effect chain to render:
        // - Colorkey: chroma key removal for video overlays
        // - Flare/lens: need additive blend from effect chain
        // Strip heavy cosmetic effects (lightshafts, audio bars, waterwaves).
        // Detect audio/lightshaft effects — these are heavy and non-essential
        bool hasHeavyEffect = false;
        for (const auto& eff : effectObjects) {
            std::string shader;
            if (! eff.materials.empty()) shader = eff.materials[0].shader;
            if (eff.name.find("audio") != std::string::npos ||
                eff.name.find("lightshaft") != std::string::npos ||
                shader.find("audio") != std::string::npos ||
                shader.find("Audio") != std::string::npos ||
                shader.find("lightshaft") != std::string::npos) {
                hasHeavyEffect = true;
            }
        }
        const bool isEssentialEffect = hasColorkey ||
            (! hasHeavyEffect && (
                wpimgobj.name.find("flare") != std::string::npos ||
                wpimgobj.name.find("lense") != std::string::npos ||
                wpimgobj.name.find("lens") != std::string::npos ||
                wpimgobj.colorBlendMode != 0));
        // The offscreen effect rendering path breaks alpha compositing,
        // causing transparent regions to become opaque (hiding layers behind).
        // Strip effects from all regular layers until the offscreen alpha path
        // is fixed. Composelayer and flare/lens layers keep their effects.
        if (! isEssentialEffect) {
            LOG_INFO("stripping %d effects from layer (alpha fix): name=%s",
                     count_eff, wpimgobj.name.c_str());
            count_eff = 0;
            hasEffect = false;
            effectObjects.clear();
            if (isCompose || wpimgobj.fullscreen) return;
            // Utility layers only exist as effect carriers — hide them entirely
            // when their effects are stripped, since they render as garbage without.
            const bool isUtilityLayer =
                wpimgobj.image.find("solidlayer") != std::string::npos ||
                wpimgobj.image.find("projectlayer") != std::string::npos ||
                wpimgobj.image.find("fullscreenlayer") != std::string::npos;
            if (isUtilityLayer) return;
        } else if (true) {
            // Keep effects for composelayer and flare/lens
        } else {
            // Strip effects to prevent washed-out output from unknown effect types
            const bool isHashElement =
                wpimgobj.name.size() >= 16 &&
                wpimgobj.name.find_first_not_of("0123456789abcdef") == std::string::npos;
            if (! isEssentialEffect && ! isHashElement && ! isCompose) {
                count_eff = 0;
                hasEffect = false;
                effectObjects.clear();
            }
        }
        if (isEssentialEffect && wpimgobj.alpha == 0.0f) {
            wpimgobj.alpha = 1.0f;
        }
    }
    bool                  usePuppetChannelMapPrepass { false };
    bool                  routePuppetPrepassThroughAuthoredEffects { false };
    bool                  useStandalonePuppetFinalDisplay { false };
    bool                  useStandalonePuppetBaseDisplay { false };
    bool                  zeroSlotChannelMapFallback { false };
    std::vector<uint32_t> activePuppetChannelBlendSlots;
    std::vector<WPPuppetLayer::AnimationLayer> renderPuppetLayers = wpimgobj.puppet_layers;
    {
        if (! hasEffect) {
            svData.parallaxDepth = { wpimgobj.parallaxDepth[0], wpimgobj.parallaxDepth[1] };
            if (puppet) {
                WPMdlParser::AddPuppetShaderInfo(shaderInfo, *puppet);
            }
        }

        {
            // Apply composite tint from scene color properties to layer color.
            // Skip puppet layers (need their own luminance contrast).
            // Apply script-resolved color/alpha bindings if available.
            // These override the layer's default color/alpha with values
            // from scene properties (resolved from WE script patterns).
            auto it = context.script_color_bindings.find(wpimgobj.id);
            if (it != context.script_color_bindings.end()) {
                if (it->second.has_color) wpimgobj.color = it->second.color;
                if (it->second.has_alpha) wpimgobj.alpha = it->second.alpha;
            }

            // Textures have their own colors baked in (BC3/DXT5 with RGB).
            // Use g_Color4 from the layer's own color/alpha properties only.
            baseConstSvs["g_Color4"] = std::array<float, 4> {
                wpimgobj.color[0],
                wpimgobj.color[1],
                wpimgobj.color[2],
                wpimgobj.alpha
            };
        }
        baseConstSvs["g_UserAlpha"]  = wpimgobj.alpha;
        baseConstSvs["g_Brightness"] = wpimgobj.brightness;

        if (puppet && hasEffect) {
            usePuppetChannelMapPrepass =
                TryPreparePuppetChannelMapPrepass(wpimgobj,
                                                  *puppet,
                                                  vfs,
                                                  sourceMaterial,
                                                  baseConstSvs,
                                                  &activePuppetChannelBlendSlots);
            if (usePuppetChannelMapPrepass && activePuppetChannelBlendSlots.empty()) {
                LOG_INFO("native puppet channelmap prepass disabled because no active visible blend slots remain: image=%s",
                         wpimgobj.name.c_str());
                usePuppetChannelMapPrepass = false;
                zeroSlotChannelMapFallback = true;
                sourceMaterial             = wpimgobj.material;
                svData.parallaxDepth = { wpimgobj.parallaxDepth[0], wpimgobj.parallaxDepth[1] };
                WPMdlParser::AddPuppetShaderInfo(shaderInfo, *puppet);
                LOG_INFO("native puppet channelmap prepass falling back to ordinary puppet image-effects route: image=%s authoredEffects=%d",
                         wpimgobj.name.c_str(),
                         count_eff);
            } else if (! usePuppetChannelMapPrepass) {
                svData.parallaxDepth = { wpimgobj.parallaxDepth[0], wpimgobj.parallaxDepth[1] };
                WPMdlParser::AddPuppetShaderInfo(shaderInfo, *puppet);
            }
        }

        shaderInfo.baseConstSvs = baseConstSvs;
        if (! LoadMaterial(vfs,
                           sourceMaterial,
                           context.scene.get(),
                           spImgNode.get(),
                           &material,
                           &svData,
                           &shaderInfo)) {
            LOG_ERROR("load imageobj '%s' material faild", wpimgobj.name.c_str());
            return;
        };
        LoadConstvalue(material, sourceMaterial, shaderInfo);
    }

    for (const auto& cs : sourceMaterial.constantshadervalues) {
        const auto&               name  = cs.first;
        const std::vector<float>& value = cs.second;
        std::string               glname;
        if (shaderInfo.alias.count(name) != 0) {
            glname = shaderInfo.alias.at(name);
        } else {
            for (const auto& el : shaderInfo.alias) {
                if (el.second.substr(2) == name) {
                    glname = el.second;
                    break;
                }
            }
        }
        if (glname.empty()) {
            LOG_ERROR("ShaderValue: %s not found in glsl", name.c_str());
        } else {
            material.customShader.constValues[glname] = value;
        }
    }

    // mesh
    SceneMesh effct_final_mesh {};
    auto      spMesh = std::make_shared<SceneMesh>();
    auto&     mesh   = *spMesh;

    {
        // deal with pow of 2
        std::array<float, 2> mapRate { 1.0f, 1.0f };
        if (! wpimgobj.nopadding &&
            exists(material.customShader.constValues, WE_GLTEX_RESOLUTION_NAMES[0])) {
            const auto& r = material.customShader.constValues.at(WE_GLTEX_RESOLUTION_NAMES[0]);
            mapRate       = { r[2] / r[0], r[3] / r[1] };
        }

        if (puppet) {
            if (hasEffect) {
                if (usePuppetChannelMapPrepass) {
                    WPMdlParser::GenPuppetChannelMapBaseUvMesh(mesh,
                                                               *puppet,
                                                               { wpimgobj.size[0], wpimgobj.size[1] });
                    WPMdlParser::GenPuppetMesh(effct_final_mesh, *puppet);
                    LOG_INFO("native puppet channelmap prepass using authored base-uv flat prepass mesh: image=%s bones=%zu",
                             wpimgobj.name.c_str(),
                             puppet->puppet->bones.size());
                    routePuppetPrepassThroughAuthoredEffects = true;
                    useStandalonePuppetFinalDisplay = true;
                    useStandalonePuppetBaseDisplay  = true;
                } else {
                    svData.puppet_layer = WPPuppetLayer(puppet->puppet);
                    svData.puppet_layer.prepared(renderPuppetLayers);
                    WPMdlParser::GenPuppetMesh(mesh, *puppet);
                    GenCardMesh(effct_final_mesh,
                                { (uint16_t)wpimgobj.size[0], (uint16_t)wpimgobj.size[1] });
                    useStandalonePuppetFinalDisplay = true;
                    LOG_INFO("routing full puppet render through offscreen target before flat final effect publish: image=%s authoredEffects=%d",
                             wpimgobj.name.c_str(),
                             count_eff);
                }

                if (routePuppetPrepassThroughAuthoredEffects) {
                    LOG_INFO("routing puppet channelmap prepass through authored image effects before final generic puppet stage: image=%s authoredEffects=%d",
                             wpimgobj.name.c_str(),
                             count_eff);
                }
            } else {
                svData.puppet_layer = WPPuppetLayer(puppet->puppet);
                svData.puppet_layer.prepared(renderPuppetLayers);
                WPMdlParser::GenPuppetMesh(mesh, *puppet);
            }
        }
        if (! puppet) {
            GenCardMesh(mesh, { (uint16_t)wpimgobj.size[0], (uint16_t)wpimgobj.size[1] }, mapRate);
            GenCardMesh(effct_final_mesh,
                        { (uint16_t)wpimgobj.size[0], (uint16_t)wpimgobj.size[1] });
        }
    }
    // material blendmode for last step to use
    auto imgBlendMode = material.blenmode;
    // disable img material blend, as it's the first effect node now
    if (hasEffect) {
        material.blenmode = BlendMode::Normal;
    }
    mesh.AddMaterial(std::move(material));
    spImgNode->AddMesh(spMesh);

    context.shader_updater->SetNodeData(spImgNode.get(), svData);
    SceneNode standaloneDisplayTransform;
    bool      hasStandaloneDisplayTransform = false;
    if (hasEffect) {
        auto& scene = *context.scene;
        // currently use addr for unique
        std::string nodeAddr = getAddr(spImgNode.get());
        // set camera to attatch effect
        if (isCompose) {
            scene.cameras[nodeAddr] =
                std::make_shared<SceneCamera>((int32_t)scene.activeCamera->Width(),
                                              (int32_t)scene.activeCamera->Height(),
                                              -1.0f,
                                              1.0f);
            scene.cameras.at(nodeAddr)->AttatchNode(scene.activeCamera->GetAttachedNode());
            if (scene.linkedCameras.count("global") == 0) scene.linkedCameras["global"] = {};
            scene.linkedCameras.at("global").push_back(nodeAddr);
        } else {
            // Create per-layer effect camera. Positioned after CopyTrans
            // resets the node so the camera matches the node's post-reset
            // world position (parent chain only, no local offset).
            i32 w = (i32)wpimgobj.size[0];
            i32 h = (i32)wpimgobj.size[1];
            scene.cameras[nodeAddr] = std::make_shared<SceneCamera>(w, h, -1.0f, 1.0f);
            // Camera node positioning deferred to after CopyTrans below
        }
        spImgNode->SetCamera(nodeAddr);
        std::string effect_ppong_a, effect_ppong_b;
        effect_ppong_a = WE_EFFECT_PPONG_PREFIX_A.data() + nodeAddr;
        effect_ppong_b = WE_EFFECT_PPONG_PREFIX_B.data() + nodeAddr;
        const bool debugAronaRenderTargets = wpimgobj.name == "ARONA_CROP_SHEET";
        std::string aronaDebugSourceTarget;
        std::string aronaDebugLastEffectTarget;
        // set image effect
        auto imgEffectLayer = std::make_shared<SceneImageEffectLayer>(
            spImgNode.get(), wpimgobj.size[0], wpimgobj.size[1], effect_ppong_a, effect_ppong_b);
        {
            imgEffectLayer->SetFinalBlend(imgBlendMode);
            if (wpimgobj.fullscreen || isCompose) {
                imgEffectLayer->SetFullscreen(true);
            }
            if (useStandalonePuppetFinalDisplay) {
                imgEffectLayer->SetPublishFinalOutput(false);
            }
            imgEffectLayer->FinalMesh().ChangeMeshDataFrom(effct_final_mesh);
            imgEffectLayer->FinalNode().CopyTrans(*spImgNode);
            if (useStandalonePuppetFinalDisplay) {
                standaloneDisplayTransform.CopyTrans(*spImgNode);
                hasStandaloneDisplayTransform = true;
            }
            if (isCompose) {
            } else {
                spImgNode->CopyTrans(SceneNode());
                // Position effect camera at the parent container's world
                // position. After CopyTrans reset, the node's local transform
                // is identity, but the scene graph parent (attached later via
                // AttachObjectNode) will shift the mesh. The camera must match.
                // Note: Parent() is NULL here — node isn't attached yet — so
                // we look up the parent from already-parsed object_nodes.
                // The effect camera stays at (0,0,0) to match the node's
                // post-reset position. The node has no parent during the base
                // pass (AttachObjectNode runs later), so the mesh renders at
                // local origin — the camera must be there too.
                scene.cameras.at(nodeAddr)->AttatchNode(context.effect_camera_node);
            }
            scene.cameras.at(nodeAddr)->AttatchImgEffect(imgEffectLayer);
        }
        // set renderTarget for ping-pong operate
        {
            uint16_t rtW = (uint16_t)wpimgobj.size[0];
            uint16_t rtH = (uint16_t)wpimgobj.size[1];
            scene.renderTargets[effect_ppong_a] = {
                .width      = rtW,
                .height     = rtH,
                .allowReuse = true,
            };
            if (wpimgobj.fullscreen) {
                scene.renderTargets[effect_ppong_a].bind = { .enable = true, .screen = true };
            }
            scene.renderTargets[effect_ppong_b] = scene.renderTargets.at(effect_ppong_a);
            if (debugAronaRenderTargets) {
                aronaDebugSourceTarget     = "_rt_debug_arona_source_" + nodeAddr;
                aronaDebugLastEffectTarget =
                    ResolveEffectPingPongFinalTarget(effect_ppong_a, effect_ppong_b, count_eff);
                scene.renderTargets[aronaDebugSourceTarget] = {
                    .width      = scene.renderTargets.at(effect_ppong_a).width,
                    .height     = scene.renderTargets.at(effect_ppong_a).height,
                    .allowReuse = false,
                };
                scene.debugRenderDumps.push_back({
                    .label        = "arona-source-puppet",
                    .renderTarget = aronaDebugSourceTarget,
                    .path         = "/tmp/yakkai-debug/arona-source-puppet.tga",
                });
                scene.debugRenderDumps.push_back({
                    .label        = "arona-last-effect",
                    .renderTarget = aronaDebugLastEffectTarget,
                    .path         = "/tmp/yakkai-debug/arona-last-effect.tga",
                });
                scene.debugRenderDumps.push_back({
                    .label        = "arona-final-default",
                    .renderTarget = SpecTex_Default.data(),
                    .path         = "/tmp/yakkai-debug/arona-final-default.tga",
                });
                LOG_INFO("registered arona debug render dumps: source=%s last=%s final=%s",
                         aronaDebugSourceTarget.c_str(),
                         aronaDebugLastEffectTarget.c_str(),
                         SpecTex_Default.data());
            }
        }

        int32_t i_eff = -1;
        int32_t debug_eff_total = effectObjects.size();
        for (const auto& wpeffobj : effectObjects) {
            i_eff++;
            if (! wpeffobj.visible) {
                i_eff--;
                continue;
            }
            std::shared_ptr<SceneImageEffect> imgEffect = std::make_shared<SceneImageEffect>();

            // this will be replace when resolve, use here to get rt info
            const std::string inRT { effect_ppong_a };

            // fbo name map and effect command
            std::string effaddr = getAddr(imgEffectLayer.get());

            std::unordered_map<std::string, std::string> fboMap;
            {
                fboMap["previous"] = inRT;
                for (usize i = 0; i < wpeffobj.fbos.size(); i++) {
                    const auto& wpfbo  = wpeffobj.fbos.at(i);
                    std::string rtname = CanonicalizeRuntimeRenderTargetName(wpfbo.name, effaddr);
                    if (wpimgobj.fullscreen) {
                        scene.renderTargets[rtname]      = { 2, 2, true };
                        scene.renderTargets[rtname].bind = {
                            .enable = true,
                            .screen = true,
                            .scale  = 1.0 / wpfbo.scale,
                        };
                    } else {
                        // i+2 for not override object's rt
                        scene.renderTargets[rtname] = {
                            .width      = (uint16_t)(wpimgobj.size[0] / (float)wpfbo.scale),
                            .height     = (uint16_t)(wpimgobj.size[1] / (float)wpfbo.scale),
                            .allowReuse = true
                        };
                    }
                    fboMap[wpfbo.name] = rtname;
                }
            }
            // load! effect commands
            {
                if (debugAronaRenderTargets && i_eff == 0 && ! aronaDebugSourceTarget.empty()) {
                    imgEffect->commands.push_back({ .cmd      = SceneImageEffect::CmdType::Copy,
                                                    .dst      = aronaDebugSourceTarget,
                                                    .src      = inRT,
                                                    .afterpos = 0 });
                }
                for (const auto& el : wpeffobj.commands) {
                    if (el.command != "copy") {
                        LOG_ERROR("Unknown effect command: %s", el.command.c_str());
                        continue;
                    }
                    if (fboMap.count(el.target) + fboMap.count(el.source) < 2) {
                        LOG_ERROR("Unknown effect command dst or src: %s %s",
                                  el.target.c_str(),
                                  el.source.c_str());
                        continue;
                    }
                    imgEffect->commands.push_back({ .cmd      = SceneImageEffect::CmdType::Copy,
                                                    .dst      = fboMap[el.target],
                                                    .src      = fboMap[el.source],
                                                    .afterpos = el.afterpos });
                }
            }

            bool eff_mat_ok { true };

            for (usize i_mat = 0; i_mat < wpeffobj.materials.size(); i_mat++) {
                wpscene::WPMaterial wpmat = wpeffobj.materials.at(i_mat);
                std::string         matOutRT { WE_EFFECT_PPONG_PREFIX_B };
                if (wpeffobj.passes.size() > i_mat) {
                    const auto& wppass = wpeffobj.passes.at(i_mat);
                    wpmat.MergePass(wppass);
                    // Map ENABLEMASK → MASK (scene JSON uses ENABLEMASK,
                    // shader uses MASK as the preprocessor combo name)
                    if (wpmat.combos.count("ENABLEMASK") && wpmat.combos.at("ENABLEMASK") != 0) {
                        wpmat.combos["MASK"] = wpmat.combos.at("ENABLEMASK");
                    }
                    // Set rendertarget, in and out
                    for (const auto& el : wppass.bind) {
                        if (fboMap.count(el.name) == 0) {
                            LOG_ERROR("fbo %s not found", el.name.c_str());
                            continue;
                        }
                        if (wpmat.textures.size() <= (usize)el.index)
                            wpmat.textures.resize((usize)el.index + 1);
                        wpmat.textures[(usize)el.index] = fboMap[el.name];
                    }
                    if (! wppass.target.empty()) {
                        if (fboMap.count(wppass.target) == 0) {
                            LOG_ERROR("fbo %s not found", wppass.target.c_str());
                        } else {
                            matOutRT = fboMap.at(wppass.target);
                        }
                    }
                }
                if (wpmat.textures.size() == 0) wpmat.textures.resize(1);
                if (wpmat.textures.at(0).empty()) {
                    wpmat.textures[0] = inRT;
                }
                auto         spEffNode  = std::make_shared<SceneNode>();
                std::string  effmataddr = getAddr(spEffNode.get());
                WPShaderInfo wpEffShaderInfo;
                wpEffShaderInfo.baseConstSvs = baseConstSvs;
                wpEffShaderInfo.baseConstSvs["g_EffectTextureProjectionMatrix"] =
                    ShaderValue::fromMatrix(Eigen::Matrix4f::Identity());
                wpEffShaderInfo.baseConstSvs["g_EffectTextureProjectionMatrixInverse"] =
                    ShaderValue::fromMatrix(Eigen::Matrix4f::Identity());
                SceneMaterial     material;
                WPShaderValueData svData;
                if (! LoadMaterial(vfs,
                                   wpmat,
                                   context.scene.get(),
                                   spEffNode.get(),
                                   &material,
                                   &svData,
                                   &wpEffShaderInfo)) {
                    eff_mat_ok = false;
                    break;
                }

                // load glname from alias and load to constvalue
                LoadConstvalue(material, wpmat, wpEffShaderInfo);
                if (wpmat.shader == "genericimage4") {
                    LOG_INFO("effect stage material loaded: effectIndex=%d shader=%s usePuppet=%d tex0=%s combosSKINNING=%d combosBONECOUNT=%s",
                             i_eff + 1,
                             wpmat.shader.c_str(),
                             wpmat.use_puppet ? 1 : 0,
                             wpmat.textures.empty() ? "" : wpmat.textures.front().c_str(),
                             exists(wpEffShaderInfo.combos, "SKINNING") &&
                                     wpEffShaderInfo.combos.at("SKINNING") != "0"
                                 ? 1
                                 : 0,
                             exists(wpEffShaderInfo.combos, "BONECOUNT")
                                 ? wpEffShaderInfo.combos.at("BONECOUNT").c_str()
                                 : "");
                }
                auto spMesh = std::make_shared<SceneMesh>();
                const bool preservePuppetMesh =
                    routePuppetPrepassThroughAuthoredEffects && puppet && wpmat.use_puppet;
                {
                    svData.parallaxDepth = { wpimgobj.parallaxDepth[0], wpimgobj.parallaxDepth[1] };
                    if (puppet && wpmat.use_puppet) {
                        svData.puppet_layer = WPPuppetLayer(puppet->puppet);
                        svData.puppet_layer.prepared(wpimgobj.puppet_layers);
                    }
                }
                if (preservePuppetMesh) {
                    spMesh->ChangeMeshDataFrom(effct_final_mesh);
                }
                spMesh->AddMaterial(std::move(material));
                spEffNode->AddMesh(spMesh);

                context.shader_updater->SetNodeData(spEffNode.get(), svData);
                imgEffect->nodes.push_back({ matOutRT, spEffNode, preservePuppetMesh });
            }

            if (eff_mat_ok)
                imgEffectLayer->AddEffect(imgEffect);
            else {
                LOG_ERROR("effect \'%s\' failed to load", wpeffobj.name.c_str());
            }
        }

        if (useStandalonePuppetFinalDisplay && puppet) {
            auto spBaseNode = std::make_shared<SceneNode>();

            if (useStandalonePuppetBaseDisplay) {
                wpscene::WPMaterial baseSourceMaterial = wpimgobj.material;
                if (usePuppetChannelMapPrepass && ! sourceMaterial.textures.empty() &&
                    ! sourceMaterial.textures[0].empty()) {
                    baseSourceMaterial.textures.resize(std::max<usize>(baseSourceMaterial.textures.size(), 2));
                    baseSourceMaterial.textures[1] = sourceMaterial.textures[0];
                    baseSourceMaterial.combos["YAKKAI_CHANNELMAP_BASE_EXCLUDE"] = 1;
                    LOG_INFO("native puppet base display applying inverse channelmap alpha mask: image=%s channelMask=%s",
                             wpimgobj.name.c_str(),
                             baseSourceMaterial.textures[1].c_str());
                }
                WPMdlParser::AddPuppetMatInfo(baseSourceMaterial, *puppet);

                SceneMaterial     baseMaterial;
                WPShaderValueData baseSvData;
                WPShaderInfo      baseShaderInfo;
                baseShaderInfo.baseConstSvs = baseConstSvs;
                WPMdlParser::AddPuppetShaderInfo(baseShaderInfo, *puppet);
                if (! LoadMaterial(vfs,
                                   baseSourceMaterial,
                                   context.scene.get(),
                                   spBaseNode.get(),
                                   &baseMaterial,
                                   &baseSvData,
                                   &baseShaderInfo)) {
                    LOG_ERROR("load standalone puppet base material failed: %s", wpimgobj.name.c_str());
                } else {
                    LoadConstvalue(baseMaterial, baseSourceMaterial, baseShaderInfo);
                    baseSvData.parallaxDepth = { wpimgobj.parallaxDepth[0], wpimgobj.parallaxDepth[1] };
                    baseSvData.puppet_layer = WPPuppetLayer(puppet->puppet);
                    baseSvData.puppet_layer.prepared(wpimgobj.puppet_layers);

                    auto spBaseMesh = std::make_shared<SceneMesh>();
                    WPMdlParser::GenPuppetMesh(*spBaseMesh, *puppet);
                    spBaseMesh->AddMaterial(std::move(baseMaterial));
                    spBaseNode->AddMesh(spBaseMesh);
                    if (hasStandaloneDisplayTransform) {
                        spBaseNode->CopyTrans(standaloneDisplayTransform);
                    }
                    context.shader_updater->SetNodeData(spBaseNode.get(), baseSvData);
                    spImgNode->AppendChild(spBaseNode);

                    LOG_INFO("native puppet standalone base display enabled: image=%s tex0=%s",
                             wpimgobj.name.c_str(),
                             baseSourceMaterial.textures.empty() ? "" : baseSourceMaterial.textures.front().c_str());
                    if (usePuppetChannelMapPrepass && ! activePuppetChannelBlendSlots.empty()) {
                        std::ostringstream activeIndicesStream;
                        for (size_t i = 0; i < activePuppetChannelBlendSlots.size(); ++i) {
                            if (i != 0) activeIndicesStream << ", ";
                            activeIndicesStream << activePuppetChannelBlendSlots[i];
                        }
                        LOG_INFO("native puppet standalone base display keeping full mesh under filtered overlay: image=%s activeOverlayIndices=[%s]",
                                 wpimgobj.name.c_str(),
                                 activeIndicesStream.str().c_str());
                    }
                }
            }

            if (usePuppetChannelMapPrepass && activePuppetChannelBlendSlots.empty()) {
                LOG_INFO("native puppet final overlay suppressed because all active channelmap layers are paused or hidden: image=%s",
                         wpimgobj.name.c_str());
            } else {
                auto spFinalNode = std::make_shared<SceneNode>();

                const std::string finalEffectTexture =
                    ResolveEffectPingPongFinalTarget(effect_ppong_a, effect_ppong_b, count_eff);

                wpscene::WPMaterial finalSourceMaterial = wpimgobj.material;
                if (finalSourceMaterial.textures.empty()) {
                    finalSourceMaterial.textures.resize(1);
                }
                finalSourceMaterial.textures[0] = finalEffectTexture;
                if (usePuppetChannelMapPrepass && ! sourceMaterial.textures.empty() &&
                    ! sourceMaterial.textures[0].empty()) {
                    finalSourceMaterial.textures.resize(std::max<usize>(finalSourceMaterial.textures.size(), 3));
                    finalSourceMaterial.textures[1] = sourceMaterial.textures[0];
                    if (sourceMaterial.textures.size() > 1 && ! sourceMaterial.textures[1].empty()) {
                        finalSourceMaterial.textures[2] = sourceMaterial.textures[1];
                    } else if (! wpimgobj.material.textures.empty()) {
                        finalSourceMaterial.textures[2] = wpimgobj.material.textures[0];
                    }
                    finalSourceMaterial.combos["YAKKAI_CHANNELMAP_ALPHA_MASK"] = 1;
                    LOG_INFO("native puppet final display applying channelmap/base alpha masks: image=%s channelMask=%s baseMask=%s",
                             wpimgobj.name.c_str(),
                             finalSourceMaterial.textures[1].c_str(),
                             finalSourceMaterial.textures.size() > 2 ? finalSourceMaterial.textures[2].c_str() : "");
                }
                // Non-channelmap path: the effect chain output already has correct
                // alpha from the puppet source render — no additional masking needed.
                SceneMaterial     finalMaterial;
                WPShaderValueData finalSvData;
                WPShaderInfo      finalShaderInfo;
                finalShaderInfo.baseConstSvs = baseConstSvs;
                if (usePuppetChannelMapPrepass) {
                    WPMdlParser::AddPuppetMatInfo(finalSourceMaterial, *puppet);
                    WPMdlParser::AddPuppetShaderInfo(finalShaderInfo, *puppet);
                }
                if (! LoadMaterial(vfs,
                                   finalSourceMaterial,
                                   context.scene.get(),
                                   spFinalNode.get(),
                                   &finalMaterial,
                                   &finalSvData,
                                   &finalShaderInfo)) {
                    LOG_ERROR("load standalone puppet final material failed: %s", wpimgobj.name.c_str());
                } else {
                    LoadConstvalue(finalMaterial, finalSourceMaterial, finalShaderInfo);
                    // Force Translucent blend so the effect chain's alpha channel
                    // correctly composites over the scene (transparent areas pass through).
                    finalMaterial.blenmode = BlendMode::Translucent;
                    finalSvData.parallaxDepth = { wpimgobj.parallaxDepth[0], wpimgobj.parallaxDepth[1] };
                    if (usePuppetChannelMapPrepass) {
                        finalSvData.puppet_layer = WPPuppetLayer(puppet->puppet);
                        finalSvData.puppet_layer.prepared(wpimgobj.puppet_layers);
                    }

                    auto spFinalMesh = std::make_shared<SceneMesh>();
                    bool usingFilteredOverlayMesh = false;
                    if (usePuppetChannelMapPrepass && ! activePuppetChannelBlendSlots.empty()) {
                        usingFilteredOverlayMesh =
                            WPMdlParser::GenPuppetImageSpaceMesh(*spFinalMesh,
                                                                 *puppet,
                                                                 { wpimgobj.size[0], wpimgobj.size[1] },
                                                                 activePuppetChannelBlendSlots);
                    }
                    if (! usingFilteredOverlayMesh) {
                        if (usePuppetChannelMapPrepass) {
                            WPMdlParser::GenPuppetMesh(*spFinalMesh, *puppet);
                        } else {
                            spFinalMesh->ChangeMeshDataFrom(effct_final_mesh);
                        }
                    }
                    spFinalMesh->AddMaterial(std::move(finalMaterial));
                    spFinalNode->AddMesh(spFinalMesh);
                    if (hasStandaloneDisplayTransform) {
                        spFinalNode->CopyTrans(standaloneDisplayTransform);
                    }
                    context.shader_updater->SetNodeData(spFinalNode.get(), finalSvData);
                    spImgNode->AppendChild(spFinalNode);

                    LOG_INFO("native puppet standalone final display enabled: image=%s tex0=%s authoredEffects=%d",
                             wpimgobj.name.c_str(),
                             finalEffectTexture.c_str(),
                             count_eff);
                    if (usingFilteredOverlayMesh) {
                        LOG_INFO("native puppet final stage using filtered image-space overlay mesh: image=%s size=%.1fx%.1f",
                                 wpimgobj.name.c_str(),
                                 wpimgobj.size[0],
                                 wpimgobj.size[1]);
                        std::ostringstream activeIndicesStream;
                        for (size_t i = 0; i < activePuppetChannelBlendSlots.size(); ++i) {
                            if (i != 0) activeIndicesStream << ", ";
                            activeIndicesStream << activePuppetChannelBlendSlots[i];
                        }
                        LOG_INFO("native puppet final overlay restricted to active blend slots: image=%s indices=[%s]",
                                 wpimgobj.name.c_str(),
                                 activeIndicesStream.str().c_str());
                    } else if (! usePuppetChannelMapPrepass) {
                        LOG_INFO("native puppet final stage using flat effect card mesh: image=%s size=%.1fx%.1f",
                                 wpimgobj.name.c_str(),
                                 wpimgobj.size[0],
                                 wpimgobj.size[1]);
                    } else {
                        LOG_INFO("native puppet final stage using authored puppet UV mesh: image=%s size=%.1fx%.1f",
                                 wpimgobj.name.c_str(),
                                 wpimgobj.size[0],
                                 wpimgobj.size[1]);
                    }
                }
            }
        }
    }
    AttachObjectNode(context, spImgNode, wpimgobj.id, wpimgobj.parent);
}

struct ParticleChildPtr {
    wpscene::ParticleChild* child { nullptr };
    SceneNode*              node_parent { nullptr };
    ParticleSubSystem*      particle_parent { nullptr };

    i32 max_instancecount { 1 };
};

void ParseParticleObj(ParseContext& context, wpscene::WPParticleObject& wppartobj,
                      ParticleChildPtr child_ptr = {}) {
    if (! wppartobj.visible) return;
    if (wppartobj.parent > 0 && context.hidden_containers.count(wppartobj.parent)) return;
    struct ChildData {
        ChildData() = default;
        ChildData(const wpscene::ParticleChild& o)
            : type(o.type),
              maxcount(o.maxcount),
              controlpointstartindex(o.controlpointstartindex),
              probability(o.probability) {}
        std::string type { "static" };
        i32         maxcount { 20 };
        i32         controlpointstartindex { 0 };
        float       probability { 1.0f };
    };

    wpscene::Particle*         p_particle_obj { nullptr };
    std::shared_ptr<SceneNode> spNode;
    ChildData                  child_data;

    bool is_child = child_ptr.child != nullptr;
    if (is_child) {
        p_particle_obj = &(child_ptr.child->obj);
        spNode         = std::make_shared<SceneNode>(Vector3f(child_ptr.child->origin.data()),
                                             Vector3f(child_ptr.child->scale.data()),
                                             Vector3f(child_ptr.child->angles.data()));
        child_data     = ChildData(*child_ptr.child);

        child_ptr.max_instancecount *= child_data.maxcount;

    } else {
        p_particle_obj = &wppartobj.particleObj;
        spNode         = std::make_shared<SceneNode>(Vector3f(wppartobj.origin.data()),
                                             Vector3f(wppartobj.scale.data()),
                                             Vector3f(wppartobj.angles.data()));
    }

    wpscene::ParticleInstanceoverride override = wppartobj.instanceoverride;

    auto& particle_obj = *p_particle_obj;
    auto& vfs          = *context.vfs;

    if (! is_child && DebugSkipLayerByName(wppartobj.name)) {
        LOG_INFO("debug skipping particle layer: name=%s id=%d particle=%s",
                 wppartobj.name.c_str(),
                 wppartobj.id,
                 wppartobj.particle.c_str());
        return;
    }

    if (! is_child && IsUnsupportedWorkshopBokehParticle(wppartobj.particle)) {
        LOG_INFO("suppressing unsupported workshop bokeh particle system: name=%s id=%d particle=%s",
                 wppartobj.name.c_str(),
                 wppartobj.id,
                 wppartobj.particle.c_str());
        return;
    }

    auto wppartRenderer = particle_obj.renderers.at(0);
    bool render_rope    = sstart_with(wppartRenderer.name, "rope");
    bool hastrail       = send_with(wppartRenderer.name, "trail");

    if (render_rope) particle_obj.material.shader = "genericropeparticle";

    // wppartobj.origin[1] = context.ortho_h - wppartobj.origin[1];

    if (particle_obj.flags[wpscene::Particle::FlagEnum::perspective]) {
        spNode->SetCamera("global_perspective");
    }

    SceneMaterial     material;
    WPShaderValueData svData;

    if (! is_child) {
        svData.parallaxDepth = { wppartobj.parallaxDepth[0], wppartobj.parallaxDepth[1] };
    }

    WPShaderInfo shaderInfo;
    shaderInfo.baseConstSvs                         = context.global_base_uniforms;
    shaderInfo.baseConstSvs["g_OrientationUp"]      = std::array { 0.0f, 1.0f, 0.0f };
    shaderInfo.baseConstSvs["g_OrientationRight"]   = std::array { 1.0f, 0.0f, 0.0f };
    shaderInfo.baseConstSvs["g_OrientationForward"] = std::array { 0.0f, 0.0f, 1.0f };
    shaderInfo.baseConstSvs["g_ViewUp"]             = std::array { 0.0f, 1.0f, 0.0f };
    shaderInfo.baseConstSvs["g_ViewRight"]          = std::array { 1.0f, 0.0f, 0.0f };

    u32 maxcount = particle_obj.maxcount;
    maxcount     = std::min(maxcount, 20000u);

    if (hastrail) {
        double in_SegmentUVTimeOffset           = 0.0;
        double in_SegmentMaxCount               = maxcount - 1.0;
        shaderInfo.baseConstSvs["g_RenderVar0"] = std::array {
            (float)wppartRenderer.length,
            (float)wppartRenderer.maxlength,
            (float)in_SegmentUVTimeOffset,
            (float)in_SegmentMaxCount,
        };
        shaderInfo.combos["THICKFORMAT"]   = "1";
        shaderInfo.combos["TRAILRENDERER"] = "1";
    }

    if (! particle_obj.flags[wpscene::Particle::FlagEnum::spritenoframeblending]) {
        shaderInfo.combos["SPRITESHEETBLEND"] = "1";
    }

    bool mat_ok = false;
    try {
        mat_ok = LoadMaterial(vfs,
                              particle_obj.material,
                              context.scene.get(),
                              spNode.get(),
                              &material,
                              &svData,
                              &shaderInfo);
    } catch (const std::exception& e) {
        LOG_ERROR("load particleobj '%s' material exception: %s", wppartobj.name.c_str(), e.what());
    }
    if (! mat_ok) {
        LOG_ERROR("load particleobj '%s' material faild", wppartobj.name.c_str());
        return;
    }
    LoadConstvalue(material, particle_obj.material, shaderInfo);
    auto  spMesh             = std::make_shared<SceneMesh>(true);
    auto& mesh               = *spMesh;
    auto  animationmode      = ToAnimMode(particle_obj.animationmode);
    auto  sequencemultiplier = particle_obj.sequencemultiplier;
    bool  hasSprite          = material.hasSprite;
    (void)hasSprite;

    bool thick_format = material.hasSprite || hastrail;
    {
        u32 mesh_maxcount = maxcount * (u32)child_ptr.max_instancecount;
        if (render_rope)
            SetRopeParticleMesh(mesh, particle_obj, mesh_maxcount, thick_format);
        else
            SetParticleMesh(mesh, particle_obj, mesh_maxcount, thick_format);
    }

    auto particleSub = std::make_unique<ParticleSubSystem>(
        *context.scene->paritileSys,
        spMesh,
        maxcount,
        override.rate,
        child_data.maxcount,
        child_data.probability,
        ParseSpawnType(child_data.type),
        [=](const Particle& p, const ParticleRawGenSpec& spec) {
            auto& lifetime = *(spec.lifetime);
            if (lifetime <= 0.0f) {
                lifetime = 0.0f;
                return;
            }
            switch (animationmode) {
            case ParticleAnimationMode::RANDOMONE: lifetime = std::floor(p.init.lifetime); break;
            case ParticleAnimationMode::SEQUENCE:
                lifetime = (1.0f - (p.lifetime / p.init.lifetime)) * sequencemultiplier;
                break;
            }
        });

    LoadEmitter(*particleSub, particle_obj, override.count, render_rope);
    LoadInitializer(*particleSub, particle_obj, override);
    LoadOperator(*particleSub, particle_obj, override);
    LoadControlPoint(*particleSub, particle_obj);

    mesh.AddMaterial(std::move(material));
    spNode->AddMesh(spMesh);
    context.shader_updater->SetNodeData(spNode.get(), svData);

    for (auto& child : particle_obj.children) {
        ParseParticleObj(context,
                         wppartobj,
                         {
                             .child             = &child,
                             .node_parent       = spNode.get(),
                             .particle_parent   = particleSub.get(),
                             .max_instancecount = child_ptr.max_instancecount,
                         });
    }

    if (is_child)
        child_ptr.particle_parent->AddChild(std::move(particleSub));
    else
        context.scene->paritileSys->subsystems.emplace_back(std::move(particleSub));

    if (is_child)
        child_ptr.node_parent->AppendChild(spNode);
    else
        AttachObjectNode(context, spNode, wppartobj.id, wppartobj.parent);
}

void ParseSolidAnchorObj(ParseContext& context, WPSolidAnchorObject& solid_obj) {
    if (! solid_obj.visible) return;

    auto node = std::make_shared<SceneNode>(Vector3f(solid_obj.origin.data()),
                                            Vector3f(solid_obj.scale.data()),
                                            Vector3f(solid_obj.angles.data()));
    node->ID() = solid_obj.id;
    AttachObjectNode(context, node, solid_obj.id, solid_obj.parent);
}

void ParseLightObj(ParseContext& context, wpscene::WPLightObject& light_obj) {
    auto node = std::make_shared<SceneNode>(Vector3f(light_obj.origin.data()),
                                            Vector3f(light_obj.scale.data()),
                                            Vector3f(light_obj.angles.data()));
    node->ID() = light_obj.id;

    context.scene->lights.emplace_back(std::make_unique<SceneLight>(
        Vector3f(light_obj.color.data()), light_obj.radius, light_obj.intensity));

    auto& light = *(context.scene->lights.back());
    light.setNode(node);

    AttachObjectNode(context, node, light_obj.id, light_obj.parent);
}

bool LoadModelFallbackMaterial(fs::VFS& vfs, const std::string& matJsonFile,
                               wpscene::WPMaterial& material,
                               bool&                useStaticGenericMaterial) {
    if (matJsonFile.empty()) {
        LOG_ERROR("model fallback material missing for mdl");
        return false;
    }

    nlohmann::json jMat;
    if (! PARSE_JSON(fs::GetFileContent(vfs, "/assets/" + matJsonFile), jMat)) {
        LOG_ERROR("model fallback can't load material json: %s", matJsonFile.c_str());
        return false;
    }

    wpscene::WPMaterial sourceMaterial;
    if (! sourceMaterial.FromJson(jMat)) {
        LOG_ERROR("model fallback can't parse material json: %s", matJsonFile.c_str());
        return false;
    }

    if (sourceMaterial.textures.empty() || sourceMaterial.textures[0].empty()) {
        LOG_ERROR("model fallback material has no diffuse texture: %s", matJsonFile.c_str());
        return false;
    }

    useStaticGenericMaterial = false;
    if (sourceMaterial.shader == "generic") {
        const bool wantsLightmap = MaterialComboEnabled(sourceMaterial, "lightmap");
        const bool wantsNormalmap = MaterialComboEnabled(sourceMaterial, "normalmap");
        const bool wantsReflection = MaterialComboEnabled(sourceMaterial, "reflection");
        material = sourceMaterial;
        if (material.blending.empty()) material.blending = "disabled";
        useStaticGenericMaterial = true;
        LOG_INFO("model fallback using authored generic material: %s lightmap=%d normalmap=%d reflection=%d",
                 matJsonFile.c_str(),
                 wantsLightmap ? 1 : 0,
                 wantsNormalmap ? 1 : 0,
                 wantsReflection ? 1 : 0);
        return true;
    }

    // Static diffuse fallback only carries the albedo texture. Many WE diffuse maps store
    // non-opacity data in alpha, so preserving the material default translucent blend would
    // silently fade the whole model toward black.
    material.blending   = "disabled";
    material.cullmode   = sourceMaterial.cullmode;
    material.depthtest  = sourceMaterial.depthtest;
    material.depthwrite = sourceMaterial.depthwrite;
    material.shader     = "genericimage";
    material.textures   = { ResolveStaticFallbackDiffuseTexture(vfs, sourceMaterial.textures[0]) };
    LOG_INFO("model fallback forcing opaque blend for diffuse-only path: %s shader=%s sourceBlend=%s",
             matJsonFile.c_str(),
             material.shader.c_str(),
             sourceMaterial.blending.c_str());
    return true;
}

void ParseModelObj(ParseContext& context, wpscene::WPModelObject& model_obj) {
    if (! model_obj.visible) return;

    auto& vfs = *context.vfs;

    WPMdl model;
    if (! WPMdlParser::Parse(model_obj.model, vfs, model)) {
        LOG_ERROR("parse model failed: %s", model_obj.model.c_str());
        return;
    }

    const bool hasStaticSubmeshes = ! model.submeshes.empty();
    std::vector<WPMdl::Submesh> allSubmeshes;
    std::vector<WPMdl::Submesh> framingSubmeshes;
    if (hasStaticSubmeshes) {
        allSubmeshes = model.submeshes;
        framingSubmeshes = allSubmeshes;

        std::stable_partition(allSubmeshes.begin(), allSubmeshes.end(), [](const WPMdl::Submesh& submesh) {
            return ! ClassifyStaticBackdropSubmesh(submesh).isBackdrop;
        });

        size_t denseNonBackdropCount = 0;
        for (const auto& submesh : allSubmeshes) {
            const StaticBackdropSubmeshInfo backdropInfo = ClassifyStaticBackdropSubmesh(submesh);
            if (! backdropInfo.isBackdrop && submesh.vertexs.size() > 128) {
                ++denseNonBackdropCount;
            }
        }

        if (denseNonBackdropCount > 0) {
            auto removeBegin = std::remove_if(framingSubmeshes.begin(), framingSubmeshes.end(), [&](const WPMdl::Submesh& submesh) {
                const StaticBackdropSubmeshInfo backdropInfo = ClassifyStaticBackdropSubmesh(submesh);
                if (! backdropInfo.isBackdrop) {
                    return false;
                }

                LOG_INFO("excluding static backdrop submesh from framing heuristics: %s material=%s vertices=%zu extents=(%.3f, %.3f, %.3f)",
                         model_obj.model.c_str(),
                         submesh.mat_json_file.c_str(),
                         submesh.vertexs.size(),
                         backdropInfo.extents.x(),
                         backdropInfo.extents.y(),
                         backdropInfo.extents.z());
                return true;
            });
            framingSubmeshes.erase(removeBegin, framingSubmeshes.end());
        }
    } else {
        allSubmeshes.push_back(WPMdl::Submesh {
            model.mat_json_file,
            model.vertexs,
            model.indices,
        });
        framingSubmeshes = allSubmeshes;
    }

    const Eigen::Vector3f authoredModelScale(model_obj.scale.data());
    StaticModelBasisChoice staticBasisChoice;
    float                  staticFallbackScale = 1.0f;
    if (hasStaticSubmeshes) {
        staticBasisChoice = ResolveStaticModelBasis(context, model_obj, framingSubmeshes, allSubmeshes);
        MaybeReframeStaticModelCamera(context, model_obj, staticBasisChoice);
        if (std::all_of(authoredModelScale.begin(), authoredModelScale.end(),
                        [](float v) { return std::abs(v - 1.0f) <= 1.0e-4f; })) {
            staticFallbackScale =
                ResolveStaticModelAutoFitScale(context, model_obj, staticBasisChoice, framingSubmeshes);
        }
    }

    auto spNode = std::make_shared<SceneNode>(Vector3f(model_obj.origin.data()),
                                              authoredModelScale,
                                              Vector3f(model_obj.angles.data()));
    spNode->ID() = model_obj.id;

    std::shared_ptr<SceneNode> meshParent = spNode;
    if (hasStaticSubmeshes && std::abs(staticFallbackScale - 1.0f) > 1.0e-4f) {
        auto basisNode = std::make_shared<SceneNode>(Vector3f::Zero(),
                                                     Vector3f::Constant(staticFallbackScale),
                                                     Vector3f::Zero());
        spNode->AppendChild(basisNode);
        meshParent = basisNode;
    }

    WPShaderValueData svData;
    WPShaderInfo      shaderInfo;
    shaderInfo.baseConstSvs = context.global_base_uniforms;
    shaderInfo.baseConstSvs["g_Color4"] = std::array<float, 4> { 1.0f, 1.0f, 1.0f, 1.0f };
    shaderInfo.baseConstSvs["g_UserAlpha"] = 1.0f;
    shaderInfo.baseConstSvs["g_Brightness"] = 1.0f;

    if (model.puppet && ! model.puppet->bones.empty()) {
        WPMdlParser::AddPuppetShaderInfo(shaderInfo, model);
        svData.puppet_layer = WPPuppetLayer(model.puppet);
        svData.puppet_layer.prepared({});
    }

    size_t attachedSubmeshCount = 0;
    for (const auto& submesh : allSubmeshes) {
        auto submeshNode = std::make_shared<SceneNode>();
        submeshNode->ID() = model_obj.id * 1000 + static_cast<int>(attachedSubmeshCount);
        submeshNode->SetCamera("global_perspective");

        wpscene::WPMaterial fallbackMat;
        bool                useStaticGenericMaterial = false;
        if (! LoadModelFallbackMaterial(vfs,
                                        submesh.mat_json_file,
                                        fallbackMat,
                                        useStaticGenericMaterial)) {
            LOG_ERROR("load model fallback material failed: %s", submesh.mat_json_file.c_str());
            continue;
        }

        SceneMaterial material;
        if (! LoadMaterial(vfs,
                           fallbackMat,
                           context.scene.get(),
                           submeshNode.get(),
                           &material,
                           &svData,
                           &shaderInfo)) {
            LOG_ERROR("load model fallback shader failed: %s", submesh.mat_json_file.c_str());
            continue;
        }
        LoadConstvalue(material, fallbackMat, shaderInfo);

        auto spMesh = std::make_shared<SceneMesh>();
        if (useStaticGenericMaterial) {
            WPMdlParser::GenStaticMesh(*spMesh,
                                       submesh,
                                       MaterialComboEnabled(fallbackMat, "normalmap"),
                                       MaterialComboEnabled(fallbackMat, "lightmap"),
                                       staticBasisChoice.linear);
        } else {
            if (hasStaticSubmeshes) {
                WPMdlParser::GenStaticMesh(*spMesh, submesh, false, false, staticBasisChoice.linear);
            } else {
                WPMdlParser::GenPuppetMesh(*spMesh, submesh, staticBasisChoice.linear);
            }
        }
        spMesh->AddMaterial(std::move(material));
        submeshNode->AddMesh(spMesh);
        meshParent->AppendChild(submeshNode);
        context.shader_updater->SetNodeData(submeshNode.get(), svData);
        ++attachedSubmeshCount;
    }

    if (attachedSubmeshCount == 0) {
        LOG_ERROR("no model fallback submeshes were attached: %s", model_obj.model.c_str());
        return;
    }

    AttachObjectNode(context, spNode, model_obj.id, model_obj.parent);
    if (hasStaticSubmeshes) {
        LogStaticModelProjectedBounds(
            context, model_obj, staticBasisChoice, staticFallbackScale, framingSubmeshes);
    }

    LOG_INFO("model scene object using experimental static fallback: %s submeshes=%zu basis=\"%s\" scale=%.4f",
             model_obj.model.c_str(),
             attachedSubmeshCount,
             staticBasisChoice.label.c_str(),
             staticFallbackScale);
}

template<typename T>
void AddWPObject(std::vector<WPObjectVar>& objs, const nlohmann::json& json_obj, fs::VFS& vfs) {
    T wpobj;
    if (! wpobj.FromJson(json_obj, vfs)) {
        LOG_ERROR("parse scene object failed, name: %s", wpobj.name.c_str());
        return;
    }
    if (! wpobj.visible) return;
    objs.push_back(wpobj);
}
} // namespace

std::shared_ptr<Scene> WPSceneParser::Parse(std::string_view scene_id, const std::string& buf,
                                            fs::VFS& vfs, audio::SoundManager& sm) {
    nlohmann::json json;
    if (! PARSE_JSON(buf, json)) return nullptr;


    nlohmann::json sceneProperties = ResolveSceneProperties(vfs, m_scene_properties_json);
    SetActiveScenePropertyState(sceneProperties);
    struct ClearScenePropertyStateGuard {
        ~ClearScenePropertyStateGuard() { ClearActiveScenePropertyState(); }
    } clearScenePropertyStateGuard;

    if (! sceneProperties.empty()) {
        LOG_INFO("scene property defaults active: count=%zu", sceneProperties.size());
    }

    wpscene::WPScene sc;
    sc.FromJson(json);
    //	LOG_INFO(nlohmann::json(sc).dump(4));

    ParseContext context;

    // Detect color tint overlay properties from scene settings.
    // WE scenes commonly have colour0/opacity0 + colour1/opacity1 pairs
    // that are applied to solid layers via scripts at runtime.
    if (sceneProperties.is_object()) {
        // Scan for sequential color+opacity property pairs
        for (int idx = 0; idx < 20; ++idx) {
            auto colorKey = [&](int i) -> std::string {
                // Try common naming patterns
                for (const auto& prefix : { "newproperty", "colour", "color" }) {
                    std::string base = std::string(prefix) + std::to_string(i);
                    if (sceneProperties.contains(base)) {
                        const auto& prop = sceneProperties.at(base);
                        if (prop.is_object() && prop.contains("type") &&
                            prop.at("type").is_string() && prop.at("type").get<std::string>() == "color")
                            return base;
                    }
                }
                return {};
            };
            // Look for colour0/opacity + colour1/opacity pattern starting from index 11
            // (common WE naming: newproperty11=colour0, newproperty12=opacity, etc.)
            std::string ck = colorKey(11 + idx * 2);
            if (ck.empty()) continue;
            std::string ok = "newproperty" + std::to_string(12 + idx * 2);
            if (! sceneProperties.contains(ok)) continue;
            const auto& colorProp = sceneProperties.at(ck);
            const auto& opacityProp = sceneProperties.at(ok);
            if (! colorProp.contains("value") || ! opacityProp.contains("value")) continue;
            std::array<float, 3> col { 0, 0, 0 };
            float opacity = 1.0f;
            {
                std::string sv = colorProp.at("value").is_string()
                    ? colorProp.at("value").get<std::string>()
                    : colorProp.at("value").dump();
                std::istringstream iss(sv);
                iss >> col[0] >> col[1] >> col[2];
            }
            {
                const auto& ov = opacityProp.at("value");
                if (ov.is_number()) opacity = ov.get<float>();
                else if (ov.is_string()) {
                    try { opacity = std::stof(ov.get<std::string>()); }
                    catch (...) { opacity = 1.0f; }
                }
            }
            context.pending_tint_overlays.push_back({ col, opacity });
            LOG_INFO("detected tint overlay property: %s color=(%.3f,%.3f,%.3f) opacity=%.2f",
                     ck.c_str(), col[0], col[1], col[2], opacity);
        }
    }

    // Pre-scan: evaluate SceneScript modules found in scene objects.
    // WE scenes can have {"script": "...", "value": ...} structured values
    // that contain JavaScript modules. We evaluate these with QuickJS to
    // resolve color/alpha/origin bindings from scene properties.
    {
        SceneScriptContext scriptCtx;
        scriptCtx.setUserProperties(sceneProperties);
        scriptCtx.setCanvasSize(sc.general.orthogonalprojection.width,
                                sc.general.orthogonalprojection.height);
        {
            // Compute current time-of-day fraction (0.0 = midnight, 0.5 = noon)
            using clock = std::chrono::system_clock;
            auto now = clock::to_time_t(clock::now());
            std::tm lt {};
            localtime_r(&now, &lt);
            double frac = (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) / 86400.0;
            scriptCtx.setTimeOfDay(frac);
        }

        for (const auto& obj : json.at("objects")) {
            if (! obj.contains("id")) continue;
            int32_t objId = obj.at("id").get<int32_t>();

            // Find script strings anywhere in the object's JSON tree
            std::function<void(const nlohmann::json&)> scanForScripts =
                [&](const nlohmann::json& node) {
                if (node.is_object() && node.contains("script") &&
                    node.at("script").is_string()) {
                    const auto& script = node.at("script").get_ref<const std::string&>();
                    if (script.size() < 50) return; // skip trivial scripts

                    // Get current layer defaults
                    std::array<float, 3> origin { 0, 0, 0 };
                    std::array<float, 3> color { 1, 1, 1 };
                    float alpha = 1.0f;
                    if (obj.contains("origin")) {
                        std::string ov = obj.at("origin").is_string()
                            ? obj.at("origin").get<std::string>() : "";
                        std::istringstream iss(ov);
                        iss >> origin[0] >> origin[1] >> origin[2];
                    }

                    // Resolve scriptproperties user bindings before evaluating.
                    // Scene JSON scriptproperties can have {"user": "propName", "value": default}
                    // entries that need to be resolved to the actual user property value.
                    if (node.contains("scriptproperties") && node.at("scriptproperties").is_object()) {
                        const auto& sp = node.at("scriptproperties");
                        for (const auto& [spKey, spVal] : sp.items()) {
                            double dval = 0;
                            bool resolved = false;
                            auto jsonToDouble = [](const nlohmann::json& v, double& out) -> bool {
                                if (v.is_number()) { out = v.get<double>(); return true; }
                                if (v.is_boolean()) { out = v.get<bool>() ? 1.0 : 0.0; return true; }
                                if (v.is_string()) {
                                    try { out = std::stod(v.get<std::string>()); return true; } catch (...) {}
                                }
                                return false;
                            };
                            if (spVal.is_object() && spVal.contains("user") && spVal.at("user").is_string()) {
                                std::string userPropName = spVal.at("user").get<std::string>();
                                if (auto uval = LookupUserPropertyValue(userPropName)) {
                                    resolved = jsonToDouble(*uval, dval);
                                }
                                if (! resolved && spVal.contains("value")) {
                                    resolved = jsonToDouble(spVal.at("value"), dval);
                                }
                            } else {
                                resolved = jsonToDouble(spVal, dval);
                            }
                            if (resolved) scriptCtx.setScriptProperty(spKey, dval);
                        }
                    }
                    auto result = scriptCtx.evaluateLayerScript(script, origin, color, alpha);
                    if (result.color) {
                        context.script_color_bindings[objId].color = *result.color;
                        context.script_color_bindings[objId].has_color = true;
                        LOG_INFO("QuickJS binding: id=%d color=(%.3f,%.3f,%.3f)",
                                 objId, (*result.color)[0], (*result.color)[1], (*result.color)[2]);
                    }
                    if (result.alpha) {
                        context.script_color_bindings[objId].alpha = *result.alpha;
                        context.script_color_bindings[objId].has_alpha = true;
                        LOG_INFO("QuickJS binding: id=%d alpha=%.3f", objId, *result.alpha);
                    }
                    if (result.origin) {
                        context.script_color_bindings[objId].origin = *result.origin;
                        context.script_color_bindings[objId].has_origin = true;
                        LOG_INFO("QuickJS binding: id=%d origin=(%.0f,%.0f,%.0f)",
                                 objId, (*result.origin)[0], (*result.origin)[1], (*result.origin)[2]);
                    }
                } else if (node.is_object()) {
                    for (const auto& [k, v] : node.items()) scanForScripts(v);
                } else if (node.is_array()) {
                    for (const auto& v : node) scanForScripts(v);
                }
            };
            scanForScripts(obj);
        }
        if (! context.script_color_bindings.empty()) {
            LOG_INFO("QuickJS resolved %zu script bindings from scene JSON",
                     context.script_color_bindings.size());
        }
    }

    // First pass: identify container objects (no image/particle/etc) whose
    // conditional visibility resolves to false. Their children should be hidden.
    for (const auto& obj : json.at("objects")) {
        if (obj.contains("image") || obj.contains("particle") || obj.contains("sound") ||
            obj.contains("light") || obj.contains("model")) continue;
        if (! obj.contains("id")) continue;
        int32_t objId = obj.at("id").get<int32_t>();
        bool objVisible = true;
        if (obj.contains("visible") && obj.at("visible").is_object()) {
            if (auto resolved = ResolveConditionalProperty(obj.at("visible"))) {
                if (resolved->is_boolean()) objVisible = resolved->get<bool>();
            }
        } else if (obj.contains("visible") && obj.at("visible").is_boolean()) {
            objVisible = obj.at("visible").get<bool>();
        }
        context.all_containers.insert(objId);
        if (! objVisible) {
            context.hidden_containers.insert(objId);
        }
    }

    std::vector<WPObjectVar> wp_objs;
    int modelObjectCount = 0;

    for (auto& obj : json.at("objects")) {
        RegisterPuppetPauseDirectives(context, obj);
        if (obj.contains("image") && ! obj.at("image").is_null()) {
            AddWPObject<wpscene::WPImageObject>(wp_objs, obj, vfs);
        } else if (obj.contains("particle") && ! obj.at("particle").is_null()) {
            AddWPObject<wpscene::WPParticleObject>(wp_objs, obj, vfs);
        } else if (obj.contains("sound") && ! obj.at("sound").is_null()) {
            AddWPObject<wpscene::WPSoundObject>(wp_objs, obj, vfs);
        } else if (obj.contains("light") && ! obj.at("light").is_null()) {
            AddWPObject<wpscene::WPLightObject>(wp_objs, obj, vfs);
        } else if (obj.contains("model") && ! obj.at("model").is_null()) {
            AddWPObject<wpscene::WPModelObject>(wp_objs, obj, vfs);
            ++modelObjectCount;
        } else if (IsTransformAnchorObject(obj)) {
            AddWPObject<WPSolidAnchorObject>(wp_objs, obj, vfs);
        }
    }

    // Detect puppet references. Actual parse success is tracked during
    // ParseImageObj — if all puppets fail to parse, scene_type is downgraded
    // to Standard after parsing completes.
    for (const auto& obj : wp_objs) {
        if (const auto* img = std::get_if<wpscene::WPImageObject>(&obj)) {
            if (! img->puppet.empty()) {
                context.has_puppet_objects = true;
            }
        }
    }
    if (context.has_puppet_objects) {
        context.scene_type = ParseContext::SceneType::Puppet;
    } else if (context.has_video_textures) {
        context.scene_type = ParseContext::SceneType::Video;
    } else {
        context.scene_type = ParseContext::SceneType::Standard;
    }
    LOG_INFO("scene type: %s (puppet=%d video=%d)",
             context.scene_type == ParseContext::SceneType::Puppet ? "Puppet" :
             context.scene_type == ParseContext::SceneType::Video ? "Video" : "Standard",
             context.has_puppet_objects ? 1 : 0,
             context.has_video_textures ? 1 : 0);

    if (modelObjectCount > 0) {
        LOG_INFO("scene contains %d model object(s); using the experimental static model fallback",
                 modelObjectCount);
    }

    if (sc.general.orthogonalprojection.auto_) {
        i32 w = 0, h = 0;
        for (auto& obj : wp_objs) {
            auto* img = std::get_if<wpscene::WPImageObject>(&obj);
            if (img == nullptr) continue;
            i32 size = (i32)(img->size.at(0) * img->size.at(1));
            if (size > w * h) {
                w = (i32)img->size.at(0);
                h = (i32)img->size.at(1);
            }
        }
        sc.general.orthogonalprojection.width  = w;
        sc.general.orthogonalprojection.height = h;
    }

    InitContext(context, vfs, sc);
    ParseCamera(context, sc, modelObjectCount > 0);

    {
        context.scene->renderTargets[SpecTex_Default.data()] = {
            .width  = context.ortho_w,
            .height = context.ortho_h,
            .bind   = { .enable = true, .screen = true },
        };
        context.scene->renderTargets[WE_MIP_MAPPED_FRAME_BUFFER.data()] = {
            .width      = context.ortho_w,
            .height     = context.ortho_h,
            .has_mipmap = true,
            .bind       = { .enable = true, .name = SpecTex_Default.data() }
        };
        context.scene->renderTargets[WE_REFLECTION_BUFFER.data()] = {
            .width      = context.ortho_w,
            .height     = context.ortho_h,
            .allowReuse = true,
            .bind       = { .enable = true, .name = SpecTex_Default.data() }
        };
    }

    context.scene->scene_id = scene_id;

    WPShaderParser::InitGlslang();

    for (WPObjectVar& obj : wp_objs) {
        std::visit(visitor::overload {
                       [&context](wpscene::WPImageObject& obj) {
                            ParseImageObj(context, obj);
                       },
                       [&context](wpscene::WPParticleObject& obj) {
                           ParseParticleObj(context, obj);
                       },
                       [&context, &sm](wpscene::WPSoundObject& obj) {
                           WPSoundParser::Parse(obj, *context.vfs, sm);
                       },
                       [&context](wpscene::WPLightObject& obj) {
                           ParseLightObj(context, obj);
                       },
                       [&context](wpscene::WPModelObject& obj) {
                           ParseModelObj(context, obj);
                       },
                       [&context](WPSolidAnchorObject& obj) {
                           ParseSolidAnchorObj(context, obj);
                       },
                   },
                   obj);
    }

    // Post-parse fixup: if scene was detected as Puppet but NO puppets
    // actually parsed, downgrade to Standard. This prevents the effect bypass
    // from activating for scenes with broken/unsupported puppet MDL formats.
    if (context.has_puppet_objects && context.puppet_parse_successes == 0) {
        LOG_INFO("all puppet parses failed — downgrading scene type from Puppet to Standard");
        context.has_puppet_objects = false;
        context.scene_type = ParseContext::SceneType::Standard;
    }

    // Attach orphaned children whose parent is a visible container-only object
    // (known container, not hidden, no scene graph node) to the scene root.
    for (const auto& [parentId, children] : context.deferred_children) {
        if (! children.empty() &&
            context.object_nodes.count(parentId) == 0 &&
            context.all_containers.count(parentId) &&
            ! context.hidden_containers.count(parentId)) {
            for (const auto& child : children) {
                context.scene->sceneGraph->AppendChild(child);
            }
        }
    }

    if (context.scene->sceneGraph->GetChildren().empty() && modelObjectCount > 0) {
        LOG_ERROR("scene produced no drawable nodes after model fallback processing");
    }

    WPShaderParser::FinalGlslang();
    return context.scene;
}
