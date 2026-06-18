#include "WPSceneParser.hpp"
#include "WPJson.hpp"
#include "Policy/EffectPolicy.hpp"
#include "Policy/MediaIntegrationPolicy.hpp"
#include "Policy/ModelFallbackPolicy.hpp"
#include "Scene/PuppetEffectRoutePlan.hpp"
#include "Scene/PuppetFinalDisplayBuilder.hpp"
#include "SceneScriptMediaState.hpp"
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
#include "Shader/ShaderCompatPatches.hpp"
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
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <regex>
#include <variant>
#include <Eigen/Dense>
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontMetrics>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtCore/QRectF>

using namespace wallpaper;
using namespace Eigen;

std::string getAddr(void* p) { return std::to_string(reinterpret_cast<intptr_t>(p)); }

std::string EscapeSceneScriptLogText(const std::string& text)
{
    std::string escaped;
    escaped.reserve(text.size());
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char ch : text) {
        switch (ch) {
        case '\0': escaped += "\\0"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (ch < 0x20 || ch == 0x7f) {
                escaped += "\\x";
                escaped.push_back(hex[(ch >> 4) & 0x0f]);
                escaped.push_back(hex[ch & 0x0f]);
            } else {
                escaped.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return escaped;
}

namespace wallpaper {

namespace {

std::string NormalizeGeneratedTextFontToken(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return out;
}

std::string GeneratedTextFontStem(std::string_view fontPath)
{
    const std::size_t nameStart = fontPath.find_last_of("/\\");
    std::string_view name = nameStart == std::string_view::npos
        ? fontPath
        : fontPath.substr(nameStart + 1);
    const std::size_t extensionStart = name.find_last_of('.');
    if (extensionStart != std::string_view::npos) {
        name = name.substr(0, extensionStart);
    }
    return std::string(name);
}

} // namespace

std::string ChooseGeneratedTextFontFamily(std::string_view fontPath,
                                          const std::vector<std::string>& families)
{
    if (families.empty()) {
        return {};
    }

    const std::string stem = NormalizeGeneratedTextFontToken(GeneratedTextFontStem(fontPath));
    if (!stem.empty()) {
        for (const auto& family : families) {
            if (NormalizeGeneratedTextFontToken(family) == stem) {
                return family;
            }
        }
    }

    for (const auto& family : families) {
        if (!family.empty()) {
            return family;
        }
    }
    return families.front();
}

} // namespace wallpaper

bool ParseSceneVec3Value(const nlohmann::json& field, std::array<float, 3>& out)
{
    const nlohmann::json* value = &field;
    if (field.is_object() && field.contains("value")) {
        value = &field.at("value");
    }

    if (value->is_string()) {
        std::istringstream iss(value->get<std::string>());
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (iss >> x >> y >> z) {
            out = {x, y, z};
            return true;
        }
        return false;
    }

    if (value->is_array() && value->size() >= 3) {
        out = {
            value->at(0).get<float>(),
            value->at(1).get<float>(),
            value->at(2).get<float>(),
        };
        return true;
    }

    return false;
}

bool ParseSceneVec2Value(const nlohmann::json& field, std::array<float, 2>& out)
{
    const nlohmann::json* value = &field;
    if (field.is_object() && field.contains("value")) {
        value = &field.at("value");
    }

    if (value->is_string()) {
        std::istringstream iss(value->get<std::string>());
        float x = 0.0f;
        float y = 0.0f;
        if (iss >> x >> y) {
            out = {x, y};
            return true;
        }
        return false;
    }

    if (value->is_array() && value->size() >= 2) {
        out = {
            value->at(0).get<float>(),
            value->at(1).get<float>(),
        };
        return true;
    }

    return false;
}

bool ParseSceneScalarValue(const nlohmann::json& field, float& out)
{
    const nlohmann::json* value = &field;
    if (field.is_object()) {
        if (auto resolved = ResolveConditionalProperty(field)) {
            value = &*resolved;
        } else if (field.contains("value")) {
            value = &field.at("value");
        }
    }

    if (value->is_number()) {
        out = value->get<float>();
        return true;
    }
    if (value->is_string()) {
        try {
            out = std::stof(value->get<std::string>());
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

std::vector<float> DebugVec3(const Eigen::Vector3f& value)
{
    return {value.x(), value.y(), value.z()};
}

std::array<float, 3> DebugVec3Array(const Eigen::Vector3f& value)
{
    return {value.x(), value.y(), value.z()};
}

wallpaper::debug::EffectCaptureTransformInfo DebugNodeTransform(const SceneNode& node)
{
    return {
        .origin = DebugVec3(node.Translate()),
        .scale = DebugVec3(node.Scale()),
        .angles = DebugVec3(node.Rotation()),
    };
}

wallpaper::debug::EffectCaptureMeshBoundsInfo DebugMeshBounds(const SceneMesh& mesh)
{
    wallpaper::debug::EffectCaptureMeshBoundsInfo info;
    info.vertexArrayCount = static_cast<int>(mesh.VertexCount());
    info.indexArrayCount = static_cast<int>(mesh.IndexCount());

    for (usize i = 0; i < mesh.IndexCount(); ++i) {
        const auto& indices = mesh.GetIndexArray(i);
        info.indexDataCount += static_cast<int>(indices.DataCount());
        info.indexRenderDataCount += static_cast<int>(indices.RenderDataCount());
    }

    std::array<float, 3> positionMin {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    std::array<float, 3> positionMax {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    bool hasPosition = false;

    for (usize i = 0; i < mesh.VertexCount(); ++i) {
        const auto& vertex = mesh.GetVertexArray(i);
        info.vertexCount += static_cast<int>(vertex.VertexCount());

        const auto attrs = vertex.GetAttrOffsetMap();
        const auto posIt = attrs.find(std::string(WE_IN_POSITION));
        if (posIt == attrs.end()) {
            continue;
        }
        if (SceneVertexArray::TypeCount(posIt->second.attr.type) < 3) {
            continue;
        }

        const float* raw = vertex.Data();
        if (raw == nullptr) {
            continue;
        }

        const usize strideFloats = vertex.OneSize();
        const usize positionOffsetFloats = posIt->second.offset / sizeof(float);
        for (usize vertexIndex = 0; vertexIndex < vertex.VertexCount(); ++vertexIndex) {
            const float* position = raw + vertexIndex * strideFloats + positionOffsetFloats;
            for (usize axis = 0; axis < 3; ++axis) {
                positionMin[axis] = std::min(positionMin[axis], position[axis]);
                positionMax[axis] = std::max(positionMax[axis], position[axis]);
            }
            hasPosition = true;
        }
    }

    if (hasPosition) {
        info.positionMin = {positionMin[0], positionMin[1], positionMin[2]};
        info.positionMax = {positionMax[0], positionMax[1], positionMax[2]};
    }

    return info;
}

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
    std::unordered_map<int32_t, std::string> object_names;
    std::unordered_map<int32_t, int32_t> object_parent_ids;
    std::unordered_map<int32_t, std::vector<int>> object_child_ids;
    std::unordered_map<int32_t, std::array<float, 2>> object_parallax_depths;
    struct DebugMouseParallaxSidecarLayer {
        int32_t              id { 0 };
        int32_t              parent { 0 };
        std::string          name;
        std::array<float, 2> parallaxDepth { 0.0f, 0.0f };
    };
    std::vector<DebugMouseParallaxSidecarLayer> debug_mouse_parallax_sidecar_layers;
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

    // Script-resolved layer-property bindings per object ID.
    // Populated by pre-scanning scene JSON for thisLayer color/alpha/origin scripts.
    struct ScriptColorBinding {
        std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
        float                alpha { 1.0f };
        std::array<float, 3> origin { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> scale { 1.0f, 1.0f, 1.0f };
        bool                 visible { true };
        float                maxWidth { 0.0f };
        bool                 has_color { false };
        bool                 has_alpha { false };
        bool                 has_origin { false };
        bool                 has_scale { false };
        bool                 has_visible { false };
        bool                 has_max_width { false };
        std::string          horizontalAlign;
        std::string          verticalAlign;
        bool                 has_horizontal_align { false };
        bool                 has_vertical_align { false };
    };
    std::unordered_map<int32_t, ScriptColorBinding> script_color_bindings;
    struct MediaTimelineScaleScript {
        std::string script;
        std::unordered_map<std::string, double> scriptProperties;
    };
    std::unordered_map<int32_t, MediaTimelineScaleScript> media_timeline_scale_scripts;
    struct MediaRuntimeScript {
        Scene::MediaRuntimeBindingField field { Scene::MediaRuntimeBindingField::Layer };
        std::string script;
        std::unordered_map<std::string, double> scriptProperties;
    };
    std::unordered_map<int32_t, std::vector<MediaRuntimeScript>> media_runtime_scripts;
    nlohmann::json scene_properties;
    int canvas_width { 1920 };
    int canvas_height { 1080 };

    struct ScriptTextBinding {
        std::string text;
        bool        has_text { false };
    };
    std::unordered_map<int32_t, ScriptTextBinding> script_text_bindings;
    std::unordered_map<std::string, std::vector<float>> script_material_constant_bindings;

    // Container objects (no image/particle) whose conditional visibility is false.
    // Child objects with a parent in this set should be hidden.
    std::unordered_set<int32_t> hidden_containers;
    // Any object hidden by its own visibility or by a hidden ancestor.
    std::unordered_set<int32_t> hidden_objects;
    // All container object IDs (visible or not) — used to reparent orphaned children.
    std::unordered_set<int32_t> all_containers;
    std::unordered_map<int32_t, bool> debug_layer_visibility_originals;
};

bool SceneScriptContainsMediaRuntimeCallback(const std::string& script)
{
    return script.find("mediaPlaybackChanged") != std::string::npos ||
           script.find("mediaPropertiesChanged") != std::string::npos ||
           script.find("mediaThumbnailChanged") != std::string::npos ||
           script.find("mediaTimelineChanged") != std::string::npos;
}

std::optional<Scene::MediaRuntimeBindingField>
MediaRuntimeBindingFieldForScriptField(const std::string& scriptField)
{
    if (scriptField.empty()) return Scene::MediaRuntimeBindingField::Layer;
    if (scriptField == "origin") return Scene::MediaRuntimeBindingField::Origin;
    if (scriptField == "scale") return Scene::MediaRuntimeBindingField::Scale;
    if (scriptField == "color") return Scene::MediaRuntimeBindingField::Color;
    if (scriptField == "alpha") return Scene::MediaRuntimeBindingField::Alpha;
    if (scriptField == "visible") return Scene::MediaRuntimeBindingField::Visible;
    return std::nullopt;
}

std::string ScriptMaterialConstantKey(int32_t layerId,
                                      int32_t effectId,
                                      int32_t passId,
                                      std::string_view name)
{
    return std::to_string(layerId) + ":" + std::to_string(effectId) + ":" +
           std::to_string(passId) + ":" + std::string(name);
}

bool IsScalarScriptMaterialConstant(std::string_view name)
{
    return name == "alpha" || name == "multiply";
}

std::optional<std::vector<float>>
ScalarScriptMaterialConstantValue(std::string_view name, const std::array<float, 3>& value)
{
    if (!IsScalarScriptMaterialConstant(name)) {
        return std::nullopt;
    }

    constexpr float kEqualComponentEpsilon = 1.0e-6f;
    if (std::abs(value[0] - value[1]) <= kEqualComponentEpsilon &&
        std::abs(value[0] - value[2]) <= kEqualComponentEpsilon) {
        return std::vector<float> { value[0] };
    }

    return std::nullopt;
}

std::optional<std::vector<float>>
ScriptMaterialConstantValue(const SceneScriptResult& result, std::string_view name)
{
    if (result.scalar) {
        return std::vector<float> { *result.scalar };
    }
    if (result.returnVector) {
        if (auto scalar = ScalarScriptMaterialConstantValue(name, *result.returnVector)) {
            return scalar;
        }
        return std::vector<float> {
            (*result.returnVector)[0],
            (*result.returnVector)[1],
            (*result.returnVector)[2],
        };
    }
    if (result.origin) {
        if (auto scalar = ScalarScriptMaterialConstantValue(name, *result.origin)) {
            return scalar;
        }
        return std::vector<float> {
            (*result.origin)[0],
            (*result.origin)[1],
            (*result.origin)[2],
        };
    }
    if (result.color) {
        if (auto scalar = ScalarScriptMaterialConstantValue(name, *result.color)) {
            return scalar;
        }
        return std::vector<float> {
            (*result.color)[0],
            (*result.color)[1],
            (*result.color)[2],
        };
    }
    if (name == "alpha" && result.alpha) {
        return std::vector<float> { *result.alpha };
    }
    return std::nullopt;
}

void ApplyScriptMaterialConstantBindings(ParseContext& context,
                                         int32_t layerId,
                                         int32_t effectId,
                                         int32_t passId,
                                         wpscene::WPMaterial& material)
{
    for (auto& [name, value] : material.constantshadervalues) {
        const auto key = ScriptMaterialConstantKey(layerId, effectId, passId, name);
        auto bindingIt = context.script_material_constant_bindings.find(key);
        if (bindingIt != context.script_material_constant_bindings.end()) {
            value = bindingIt->second;
        }
    }
}

bool HasHiddenParent(const ParseContext& context, int32_t parentId)
{
    return parentId > 0 &&
           (context.hidden_containers.count(parentId) || context.hidden_objects.count(parentId));
}

struct WPSolidAnchorObject {
    bool FromJson(const nlohmann::json& json, fs::VFS& vfs) {
        GET_JSON_NAME_VALUE_NOWARN(json, "id", id);
        GET_JSON_NAME_VALUE_NOWARN(json, "parent", parent);
        GET_JSON_NAME_VALUE_NOWARN(json, "name", name);
        GET_JSON_NAME_VALUE_NOWARN(json, "origin", origin);
        GET_JSON_NAME_VALUE_NOWARN(json, "angles", angles);
        GET_JSON_NAME_VALUE_NOWARN(json, "scale", scale);
        GET_JSON_NAME_VALUE_NOWARN(json, "parallaxDepth", parallaxDepth);
        GET_JSON_NAME_VALUE_NOWARN(json, "visible", visible);
        return true;
    }

    int32_t              id { 0 };
    int32_t              parent { 0 };
    std::string          name;
    std::array<float, 3> origin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> scale { 1.0f, 1.0f, 1.0f };
    std::array<float, 3> angles { 0.0f, 0.0f, 0.0f };
    std::array<float, 2> parallaxDepth { 0.0f, 0.0f };
    bool                 visible { true };
};

struct WPTextObject {
    bool FromJson(const nlohmann::json& json, fs::VFS& vfs) {
        GET_JSON_NAME_VALUE_NOWARN(json, "id", id);
        GET_JSON_NAME_VALUE_NOWARN(json, "parent", parent);
        GET_JSON_NAME_VALUE_NOWARN(json, "name", name);
        GET_JSON_NAME_VALUE_NOWARN(json, "origin", origin);
        GET_JSON_NAME_VALUE_NOWARN(json, "angles", angles);
        GET_JSON_NAME_VALUE_NOWARN(json, "scale", scale);
        GET_JSON_NAME_VALUE_NOWARN(json, "size", size);
        GET_JSON_NAME_VALUE_NOWARN(json, "font", font);
        GET_JSON_NAME_VALUE_NOWARN(json, "pointsize", pointSize);
        GET_JSON_NAME_VALUE_NOWARN(json, "horizontalalign", horizontalAlign);
        GET_JSON_NAME_VALUE_NOWARN(json, "verticalalign", verticalAlign);
        GET_JSON_NAME_VALUE_NOWARN(json, "limitwidth", limitWidth);
        if (json.contains("maxwidth")) {
            ParseSceneScalarValue(json.at("maxwidth"), maxWidth);
        }
        GET_JSON_NAME_VALUE_NOWARN(json, "maxrows", maxRows);
        if (json.contains("color")) {
            const auto& colorField = json.at("color");
            if (colorField.is_object()) {
                if (colorField.contains("value")) {
                    GET_JSON_NAME_VALUE_NOWARN(colorField, "value", color);
                } else if (auto resolved = ResolveConditionalProperty(colorField)) {
                    GET_JSON_VALUE_NOWARN(*resolved, color);
                }
            } else {
                GET_JSON_VALUE_NOWARN(colorField, color);
            }
        }
        if (json.contains("alpha")) {
            const auto& alphaField = json.at("alpha");
            if (alphaField.is_number()) {
                alpha = alphaField.get<float>();
            } else if (alphaField.is_object()) {
                if (auto animValue = EvaluateAnimationCurve(alphaField, GetSceneTimeSec())) {
                    alpha = static_cast<float>(*animValue);
                } else if (alphaField.contains("value")) {
                    GET_JSON_NAME_VALUE_NOWARN(alphaField, "value", alpha);
                } else if (auto resolved = ResolveConditionalProperty(alphaField)) {
                    if (resolved->is_number()) {
                        alpha = resolved->get<float>();
                    }
                }
            }
        }
        if (json.contains("visible")) {
            const auto& visibleField = json.at("visible");
            if (visibleField.is_boolean()) {
                visible = visibleField.get<bool>();
            } else if (auto resolved = ResolveConditionalProperty(visibleField)) {
                if (resolved->is_boolean()) {
                    visible = resolved->get<bool>();
                }
            }
        }
        if (json.contains("text")) {
            const auto& textNode = json.at("text");
            if (textNode.is_string()) {
                text = textNode.get<std::string>();
            } else if (textNode.is_object()) {
                GET_JSON_NAME_VALUE_NOWARN(textNode, "value", text);
            }
        }
        if (json.contains("effects")) {
            for (const auto& jE : json.at("effects")) {
                wpscene::WPImageEffect effect;
                if (effect.FromJson(jE, vfs)) {
                    effects.push_back(std::move(effect));
                }
            }
        }
        return true;
    }

    int32_t              id { 0 };
    int32_t              parent { 0 };
    std::string          name;
    std::string          text;
    std::string          font;
    std::array<float, 3> origin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> scale { 1.0f, 1.0f, 1.0f };
    std::array<float, 3> angles { 0.0f, 0.0f, 0.0f };
    std::array<float, 2> size { 256.0f, 64.0f };
    std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
    std::string          horizontalAlign { "left" };
    std::string          verticalAlign { "center" };
    float                pointSize { 0.0f };
    float                alpha { 1.0f };
    float                maxWidth { 0.0f };
    int32_t              maxRows { 0 };
    bool                 limitWidth { false };
    bool                 visible { true };
    std::vector<wpscene::WPImageEffect> effects;
};

using WPObjectVar = std::variant<wpscene::WPImageObject, wpscene::WPParticleObject,
                                 wpscene::WPSoundObject, wpscene::WPLightObject,
                                 wpscene::WPModelObject, WPSolidAnchorObject,
                                 WPTextObject>;

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

bool ResolveObjectVisibleForDebug(const nlohmann::json& obj)
{
    if (! obj.contains("visible")) {
        return true;
    }
    const auto& visible = obj.at("visible");
    if (visible.is_boolean()) {
        return visible.get<bool>();
    }
    if (auto resolved = ResolveConditionalProperty(visible)) {
        if (resolved->is_boolean()) {
            return resolved->get<bool>();
        }
        if (resolved->is_number()) {
            return resolved->get<double>() > 0.5;
        }
    }
    if (visible.is_object() && visible.contains("value")) {
        const auto& value = visible.at("value");
        if (value.is_boolean()) {
            return value.get<bool>();
        }
        if (value.is_number()) {
            return value.get<double>() > 0.5;
        }
    }
    return true;
}

void ResolveStructuredObjectField(nlohmann::json& obj, const char* name)
{
    if (!obj.contains(name) || obj.at(name).is_null()) {
        return;
    }
    try {
        auto resolved = ResolveConditionalProperty(obj.at(name));
        if (!resolved) {
            return;
        }
        obj[name] = *resolved;
    } catch (const std::exception&) {
    }
}

void ResolveStructuredObjectFields(nlohmann::json& obj)
{
    ResolveStructuredObjectField(obj, "origin");
    ResolveStructuredObjectField(obj, "angles");
    ResolveStructuredObjectField(obj, "scale");
    ResolveStructuredObjectField(obj, "color");
    ResolveStructuredObjectField(obj, "alpha");
    ResolveStructuredObjectField(obj, "visible");
}

void ApplyDebugLayerVisibilityOverride(ParseContext& context,
                                       nlohmann::json& obj,
                                       const wallpaper::debug::EffectCaptureConfig& config)
{
    if (! config.enabled() || ! obj.contains("id") || ! obj.at("id").is_number_integer()) {
        return;
    }

    const int32_t layerId = obj.at("id").get<int32_t>();
    const auto visibleOverride = config.layerVisibilityOverrideFor(layerId);
    if (! visibleOverride) {
        return;
    }

    const bool originalVisible = ResolveObjectVisibleForDebug(obj);
    context.debug_layer_visibility_originals[layerId] = originalVisible;
    obj["visible"] = *visibleOverride;
    LOG_INFO("debug layer visibility override applied: layer=%d originalVisible=%d visible=%d",
             layerId,
             originalVisible ? 1 : 0,
             *visibleOverride ? 1 : 0);
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
           obj.contains("parent") || obj.contains("solid");
}

bool ParseDebugVec2(const nlohmann::json& value, std::array<float, 2>& out)
{
    if (value.is_array() && value.size() >= 2) {
        if (!value.at(0).is_number() || !value.at(1).is_number()) {
            return false;
        }
        out = {value.at(0).get<float>(), value.at(1).get<float>()};
        return true;
    }
    if (value.is_string()) {
        std::istringstream iss(value.get<std::string>());
        float x = 0.0f;
        float y = 0.0f;
        if (iss >> x >> y) {
            out = {x, y};
            return true;
        }
    }
    return false;
}

bool HasDebugNonzeroParallaxDepth(const std::array<float, 2>& value)
{
    return std::abs(value[0]) > 1.0e-6f || std::abs(value[1]) > 1.0e-6f;
}

std::array<float, 2> ResolveEffectiveParallaxDepth(const ParseContext& context,
                                                   int32_t parentId,
                                                   const std::array<float, 2>& ownDepth)
{
    if (HasDebugNonzeroParallaxDepth(ownDepth)) {
        return ownDepth;
    }

    std::unordered_set<int32_t> seen;
    int32_t current = parentId;
    while (current > 0 && seen.insert(current).second) {
        const auto depthIt = context.object_parallax_depths.find(current);
        if (depthIt != context.object_parallax_depths.end() &&
            HasDebugNonzeroParallaxDepth(depthIt->second)) {
            return depthIt->second;
        }
        const auto parentIt = context.object_parent_ids.find(current);
        if (parentIt == context.object_parent_ids.end()) {
            break;
        }
        current = parentIt->second;
    }
    return ownDepth;
}

void RegisterDebugObjectGraphEntry(ParseContext& context,
                                   int32_t id,
                                   int32_t parent,
                                   std::string_view name);
void RegisterDebugObjectParallaxDepth(ParseContext& context,
                                      int32_t id,
                                      const std::array<float, 2>& parallaxDepth);

void RegisterDebugJsonObjectGraph(ParseContext& context, const nlohmann::json& obj)
{
    if (!obj.contains("id") || !obj.at("id").is_number_integer()) {
        return;
    }
    if (!ResolveObjectVisibleForDebug(obj)) {
        return;
    }

    const int32_t id = obj.at("id").get<int32_t>();
    int32_t parent = 0;
    if (obj.contains("parent") && obj.at("parent").is_number_integer()) {
        parent = obj.at("parent").get<int32_t>();
    }
    std::string name;
    if (obj.contains("name") && obj.at("name").is_string()) {
        name = obj.at("name").get<std::string>();
    }

    RegisterDebugObjectGraphEntry(context, id, parent, name);

    std::array<float, 2> parallaxDepth {0.0f, 0.0f};
    if (obj.contains("parallaxDepth") &&
        ParseDebugVec2(obj.at("parallaxDepth"), parallaxDepth)) {
        RegisterDebugObjectParallaxDepth(context, id, parallaxDepth);
    }
}

void CollectDebugMouseParallaxSidecarLayer(ParseContext& context, const nlohmann::json& obj)
{
    if (HasDrawablePayload(obj) || obj.contains("text") || obj.contains("font")) {
        return;
    }
    if (!obj.contains("id") || !obj.at("id").is_number_integer() || !obj.contains("parallaxDepth")) {
        return;
    }
    if (!ResolveObjectVisibleForDebug(obj)) {
        return;
    }

    std::array<float, 2> parallaxDepth {0.0f, 0.0f};
    if (!ParseDebugVec2(obj.at("parallaxDepth"), parallaxDepth) ||
        !HasDebugNonzeroParallaxDepth(parallaxDepth)) {
        return;
    }

    ParseContext::DebugMouseParallaxSidecarLayer layer;
    layer.id = obj.at("id").get<int32_t>();
    layer.parallaxDepth = parallaxDepth;
    context.object_parallax_depths[layer.id] = parallaxDepth;
    if (obj.contains("parent") && obj.at("parent").is_number_integer()) {
        layer.parent = obj.at("parent").get<int32_t>();
    }
    if (obj.contains("name") && obj.at("name").is_string()) {
        layer.name = obj.at("name").get<std::string>();
    }
    context.debug_mouse_parallax_sidecar_layers.push_back(std::move(layer));
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

std::vector<wallpaper::debug::PuppetCutoutSlotCoverageInfo>
BuildPuppetCutoutSlotCoverage(const WPMdl& mdl, const std::vector<int>& activeSlots)
{
    struct SlotBoundsAccumulator {
        bool has { false };
        float minX { 0.0f };
        float minY { 0.0f };
        float maxX { 0.0f };
        float maxY { 0.0f };
        double weightedSumX { 0.0 };
        double weightedSumY { 0.0 };
        double weightSum { 0.0 };

        void add(const WPMdl::Vertex& vertex, double sampleWeight)
        {
            if (sampleWeight <= 0.0) {
                return;
            }
            const float x = vertex.position[0];
            const float y = vertex.position[1];
            if (! has) {
                minX = maxX = x;
                minY = maxY = y;
                has = true;
            } else {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
            weightedSumX += static_cast<double>(x) * sampleWeight;
            weightedSumY += static_cast<double>(y) * sampleWeight;
            weightSum += sampleWeight;
        }

        void copyTo(wallpaper::debug::PuppetCutoutSlotCoverageInfo& info) const
        {
            if (! has || weightSum <= 0.0) {
                return;
            }
            info.layerLocalBounds = { minX, minY, maxX, maxY };
            info.layerLocalCentroid = {
                static_cast<float>(weightedSumX / weightSum),
                static_cast<float>(weightedSumY / weightSum),
            };
        }
    };

    std::vector<uint32_t> activeUnsignedSlots;
    activeUnsignedSlots.reserve(activeSlots.size());
    for (const int slot : activeSlots) {
        if (slot >= 0) {
            activeUnsignedSlots.push_back(static_cast<uint32_t>(slot));
        }
    }
    const std::vector<uint32_t> expandedActiveSlots =
        WPMdlParser::ExpandPuppetActiveBlendSlots(mdl, activeUnsignedSlots);

    std::unordered_set<int> active;
    for (const uint32_t slot : expandedActiveSlots) {
        if (slot <= static_cast<uint32_t>(std::numeric_limits<int>::max())) {
            active.insert(static_cast<int>(slot));
        }
    }

    std::map<int, wallpaper::debug::PuppetCutoutSlotCoverageInfo> coverage;
    std::map<int, SlotBoundsAccumulator> primaryBounds;
    std::map<int, SlotBoundsAccumulator> weightedBounds;
    const auto ensureSlot = [&coverage, &active, &mdl](int slot)
        -> wallpaper::debug::PuppetCutoutSlotCoverageInfo& {
        auto& info = coverage[slot];
        info.slot = slot;
        info.active = active.count(slot) != 0;
        if (mdl.puppet && slot >= 0 && static_cast<size_t>(slot) < mdl.puppet->bones.size()) {
            const auto& bone = mdl.puppet->bones[static_cast<size_t>(slot)];
            info.boneName = bone.name;
            info.simulationMetadata = bone.simulationMetadata;
            info.simulationMetadataPresent = !bone.simulationMetadata.empty();
            info.simulationMetadataValid = bone.parsedSimulationMetadata.valid;
            info.simulationPhysicsActive = bone.parsedSimulationMetadata.physicsActive;
            info.simulationTargetPointPresent =
                bone.parsedSimulationMetadata.targetPointPresent;
            if (bone.parsedSimulationMetadata.targetPointPresent) {
                info.simulationTargetPoint = {
                    bone.parsedSimulationMetadata.targetPoint[0],
                    bone.parsedSimulationMetadata.targetPoint[1],
                    bone.parsedSimulationMetadata.targetPoint[2],
                };
            }
            info.simulationTargetMassPresent =
                bone.parsedSimulationMetadata.targetMassPresent;
            info.simulationTargetMass = bone.parsedSimulationMetadata.targetMass;
            info.simulatedInactive = info.simulationPhysicsActive && !info.active;
            if (! bone.noParent()) {
                info.parentSlot = static_cast<int>(bone.parent);
                if (static_cast<size_t>(bone.parent) < mdl.puppet->bones.size()) {
                    info.parentBoneName = mdl.puppet->bones[static_cast<size_t>(bone.parent)].name;
                }
            }
        }
        return info;
    };

    for (const auto& vertex : mdl.vertexs) {
        const int slot = static_cast<int>(vertex.blend_indices[0]);
        auto& info = ensureSlot(slot);
        info.vertexCount += 1;
        info.primaryVertexCount += 1;
        primaryBounds[slot].add(vertex, 1.0);

        std::unordered_set<int> weightedSlotsForVertex;
        for (size_t i = 0; i < vertex.blend_indices.size() && i < vertex.weight.size(); ++i) {
            if (vertex.weight[i] <= 1.0e-3f) {
                continue;
            }
            const int weightedSlot = static_cast<int>(vertex.blend_indices[i]);
            if (! weightedSlotsForVertex.insert(weightedSlot).second) {
                continue;
            }
            auto& weightedInfo = ensureSlot(weightedSlot);
            weightedInfo.weightedVertexCount += 1;
            weightedInfo.weightedVertexWeightSum += vertex.weight[i];
            weightedBounds[weightedSlot].add(vertex, vertex.weight[i]);
        }
    }

    for (const auto& tri : mdl.indices) {
        if (tri[0] >= mdl.vertexs.size() || tri[1] >= mdl.vertexs.size() || tri[2] >= mdl.vertexs.size()) {
            continue;
        }
        const int a = static_cast<int>(mdl.vertexs[tri[0]].blend_indices[0]);
        const int b = static_cast<int>(mdl.vertexs[tri[1]].blend_indices[0]);
        const int c = static_cast<int>(mdl.vertexs[tri[2]].blend_indices[0]);
        if (a == b && b == c) {
            auto& info = ensureSlot(a);
            info.triangleCount += 1;
            info.primaryTriangleCount += 1;
        }

        std::unordered_set<int> weightedSlotsForTriangle;
        for (const uint16_t vertexIndex : tri) {
            const auto& vertex = mdl.vertexs[vertexIndex];
            for (size_t i = 0; i < vertex.blend_indices.size() && i < vertex.weight.size(); ++i) {
                if (vertex.weight[i] <= 1.0e-3f) {
                    continue;
                }
                weightedSlotsForTriangle.insert(static_cast<int>(vertex.blend_indices[i]));
            }
        }
        for (const int weightedSlot : weightedSlotsForTriangle) {
            ensureSlot(weightedSlot).weightedTriangleCount += 1;
        }
    }

    std::vector<wallpaper::debug::PuppetCutoutSlotCoverageInfo> out;
    out.reserve(coverage.size());
    for (auto& [_, info] : coverage) {
        info.secondaryOnly = info.primaryVertexCount == 0 && info.weightedVertexCount > 0;
        const auto weightedIt = weightedBounds.find(info.slot);
        if (weightedIt != weightedBounds.end() && weightedIt->second.has) {
            weightedIt->second.copyTo(info);
        } else {
            const auto primaryIt = primaryBounds.find(info.slot);
            if (primaryIt != primaryBounds.end()) {
                primaryIt->second.copyTo(info);
            }
        }
        if (info.vertexCount > 0 || info.triangleCount > 0 ||
            info.weightedVertexCount > 0 || info.weightedTriangleCount > 0 || info.active) {
            out.push_back(info);
        }
    }
    return out;
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

    const std::vector<uint32_t> expandedActiveIndices =
        WPMdlParser::ExpandPuppetActiveBlendSlots(mdl, activeIndices);
    std::vector<uint32_t> finalActiveIndices;
    finalActiveIndices.reserve(expandedActiveIndices.size());
    const auto inheritedBlendValue = [&mdl, &blendMap](uint32_t slot) {
        if (! mdl.puppet || slot >= mdl.puppet->bones.size()) {
            return 0.0f;
        }

        std::vector<bool> visited(mdl.puppet->bones.size(), false);
        uint32_t parent = mdl.puppet->bones[slot].parent;
        while (parent < mdl.puppet->bones.size() && ! visited[parent]) {
            if (parent < blendMap.size() && blendMap[parent] > kInactiveChannelMapBlendValue) {
                return blendMap[parent];
            }
            visited[parent] = true;
            if (mdl.puppet->bones[parent].noParent()) {
                break;
            }
            parent = mdl.puppet->bones[parent].parent;
        }
        return 0.0f;
    };

    for (const uint32_t slot : expandedActiveIndices) {
        if (slot >= blendMap.size()) {
            continue;
        }
        if (blendMap[slot] <= kInactiveChannelMapBlendValue) {
            const float inherited = inheritedBlendValue(slot);
            blendMap[slot] = inherited > kInactiveChannelMapBlendValue
                ? inherited
                : kActiveChannelMapBlendValue;
        }
        if (std::find(finalActiveIndices.begin(), finalActiveIndices.end(), slot) ==
            finalActiveIndices.end()) {
            finalActiveIndices.push_back(slot);
        }
    }
    activeIndices = std::move(finalActiveIndices);
    activeBlendSlots = activeIndices.size();

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

std::string SceneTypeText(ParseContext::SceneType sceneType) {
    switch (sceneType) {
    case ParseContext::SceneType::Puppet:
        return "Puppet";
    case ParseContext::SceneType::Video:
        return "Video";
    case ParseContext::SceneType::Standard:
    default:
        return "Standard";
    }
}

bool TryPreparePuppetChannelMapPrepass(const wpscene::WPImageObject& imageObject,
                                       const WPMdl&                  puppet,
                                       fs::VFS&                      vfs,
                                       wpscene::WPMaterial&          material,
                                       ShaderValueMap&               baseUniforms,
                                       std::vector<uint32_t>*        activeBlendSlotsOut,
                                       std::string*                  prepassModeOut,
                                       std::string*                  materialPathOut) {
    const bool sourceMaterialExplicitlyRequestsChannelMap =
        imageObject.material.shader == "puppettexturechannels" ||
        std::any_of(imageObject.material.textures.begin(),
                    imageObject.material.textures.end(),
                    [](const std::string& textureName) {
                        return textureName.find("_channelmap") != std::string::npos;
                    });
    const std::string channelMapMaterialPath = DerivePuppetChannelMapMaterialPath(imageObject.image);
    if (prepassModeOut) {
        prepassModeOut->clear();
    }
    if (materialPathOut) {
        materialPathOut->clear();
    }
    if (channelMapMaterialPath.empty()) {
        return false;
    }

    if (! vfs.Contains("/assets/" + channelMapMaterialPath)) {
        return false; // channelmap is optional — many scenes don't have one
    }

    if (! sourceMaterialExplicitlyRequestsChannelMap) {
        LOG_INFO("native puppet channelmap prepass skipped because source material has no explicit channelmap route: image=%s shader=%s",
                 imageObject.name.c_str(),
                 imageObject.material.shader.c_str());
        return false;
    }

    nlohmann::json materialJson;
    wpscene::WPMaterial parsedMaterial;
    if (! PARSE_JSON(fs::GetFileContent(vfs, "/assets/" + channelMapMaterialPath), materialJson) ||
        ! parsedMaterial.FromJson(materialJson)) {
        LOG_ERROR("failed to load puppet channelmap material: %s", channelMapMaterialPath.c_str());
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
        SeedPuppetChannelMapBlendMapFromVisibleLayers(imageObject,
                                                      puppet,
                                                      blendMap,
                                                      activeBlendSlotsOut);
    baseUniforms["g_BlendMap"] = blendMap;
    LogPuppetChannelMapBlendIndexCoverage(imageObject, puppet, blendMap);

    if (prepassModeOut) {
        *prepassModeOut = "source-explicit";
    }
    if (materialPathOut) {
        *materialPathOut = channelMapMaterialPath;
    }

    LOG_INFO("native puppet channelmap prepass enabled: image=%s material=%s mode=source-explicit rowCount=%u activeBlendSlots=%zu baseTexture=%s channelTexture=%s",
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

void GenCardMeshFromLocalBounds(SceneMesh& mesh,
                                const std::array<float, 4>& bounds,
                                const std::array<float, 2> mapRate = { 1.0f, 1.0f })
{
    const float left = bounds[0];
    const float bottom = bounds[1];
    const float right = bounds[2];
    const float top = bounds[3];
    constexpr float z = 0.0f;

    const float tw = mapRate[0];
    const float th = mapRate[1];

    // clang-format off
    const std::array pos = {
        left, bottom, z,
        left, top, z,
        right, bottom, z,
        right, top, z,
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

void AttachSceneGraphNode(ParseContext&                    context,
                          const std::shared_ptr<SceneNode>& node,
                          int32_t                           parentId) {
    if (! node) {
        return;
    }

    if (parentId > 0 && parentId != node->ID()) {
        auto parentIt = context.object_nodes.find(parentId);
        if (parentIt != context.object_nodes.end()) {
            parentIt->second->AppendChild(node);
        } else {
            context.deferred_children[parentId].push_back(node);
        }
    } else {
        context.scene->sceneGraph->AppendChild(node);
    }
}

void AttachObjectNode(ParseContext&                    context,
                      const std::shared_ptr<SceneNode>& node,
                      int32_t                           id,
                      int32_t                           parentId) {
    AttachSceneGraphNode(context, node, parentId);

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

std::string DebugObjectName(const ParseContext& context, int32_t id)
{
    auto it = context.object_names.find(id);
    return it != context.object_names.end() ? it->second : std::string();
}

std::vector<int> DebugChildLayerIds(const ParseContext& context, int32_t id)
{
    auto it = context.object_child_ids.find(id);
    if (it == context.object_child_ids.end()) {
        return {};
    }
    std::vector<int> ids = it->second;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::vector<wallpaper::debug::GeneratedTextParentInfo>
DebugParentChain(const ParseContext& context, int32_t parentId)
{
    std::vector<wallpaper::debug::GeneratedTextParentInfo> chain;
    std::unordered_set<int32_t> visited;
    int32_t current = parentId;
    while (current > 0 && visited.insert(current).second && chain.size() < 32) {
        chain.push_back({
            .layerId = current,
            .layerName = DebugObjectName(context, current),
        });
        auto parentIt = context.object_parent_ids.find(current);
        if (parentIt == context.object_parent_ids.end()) {
            break;
        }
        current = parentIt->second;
    }
    return chain;
}

std::vector<wallpaper::debug::GeneratedTextParentInfo>
DebugSceneNodeParentChain(const ParseContext& context, SceneNode* parent)
{
    std::vector<wallpaper::debug::GeneratedTextParentInfo> chain;
    std::unordered_set<SceneNode*> visited;
    SceneNode* current = parent;
    while (current != nullptr && current->ID() > 0 &&
           visited.insert(current).second && chain.size() < 32) {
        chain.push_back({
            .layerId = current->ID(),
            .layerName = DebugObjectName(context, current->ID()),
            .translate = DebugVec3Array(current->Translate()),
            .scale = DebugVec3Array(current->Scale()),
        });
        current = current->Parent();
    }
    return chain;
}

std::string NormalizeGeneratedTextAlign(std::string value);

std::array<float, 4> LocalCardBounds(const WPTextObject& text_obj)
{
    const float width = text_obj.size[0];
    const float height = text_obj.size[1];
    const float halfHeight = height * 0.5f;
    const std::string horizontalAlign = NormalizeGeneratedTextAlign(text_obj.horizontalAlign);
    const std::string verticalAlign = NormalizeGeneratedTextAlign(text_obj.verticalAlign);

    float left = width * -0.5f;
    float right = width * 0.5f;
    if (horizontalAlign == "left") {
        left = 0.0f;
        right = width;
    } else if (horizontalAlign == "right") {
        left = -width;
        right = 0.0f;
    }

    float bottom = -halfHeight;
    float top = halfHeight;
    if (verticalAlign == "top") {
        bottom = -height;
        top = 0.0f;
    } else if (verticalAlign == "bottom") {
        bottom = 0.0f;
        top = height;
    }

    return {left, bottom, right, top};
}

std::array<float, 4> WorldBoundsForLocalCard(SceneNode& node,
                                             const std::array<float, 4>& localBounds)
{
    node.UpdateTrans();
    const auto transform = node.ModelTrans();
    const std::array<Eigen::Vector4d, 4> corners {{
        {localBounds[0], localBounds[1], 0.0, 1.0},
        {localBounds[2], localBounds[1], 0.0, 1.0},
        {localBounds[2], localBounds[3], 0.0, 1.0},
        {localBounds[0], localBounds[3], 0.0, 1.0},
    }};

    float minX = std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    for (const auto& corner : corners) {
        const auto world = transform * corner;
        minX = std::min(minX, static_cast<float>(world.x()));
        minY = std::min(minY, static_cast<float>(world.y()));
        maxX = std::max(maxX, static_cast<float>(world.x()));
        maxY = std::max(maxY, static_cast<float>(world.y()));
    }
    if (!std::isfinite(minX) || !std::isfinite(minY) ||
        !std::isfinite(maxX) || !std::isfinite(maxY)) {
        const auto& translate = node.Translate();
        const auto& scale = node.Scale();
        LOG_INFO("generated text world bounds invalid: node=%d translate=(%.3f,%.3f,%.3f) scale=(%.3f,%.3f,%.3f) parent=%d",
                 node.ID(),
                 translate.x(),
                 translate.y(),
                 translate.z(),
                 scale.x(),
                 scale.y(),
                 scale.z(),
                 node.Parent() ? node.Parent()->ID() : 0);
    }
    return {minX, minY, maxX, maxY};
}

std::pair<std::string, std::string>
ClassifyGeneratedTextVisibility(const ParseContext& context,
                                const WPTextObject& text_obj,
                                const std::array<float, 4>& worldBounds,
                                bool hasVisibleAlpha)
{
    if (text_obj.alpha <= 1.0e-6f || !hasVisibleAlpha) {
        return {"transparent", "generated text has no visible alpha"};
    }
    if (worldBounds[2] <= worldBounds[0] || worldBounds[3] <= worldBounds[1]) {
        return {"collapsed", "world bounds have zero area"};
    }

    const float halfWidth = static_cast<float>(context.ortho_w) * 0.5f;
    const float halfHeight = static_cast<float>(context.ortho_h) * 0.5f;
    if (worldBounds[2] < -halfWidth || worldBounds[0] > halfWidth ||
        worldBounds[3] < -halfHeight || worldBounds[1] > halfHeight) {
        return {"offscreen", "world bounds outside orthographic viewport"};
    }

    return {"visible-in-frame", "world bounds overlap orthographic viewport"};
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

void LoadAlignment(SceneNode& node,
                   std::string_view align,
                   Vector2f size,
                   bool applyHorizontalAlignment = true) {
    Vector3f trans = node.Translate();
    size *= 0.5f;
    size.y() *= 1.0f;

    auto contains = [&](std::string_view s) {
        return align.find(s) != std::string::npos;
    };

    // topleft top center ...
    if (contains("top")) trans.y() -= size.y();
    if (applyHorizontalAlignment && contains("left")) trans.x() += size.x();
    if (applyHorizontalAlignment && contains("right")) trans.x() -= size.x();
    if (contains("bottom")) trans.y() += size.y();

    node.SetTranslate(trans);
}

float ParentHorizontalSign(ParseContext& context, int32_t parentId)
{
    auto parentIt = context.object_nodes.find(parentId);
    if (parentIt == context.object_nodes.end() || !parentIt->second) {
        return 1.0f;
    }

    parentIt->second->UpdateTrans();
    const auto axis = parentIt->second->ModelTrans() * Eigen::Vector4d {1.0, 0.0, 0.0, 0.0};
    return axis.x() < 0.0 ? -1.0f : 1.0f;
}

void AnchorTimelineSolidLayerLeadingEdge(ParseContext& context,
                                         const wpscene::WPImageObject& imageObject,
                                         SceneNode& node)
{
    if (!imageObject.mediaTimelineSolidLayer ||
        imageObject.image != "models/util/solidlayer.json") {
        return;
    }

    const float scaleX = imageObject.scale[0];
    if (!std::isfinite(scaleX)) {
        return;
    }
    const float absScaleX = std::abs(scaleX);
    if (absScaleX >= 1.0f) {
        return;
    }

    const float parentSign = ParentHorizontalSign(context, imageObject.parent);
    const float compensation = (1.0f - absScaleX) * imageObject.size[0] * 0.5f;
    Vector3f trans = node.Translate();
    trans.x() -= parentSign * compensation;
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

std::vector<float> ShaderValueToVector(const ShaderValue& value) {
    std::vector<float> result;
    result.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        result.push_back(value[index]);
    }
    return result;
}

std::unordered_map<std::string, std::string>
StringifyCombos(const std::unordered_map<std::string, int32_t>& combos) {
    std::unordered_map<std::string, std::string> result;
    for (const auto& entry : combos) {
        result[entry.first] = std::to_string(entry.second);
    }
    return result;
}

std::unordered_map<std::string, std::string>
StringifyCombos(const Combos& combos) {
    std::unordered_map<std::string, std::string> result;
    for (const auto& entry : combos) {
        result[entry.first] = entry.second;
    }
    return result;
}

std::unordered_map<std::string, std::vector<float>>
ShaderValuesToDebugMap(const ShaderValues& values) {
    std::unordered_map<std::string, std::vector<float>> result;
    for (const auto& entry : values) {
        result[entry.first] = ShaderValueToVector(entry.second);
    }
    return result;
}

std::vector<wallpaper::debug::EffectCaptureTextureBindingInfo>
BuildTextureBindings(const std::vector<std::string>& authored,
                     const std::vector<std::string>& resolved) {
    const size_t count = std::max(authored.size(), resolved.size());
    std::vector<wallpaper::debug::EffectCaptureTextureBindingInfo> bindings;
    bindings.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        bindings.push_back({
            .slot = static_cast<int>(index),
            .authored = index < authored.size() ? authored[index] : std::string(),
            .resolved = index < resolved.size() ? resolved[index] : std::string(),
        });
    }
    return bindings;
}

std::vector<wallpaper::debug::EffectCaptureMaterialInfo>
BuildAuthoredEffectMaterialDiagnostics(const std::vector<wpscene::WPImageEffect>& effects) {
    std::vector<wallpaper::debug::EffectCaptureMaterialInfo> materials;
    for (usize i_eff = 0; i_eff < effects.size(); ++i_eff) {
        const auto& effect = effects.at(i_eff);
        if (! effect.visible) {
            continue;
        }
        for (usize i_mat = 0; i_mat < effect.materials.size(); ++i_mat) {
            wpscene::WPMaterial material = effect.materials.at(i_mat);
            std::string passTarget;
            if (effect.passes.size() > i_mat) {
                const auto& pass = effect.passes.at(i_mat);
                material.MergePass(pass);
                passTarget = pass.target;
            }

            wallpaper::debug::EffectCaptureMaterialInfo materialInfo;
            materialInfo.effectIndex = static_cast<int>(i_eff + 1);
            materialInfo.materialIndex = static_cast<int>(i_mat);
            materialInfo.shader = material.shader;
            materialInfo.authoredOutputRenderTarget = passTarget;
            materialInfo.authoredTextures = material.textures;
            materialInfo.textureBindings =
                BuildTextureBindings(materialInfo.authoredTextures, {});
            materialInfo.authoredCombos = StringifyCombos(material.combos);
            materialInfo.materialValues = material.constantshadervalues;
            materials.push_back(std::move(materialInfo));
        }
    }
    return materials;
}

std::vector<wallpaper::debug::PuppetAnimationLayerInfo>
BuildPuppetAnimationLayerDiagnostics(
    const std::vector<WPPuppetLayer::AnimationLayer>& animationLayers,
    const WPMdl* puppet) {
    std::vector<wallpaper::debug::PuppetAnimationLayerInfo> layers;
    layers.reserve(animationLayers.size());
    for (const auto& animationLayer : animationLayers) {
        wallpaper::debug::PuppetAnimationLayerInfo info {
            .animationId = animationLayer.id,
            .animationName = animationLayer.name,
            .rate = animationLayer.rate,
            .blend = animationLayer.blend,
            .visible = animationLayer.visible,
            .paused = animationLayer.paused,
            .additive = animationLayer.additive,
            .curTime = animationLayer.cur_time,
        };

        if (puppet && puppet->puppet) {
            const auto animIt = std::find_if(
                puppet->puppet->anims.begin(),
                puppet->puppet->anims.end(),
                [&animationLayer](const auto& anim) {
                    return anim.id == animationLayer.id;
                });
            info.matchedAnimation = animIt != puppet->puppet->anims.end();
            info.visibleAndWeighted =
                info.matchedAnimation && animationLayer.visible && animationLayer.blend > 1.0e-6;
            if (info.visibleAndWeighted) {
                const size_t boneCount = animIt->bframes_array.size();
                for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
                    if (PuppetBoneFramesHaveMeaningfulDelta(animIt->bframes_array[boneIndex])) {
                        info.activeBoneSlots.push_back(static_cast<int>(boneIndex));
                    }
                }
            }
        }

        layers.push_back(std::move(info));
    }
    return layers;
}

void ApplyDebugPuppetAnimationLayerOverrides(
    wpscene::WPImageObject& imageObject,
    const std::vector<wallpaper::debug::PuppetAnimationLayerOverride>& overrides) {
    if (overrides.empty() || imageObject.puppet_layers.empty()) {
        return;
    }

    for (const auto& rule : overrides) {
        if (rule.layerId != imageObject.id) {
            continue;
        }
        for (auto& animationLayer : imageObject.puppet_layers) {
            if (animationLayer.id != rule.animationId) {
                continue;
            }
            if (rule.visible) {
                animationLayer.visible = *rule.visible;
            }
            if (rule.paused) {
                animationLayer.paused = *rule.paused;
            }
            if (rule.additive) {
                animationLayer.additive = *rule.additive;
            }
            if (rule.blend) {
                animationLayer.blend = *rule.blend;
            }
            if (rule.rate) {
                animationLayer.rate = *rule.rate;
            }
            if (rule.curTime) {
                animationLayer.cur_time = *rule.curTime;
            }
            LOG_INFO("debug puppet animation override applied: layer=%d animation=%d visible=%d paused=%d additive=%d blend=%.3f rate=%.3f curTime=%.3f",
                     imageObject.id,
                     animationLayer.id,
                     animationLayer.visible ? 1 : 0,
                     animationLayer.paused ? 1 : 0,
                     animationLayer.additive ? 1 : 0,
                     animationLayer.blend,
                     animationLayer.rate,
                     animationLayer.cur_time);
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

std::vector<uint8_t> SyntheticMediaThumbnailPlaceholder(const SceneScriptMediaState& mediaState,
                                                        int width,
                                                        int height)
{
    std::vector<uint8_t> rgba(static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height) * 4u);
    const auto colorAt = [](const std::array<float, 3>& color, int channel) -> uint8_t {
        return static_cast<uint8_t>(
            std::clamp(std::lround(color[static_cast<std::size_t>(channel)] * 255.0f),
                       0l,
                       255l));
    };

    const std::array<uint8_t, 3> top {
        colorAt(mediaState.secondaryColor, 0),
        colorAt(mediaState.secondaryColor, 1),
        colorAt(mediaState.secondaryColor, 2),
    };
    const std::array<uint8_t, 3> bottom {
        colorAt(mediaState.tertiaryColor, 0),
        colorAt(mediaState.tertiaryColor, 1),
        colorAt(mediaState.tertiaryColor, 2),
    };
    for (int y = 0; y < height; ++y) {
        const float t = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
        for (int x = 0; x < width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            for (int channel = 0; channel < 3; ++channel) {
                rgba[offset + static_cast<std::size_t>(channel)] = static_cast<uint8_t>(
                    std::clamp(std::lround(top[static_cast<std::size_t>(channel)] * (1.0f - t) +
                                           bottom[static_cast<std::size_t>(channel)] * t),
                               0l,
                               255l));
            }
            rgba[offset + 3] = mediaState.available ? 255u : 0u;
        }
    }
    return rgba;
}

float Luma(const std::array<float, 3>& color)
{
    return color[0] * 0.2126f + color[1] * 0.7152f + color[2] * 0.0722f;
}

std::array<float, 3> AverageMediaColors(const std::vector<std::array<float, 4>>& colors,
                                        std::size_t begin,
                                        std::size_t end)
{
    if (colors.empty() || begin >= end || begin >= colors.size()) {
        return { 0.0f, 0.0f, 0.0f };
    }
    end = std::min(end, colors.size());

    std::array<double, 3> sum { 0.0, 0.0, 0.0 };
    for (std::size_t i = begin; i < end; ++i) {
        sum[0] += colors[i][0];
        sum[1] += colors[i][1];
        sum[2] += colors[i][2];
    }

    const double count = static_cast<double>(end - begin);
    return {
        static_cast<float>(sum[0] / count),
        static_cast<float>(sum[1] / count),
        static_cast<float>(sum[2] / count),
    };
}

std::array<float, 3> MixMediaColors(const std::array<float, 3>& lhs,
                                    const std::array<float, 3>& rhs,
                                    float t)
{
    return {
        lhs[0] * (1.0f - t) + rhs[0] * t,
        lhs[1] * (1.0f - t) + rhs[1] * t,
        lhs[2] * (1.0f - t) + rhs[2] * t,
    };
}

bool DeriveSyntheticMediaThumbnailColors(SceneScriptMediaState& mediaState)
{
    if (mediaState.hasThumbnailColors || mediaState.albumArtPath.empty()) {
        return false;
    }

    QImage source(QString::fromStdString(mediaState.albumArtPath));
    if (source.isNull()) {
        LOG_INFO("synthetic media album art colors failed to load: path=%s",
                 mediaState.albumArtPath.c_str());
        return false;
    }

    constexpr int kSampleSize = 64;
    QImage sampled = source.convertToFormat(QImage::Format_RGBA8888)
                         .scaled(kSampleSize,
                                 kSampleSize,
                                 Qt::KeepAspectRatioByExpanding,
                                 Qt::SmoothTransformation);
    const int cropX = std::max(0, (sampled.width() - kSampleSize) / 2);
    const int cropY = std::max(0, (sampled.height() - kSampleSize) / 2);
    sampled = sampled.copy(cropX, cropY, kSampleSize, kSampleSize)
                    .convertToFormat(QImage::Format_RGBA8888);

    std::vector<std::array<float, 4>> colors;
    colors.reserve(static_cast<std::size_t>(sampled.width()) *
                   static_cast<std::size_t>(sampled.height()));
    for (int y = 0; y < sampled.height(); ++y) {
        for (int x = 0; x < sampled.width(); ++x) {
            const QColor pixel = sampled.pixelColor(x, y);
            if (pixel.alphaF() < 0.05f) {
                continue;
            }
            const std::array<float, 3> rgb {
                static_cast<float>(pixel.redF()),
                static_cast<float>(pixel.greenF()),
                static_cast<float>(pixel.blueF()),
            };
            colors.push_back({ rgb[0], rgb[1], rgb[2], Luma(rgb) });
        }
    }
    if (colors.empty()) {
        return false;
    }

    std::sort(colors.begin(), colors.end(), [](const auto& lhs, const auto& rhs) {
        return lhs[3] < rhs[3];
    });

    const std::size_t quartile = std::max<std::size_t>(1u, colors.size() / 4u);
    const std::size_t mutedBaseEnd =
        std::min(colors.size(), std::max<std::size_t>(quartile, (colors.size() * 3u) / 4u));
    const auto highlightColor = AverageMediaColors(colors,
                                                   colors.size() > quartile
                                                       ? colors.size() - quartile
                                                       : 0u,
                                                   colors.size());
    mediaState.primaryColor = AverageMediaColors(colors, 0u, mutedBaseEnd);
    mediaState.secondaryColor = AverageMediaColors(colors, 0u, colors.size());
    mediaState.tertiaryColor = MixMediaColors(mediaState.secondaryColor, highlightColor, 0.45f);
    const std::array<float, 3> contrastColor = Luma(mediaState.primaryColor) > 0.45f
        ? std::array<float, 3> { 0.0f, 0.0f, 0.0f }
        : std::array<float, 3> { 1.0f, 1.0f, 1.0f };
    mediaState.textColor = MixMediaColors(mediaState.secondaryColor, contrastColor, 0.55f);
    mediaState.hasThumbnailColors = true;

    LOG_INFO("synthetic media album art colors derived: primary=(%.3f,%.3f,%.3f) secondary=(%.3f,%.3f,%.3f) tertiary=(%.3f,%.3f,%.3f)",
             mediaState.primaryColor[0],
             mediaState.primaryColor[1],
             mediaState.primaryColor[2],
             mediaState.secondaryColor[0],
             mediaState.secondaryColor[1],
             mediaState.secondaryColor[2],
             mediaState.tertiaryColor[0],
             mediaState.tertiaryColor[1],
             mediaState.tertiaryColor[2]);
    return true;
}

void RegisterSyntheticMediaThumbnailTexture(ParseContext& context,
                                            const SceneScriptMediaState& mediaState)
{
    if (!context.scene || !context.scene->imageParser) {
        return;
    }
    auto* texParser = dynamic_cast<WPTexImageParser*>(context.scene->imageParser.get());
    if (texParser == nullptr) {
        return;
    }

    constexpr int kThumbnailSize = 512;
    QImage thumbnail;
    if (!mediaState.albumArtPath.empty()) {
        QImage source(QString::fromStdString(mediaState.albumArtPath));
        if (!source.isNull()) {
            QImage fitted = source.convertToFormat(QImage::Format_RGBA8888)
                                .scaled(kThumbnailSize,
                                        kThumbnailSize,
                                        Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
            const int cropX = std::max(0, (fitted.width() - kThumbnailSize) / 2);
            const int cropY = std::max(0, (fitted.height() - kThumbnailSize) / 2);
            thumbnail = fitted.copy(cropX, cropY, kThumbnailSize, kThumbnailSize)
                            .convertToFormat(QImage::Format_RGBA8888);
        } else {
            LOG_INFO("synthetic media album art failed to load: path=%s",
                     mediaState.albumArtPath.c_str());
        }
    }

    std::vector<uint8_t> rgba;
    int width = kThumbnailSize;
    int height = kThumbnailSize;
    if (!thumbnail.isNull()) {
        width = thumbnail.width();
        height = thumbnail.height();
        rgba.resize(static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height) * 4u);
        for (int y = 0; y < height; ++y) {
            const auto* row = thumbnail.constScanLine(y);
            std::memcpy(rgba.data() + static_cast<std::size_t>(y) *
                                      static_cast<std::size_t>(width) * 4u,
                        row,
                        static_cast<std::size_t>(width) * 4u);
        }
    } else {
        rgba = SyntheticMediaThumbnailPlaceholder(mediaState, width, height);
    }

    texParser->RegisterGeneratedRgbaImage("$mediaThumbnail", width, height, rgba);
    texParser->RegisterGeneratedRgbaImage("$mediaPreviousThumbnail", width, height, rgba);
    LOG_INFO("synthetic media thumbnail texture registered: size=%dx%d source=%s",
             width,
             height,
             thumbnail.isNull() ? "placeholder" : "albumArtPath");
}

void ParseImageObj(ParseContext& context, wpscene::WPImageObject& img_obj) {
    auto& wpimgobj = img_obj;
    if (! wpimgobj.visible) return;
    // Hide children of invisible container objects
    if (HasHiddenParent(context, wpimgobj.parent)) return;
    if (DebugSkipLayerByName(wpimgobj.name)) {
        LOG_INFO("debug skipping image layer: name=%s id=%d image=%s",
                 wpimgobj.name.c_str(),
                 wpimgobj.id,
                 wpimgobj.image.c_str());
        return;
    }

    auto& vfs = *context.vfs;

    if (wpimgobj.image == "models/util/solidlayer.json" &&
        context.media_timeline_scale_scripts.count(wpimgobj.id) > 0) {
        wpimgobj.supportedMediaWidgetUtility = true;
        wpimgobj.mediaTimelineSolidLayer = true;
    }

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

    if (context.scene && context.scene->debugEffectCaptures.enabled()) {
        ApplyDebugPuppetAnimationLayerOverrides(
            wpimgobj, context.scene->debugEffectCaptures.puppetAnimationLayerOverrides);
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

    bool isCompose = (wpimgobj.image == "models/util/composelayer.json");

    const auto buildEffectInput = [&]() {
        wallpaper::policy::LayerEffectInput effectInput;
        effectInput.sceneHasPuppetObjects = context.has_puppet_objects;
        effectInput.hasVisibleEffects = hasEffect;
        effectInput.noEffectsDebug = std::getenv("YAKKAI_NO_EFFECTS") != nullptr;
        effectInput.isComposelayer = isCompose;
        effectInput.isPuppetLayer = hasPuppet;
        effectInput.supportedMediaWidgetUtility = wpimgobj.supportedMediaWidgetUtility;
        effectInput.fullscreen = wpimgobj.fullscreen;
        effectInput.visibleEffectCount = count_eff;
        effectInput.colorBlendMode = wpimgobj.colorBlendMode;
        effectInput.alpha = wpimgobj.alpha;
        effectInput.layerName = wpimgobj.name;
        effectInput.imagePath = wpimgobj.image;
        for (const auto& eff : effectObjects) {
            wallpaper::policy::LayerEffectDescriptor effectDescriptor;
            effectDescriptor.name = eff.name;
            effectDescriptor.visible = eff.visible;
            if (!eff.materials.empty()) {
                effectDescriptor.firstMaterialShader = eff.materials[0].shader;
            }
            for (const auto& mat : eff.materials) {
                effectDescriptor.materialShaders.push_back(mat.shader);
            }
            effectInput.effects.push_back(effectDescriptor);
        }
        return effectInput;
    };

    const auto earlyEffectDecision = wallpaper::policy::decideLayerEffects(buildEffectInput());
    if (earlyEffectDecision.reason == "effectless-fullscreen" ||
        earlyEffectDecision.reason == "effectless-composelayer") {
        return;
    }
    if (earlyEffectDecision.reason == "debug-no-effects" ||
        earlyEffectDecision.reason == "debug-no-effects-composelayer") {
        count_eff = 0;
        hasEffect = false;
        effectObjects.clear();
        if (earlyEffectDecision.reason == "debug-no-effects-composelayer") {
            return;
        }
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

    const std::array<float, 3> authoredScale = wpimgobj.scale;

    // Apply script-resolved origin binding before creating the scene node.
    {
        auto it = context.script_color_bindings.find(wpimgobj.id);
        if (it != context.script_color_bindings.end() && it->second.has_origin) {
            wpimgobj.origin = it->second.origin;
        }
        if (it != context.script_color_bindings.end() && it->second.has_scale) {
            wpimgobj.scale = it->second.scale;
        }
    }
    auto spImgNode = std::make_shared<SceneNode>(Vector3f(wpimgobj.origin.data()),
                                                 Vector3f(wpimgobj.scale.data()),
                                                 Vector3f(wpimgobj.angles.data()));
    spImgNode->SetVisible(wpimgobj.visible);
    LoadAlignment(*spImgNode,
                  wpimgobj.alignment,
                  { wpimgobj.size[0], wpimgobj.size[1] },
                  !wpimgobj.mediaTimelineSolidLayer);
    const Eigen::Vector3f timelineBaseTranslate = spImgNode->Translate();
    AnchorTimelineSolidLayerLeadingEdge(context, wpimgobj, *spImgNode);
    spImgNode->ID() = wpimgobj.id;
    if (context.scene && wpimgobj.mediaTimelineSolidLayer &&
        wpimgobj.image == "models/util/solidlayer.json") {
        auto scriptIt = context.media_timeline_scale_scripts.find(wpimgobj.id);
        if (scriptIt != context.media_timeline_scale_scripts.end()) {
            Scene::MediaTimelineScaleBinding binding;
            binding.layerId = wpimgobj.id;
            binding.script = scriptIt->second.script;
            binding.authoredOrigin = {
                timelineBaseTranslate.x(),
                timelineBaseTranslate.y(),
                timelineBaseTranslate.z()
            };
            binding.authoredScale = authoredScale;
            binding.size = { wpimgobj.size[0], wpimgobj.size[1] };
            binding.parentHorizontalSign = ParentHorizontalSign(context, wpimgobj.parent);
            binding.canvasWidth = context.canvas_width;
            binding.canvasHeight = context.canvas_height;
            binding.leadingEdgeAnchored = true;
            binding.userProperties = context.scene_properties;
            binding.scriptProperties = scriptIt->second.scriptProperties;
            binding.node = spImgNode;
            context.scene->mediaTimelineScaleBindings.push_back(std::move(binding));
        }
    }
    if (context.scene) {
        auto runtimeIt = context.media_runtime_scripts.find(wpimgobj.id);
        if (runtimeIt != context.media_runtime_scripts.end()) {
            for (const auto& script : runtimeIt->second) {
                Scene::MediaRuntimeBinding binding;
                binding.layerId = wpimgobj.id;
                binding.field = script.field;
                binding.script = script.script;
                binding.authoredOrigin = {
                    spImgNode->Translate().x(),
                    spImgNode->Translate().y(),
                    spImgNode->Translate().z()
                };
                binding.authoredScale = {
                    spImgNode->Scale().x(),
                    spImgNode->Scale().y(),
                    spImgNode->Scale().z()
                };
                binding.authoredColor = wpimgobj.color;
                binding.authoredAlpha = wpimgobj.alpha;
                binding.authoredVisible = wpimgobj.visible;
                binding.canvasWidth = context.canvas_width;
                binding.canvasHeight = context.canvas_height;
                binding.userProperties = context.scene_properties;
                binding.scriptProperties = script.scriptProperties;
                binding.node = spImgNode;
                context.scene->mediaRuntimeBindings.push_back(std::move(binding));
            }
        }
    }
    std::shared_ptr<SceneNode> childTransformAnchorNode;
    const auto childIdsIt = context.object_child_ids.find(wpimgobj.id);
    const bool hasAuthoredChildren =
        childIdsIt != context.object_child_ids.end() && !childIdsIt->second.empty();
    if (hasEffect && hasAuthoredChildren) {
        childTransformAnchorNode = std::make_shared<SceneNode>();
        childTransformAnchorNode->CopyTrans(*spImgNode);
        childTransformAnchorNode->ID() = wpimgobj.id;
        LOG_INFO("effect image child transform anchor enabled: name=%s id=%d children=%zu",
                 wpimgobj.name.c_str(),
                 wpimgobj.id,
                 childIdsIt->second.size());
    }
    if (wpimgobj.transformOnly) {
        LOG_INFO("media widget transform-only node attached: name=%s id=%d",
                 wpimgobj.name.c_str(),
                 wpimgobj.id);
        AttachObjectNode(context, spImgNode, wpimgobj.id, wpimgobj.parent);
        return;
    }

    SceneMaterial     material;
    WPShaderValueData svData;

    ShaderValueMap baseConstSvs = context.global_base_uniforms;
    WPShaderInfo   shaderInfo;
    wpscene::WPMaterial sourceMaterial = wpimgobj.material;
    const auto layerEffectInput = buildEffectInput();
    const auto puppetEffectDecision = wallpaper::policy::decideLayerEffects(layerEffectInput);
    const auto candidateClassification =
        wallpaper::policy::classifyStrippedEffectCandidate(layerEffectInput);
    const auto effectiveParallaxDepth =
        ResolveEffectiveParallaxDepth(context,
                                      wpimgobj.parent,
                                      {wpimgobj.parallaxDepth[0], wpimgobj.parallaxDepth[1]});
    wallpaper::debug::EffectCaptureLayerInfo effectCaptureInfo;
    effectCaptureInfo.sceneId = context.scene ? context.scene->scene_id : "unknown_scene";
    effectCaptureInfo.sceneType = SceneTypeText(context.scene_type);
    effectCaptureInfo.layerName = wpimgobj.name;
    effectCaptureInfo.layerImage = wpimgobj.image;
    effectCaptureInfo.layerId = wpimgobj.id;
    effectCaptureInfo.visibleEffectCount = count_eff;
    effectCaptureInfo.alpha = wpimgobj.alpha;
    effectCaptureInfo.parallaxDepth = effectiveParallaxDepth;
    effectCaptureInfo.parallaxDepthNonzero =
        std::abs(effectCaptureInfo.parallaxDepth[0]) > 1.0e-6f ||
        std::abs(effectCaptureInfo.parallaxDepth[1]) > 1.0e-6f;
    if (const auto originalVisibleIt = context.debug_layer_visibility_originals.find(wpimgobj.id);
        originalVisibleIt != context.debug_layer_visibility_originals.end()) {
        effectCaptureInfo.debugLayerVisibilityOverrideRequested = true;
        effectCaptureInfo.debugLayerVisibilityOverrideOriginalVisible = originalVisibleIt->second;
        effectCaptureInfo.debugLayerVisibilityOverrideVisible = wpimgobj.visible;
    }
    effectCaptureInfo.keepLayer = puppetEffectDecision.keepLayer;
    effectCaptureInfo.keepEffects = puppetEffectDecision.keepEffects;
    effectCaptureInfo.strippedEffects = puppetEffectDecision.strippedEffects;
    effectCaptureInfo.policyReason = std::string(puppetEffectDecision.reason);
    effectCaptureInfo.puppetAnimationLayers =
        BuildPuppetAnimationLayerDiagnostics(wpimgobj.puppet_layers, puppet.get());
    if (puppet) {
        std::vector<int> activePuppetCutoutSlots;
        for (const auto& animationLayer : effectCaptureInfo.puppetAnimationLayers) {
            for (const int slot : animationLayer.activeBoneSlots) {
                if (std::find(activePuppetCutoutSlots.begin(), activePuppetCutoutSlots.end(), slot) ==
                    activePuppetCutoutSlots.end()) {
                    activePuppetCutoutSlots.push_back(slot);
                }
            }
        }
        effectCaptureInfo.publish.puppetCutoutSlotCoverage =
            BuildPuppetCutoutSlotCoverage(*puppet, activePuppetCutoutSlots);
    }
    for (const auto& effect : effectObjects) {
        effectCaptureInfo.effectNames.push_back(effect.name);
        for (const auto& material : effect.materials) {
            effectCaptureInfo.materialShaders.push_back(material.shader);
        }
    }
    effectCaptureInfo.candidateFamilies = candidateClassification.candidateFamilies;
    effectCaptureInfo.candidateMixFamilies = candidateClassification.candidateMixFamilies;
    effectCaptureInfo.candidateChainShape = candidateClassification.candidateChainShape;
    effectCaptureInfo.candidateEffectClass = candidateClassification.candidateEffectClass;
    effectCaptureInfo.candidateRisk = candidateClassification.candidateRisk;
    effectCaptureInfo.candidateBlockedReason = candidateClassification.candidateBlockedReason;
    effectCaptureInfo.candidateChecks = candidateClassification.candidateChecks;
    auto refreshEffectCaptureEffectLists = [&]() {
        effectCaptureInfo.effectNames.clear();
        effectCaptureInfo.materialShaders.clear();
        effectCaptureInfo.visibleEffectCount = count_eff;
        for (const auto& effect : effectObjects) {
            effectCaptureInfo.effectNames.push_back(effect.name);
            for (const auto& material : effect.materials) {
                effectCaptureInfo.materialShaders.push_back(material.shader);
            }
        }
    };
    if (context.scene) {
        effectCaptureInfo.debugProbeRequested =
            context.scene->debugEffectCaptures.shouldProbeLayer(wpimgobj.id) ||
            context.scene->debugEffectCaptures.shouldProbeHighRiskLayer(wpimgobj.id);
        wallpaper::debug::recordMouseParallaxLayer(*context.scene,
                                                   wpimgobj.id,
                                                   wpimgobj.name,
                                                   "image",
                                                   effectCaptureInfo.parallaxDepth,
                                                   wpimgobj.parent,
                                                   DebugObjectName(context, wpimgobj.parent),
                                                   DebugChildLayerIds(context, wpimgobj.id),
                                                   true);
        wallpaper::debug::recordPuppetAnimationLayerInventory(*context.scene, effectCaptureInfo);
    }
    const std::string debugProbeReason =
        context.scene ? wallpaper::debug::strippedEffectProbeReason(
                            context.scene->debugEffectCaptures,
                            effectCaptureInfo)
                      : std::string();
    const bool debugProbeStrippedLayer = !debugProbeReason.empty();
    if (effectCaptureInfo.debugProbeRequested) {
        effectCaptureInfo.debugProbeOverrodePolicy = debugProbeStrippedLayer;
        effectCaptureInfo.debugProbeReason =
            debugProbeStrippedLayer ? debugProbeReason : "not-eligible";
    }
    if (debugProbeStrippedLayer && context.scene &&
        (context.scene->debugEffectCaptures.probeMaxEffects >= 0 ||
         context.scene->debugEffectCaptures.puppetEffectRouteOnly)) {
        const bool routeOnly = context.scene->debugEffectCaptures.puppetEffectRouteOnly;
        const int maxEffects = routeOnly ? 0 : context.scene->debugEffectCaptures.probeMaxEffects >= 0
            ? context.scene->debugEffectCaptures.probeMaxEffects
            : count_eff;
        const auto limitDecision =
            wallpaper::debug::decideEffectProbeLimit(
                count_eff,
                maxEffects,
                routeOnly);
        effectCaptureInfo.debugProbeMaxEffects = maxEffects;
        effectCaptureInfo.debugProbeOriginalVisibleEffectCount = count_eff;
        effectCaptureInfo.debugProbeKeptVisibleEffectCount =
            limitDecision.keptVisibleEffectCount;
        effectCaptureInfo.debugProbeEffectLimitTruncated =
            limitDecision.effectLimitTruncated;
        effectCaptureInfo.debugProbeRouteOnly = limitDecision.routeOnly;
    }
    auto applyDebugProbeLimit = [&](const char* reason) {
        if (! context.scene ||
            (context.scene->debugEffectCaptures.probeMaxEffects < 0 &&
             ! context.scene->debugEffectCaptures.puppetEffectRouteOnly)) {
            return false;
        }

        const bool routeOnly =
            context.scene->debugEffectCaptures.puppetEffectRouteOnly && puppet != nullptr;
        const int maxEffects = routeOnly ? 0 : context.scene->debugEffectCaptures.probeMaxEffects >= 0
            ? context.scene->debugEffectCaptures.probeMaxEffects
            : count_eff;
        const auto limitDecision =
            wallpaper::debug::decideEffectProbeLimit(
                count_eff,
                maxEffects,
                routeOnly);
        std::vector<wpscene::WPImageEffect> limitedEffectObjects;
        limitedEffectObjects.reserve(effectObjects.size());
        int keptVisibleEffectCount = 0;
        if (! limitDecision.routeOnly) {
            for (const auto& effect : effectObjects) {
                if (! effect.visible) {
                    if (keptVisibleEffectCount < limitDecision.keptVisibleEffectCount) {
                        limitedEffectObjects.push_back(effect);
                    }
                    continue;
                }
                if (keptVisibleEffectCount >= limitDecision.keptVisibleEffectCount) {
                    break;
                }
                limitedEffectObjects.push_back(effect);
                keptVisibleEffectCount++;
            }
        }
        LOG_INFO("debug effect probe limiting layer: id=%d name=%s maxEffects=%d routeOnly=%d originalVisible=%d keptVisible=%d keepRoute=%d",
                 wpimgobj.id,
                 wpimgobj.name.c_str(),
                 maxEffects,
                 limitDecision.routeOnly ? 1 : 0,
                 count_eff,
                 limitDecision.keptVisibleEffectCount,
                 limitDecision.keepEffectRouteActive ? 1 : 0);
        effectObjects = std::move(limitedEffectObjects);
        count_eff = limitDecision.keptVisibleEffectCount;
        hasEffect = limitDecision.keepEffectRouteActive;
        effectCaptureInfo.debugProbeReason = reason ? reason : "";
        effectCaptureInfo.debugProbeMaxEffects = maxEffects;
        effectCaptureInfo.debugProbeOriginalVisibleEffectCount =
            limitDecision.routeOnly
                ? effectCaptureInfo.visibleEffectCount
                : std::max(effectCaptureInfo.visibleEffectCount,
                           limitDecision.keptVisibleEffectCount);
        effectCaptureInfo.debugProbeKeptVisibleEffectCount =
            limitDecision.keptVisibleEffectCount;
        effectCaptureInfo.debugProbeEffectLimitTruncated =
            limitDecision.effectLimitTruncated;
        effectCaptureInfo.debugProbeRouteOnly = limitDecision.routeOnly;
        refreshEffectCaptureEffectLists();
        return true;
    };
    if (! debugProbeStrippedLayer && context.scene &&
        wallpaper::debug::shouldLimitRequestedEffectProbeLayer(
            context.scene->debugEffectCaptures,
            effectCaptureInfo)) {
        const char* reason =
            context.scene->debugEffectCaptures.shouldProbeHighRiskLayer(wpimgobj.id)
                ? "high-risk-layer-id-probe"
                : "layer-id-probe";
        applyDebugProbeLimit(reason);
    }
    if (puppetEffectDecision.reason == "puppet-alpha-strip") {
        if (context.scene && context.scene->debugEffectCaptures.enabled()) {
            if (effectCaptureInfo.candidateChecks.isProtectedPuppetPath) {
                effectCaptureInfo.effectMaterials =
                    BuildAuthoredEffectMaterialDiagnostics(effectObjects);
                effectCaptureInfo.publish.enabled = true;
                effectCaptureInfo.publish.parentId = wpimgobj.parent;
                effectCaptureInfo.publish.objectSize = {
                    static_cast<float>(wpimgobj.size[0]),
                    static_cast<float>(wpimgobj.size[1]),
                };
                effectCaptureInfo.publish.origin = {
                    wpimgobj.origin[0],
                    wpimgobj.origin[1],
                    wpimgobj.origin[2],
                };
                effectCaptureInfo.publish.scale = {
                    wpimgobj.scale[0],
                    wpimgobj.scale[1],
                    wpimgobj.scale[2],
                };
                effectCaptureInfo.publish.angles = {
                    wpimgobj.angles[0],
                    wpimgobj.angles[1],
                    wpimgobj.angles[2],
                };
                effectCaptureInfo.publish.publishFinalOutput = true;
                effectCaptureInfo.publish.finalPublishRenderTarget =
                    std::string(SpecTex_Default);
            }
            wallpaper::debug::recordStrippedEffectCandidate(*context.scene, effectCaptureInfo);
        }
        if (debugProbeStrippedLayer) {
            LOG_INFO("debug probing stripped effect layer: id=%d name=%s reason=%s",
                     wpimgobj.id,
                     wpimgobj.name.c_str(),
                     debugProbeReason.c_str());
            applyDebugProbeLimit(debugProbeReason.c_str());
        } else {
            LOG_INFO("stripping %d effects from layer (alpha fix): name=%s",
                     count_eff, wpimgobj.name.c_str());
            count_eff = 0;
            hasEffect = false;
            effectObjects.clear();
            if (! puppetEffectDecision.keepLayer) return;
        }
    }

    bool                  usePuppetChannelMapPrepass { false };
    bool                  routePuppetPrepassThroughAuthoredEffects { false };
    bool                  useStandalonePuppetFinalDisplay { false };
    bool                  useStandalonePuppetBaseDisplay { false };
    bool                  useDeferredPuppetFinalRoute { false };
    bool                  zeroSlotChannelMapFallback { false };
    const std::string     puppetFinalMeshOverride =
        context.scene ? context.scene->debugEffectCaptures.puppetFinalMeshOverride : std::string();
    std::vector<uint32_t> activePuppetChannelBlendSlots;
    std::string           channelMapPrepassMode;
    std::string           channelMapMaterialPath;
    std::vector<WPPuppetLayer::AnimationLayer> renderPuppetLayers = wpimgobj.puppet_layers;
    {
        if (! hasEffect) {
            svData.parallaxDepth = effectiveParallaxDepth;
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
            baseConstSvs["g_Color"] = std::array<float, 3> {
                wpimgobj.color[0],
                wpimgobj.color[1],
                wpimgobj.color[2],
            };
            baseConstSvs["g_Alpha"] = wpimgobj.alpha;
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
                                                  &activePuppetChannelBlendSlots,
                                                  &channelMapPrepassMode,
                                                  &channelMapMaterialPath);
            if (usePuppetChannelMapPrepass && activePuppetChannelBlendSlots.empty()) {
                LOG_INFO("native puppet channelmap prepass disabled because no active blend slots remain: image=%s",
                         wpimgobj.name.c_str());
                usePuppetChannelMapPrepass = false;
                zeroSlotChannelMapFallback = true;
                channelMapPrepassMode.clear();
                channelMapMaterialPath.clear();
                sourceMaterial             = wpimgobj.material;
                svData.parallaxDepth = effectiveParallaxDepth;
                WPMdlParser::AddPuppetShaderInfo(shaderInfo, *puppet);
                LOG_INFO("native puppet channelmap prepass falling back to ordinary puppet image-effects route: image=%s authoredEffects=%d",
                         wpimgobj.name.c_str(),
                         count_eff);
            } else if (! usePuppetChannelMapPrepass) {
                const bool forceLegacyPuppetFinalRoute =
                    puppetFinalMeshOverride == "layer-card" ||
                    puppetFinalMeshOverride == "image-space";
                useDeferredPuppetFinalRoute = ! forceLegacyPuppetFinalRoute;
                svData.parallaxDepth = effectiveParallaxDepth;
                if (useDeferredPuppetFinalRoute) {
                    LOG_INFO("deferred puppet-final route enabled: image=%s authoredEffects=%d",
                             wpimgobj.name.c_str(),
                             count_eff);
                } else {
                    WPMdlParser::AddPuppetShaderInfo(shaderInfo, *puppet);
                }
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
    auto      effectViewport = wallpaper::policy::decideLayerEffectViewport({
        .objectWidth = wpimgobj.size[0],
        .objectHeight = wpimgobj.size[1],
    });
    wallpaper::debug::EffectCaptureMeshBoundsInfo effectInputMeshBoundsForViewport;
    bool effectInputMeshBoundsCaptured { false };
    std::array<float, 2> textureMapRate { 1.0f, 1.0f };

    {
        // deal with pow of 2
        if (! wpimgobj.nopadding &&
            exists(material.customShader.constValues, WE_GLTEX_RESOLUTION_NAMES[0])) {
            const auto& r = material.customShader.constValues.at(WE_GLTEX_RESOLUTION_NAMES[0]);
            textureMapRate = { r[2] / r[0], r[3] / r[1] };
            if (puppet && (std::abs(textureMapRate[0] - 1.0f) > 1.0e-6f ||
                           std::abs(textureMapRate[1] - 1.0f) > 1.0e-6f)) {
                LOG_INFO("native puppet texture map-rate applied: image=%s rate=(%.6f, %.6f)",
                         wpimgobj.name.c_str(),
                         textureMapRate[0],
                         textureMapRate[1]);
            }
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
                } else if (useDeferredPuppetFinalRoute) {
                    GenCardMesh(mesh,
                                { static_cast<uint16_t>(wpimgobj.size[0]),
                                  static_cast<uint16_t>(wpimgobj.size[1]) },
                                textureMapRate);
                    WPMdlParser::GenPuppetMesh(effct_final_mesh, *puppet, textureMapRate);
                    routePuppetPrepassThroughAuthoredEffects = true;

                    wpscene::WPImageEffect puppetEffect;
                    puppetEffect.name = "yakkai_deferred_puppet_final";
                    wpscene::WPMaterial puppetMaterial = wpimgobj.material;
                    if (puppetMaterial.textures.empty()) {
                        puppetMaterial.textures.push_back("");
                    } else {
                        puppetMaterial.textures[0].clear();
                    }
                    WPMdlParser::AddPuppetMatInfo(puppetMaterial, *puppet);
                    puppetEffect.materials.push_back(std::move(puppetMaterial));
                    effectObjects.push_back(std::move(puppetEffect));
                    refreshEffectCaptureEffectLists();

                    LOG_INFO("routing flat puppet source through authored effects before deferred puppet final mesh: image=%s authoredEffects=%d",
                             wpimgobj.name.c_str(),
                             count_eff);
                } else {
                    svData.puppet_layer = WPPuppetLayer(puppet->puppet);
                    svData.puppet_layer.prepared(renderPuppetLayers);
                    WPMdlParser::GenPuppetMesh(mesh, *puppet, textureMapRate);
                    effectInputMeshBoundsForViewport = DebugMeshBounds(mesh);
                    effectInputMeshBoundsCaptured = true;
                    if (effectInputMeshBoundsForViewport.positionMin.size() >= 2 &&
                        effectInputMeshBoundsForViewport.positionMax.size() >= 2) {
                        effectViewport = wallpaper::policy::decideLayerEffectViewport({
                            .objectWidth = wpimgobj.size[0],
                            .objectHeight = wpimgobj.size[1],
                            .hasMeshBounds = true,
                            .meshPositionMinX = effectInputMeshBoundsForViewport.positionMin[0],
                            .meshPositionMinY = effectInputMeshBoundsForViewport.positionMin[1],
                            .meshPositionMaxX = effectInputMeshBoundsForViewport.positionMax[0],
                            .meshPositionMaxY = effectInputMeshBoundsForViewport.positionMax[1],
                        });
                    }
                    GenCardMesh(effct_final_mesh,
                                { static_cast<uint16_t>(effectViewport.width),
                                  static_cast<uint16_t>(effectViewport.height) });
                    useStandalonePuppetFinalDisplay = true;
                    LOG_INFO("routing full puppet render through offscreen target before standalone puppet final publish: image=%s authoredEffects=%d viewport=%dx%d expanded=%d",
                             wpimgobj.name.c_str(),
                             count_eff,
                             effectViewport.width,
                             effectViewport.height,
                             effectViewport.expandedToMeshBounds ? 1 : 0);
                }

                if (routePuppetPrepassThroughAuthoredEffects) {
                    LOG_INFO("routing puppet channelmap prepass through authored image effects before final generic puppet stage: image=%s authoredEffects=%d",
                             wpimgobj.name.c_str(),
                             count_eff);
                }
            } else {
                svData.puppet_layer = WPPuppetLayer(puppet->puppet);
                svData.puppet_layer.prepared(renderPuppetLayers);
                WPMdlParser::GenPuppetMesh(mesh, *puppet, textureMapRate);
            }
        }
        if (! puppet) {
            GenCardMesh(mesh,
                        { (uint16_t)wpimgobj.size[0], (uint16_t)wpimgobj.size[1] },
                        textureMapRate);
            GenCardMesh(effct_final_mesh,
                        { (uint16_t)wpimgobj.size[0], (uint16_t)wpimgobj.size[1] });
        }
    }
    std::vector<int> activePuppetChannelBlendSlotIds;
    activePuppetChannelBlendSlotIds.reserve(activePuppetChannelBlendSlots.size());
    for (const uint32_t slot : activePuppetChannelBlendSlots) {
        activePuppetChannelBlendSlotIds.push_back(static_cast<int>(slot));
    }
    const std::string effectivePuppetFinalMeshOverride =
        useDeferredPuppetFinalRoute ? "deferred-puppet-final" : puppetFinalMeshOverride;
    const auto puppetEffectRoutePlan = wallpaper::decidePuppetEffectRoutePlan({
        .puppetLayer = puppet != nullptr,
        .fullscreen = wpimgobj.fullscreen,
        .composelayer = isCompose,
        .effectRouteActive = hasEffect,
        .usePuppetChannelMapPrepass = usePuppetChannelMapPrepass,
        .routePuppetPrepassThroughAuthoredEffects = routePuppetPrepassThroughAuthoredEffects,
        .debugRouteOnly = effectCaptureInfo.debugProbeRouteOnly,
        .puppetFinalMeshOverride = effectivePuppetFinalMeshOverride,
        .activeChannelBlendSlots = activePuppetChannelBlendSlotIds,
    });
    useStandalonePuppetFinalDisplay =
        puppetEffectRoutePlan.useStandalonePuppetFinalDisplay;
    // material blendmode for last step to use
    auto imgBlendMode = material.blenmode;
    // disable img material blend, as it's the first effect node now
    if (hasEffect && ! puppetEffectRoutePlan.effectInputMaterialPreservesLayerBlendMode) {
        material.blenmode = BlendMode::Normal;
    }
    mesh.AddMaterial(std::move(material));
    spImgNode->AddMesh(spMesh);

    context.shader_updater->SetNodeData(spImgNode.get(), svData);
    SceneNode standaloneDisplayTransform;
    bool      hasStandaloneDisplayTransform = false;
    std::vector<std::shared_ptr<SceneNode>> standaloneDisplayNodes;
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
            i32 w = effectViewport.width;
            i32 h = effectViewport.height;
            scene.cameras[nodeAddr] = std::make_shared<SceneCamera>(w, h, -1.0f, 1.0f);
            // Camera node positioning deferred to after CopyTrans below
        }
        spImgNode->SetCamera(nodeAddr);
        std::string effect_ppong_a, effect_ppong_b;
        effect_ppong_a = WE_EFFECT_PPONG_PREFIX_A.data() + nodeAddr;
        effect_ppong_b = WE_EFFECT_PPONG_PREFIX_B.data() + nodeAddr;
        const bool debugRouteOnlyCapture =
            scene.debugEffectCaptures.puppetEffectRouteOnly &&
            effectCaptureInfo.debugProbeRouteOnly &&
            effectObjects.empty();
        const bool debugEffectCaptures =
            scene.debugEffectCaptures.enabled() &&
            scene.debugEffectCaptures.shouldCaptureLayer(wpimgobj.id) &&
            (! effectObjects.empty() || debugRouteOnlyCapture);
        std::string debugEffectInputTarget;
        std::string debugEffectOutputSourceTarget;
        std::string debugEffectOutputTarget;
        std::string debugDefaultBeforeEffectTarget;
        std::string debugDefaultAfterEffectTarget;
        // set image effect
        auto imgEffectLayer = std::make_shared<SceneImageEffectLayer>(
            spImgNode.get(), effectViewport.width, effectViewport.height, effect_ppong_a, effect_ppong_b);
        {
            imgEffectLayer->SetFinalBlend(imgBlendMode);
            if (wpimgobj.fullscreen) {
                imgEffectLayer->SetFullscreen(true);
            }
            if (! puppetEffectRoutePlan.publishFinalOutput) {
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
                scene.cameras.at(nodeAddr)->AttatchNode(context.effect_camera_node);
            }
            scene.cameras.at(nodeAddr)->AttatchImgEffect(imgEffectLayer);
        }
        // set renderTarget for ping-pong operate
        {
            uint16_t rtW = static_cast<uint16_t>(effectViewport.width);
            uint16_t rtH = static_cast<uint16_t>(effectViewport.height);
            scene.renderTargets[effect_ppong_a] = {
                .width      = rtW,
                .height     = rtH,
                .allowReuse = true,
            };
            if (wpimgobj.fullscreen) {
                scene.renderTargets[effect_ppong_a].bind = { .enable = true, .screen = true };
            }
            scene.renderTargets[effect_ppong_b] = scene.renderTargets.at(effect_ppong_a);
            if (debugEffectCaptures) {
                debugEffectInputTarget = "_rt_debug_effect_input_" + nodeAddr;
                scene.renderTargets[debugEffectInputTarget] =
                    scene.renderTargets.at(effect_ppong_a);
                scene.renderTargets[debugEffectInputTarget].allowReuse = false;
                debugEffectOutputSourceTarget =
                    useStandalonePuppetFinalDisplay
                        ? std::string(WE_DEBUG_EFFECT_FINAL_OUTPUT_PREFIX) + nodeAddr
                        : std::string(SpecTex_Default);
                debugEffectOutputTarget = "_rt_debug_effect_output_" + nodeAddr;
                scene.renderTargets[debugEffectOutputTarget] =
                    scene.renderTargets.at(
                        useStandalonePuppetFinalDisplay ? effect_ppong_a : debugEffectOutputSourceTarget);
                scene.renderTargets[debugEffectOutputTarget].allowReuse = false;
                effectCaptureInfo.publish.enabled = true;
                effectCaptureInfo.publish.parentId = wpimgobj.parent;
                effectCaptureInfo.publish.hasParsedParentNode =
                    context.object_nodes.count(wpimgobj.parent) != 0;
                effectCaptureInfo.publish.objectSize = {
                    static_cast<float>(wpimgobj.size[0]),
                    static_cast<float>(wpimgobj.size[1]),
                };
                effectCaptureInfo.publish.origin = {
                    wpimgobj.origin[0],
                    wpimgobj.origin[1],
                    wpimgobj.origin[2],
                };
                effectCaptureInfo.publish.scale = {
                    wpimgobj.scale[0],
                    wpimgobj.scale[1],
                    wpimgobj.scale[2],
                };
                effectCaptureInfo.publish.angles = {
                    wpimgobj.angles[0],
                    wpimgobj.angles[1],
                    wpimgobj.angles[2],
                };
                effectCaptureInfo.publish.finalBlendMode = static_cast<int>(imgBlendMode);
                effectCaptureInfo.publish.fullscreen = wpimgobj.fullscreen;
                effectCaptureInfo.publish.composelayer = isCompose;
                effectCaptureInfo.publish.puppetLayer = puppet != nullptr;
                effectCaptureInfo.publish.effectInputViewportSize = {
                    static_cast<float>(effectViewport.width),
                    static_cast<float>(effectViewport.height),
                };
                effectCaptureInfo.publish.effectInputViewportExpanded =
                    effectViewport.expandedToMeshBounds;
                effectCaptureInfo.publish.standalonePuppetFinalDisplay = useStandalonePuppetFinalDisplay;
                effectCaptureInfo.publish.publishFinalOutput =
                    puppetEffectRoutePlan.publishFinalOutput;
                effectCaptureInfo.publish.finalNodeUsesOriginalParent =
                    ! wpimgobj.fullscreen;
                effectCaptureInfo.publish.effectInputNodeReset = ! isCompose;
                effectCaptureInfo.publish.effectInputMaterialPreservesLayerBlendMode =
                    puppetEffectRoutePlan.effectInputMaterialPreservesLayerBlendMode;
                effectCaptureInfo.publish.effectInputMeshKind =
                    puppetEffectRoutePlan.effectInputMeshKind;
                effectCaptureInfo.publish.effectFinalMeshKind =
                    puppetEffectRoutePlan.effectFinalMeshKind;
                effectCaptureInfo.publish.standaloneFinalMeshKind =
                    puppetEffectRoutePlan.standaloneFinalMeshKind;
                effectCaptureInfo.publish.finalDisplayRoute =
                    puppetEffectRoutePlan.finalDisplayRoute;
                effectCaptureInfo.publish.standaloneDisplayAttachMode =
                    puppetEffectRoutePlan.standaloneDisplayAttachMode;
                effectCaptureInfo.publish.routeRisk = puppetEffectRoutePlan.routeRisk;
                effectCaptureInfo.publish.effectInputRenderTarget = effect_ppong_a;
                effectCaptureInfo.publish.effectPingPongA = effect_ppong_a;
                effectCaptureInfo.publish.effectPingPongB = effect_ppong_b;
                effectCaptureInfo.publish.effectOutputSourceTarget = debugEffectOutputSourceTarget;
                effectCaptureInfo.publish.finalPublishRenderTarget = std::string(SpecTex_Default);
                effectCaptureInfo.publish.materialOutputCaptureTiming =
                    "effect-command-copy-after-layer-node";
                effectCaptureInfo.publish.finalPublishCaptureTiming =
                    "post-frame-render-target-dump";
                effectCaptureInfo.publish.defaultRtBoundaryCaptureTiming =
                    "effect-command-copy-around-effect-layer";
                effectCaptureInfo.publish.channelMapPrepassMode = channelMapPrepassMode;
                effectCaptureInfo.publish.channelMapMaterialPath = channelMapMaterialPath;
                effectCaptureInfo.publish.activePuppetChannelBlendSlots.clear();
                for (const uint32_t slot : activePuppetChannelBlendSlots) {
                    effectCaptureInfo.publish.activePuppetChannelBlendSlots.push_back(
                        static_cast<int>(slot));
                }
                effectCaptureInfo.publish.effectInputLocalTransform =
                    DebugNodeTransform(*spImgNode);
                if (hasStandaloneDisplayTransform) {
                    effectCaptureInfo.publish.standaloneDisplayLocalTransform =
                        DebugNodeTransform(standaloneDisplayTransform);
                }
                effectCaptureInfo.publish.standaloneDisplayParentId = wpimgobj.parent;
                effectCaptureInfo.publish.standaloneDisplayHasParsedParentNode =
                    context.object_nodes.count(wpimgobj.parent) != 0;
                effectCaptureInfo.publish.standaloneFinalTexture =
                    useStandalonePuppetFinalDisplay
                        ? ResolveEffectPingPongFinalTarget(effect_ppong_a,
                                                           effect_ppong_b,
                                                           count_eff)
                        : std::string();
                effectCaptureInfo.publish.effectInputMeshBounds =
                    effectInputMeshBoundsCaptured ? effectInputMeshBoundsForViewport : DebugMeshBounds(*spMesh);
                effectCaptureInfo.publish.effectFinalMeshBounds = DebugMeshBounds(effct_final_mesh);
                wallpaper::debug::registerEffectCapture(
                    scene, effectCaptureInfo, "effect-input", debugEffectInputTarget);
                debugDefaultBeforeEffectTarget =
                    wallpaper::debug::registerDefaultRtBoundaryCapture(scene,
                                                                       effectCaptureInfo,
                                                                       "default-before-effect",
                                                                       nodeAddr);
                wallpaper::debug::registerEffectCapture(
                    scene,
                    effectCaptureInfo,
                    "effect-output",
                    debugEffectOutputTarget);
                debugDefaultAfterEffectTarget =
                    wallpaper::debug::registerDefaultRtBoundaryCapture(scene,
                                                                       effectCaptureInfo,
                                                                       "default-after-effect",
                                                                       nodeAddr);
                wallpaper::debug::registerEffectCapture(
                    scene, effectCaptureInfo, "final-publish", SpecTex_Default);
            }
        }

        int32_t i_eff = -1;
        int32_t debug_eff_total = effectObjects.size();
        int32_t debug_visible_eff_total = 0;
        for (const auto& effectObject : effectObjects) {
            if (effectObject.visible) {
                debug_visible_eff_total++;
            }
        }
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
                            .width      = (uint16_t)(effectViewport.width / (float)wpfbo.scale),
                            .height     = (uint16_t)(effectViewport.height / (float)wpfbo.scale),
                            .allowReuse = true
                        };
                    }
                    fboMap[wpfbo.name] = rtname;
                }
            }
            // load! effect commands
            {
                if (debugEffectCaptures && i_eff == 0 && ! debugEffectInputTarget.empty()) {
                    if (! debugDefaultBeforeEffectTarget.empty()) {
                        imgEffect->commands.push_back({
                            .cmd      = SceneImageEffect::CmdType::Copy,
                            .dst      = debugDefaultBeforeEffectTarget,
                            .src      = std::string(SpecTex_Default),
                            .afterpos = 0,
                        });
                    }
                    imgEffect->commands.push_back({ .cmd      = SceneImageEffect::CmdType::Copy,
                                                    .dst      = debugEffectInputTarget,
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
            bool debugAddedMaterialOutputCapture { false };

            for (usize i_mat = 0; i_mat < wpeffobj.materials.size(); i_mat++) {
                wpscene::WPMaterial wpmat = wpeffobj.materials.at(i_mat);
                std::string         matOutRT { WE_EFFECT_PPONG_PREFIX_B };
                if (wpeffobj.passes.size() > i_mat) {
                    const auto& wppass = wpeffobj.passes.at(i_mat);
                    wpmat.MergePass(wppass);
                    ApplyScriptMaterialConstantBindings(context,
                                                        wpimgobj.id,
                                                        wpeffobj.id,
                                                        wppass.id,
                                                        wpmat);
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
                const bool isFinalPublishedMaterialNode =
                    sstart_with(matOutRT, WE_EFFECT_PPONG_PREFIX_B) &&
                    ! useStandalonePuppetFinalDisplay && i_eff == debug_visible_eff_total - 1 &&
                    i_mat + 1 == wpeffobj.materials.size();
                if (puppet && useStandalonePuppetFinalDisplay &&
                    !usePuppetChannelMapPrepass && ShouldPreservePuppetSourceAlphaForShader(wpmat.shader)) {
                    // Keep this compatibility patch limited to shaders with
                    // pass-boundary evidence proving WE preserves source alpha.
                    wpmat.combos["YAKKAI_PRESERVE_SOURCE_ALPHA"] = 1;
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
                bool debugDuplicateFinalMaterialNode { false };
                if (debugEffectCaptures) {
                    wallpaper::debug::EffectCaptureMaterialInfo materialInfo;
                    materialInfo.effectIndex = i_eff + 1;
                    materialInfo.materialIndex = static_cast<int>(i_mat);
                    materialInfo.shader = wpmat.shader;
                    materialInfo.authoredTextures = wpmat.textures;
                    materialInfo.resolvedTextures = material.textures;
                    materialInfo.textureBindings =
                        BuildTextureBindings(materialInfo.authoredTextures, materialInfo.resolvedTextures);
                    materialInfo.authoredCombos = StringifyCombos(wpmat.combos);
                    materialInfo.resolvedCombos = StringifyCombos(wpEffShaderInfo.combos);
                    materialInfo.materialValues = wpmat.constantshadervalues;
                    materialInfo.resolvedConstValues =
                        ShaderValuesToDebugMap(material.customShader.constValues);
                    materialInfo.defines = material.defines;
                    materialInfo.authoredOutputRenderTarget = matOutRT;
                    materialInfo.resolvedOutputRenderTarget = matOutRT;

                    if (wallpaper::debug::shouldRegisterMaterialOutputCaptureForShader(wpmat.shader)) {
                        const int effectIndex = i_eff + 1;
                        const int materialIndex = static_cast<int>(i_mat);
                        const bool isLutMaterial =
                            wpmat.shader.find("lut_loader") != std::string::npos;
                        const bool isFinalPublishedMaterial = isFinalPublishedMaterialNode;
                        const std::string debugFinalOutputSource =
                            std::string(WE_DEBUG_EFFECT_FINAL_OUTPUT_PREFIX) + nodeAddr;
                        std::string sourceRenderTarget = matOutRT;
                        std::string commandSource = matOutRT;
                        bool sourceFinalEffectOutput = false;
                        materialInfo.finalPublishedMaterial = isFinalPublishedMaterial;
                        if (sstart_with(matOutRT, WE_EFFECT_PPONG_PREFIX_B)) {
                            sourceRenderTarget = effect_ppong_b;
                            commandSource = debugFinalOutputSource;
                            sourceFinalEffectOutput = true;
                            if (isLutMaterial && isFinalPublishedMaterial &&
                                scene.renderTargets.count(effect_ppong_b) > 0) {
                                const std::string debugLocalMaterialOutputTarget =
                                    "_rt_debug_material_output_local_" + nodeAddr + "_" +
                                    std::to_string(effectIndex) + "_" +
                                    std::to_string(materialIndex);
                                scene.renderTargets[debugLocalMaterialOutputTarget] =
                                    scene.renderTargets.at(effect_ppong_b);
                                scene.renderTargets[debugLocalMaterialOutputTarget].allowReuse = false;
                                const std::string localMaterialOutputStage =
                                    "material-output-local-" + std::to_string(effectIndex) + "-" +
                                    std::to_string(materialIndex);
                                materialInfo.localMaterialOutputCaptureStage = localMaterialOutputStage;
                                wallpaper::debug::registerEffectCapture(scene,
                                                                        effectCaptureInfo,
                                                                        localMaterialOutputStage,
                                                                        debugLocalMaterialOutputTarget);
                                imgEffect->commands.push_back({
                                    .cmd = SceneImageEffect::CmdType::Copy,
                                    .dst = debugLocalMaterialOutputTarget,
                                    .src = debugFinalOutputSource,
                                    .afterpos = static_cast<i32>(i_mat + 1),
                                    .srcFinalEffectOutput = true,
                                });
                                debugAddedMaterialOutputCapture = true;
                                debugDuplicateFinalMaterialNode = true;
                            }
                            if (isFinalPublishedMaterial) {
                                sourceRenderTarget = std::string(SpecTex_Default);
                            }
                        } else if (sstart_with(matOutRT, WE_EFFECT_PPONG_PREFIX_A)) {
                            sourceRenderTarget = effect_ppong_a;
                        }
                        materialInfo.resolvedOutputRenderTarget = sourceRenderTarget;
                        materialInfo.debugMaterialOutputSourceRenderTarget = sourceRenderTarget;
                        materialInfo.debugMaterialOutputCommandSource = commandSource;
                        materialInfo.debugSourceFinalEffectOutput = sourceFinalEffectOutput;

                        if (scene.renderTargets.count(sourceRenderTarget) > 0) {
                            const std::string debugMaterialOutputTarget =
                                "_rt_debug_material_output_" + nodeAddr + "_" +
                                std::to_string(effectIndex) + "_" + std::to_string(materialIndex);
                            scene.renderTargets[debugMaterialOutputTarget] =
                                scene.renderTargets.at(sourceRenderTarget);
                            scene.renderTargets[debugMaterialOutputTarget].allowReuse = false;
                            const std::string materialOutputStage =
                                "material-output-" + std::to_string(effectIndex) + "-" +
                                std::to_string(materialIndex);
                            const i32 materialOutputCopyAfterPos = static_cast<i32>(
                                i_mat + (debugDuplicateFinalMaterialNode ? 2 : 1));
                            materialInfo.materialOutputCaptureStage = materialOutputStage;
                            materialInfo.materialOutputCopyAfterPos = materialOutputCopyAfterPos;
                            wallpaper::debug::registerEffectCapture(
                                scene, effectCaptureInfo, materialOutputStage, debugMaterialOutputTarget);
                            imgEffect->commands.push_back({
                                .cmd = SceneImageEffect::CmdType::Copy,
                                .dst = debugMaterialOutputTarget,
                                .src = commandSource,
                                .afterpos = materialOutputCopyAfterPos,
                                .srcFinalEffectOutput = sourceFinalEffectOutput,
                            });
                            debugAddedMaterialOutputCapture = true;
                        }
                    }
                    effectCaptureInfo.effectMaterials.push_back(std::move(materialInfo));
                }
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
                if (debugEffectCaptures && isFinalPublishedMaterialNode) {
                    wallpaper::debug::registerEffectLayerFinalPublishBoundaryCapture(
                        scene,
                        effectCaptureInfo,
                        *spEffNode,
                        nodeAddr + "_effect_final");
                }
                const bool preservePuppetMesh =
                    puppetEffectRoutePlan.preservePuppetMeshForEffectPasses &&
                    puppet && wpmat.use_puppet;
                {
                    svData.parallaxDepth =
                        wallpaper::decideEffectLayerMaterialParallaxDepth(
                            puppetEffectRoutePlan,
                            isFinalPublishedMaterialNode,
                            effectiveParallaxDepth);
                    if (puppet && wpmat.use_puppet) {
                        svData.puppet_layer = WPPuppetLayer(puppet->puppet);
                        svData.puppet_layer.prepared(wpimgobj.puppet_layers);
                    }
                }
                if (preservePuppetMesh) {
                    spMesh->ChangeMeshDataFrom(effct_final_mesh);
                }
                if (debugDuplicateFinalMaterialNode) {
                    auto spLocalNode = std::make_shared<SceneNode>();
                    auto spLocalMesh = std::make_shared<SceneMesh>();
                    if (preservePuppetMesh) {
                        spLocalMesh->ChangeMeshDataFrom(effct_final_mesh);
                    }
                    spLocalMesh->AddMaterial(SceneMaterial(material));
                    spLocalNode->AddMesh(spLocalMesh);
                    context.shader_updater->SetNodeData(spLocalNode.get(), svData);
                    imgEffect->nodes.push_back({ matOutRT, spLocalNode, preservePuppetMesh });
                }
                spMesh->AddMaterial(std::move(material));
                spEffNode->AddMesh(spMesh);

                context.shader_updater->SetNodeData(spEffNode.get(), svData);
                imgEffect->nodes.push_back({ matOutRT, spEffNode, preservePuppetMesh });
            }

            const bool isAuthoredEffectOutputCapturePoint =
                count_eff > 0 && i_eff == count_eff - 1;
            const bool isDeferredRouteOnlyOutputCapturePoint =
                count_eff == 0 &&
                routePuppetPrepassThroughAuthoredEffects &&
                i_eff == 0;
            if (debugEffectCaptures && eff_mat_ok &&
                (isAuthoredEffectOutputCapturePoint ||
                 isDeferredRouteOnlyOutputCapturePoint) &&
                ! debugEffectOutputTarget.empty() && ! debugEffectOutputSourceTarget.empty()) {
                imgEffect->commands.push_back({ .cmd      = SceneImageEffect::CmdType::Copy,
                                                .dst      = debugEffectOutputTarget,
                                                .src      = debugEffectOutputSourceTarget,
                                                .afterpos = static_cast<i32>(imgEffect->nodes.size()),
                                                .srcFinalEffectOutput = useStandalonePuppetFinalDisplay });
                if (! debugDefaultAfterEffectTarget.empty()) {
                    imgEffect->commands.push_back({
                        .cmd      = SceneImageEffect::CmdType::Copy,
                        .dst      = debugDefaultAfterEffectTarget,
                        .src      = std::string(SpecTex_Default),
                        .afterpos = static_cast<i32>(imgEffect->nodes.size()),
                    });
                }
            }
            if (debugAddedMaterialOutputCapture) {
                std::stable_sort(imgEffect->commands.begin(),
                                 imgEffect->commands.end(),
                                 [](const auto& left, const auto& right) {
                                     return left.afterpos < right.afterpos;
                                 });
            }

            if (eff_mat_ok)
                imgEffectLayer->AddEffect(imgEffect);
            else {
                LOG_ERROR("effect \'%s\' failed to load", wpeffobj.name.c_str());
            }
        }
        if (debugRouteOnlyCapture) {
            auto imgEffect = std::make_shared<SceneImageEffect>();
            if (! debugDefaultBeforeEffectTarget.empty()) {
                imgEffect->commands.push_back({
                    .cmd      = SceneImageEffect::CmdType::Copy,
                    .dst      = debugDefaultBeforeEffectTarget,
                    .src      = std::string(SpecTex_Default),
                    .afterpos = 0,
                });
            }
            if (! debugEffectInputTarget.empty()) {
                imgEffect->commands.push_back({
                    .cmd      = SceneImageEffect::CmdType::Copy,
                    .dst      = debugEffectInputTarget,
                    .src      = effect_ppong_a,
                    .afterpos = 0,
                });
            }
            if (! debugEffectOutputTarget.empty()) {
                imgEffect->commands.push_back({
                    .cmd      = SceneImageEffect::CmdType::Copy,
                    .dst      = debugEffectOutputTarget,
                    .src      = effect_ppong_a,
                    .afterpos = 0,
                });
            }
            if (! debugDefaultAfterEffectTarget.empty()) {
                imgEffect->commands.push_back({
                    .cmd      = SceneImageEffect::CmdType::Copy,
                    .dst      = debugDefaultAfterEffectTarget,
                    .src      = std::string(SpecTex_Default),
                    .afterpos = 0,
                });
            }
            imgEffectLayer->AddEffect(imgEffect);
        }
        if (debugEffectCaptures) {
            wallpaper::debug::refreshEffectCaptureLayerInfo(scene, effectCaptureInfo);
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
                    baseSvData.parallaxDepth = effectiveParallaxDepth;
                    baseSvData.puppet_layer = WPPuppetLayer(puppet->puppet);
                    baseSvData.puppet_layer.prepared(wpimgobj.puppet_layers);

                    auto spBaseMesh = std::make_shared<SceneMesh>();
                    WPMdlParser::GenPuppetMesh(*spBaseMesh, *puppet, textureMapRate);
                    spBaseMesh->AddMaterial(std::move(baseMaterial));
                    spBaseNode->AddMesh(spBaseMesh);
                    if (hasStandaloneDisplayTransform) {
                        spBaseNode->CopyTrans(standaloneDisplayTransform);
                    }
                    context.shader_updater->SetNodeData(spBaseNode.get(), baseSvData);
                    standaloneDisplayNodes.push_back(spBaseNode);

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
                const std::string finalEffectTexture =
                    ResolveEffectPingPongFinalTarget(effect_ppong_a, effect_ppong_b, count_eff);

                wallpaper::PuppetFinalDisplayBuildInput finalDisplayInput;
                finalDisplayInput.scene = &scene;
                finalDisplayInput.effectCaptureInfo = &effectCaptureInfo;
                finalDisplayInput.imageName = wpimgobj.name;
                finalDisplayInput.imageParentId = wpimgobj.parent;
                finalDisplayInput.imageParentParsed =
                    context.object_nodes.count(wpimgobj.parent) != 0;
                finalDisplayInput.nodeAddr = nodeAddr;
                finalDisplayInput.existingStandaloneDisplayNodeCount =
                    standaloneDisplayNodes.size();
                finalDisplayInput.debugEffectCaptures = debugEffectCaptures;
                finalDisplayInput.usePuppetChannelMapPrepass = usePuppetChannelMapPrepass;
                finalDisplayInput.activePuppetChannelBlendSlots =
                    activePuppetChannelBlendSlots;
                finalDisplayInput.puppet = puppet.get();
                finalDisplayInput.imageMaterial = wpimgobj.material;
                finalDisplayInput.sourceMaterial = sourceMaterial;
                finalDisplayInput.imageSize = { wpimgobj.size[0], wpimgobj.size[1] };
                finalDisplayInput.effectViewport = effectViewport;
                finalDisplayInput.routePlan = puppetEffectRoutePlan;
                finalDisplayInput.standaloneDisplayTransform =
                    hasStandaloneDisplayTransform ? &standaloneDisplayTransform : nullptr;
                finalDisplayInput.baseConstSvs = baseConstSvs;
                finalDisplayInput.parallaxDepth = effectiveParallaxDepth;
                finalDisplayInput.renderPuppetLayers = renderPuppetLayers;
                finalDisplayInput.finalEffectTexture = finalEffectTexture;
                finalDisplayInput.authoredEffectCount = count_eff;
                finalDisplayInput.loadMaterial =
                    [&](const wpscene::WPMaterial& material,
                        SceneNode* node,
                        SceneMaterial* sceneMaterial,
                        WPShaderValueData* shaderValueData,
                        WPShaderInfo* shaderInfo) {
                        return LoadMaterial(vfs,
                                            material,
                                            context.scene.get(),
                                            node,
                                            sceneMaterial,
                                            shaderValueData,
                                            shaderInfo);
                    };
                finalDisplayInput.loadConstValues =
                    [](SceneMaterial& material,
                       const wpscene::WPMaterial& source,
                       const WPShaderInfo& shaderInfo) {
                        LoadConstvalue(material, source, shaderInfo);
                    };

                auto finalDisplayResult =
                    wallpaper::buildPuppetFinalDisplay(finalDisplayInput);
                if (finalDisplayResult.success) {
                    context.shader_updater->SetNodeData(finalDisplayResult.node.get(),
                                                        finalDisplayResult.shaderValueData);
                    standaloneDisplayNodes.push_back(finalDisplayResult.node);
                }
            }
        }
        if (debugEffectCaptures) {
            wallpaper::debug::refreshEffectCaptureLayerInfo(scene, effectCaptureInfo);
        }
    }
    if (childTransformAnchorNode) {
        AttachSceneGraphNode(context, spImgNode, wpimgobj.parent);
        AttachObjectNode(context, childTransformAnchorNode, wpimgobj.id, wpimgobj.parent);
    } else {
        AttachObjectNode(context, spImgNode, wpimgobj.id, wpimgobj.parent);
    }
    for (const auto& standaloneDisplayNode : standaloneDisplayNodes) {
        AttachObjectNode(context, standaloneDisplayNode, 0, wpimgobj.parent);
    }
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
    if (HasHiddenParent(context, wppartobj.parent)) return;
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
        svData.parallaxDepth =
            ResolveEffectiveParallaxDepth(context,
                                          wppartobj.parent,
                                          { wppartobj.parallaxDepth[0], wppartobj.parallaxDepth[1] });
        if (context.scene) {
            wallpaper::debug::recordMouseParallaxLayer(*context.scene,
                                                       wppartobj.id,
                                                       wppartobj.name,
                                                       "particle",
                                                       svData.parallaxDepth,
                                                       wppartobj.parent,
                                                       DebugObjectName(context, wppartobj.parent),
                                                       DebugChildLayerIds(context, wppartobj.id),
                                                       true);
        }
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
    if (HasHiddenParent(context, solid_obj.parent)) return;
    auto bindingIt = context.script_color_bindings.find(solid_obj.id);
    if (bindingIt != context.script_color_bindings.end()) {
        if (bindingIt->second.has_origin) {
            solid_obj.origin = bindingIt->second.origin;
        }
        if (bindingIt->second.has_scale) {
            solid_obj.scale = bindingIt->second.scale;
        }
    }

    auto node = std::make_shared<SceneNode>(Vector3f(solid_obj.origin.data()),
                                            Vector3f(solid_obj.scale.data()),
                                            Vector3f(solid_obj.angles.data()));
    node->ID() = solid_obj.id;
    if (context.scene) {
        wallpaper::debug::recordMouseParallaxLayer(*context.scene,
                                                   solid_obj.id,
                                                   solid_obj.name,
                                                   "transform-anchor",
                                                   solid_obj.parallaxDepth,
                                                   solid_obj.parent,
                                                   DebugObjectName(context, solid_obj.parent),
                                                   DebugChildLayerIds(context, solid_obj.id),
                                                   true);
    }
    AttachObjectNode(context, node, solid_obj.id, solid_obj.parent);
}

std::array<uint8_t, 7> GeneratedTextGlyphRows(char ch)
{
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    switch (c) {
    case 'A': return {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    case 'B': return {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
    case 'C': return {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
    case 'D': return {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
    case 'E': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    case 'F': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
    case 'G': return {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f};
    case 'H': return {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    case 'I': return {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e};
    case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    case 'M': return {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    case 'P': return {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    case 'Q': return {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d};
    case 'R': return {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    case 'S': return {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    case 'T': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a};
    case 'X': return {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
    case '0': return {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
    case '1': return {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
    case '2': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
    case '3': return {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
    case '4': return {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
    case '5': return {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
    case '6': return {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e};
    case '7': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8': return {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
    case '9': return {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
    case ',': return {0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08};
    case ':': return {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00};
    case ';': return {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08};
    case '-': return {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
    case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f};
    case '+': return {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00};
    case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    case '\\': return {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01};
    case '\'': return {0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00};
    case '"': return {0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00};
    case '!': return {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
    case '?': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    case '&': return {0x0c, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0d};
    case '(': return {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
    case ')': return {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
    case '[': return {0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e};
    case ']': return {0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e};
    case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    default: return {0x1f, 0x11, 0x15, 0x15, 0x15, 0x11, 0x1f};
    }
}

std::vector<std::string> SplitGeneratedTextLines(const std::string& text)
{
    std::vector<std::string> lines(1);
    for (char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            lines.emplace_back();
        } else {
            lines.back().push_back(ch);
        }
    }
    return lines;
}

std::vector<std::string> LimitedGeneratedTextLines(const WPTextObject& text_obj)
{
    auto lines = SplitGeneratedTextLines(text_obj.text);
    if (text_obj.maxRows > 0 &&
        lines.size() > static_cast<std::size_t>(text_obj.maxRows)) {
        lines.resize(static_cast<std::size_t>(text_obj.maxRows));
    }
    return lines;
}

std::string NormalizeGeneratedTextAlign(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string FlipGeneratedTextHorizontalAlign(std::string align)
{
    align = NormalizeGeneratedTextAlign(std::move(align));
    if (align == "left") {
        return "right";
    }
    if (align == "right") {
        return "left";
    }
    return align;
}

float EffectiveParentScaleX(const ParseContext& context, int32_t parentId)
{
    auto it = context.object_nodes.find(parentId);
    if (it == context.object_nodes.end() || !it->second) {
        return 1.0f;
    }

    float scale = 1.0f;
    const SceneNode* node = it->second.get();
    std::unordered_set<const SceneNode*> visited;
    while (node != nullptr && visited.insert(node).second) {
        scale *= node->Scale().x();
        node = node->Parent();
    }
    return scale;
}

WPTextObject TextObjectForRasterization(const ParseContext& context,
                                        const WPTextObject& text_obj)
{
    WPTextObject rasterText = text_obj;
    const float parentScaleX = EffectiveParentScaleX(context, text_obj.parent);
    const float effectiveScaleX = parentScaleX * text_obj.scale[0];
    if (effectiveScaleX < 0.0f) {
        rasterText.horizontalAlign =
            FlipGeneratedTextHorizontalAlign(text_obj.horizontalAlign);
    }
    return rasterText;
}

int GeneratedTextLayoutWidth(const WPTextObject& text_obj, int textureWidth)
{
    int layoutWidth = textureWidth;
    if (text_obj.limitWidth && text_obj.maxWidth > 0.0f) {
        layoutWidth = std::clamp(static_cast<int>(std::ceil(text_obj.maxWidth)),
                                 1,
                                 textureWidth);
    }
    return std::max(1, layoutWidth);
}

int GeneratedTextRasterWidth(const WPTextObject& text_obj)
{
    int width = std::clamp(static_cast<int>(std::ceil(text_obj.size[0])), 1, 8192);
    if (text_obj.limitWidth && text_obj.maxWidth > static_cast<float>(width)) {
        width = std::clamp(static_cast<int>(std::ceil(text_obj.maxWidth)), 1, 8192);
    }
    return width;
}

int GeneratedTextRasterHeight(const WPTextObject& text_obj)
{
    return std::clamp(static_cast<int>(std::ceil(text_obj.size[1])), 1, 2048);
}

int GeneratedTextEffectivePixelSize(const ParseContext& context, const WPTextObject& text_obj);

int GeneratedTextRasterPadding(const ParseContext& context, const WPTextObject& text_obj)
{
    const int effectivePixelSize = GeneratedTextEffectivePixelSize(context, text_obj);
    if (effectivePixelSize <= 0) {
        return 4;
    }
    return std::clamp(static_cast<int>(std::ceil(static_cast<float>(effectivePixelSize) * 0.12f)),
                      4,
                      32);
}

QRectF GeneratedTextLayoutRect(const WPTextObject& text_obj, int width, int height)
{
    const int layoutWidth = GeneratedTextLayoutWidth(text_obj, width);
    const std::string horizontalAlign = NormalizeGeneratedTextAlign(text_obj.horizontalAlign);
    int x = 0;
    if (layoutWidth < width) {
        if (horizontalAlign == "right") {
            x = width - layoutWidth;
        } else if (horizontalAlign == "center" || horizontalAlign == "centre") {
            x = (width - layoutWidth) / 2;
        }
    }
    return QRectF(static_cast<qreal>(x),
                  0.0,
                  static_cast<qreal>(layoutWidth),
                  static_cast<qreal>(height));
}

struct GeneratedTextTextureRegistration {
    std::string textureName;
    std::array<float, 2> textureSize { 0.0f, 0.0f };
    std::array<float, 2> layoutSize { 0.0f, 0.0f };
    std::array<float, 4> alphaBounds { 0.0f, 0.0f, 0.0f, 0.0f };
    std::string rasterizer;
    bool fontLoaded { false };
    std::string fontFamily;
    std::string fontLoadStatus;
    bool hasVisibleAlpha { false };
    int padding { 0 };
};

struct GeneratedTextFontLoadResult {
    std::optional<QString> family;
    std::string status;
};

struct GeneratedTextRasterDiagnostic {
    std::string rasterizer;
    bool fontLoaded { false };
    std::string fontFamily;
    std::string fontLoadStatus;
};

uint8_t ByteFromUnit(float value)
{
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

int GeneratedTextEffectivePixelSize(const ParseContext& context, const WPTextObject& text_obj)
{
    if (text_obj.pointSize <= 0.0f) {
        return 0;
    }

    const float sceneHeight = context.ortho_h > 0 ? static_cast<float>(context.ortho_h) : 720.0f;
    const float canvasScale = sceneHeight / 720.0f;
    constexpr float kWePointToPixelScale = 100.0f / 72.0f;
    return std::max(
        1,
        static_cast<int>(std::round(text_obj.pointSize * kWePointToPixelScale * canvasScale)));
}

Qt::Alignment GeneratedTextQtAlignment(const WPTextObject& text_obj)
{
    const std::string horizontalAlign = NormalizeGeneratedTextAlign(text_obj.horizontalAlign);
    const std::string verticalAlign = NormalizeGeneratedTextAlign(text_obj.verticalAlign);

    Qt::Alignment alignment = {};
    if (horizontalAlign == "right") {
        alignment |= Qt::AlignRight;
    } else if (horizontalAlign == "center" || horizontalAlign == "centre") {
        alignment |= Qt::AlignHCenter;
    } else {
        alignment |= Qt::AlignLeft;
    }

    // WE text cards are authored in scene-space coordinates while Qt
    // rasterizes into top-left image coordinates. The card mesh maps image top
    // back to scene top, so vertical text placement needs the opposite Qt edge.
    if (verticalAlign == "top") {
        alignment |= Qt::AlignBottom;
    } else if (verticalAlign == "bottom") {
        alignment |= Qt::AlignTop;
    } else {
        alignment |= Qt::AlignVCenter;
    }
    return alignment;
}

GeneratedTextFontLoadResult LoadGeneratedTextFontFamily(ParseContext& context,
                                                        const WPTextObject& text_obj)
{
    if (text_obj.font.empty()) {
        return {std::nullopt, "no-font-requested"};
    }
    if (!context.vfs) {
        return {std::nullopt, "no-vfs"};
    }

    const std::string fontPath = "/assets/" + text_obj.font;
    if (!context.vfs->Contains(fontPath)) {
        return {std::nullopt, "missing-font-file"};
    }

    const std::string fontBytes = fs::GetFileContent(*context.vfs, fontPath);
    if (fontBytes.empty()) {
        return {std::nullopt, "empty-font-file"};
    }

    const QByteArray fontData(fontBytes.data(), static_cast<int>(fontBytes.size()));
    const int fontId = QFontDatabase::addApplicationFontFromData(fontData);
    if (fontId < 0) {
        return {std::nullopt, "invalid-font-data"};
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    if (families.empty()) {
        return {std::nullopt, "no-font-family"};
    }
    std::vector<std::string> familyNames;
    familyNames.reserve(static_cast<std::size_t>(families.size()));
    for (const auto& family : families) {
        familyNames.push_back(family.toStdString());
    }
    const std::string chosenFamily =
        wallpaper::ChooseGeneratedTextFontFamily(text_obj.font, familyNames);
    if (chosenFamily.empty()) {
        return {std::nullopt, "no-font-family"};
    }
    return {QString::fromStdString(chosenFamily), "loaded"};
}

bool TryRenderGeneratedTextWithQt(ParseContext& context,
                                  const WPTextObject& text_obj,
                                  int width,
                                  int height,
                                  std::vector<uint8_t>& rgba,
                                  GeneratedTextRasterDiagnostic* diagnostic = nullptr,
                                  int padding = 0)
{
    if (QGuiApplication::instance() == nullptr) {
        if (diagnostic) {
            diagnostic->rasterizer = "none";
            diagnostic->fontLoadStatus = "qt-unavailable";
        }
        return false;
    }

    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);

    QFont font;
    const auto fontLoad = LoadGeneratedTextFontFamily(context, text_obj);
    if (fontLoad.family) {
        font.setFamily(*fontLoad.family);
    }
    if (diagnostic) {
        diagnostic->fontLoaded = fontLoad.family.has_value();
        diagnostic->fontFamily = fontLoad.family ? fontLoad.family->toStdString() : "";
        diagnostic->fontLoadStatus = fontLoad.status;
    }
    if (text_obj.pointSize > 0.0f) {
        font.setPixelSize(GeneratedTextEffectivePixelSize(context, text_obj));
    }

    QPainter painter(&image);
    if (!painter.isActive()) {
        if (diagnostic) {
            diagnostic->rasterizer = "none";
            if (diagnostic->fontLoadStatus.empty()) {
                diagnostic->fontLoadStatus = "painter-inactive";
            }
        }
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(QColor(ByteFromUnit(text_obj.color[0]),
                          ByteFromUnit(text_obj.color[1]),
                          ByteFromUnit(text_obj.color[2]),
                          ByteFromUnit(text_obj.alpha)));
    const int contentWidth = std::max(1, width - padding * 2);
    const int contentHeight = std::max(1, height - padding * 2);
    QRectF layoutRect = GeneratedTextLayoutRect(text_obj, contentWidth, contentHeight);
    if (padding > 0) {
        layoutRect.translate(static_cast<qreal>(padding), static_cast<qreal>(padding));
    }
    painter.setClipRect(layoutRect);
    painter.drawText(layoutRect,
                     static_cast<int>(GeneratedTextQtAlignment(text_obj)),
                     QString::fromStdString(
                         [&text_obj]() {
                             const auto lines = LimitedGeneratedTextLines(text_obj);
                             std::string out;
                             for (std::size_t i = 0; i < lines.size(); ++i) {
                                 if (i > 0) {
                                     out.push_back('\n');
                                 }
                                 out += lines[i];
                             }
                             return out;
                         }()));
    painter.end();

    rgba.assign(static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height) * 4u,
                0u);
    for (int y = 0; y < height; ++y) {
        const auto* src = image.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const std::size_t dstOffset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            const int srcOffset = x * 4;
            rgba[dstOffset + 0] = src[srcOffset + 0];
            rgba[dstOffset + 1] = src[srcOffset + 1];
            rgba[dstOffset + 2] = src[srcOffset + 2];
            rgba[dstOffset + 3] = src[srcOffset + 3];
        }
    }
    if (diagnostic) {
        diagnostic->rasterizer = "qt";
    }
    return true;
}

void RenderGeneratedTextWithBitmapFallback(const ParseContext& context,
                                           const WPTextObject& text_obj,
                                           int width,
                                           int height,
                                           std::vector<uint8_t>& rgba,
                                           int padding = 0)
{
    rgba.assign(static_cast<std::size_t>(width) *
                    static_cast<std::size_t>(height) * 4u,
                0u);

    const auto lines = LimitedGeneratedTextLines(text_obj);
    std::size_t maxLineLength = 1;
    for (const auto& line : lines) {
        maxLineLength = std::max(maxLineLength, line.size());
    }
    const int contentWidth = std::max(1, width - padding * 2);
    const int contentHeight = std::max(1, height - padding * 2);
    QRectF layoutRect = GeneratedTextLayoutRect(text_obj, contentWidth, contentHeight);
    if (padding > 0) {
        layoutRect.translate(static_cast<qreal>(padding), static_cast<qreal>(padding));
    }
    const int layoutX = std::clamp(static_cast<int>(std::round(layoutRect.x())), 0, width - 1);
    const int layoutWidth = std::clamp(static_cast<int>(std::round(layoutRect.width())),
                                       1,
                                       width - layoutX);
    const int scaleByWidth = layoutWidth / static_cast<int>(maxLineLength * 6);
    const int scaleByHeight = height / static_cast<int>(std::max<std::size_t>(1, lines.size()) * 8);
    const int fitScale = std::max(1, std::min(scaleByWidth <= 0 ? 1 : scaleByWidth,
                                             scaleByHeight <= 0 ? 1 : scaleByHeight));
    int scale = fitScale;
    if (text_obj.pointSize > 0.0f) {
        const int pointScale = std::max(
            1,
            static_cast<int>(
                std::round(static_cast<float>(
                               GeneratedTextEffectivePixelSize(context, text_obj)) /
                           7.0f)));
        scale = std::max(1, std::min(pointScale, fitScale));
    }
    const int lineHeight = 8 * scale;
    const int totalTextHeight = static_cast<int>(lines.size()) * lineHeight;
    const std::string verticalAlign = NormalizeGeneratedTextAlign(text_obj.verticalAlign);
    int startY = std::max(0, (height - totalTextHeight) / 2);
    if (verticalAlign == "top") {
        startY = std::max(0, height - totalTextHeight);
    } else if (verticalAlign == "bottom") {
        startY = 0;
    }

    const std::string horizontalAlign = NormalizeGeneratedTextAlign(text_obj.horizontalAlign);
    const auto textAdvanceWidth = [scale](std::size_t length) -> int {
        if (length == 0) {
            return 0;
        }
        return static_cast<int>((length * 6 - 1) * static_cast<std::size_t>(scale));
    };

    const uint8_t red = ByteFromUnit(text_obj.color[0]);
    const uint8_t green = ByteFromUnit(text_obj.color[1]);
    const uint8_t blue = ByteFromUnit(text_obj.color[2]);
    const uint8_t alpha = ByteFromUnit(text_obj.alpha);

    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const std::string& line = lines[lineIndex];
        const int lineWidth = textAdvanceWidth(line.size());
        int penX = layoutX;
        if (horizontalAlign == "right") {
            penX = std::max(layoutX, layoutX + layoutWidth - lineWidth);
        } else if (horizontalAlign == "center" || horizontalAlign == "centre") {
            penX = std::max(layoutX, layoutX + (layoutWidth - lineWidth) / 2);
        }
        const int penY = startY + static_cast<int>(lineIndex) * lineHeight;
        for (char ch : line) {
            const auto rows = GeneratedTextGlyphRows(ch);
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if ((rows[row] & (1u << (4 - col))) == 0) {
                        continue;
                    }
                    for (int sy = 0; sy < scale; ++sy) {
                        const int y = penY + row * scale + sy;
                        if (y < 0 || y >= height) {
                            continue;
                        }
                        for (int sx = 0; sx < scale; ++sx) {
                            const int x = penX + col * scale + sx;
                            if (x < layoutX || x >= layoutX + layoutWidth || x >= width) {
                                continue;
                            }
                            const std::size_t offset =
                                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)) * 4u;
                            rgba[offset + 0] = red;
                            rgba[offset + 1] = green;
                            rgba[offset + 2] = blue;
                            rgba[offset + 3] = alpha;
                        }
                    }
                }
            }
            penX += 6 * scale;
            if (penX >= layoutX + layoutWidth) {
                break;
            }
        }
    }
}

float GeneratedTextMaterialScalar(const wpscene::WPMaterial& material,
                                  std::string_view name,
                                  float fallback)
{
    auto it = material.constantshadervalues.find(std::string(name));
    if (it == material.constantshadervalues.end() || it->second.empty()) {
        return fallback;
    }
    return it->second.front();
}

std::array<float, 3> GeneratedTextMaterialColor(const wpscene::WPMaterial& material,
                                                std::string_view name,
                                                std::array<float, 3> fallback)
{
    auto it = material.constantshadervalues.find(std::string(name));
    if (it == material.constantshadervalues.end() || it->second.size() < 3) {
        return fallback;
    }
    return {it->second[0], it->second[1], it->second[2]};
}

wpscene::WPMaterial GeneratedTextMergedEffectMaterial(const wpscene::WPImageEffect& effect,
                                                      std::size_t materialIndex)
{
    wpscene::WPMaterial material = effect.materials.at(materialIndex);
    if (effect.passes.size() > materialIndex) {
        material.MergePass(effect.passes.at(materialIndex));
    }
    return material;
}

void ApplyGeneratedTextTint(const wpscene::WPMaterial& material,
                            std::vector<uint8_t>& rgba)
{
    const auto tintColor = GeneratedTextMaterialColor(material, "color", {0.0f, 0.0f, 0.0f});
    const float tintAmount = std::clamp(GeneratedTextMaterialScalar(material, "alpha", 1.0f),
                                        0.0f,
                                        1.0f);
    const uint8_t tintRed = ByteFromUnit(tintColor[0]);
    const uint8_t tintGreen = ByteFromUnit(tintColor[1]);
    const uint8_t tintBlue = ByteFromUnit(tintColor[2]);
    for (std::size_t offset = 0; offset + 3 < rgba.size(); offset += 4) {
        if (rgba[offset + 3] == 0) {
            continue;
        }
        rgba[offset + 0] = static_cast<uint8_t>(
            std::lround(static_cast<float>(rgba[offset + 0]) * (1.0f - tintAmount) +
                        static_cast<float>(tintRed) * tintAmount));
        rgba[offset + 1] = static_cast<uint8_t>(
            std::lround(static_cast<float>(rgba[offset + 1]) * (1.0f - tintAmount) +
                        static_cast<float>(tintGreen) * tintAmount));
        rgba[offset + 2] = static_cast<uint8_t>(
            std::lround(static_cast<float>(rgba[offset + 2]) * (1.0f - tintAmount) +
                        static_cast<float>(tintBlue) * tintAmount));
    }
}

std::array<uint8_t, 3> AverageGeneratedTextVisibleColor(const std::vector<uint8_t>& rgba)
{
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double weight = 0.0;
    for (std::size_t offset = 0; offset + 3 < rgba.size(); offset += 4) {
        const double alpha = static_cast<double>(rgba[offset + 3]);
        if (alpha <= 0.0) {
            continue;
        }
        red += static_cast<double>(rgba[offset + 0]) * alpha;
        green += static_cast<double>(rgba[offset + 1]) * alpha;
        blue += static_cast<double>(rgba[offset + 2]) * alpha;
        weight += alpha;
    }
    if (weight <= 0.0) {
        return {0, 0, 0};
    }
    return {
        static_cast<uint8_t>(std::clamp(std::lround(red / weight), 0l, 255l)),
        static_cast<uint8_t>(std::clamp(std::lround(green / weight), 0l, 255l)),
        static_cast<uint8_t>(std::clamp(std::lround(blue / weight), 0l, 255l)),
    };
}

void ApplyGeneratedTextAlphaBlur(int width,
                                 int height,
                                 int radius,
                                 std::vector<uint8_t>& rgba)
{
    if (width <= 0 || height <= 0 || radius <= 0 || rgba.empty()) {
        return;
    }
    radius = std::clamp(radius, 1, 24);
    std::vector<float> alpha(static_cast<std::size_t>(width) *
                             static_cast<std::size_t>(height));
    std::vector<float> temp(alpha.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            alpha[index] = static_cast<float>(rgba[index * 4u + 3u]);
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            int count = 0;
            for (int dx = -radius; dx <= radius; ++dx) {
                const int sampleX = std::clamp(x + dx, 0, width - 1);
                sum += alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(sampleX)];
                count++;
            }
            temp[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)] = sum / static_cast<float>(count);
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int sampleY = std::clamp(y + dy, 0, height - 1);
                sum += temp[static_cast<std::size_t>(sampleY) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(x)];
                count++;
            }
            alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)] = sum / static_cast<float>(count);
        }
    }

    const auto color = AverageGeneratedTextVisibleColor(rgba);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            const uint8_t blurredAlpha = static_cast<uint8_t>(
                std::clamp(std::lround(alpha[index]), 0l, 255l));
            const std::size_t offset = index * 4u;
            if (blurredAlpha > 0) {
                rgba[offset + 0] = color[0];
                rgba[offset + 1] = color[1];
                rgba[offset + 2] = color[2];
            }
            rgba[offset + 3] = blurredAlpha;
        }
    }
}

void ApplyGeneratedTextCpuEffects(const WPTextObject& text_obj,
                                  int width,
                                  int height,
                                  std::vector<uint8_t>& rgba)
{
    for (const auto& effect : text_obj.effects) {
        if (!effect.visible) {
            continue;
        }
        for (std::size_t i = 0; i < effect.materials.size(); ++i) {
            const auto material = GeneratedTextMergedEffectMaterial(effect, i);
            if (material.shader.find("effects/tint") != std::string::npos) {
                ApplyGeneratedTextTint(material, rgba);
            } else if (material.shader.find("effects/blur_precise_gaussian") != std::string::npos ||
                       material.shader.find("effects/blur") != std::string::npos) {
                const float scale = std::max(GeneratedTextMaterialScalar(material, "scale", 2.0f),
                                             1.0f);
                const int radius = std::clamp(static_cast<int>(std::ceil(scale * 1.5f)), 1, 24);
                ApplyGeneratedTextAlphaBlur(width, height, radius, rgba);
            }
        }
    }
}

std::optional<GeneratedTextTextureRegistration>
RegisterGeneratedTextTexture(ParseContext& context,
                             const WPTextObject& text_obj)
{
    if (! context.scene || ! context.scene->imageParser || text_obj.text.empty()) {
        return std::nullopt;
    }
    auto* texParser = dynamic_cast<WPTexImageParser*>(context.scene->imageParser.get());
    if (texParser == nullptr) {
        return std::nullopt;
    }

    const int layoutWidth = GeneratedTextRasterWidth(text_obj);
    const int layoutHeight = GeneratedTextRasterHeight(text_obj);
    const int padding = GeneratedTextRasterPadding(context, text_obj);
    int width = std::clamp(layoutWidth + padding * 2, 1, 8192);
    int height = std::clamp(layoutHeight + padding * 2, 1, 2048);
    std::vector<uint8_t> rgba;
    GeneratedTextRasterDiagnostic rasterDiagnostic;
    if (!TryRenderGeneratedTextWithQt(context,
                                      text_obj,
                                      width,
                                      height,
                                      rgba,
                                      &rasterDiagnostic,
                                      padding)) {
        RenderGeneratedTextWithBitmapFallback(context, text_obj, width, height, rgba, padding);
        rasterDiagnostic.rasterizer = "bitmap-fallback";
        if (rasterDiagnostic.fontLoadStatus.empty()) {
            rasterDiagnostic.fontLoadStatus = "qt-render-failed";
        }
    }
    ApplyGeneratedTextCpuEffects(text_obj, width, height, rgba);

    int alphaMinX = width;
    int alphaMinY = height;
    int alphaMaxX = -1;
    int alphaMaxY = -1;
    const auto scanAlphaBounds = [&]() {
        alphaMinX = width;
        alphaMinY = height;
        alphaMaxX = -1;
        alphaMaxY = -1;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)) * 4u;
                if (rgba[offset + 3] == 0) {
                    continue;
                }
                alphaMinX = std::min(alphaMinX, x);
                alphaMinY = std::min(alphaMinY, y);
                alphaMaxX = std::max(alphaMaxX, x);
                alphaMaxY = std::max(alphaMaxY, y);
            }
        }
    };
    scanAlphaBounds();

    char nameBuffer[96] {};
    std::snprintf(nameBuffer,
                  sizeof(nameBuffer),
                  "__yakkai_generated_text/%d_%zx",
                  text_obj.id,
                  std::hash<std::string>{}(text_obj.text));
    const std::string textureName = nameBuffer;
    texParser->RegisterGeneratedRgbaImage(textureName, width, height, rgba);
    GeneratedTextTextureRegistration registration;
    registration.textureName = textureName;
    registration.textureSize = {
        static_cast<float>(width),
        static_cast<float>(height),
    };
    registration.layoutSize = {
        static_cast<float>(layoutWidth),
        static_cast<float>(layoutHeight),
    };
    registration.padding = padding;
    registration.rasterizer = rasterDiagnostic.rasterizer;
    registration.fontLoaded = rasterDiagnostic.fontLoaded;
    registration.fontFamily = rasterDiagnostic.fontFamily;
    registration.fontLoadStatus = rasterDiagnostic.fontLoadStatus;
    if (alphaMaxX >= alphaMinX && alphaMaxY >= alphaMinY) {
        registration.hasVisibleAlpha = true;
        registration.alphaBounds = {
            static_cast<float>(alphaMinX),
            static_cast<float>(alphaMinY),
            static_cast<float>(alphaMaxX),
            static_cast<float>(alphaMaxY),
        };
    }
    return registration;
}

std::array<float, 4>
GeneratedTextLocalBoundsWithPadding(const WPTextObject& text_obj,
                                    const GeneratedTextTextureRegistration& registration)
{
    WPTextObject boundsTextObject = text_obj;
    boundsTextObject.size[0] = std::max(boundsTextObject.size[0], registration.layoutSize[0]);
    boundsTextObject.size[1] = std::max(boundsTextObject.size[1], registration.layoutSize[1]);
    auto bounds = LocalCardBounds(boundsTextObject);
    const float padding = static_cast<float>(registration.padding);
    bounds[0] -= padding;
    bounds[1] -= padding;
    bounds[2] += padding;
    bounds[3] += padding;
    return bounds;
}

void ParseTextObj(ParseContext& context, WPTextObject& text_obj) {
    if (! text_obj.visible) return;
    if (HasHiddenParent(context, text_obj.parent)) return;

    auto it = context.script_text_bindings.find(text_obj.id);
    const bool hasScriptResolvedText = it != context.script_text_bindings.end() &&
        it->second.has_text;
    if (hasScriptResolvedText) {
        text_obj.text = it->second.text;
    }
    auto originIt = context.script_color_bindings.find(text_obj.id);
    if (originIt != context.script_color_bindings.end() && originIt->second.has_origin) {
        text_obj.origin = originIt->second.origin;
    }
    if (originIt != context.script_color_bindings.end() && originIt->second.has_scale) {
        text_obj.scale = originIt->second.scale;
    }
    if (originIt != context.script_color_bindings.end() && originIt->second.has_color) {
        text_obj.color = originIt->second.color;
    }
    if (originIt != context.script_color_bindings.end() && originIt->second.has_alpha) {
        text_obj.alpha = originIt->second.alpha;
    }
    if (originIt != context.script_color_bindings.end() &&
        originIt->second.has_horizontal_align) {
        text_obj.horizontalAlign = originIt->second.horizontalAlign;
    }
    if (originIt != context.script_color_bindings.end() &&
        originIt->second.has_vertical_align) {
        text_obj.verticalAlign = originIt->second.verticalAlign;
    }
    if (originIt != context.script_color_bindings.end() &&
        originIt->second.has_max_width) {
        text_obj.maxWidth = originIt->second.maxWidth;
    }
    auto node = std::make_shared<SceneNode>(Vector3f(text_obj.origin.data()),
                                            Vector3f(text_obj.scale.data()),
                                            Vector3f(text_obj.angles.data()));
    node->ID() = text_obj.id;
    const WPTextObject rasterTextObject = TextObjectForRasterization(context, text_obj);
    const auto textureRegistration =
        RegisterGeneratedTextTexture(context, rasterTextObject);
    if (textureRegistration) {
        wpscene::WPMaterial wpmat;
        wpmat.shader = "genericimage4";
        wpmat.blending = "translucent";
        wpmat.cullmode = "nocull";
        wpmat.depthtest = "disabled";
        wpmat.depthwrite = "disabled";
        wpmat.textures = {textureRegistration->textureName};

        SceneMaterial material;
        WPShaderValueData svData;
        WPShaderInfo shaderInfo;
        shaderInfo.baseConstSvs = context.global_base_uniforms;
        shaderInfo.baseConstSvs["g_Color4"] =
            std::array<float, 4> {1.0f, 1.0f, 1.0f, 1.0f};
        shaderInfo.baseConstSvs["g_UserAlpha"] = 1.0f;

        const bool canLoadGenericImageMaterial = context.vfs &&
            context.vfs->Contains("/assets/shaders/genericimage4.vert") &&
            context.vfs->Contains("/assets/shaders/genericimage4.frag");
        const bool materialLoaded = context.scene && context.vfs && canLoadGenericImageMaterial &&
            LoadMaterial(*context.vfs,
                         wpmat,
                         context.scene.get(),
                         node.get(),
                         &material,
                         &svData,
                         &shaderInfo);
        if (materialLoaded) {
            LoadConstvalue(material, wpmat, shaderInfo);
        } else {
            material.textures = {textureRegistration->textureName};
            material.defines = {"g_Texture0"};
            material.blenmode = BlendMode::Translucent;
        }

        auto mesh = std::make_shared<SceneMesh>();
        GenCardMeshFromLocalBounds(*mesh,
                                   GeneratedTextLocalBoundsWithPadding(
                                       text_obj,
                                       *textureRegistration));
        mesh->AddMaterial(std::move(material));
        node->AddMesh(mesh);
        context.shader_updater->SetNodeData(node.get(), svData);
        LOG_INFO("generated text texture layer: id=%d name=%s texture=%s textureSize=%dx%d authoredSize=%dx%d",
                 text_obj.id,
                 text_obj.name.c_str(),
                 textureRegistration->textureName.c_str(),
                 static_cast<int>(textureRegistration->textureSize[0]),
                 static_cast<int>(textureRegistration->textureSize[1]),
                 static_cast<int>(text_obj.size[0]),
                 static_cast<int>(text_obj.size[1]));
    }
    AttachObjectNode(context, node, text_obj.id, text_obj.parent);

    if (context.scene && context.scene->debugEffectCaptures.enabled() && textureRegistration) {
        const auto localBounds =
            GeneratedTextLocalBoundsWithPadding(text_obj, *textureRegistration);
        const auto worldBounds = WorldBoundsForLocalCard(*node, localBounds);
        const auto [visibility, reason] =
            ClassifyGeneratedTextVisibility(context,
                                            text_obj,
                                            worldBounds,
                                            textureRegistration->hasVisibleAlpha);
        wallpaper::debug::GeneratedTextDiagnostic info;
        info.layerId = text_obj.id;
        info.layerName = text_obj.name;
        info.text = text_obj.text;
        info.textureName = textureRegistration->textureName;
        info.font = text_obj.font;
        info.rasterizer = textureRegistration->rasterizer;
        info.fontLoaded = textureRegistration->fontLoaded;
        info.fontFamily = textureRegistration->fontFamily;
        info.fontLoadStatus = textureRegistration->fontLoadStatus;
        info.horizontalAlign = text_obj.horizontalAlign;
        info.verticalAlign = text_obj.verticalAlign;
        info.pointSize = text_obj.pointSize;
        info.effectivePixelSize = GeneratedTextEffectivePixelSize(context, rasterTextObject);
        info.parentId = text_obj.parent;
        info.parentChain = DebugSceneNodeParentChain(context, node->Parent());
        info.cardSize = text_obj.size;
        info.textureSize = textureRegistration->textureSize;
        info.color = text_obj.color;
        info.alpha = text_obj.alpha;
        info.nodeTranslate = DebugVec3Array(node->Translate());
        info.nodeScale = DebugVec3Array(node->Scale());
        info.localBounds = localBounds;
        info.worldBounds = worldBounds;
        info.alphaBounds = textureRegistration->alphaBounds;
        info.visibility = visibility;
        info.classificationReason = reason;
        wallpaper::debug::recordGeneratedTextDiagnostic(*context.scene, info);
    }

    const std::string logText = EscapeSceneScriptLogText(text_obj.text);
    LOG_INFO("generated text layer: id=%d name=%s text=%s",
             text_obj.id,
             text_obj.name.c_str(),
             logText.c_str());
}

void ParseLightObj(ParseContext& context, wpscene::WPLightObject& light_obj) {
    if (HasHiddenParent(context, light_obj.parent)) return;
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

    const bool wantsLightmap = MaterialComboEnabled(sourceMaterial, "lightmap");
    const bool wantsNormalmap = MaterialComboEnabled(sourceMaterial, "normalmap");
    const bool wantsReflection = MaterialComboEnabled(sourceMaterial, "reflection");
    const auto fallbackDecision = wallpaper::policy::decideModelMaterialFallback({
        .sourceShader = sourceMaterial.shader,
        .sourceBlending = sourceMaterial.blending,
        .hasDiffuseTexture = !sourceMaterial.textures.empty() && !sourceMaterial.textures[0].empty(),
        .wantsLightmap = wantsLightmap,
        .wantsNormalmap = wantsNormalmap,
        .wantsReflection = wantsReflection,
    });

    useStaticGenericMaterial = false;
    if (fallbackDecision.useAuthoredGenericMaterial) {
        material = sourceMaterial;
        if (material.blending.empty()) material.blending = fallbackDecision.outputBlending;
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
    material.blending   = fallbackDecision.outputBlending;
    material.cullmode   = sourceMaterial.cullmode;
    material.depthtest  = sourceMaterial.depthtest;
    material.depthwrite = sourceMaterial.depthwrite;
    material.shader     = fallbackDecision.outputShader;
    material.textures   = { ResolveStaticFallbackDiffuseTexture(vfs, sourceMaterial.textures[0]) };
    LOG_INFO("model fallback forcing opaque blend for diffuse-only path: %s shader=%s sourceBlend=%s",
             matJsonFile.c_str(),
             material.shader.c_str(),
             sourceMaterial.blending.c_str());
    return true;
}

void ParseModelObj(ParseContext& context, wpscene::WPModelObject& model_obj) {
    if (! model_obj.visible) return;
    if (HasHiddenParent(context, model_obj.parent)) return;

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

std::vector<std::string> CollectJsonStrings(const nlohmann::json& value)
{
    std::vector<std::string> strings;
    std::function<void(const nlohmann::json&)> visit = [&](const nlohmann::json& item) {
        if (item.is_string()) {
            strings.push_back(item.get<std::string>());
            return;
        }
        if (item.is_object()) {
            for (const auto& child : item.items()) {
                strings.push_back(child.key());
                visit(child.value());
            }
            return;
        }
        if (item.is_array()) {
            for (const auto& child : item) {
                visit(child);
            }
        }
    };
    visit(value);
    return strings;
}

std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool JsonContainsMediaWidgetRuntimeToken(const nlohmann::json& objectJson)
{
    static const std::array<std::string_view, 7> tokens {
        "shared.mi",
        "engine.media",
        "mediaintegration",
        "MediaPlaybackEvent",
        "mediaTimelineChanged",
        "mediaPlaybackChanged",
        "mediaPropertiesChanged",
    };

    const auto strings = CollectJsonStrings(objectJson);
    for (const std::string& value : strings) {
        const std::string loweredValue = LowerAscii(value);
        for (const std::string_view token : tokens) {
            const std::string loweredToken = LowerAscii(std::string(token));
            if (loweredValue.find(loweredToken) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

std::optional<int32_t> JsonObjectId(const nlohmann::json& objectJson)
{
    if (!objectJson.contains("id") || !objectJson.at("id").is_number_integer()) {
        return std::nullopt;
    }
    return objectJson.at("id").get<int32_t>();
}

int32_t JsonObjectParentId(const nlohmann::json& objectJson)
{
    if (!objectJson.contains("parent") || !objectJson.at("parent").is_number_integer()) {
        return 0;
    }
    return objectJson.at("parent").get<int32_t>();
}

bool JsonObjectVisibleDefaultTrue(const nlohmann::json& objectJson);

std::optional<SceneScriptLayerSnapshot>
SceneScriptLayerSnapshotFromJsonObject(const nlohmann::json& objectJson)
{
    const auto id = JsonObjectId(objectJson);
    if (!id) {
        return std::nullopt;
    }

    SceneScriptLayerSnapshot snapshot;
    snapshot.id = *id;
    snapshot.parentId = JsonObjectParentId(objectJson);
    if (objectJson.contains("name") && objectJson.at("name").is_string()) {
        snapshot.name = objectJson.at("name").get<std::string>();
    }
    if (objectJson.contains("origin")) {
        ParseSceneVec3Value(objectJson.at("origin"), snapshot.origin);
    }
    if (objectJson.contains("scale")) {
        ParseSceneVec3Value(objectJson.at("scale"), snapshot.scale);
    }
    if (objectJson.contains("size")) {
        ParseSceneVec2Value(objectJson.at("size"), snapshot.size);
    }
    snapshot.visible = JsonObjectVisibleDefaultTrue(objectJson);
    return snapshot;
}

bool JsonObjectVisibleDefaultTrue(const nlohmann::json& objectJson)
{
    if (!objectJson.contains("visible")) {
        return true;
    }
    const auto& visible = objectJson.at("visible");
    if (visible.is_boolean()) {
        return visible.get<bool>();
    }
    if (visible.is_object()) {
        if (auto resolved = ResolveConditionalProperty(visible)) {
            if (resolved->is_boolean()) {
                return resolved->get<bool>();
            }
            if (resolved->is_number()) {
                return resolved->get<double>() > 0.5;
            }
        }
        if (visible.contains("value")) {
            const auto& value = visible.at("value");
            if (value.is_boolean()) {
                return value.get<bool>();
            }
            if (value.is_number()) {
                return value.get<double>() > 0.5;
            }
        }
    }
    return true;
}

void AddWPImageObject(std::vector<WPObjectVar>& objs,
                      const nlohmann::json& json_obj,
                      fs::VFS& vfs)
{
    wpscene::WPImageObject wpobj;
    if (!wpobj.FromJson(json_obj, vfs)) {
        LOG_ERROR("parse scene object failed, name: %s", wpobj.name.c_str());
        return;
    }
    if (!wpobj.visible) {
        return;
    }
    objs.push_back(wpobj);
}

void RegisterDebugObjectGraphEntry(ParseContext& context,
                                   int32_t id,
                                   int32_t parent,
                                   std::string_view name)
{
    if (id <= 0) {
        return;
    }
    context.object_names[id] = std::string(name);
    context.object_parent_ids[id] = parent;
    if (parent > 0 && parent != id) {
        context.object_child_ids[parent].push_back(id);
    }
}

void RegisterDebugObjectParallaxDepth(ParseContext& context,
                                      int32_t id,
                                      const std::array<float, 2>& parallaxDepth)
{
    if (id <= 0 || ! HasDebugNonzeroParallaxDepth(parallaxDepth)) {
        return;
    }
    context.object_parallax_depths[id] = parallaxDepth;
}

void RegisterDebugObjectGraph(ParseContext& context, const wpscene::WPImageObject& obj)
{
    RegisterDebugObjectGraphEntry(context, obj.id, obj.parent, obj.name);
    RegisterDebugObjectParallaxDepth(context, obj.id, obj.parallaxDepth);
}

void RegisterDebugObjectGraph(ParseContext& context, const wpscene::WPParticleObject& obj)
{
    RegisterDebugObjectGraphEntry(context, obj.id, obj.parent, obj.name);
    RegisterDebugObjectParallaxDepth(context, obj.id, obj.parallaxDepth);
}

void RegisterDebugObjectGraph(ParseContext& context, const wpscene::WPLightObject& obj)
{
    RegisterDebugObjectGraphEntry(context, obj.id, obj.parent, obj.name);
    RegisterDebugObjectParallaxDepth(context, obj.id, obj.parallaxDepth);
}

void RegisterDebugObjectGraph(ParseContext& context, const wpscene::WPModelObject& obj)
{
    RegisterDebugObjectGraphEntry(context, obj.id, obj.parent, obj.name);
}

void RegisterDebugObjectGraph(ParseContext& context, const WPSolidAnchorObject& obj)
{
    RegisterDebugObjectGraphEntry(context, obj.id, obj.parent, obj.name);
    RegisterDebugObjectParallaxDepth(context, obj.id, obj.parallaxDepth);
}

void RegisterDebugObjectGraph(ParseContext& context, const WPTextObject& obj)
{
    RegisterDebugObjectGraphEntry(context, obj.id, obj.parent, obj.name);
}

void RegisterDebugObjectGraph(ParseContext&, const wpscene::WPSoundObject&)
{
}
} // namespace

std::shared_ptr<Scene> WPSceneParser::Parse(std::string_view scene_id, const std::string& buf,
                                            fs::VFS& vfs, audio::SoundManager& sm) {
    nlohmann::json json;
    if (! PARSE_JSON(buf, json)) return nullptr;


    nlohmann::json sceneProperties = ResolveSceneProperties(vfs, m_scene_properties_json);
    SceneScriptMediaState mediaState = SceneScriptMediaStateFromSceneProperties(sceneProperties);
    DeriveSyntheticMediaThumbnailColors(mediaState);
    SetActiveScenePropertyState(sceneProperties);
    struct ClearScenePropertyStateGuard {
        ~ClearScenePropertyStateGuard() { ClearActiveScenePropertyState(); }
    } clearScenePropertyStateGuard;

    if (! sceneProperties.empty()) {
        LOG_INFO("scene property defaults active: count=%zu", sceneProperties.size());
    }
    if (mediaState.available) {
        LOG_INFO("SceneScript synthetic media state active: title='%s' artist='%s' playing=%d",
                 mediaState.title.c_str(),
                 mediaState.artist.c_str(),
                 mediaState.playing ? 1 : 0);
    }

    wpscene::WPScene sc;
    sc.FromJson(json);
    //	LOG_INFO(nlohmann::json(sc).dump(4));

    ParseContext context;
    context.scene_properties = sceneProperties;
    context.canvas_width = sc.general.orthogonalprojection.width;
    context.canvas_height = sc.general.orthogonalprojection.height;
    // The script pre-scan runs before InitContext creates the Scene, but it
    // still needs the VFS for generated text diagnostics and font lookup.
    // SceneScript layer lookups keep authored layer sizes.
    context.vfs = &vfs;

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
        scriptCtx.setMediaState(mediaState);
        scriptCtx.setCanvasSize(sc.general.orthogonalprojection.width,
                                sc.general.orthogonalprojection.height);
        std::unordered_map<int32_t, SceneScriptLayerSnapshot> runtimeLayerSnapshots;
        std::unordered_map<int32_t, WPTextObject> runtimeTextObjects;
        for (const auto& obj : json.at("objects")) {
            if (const auto snapshot = SceneScriptLayerSnapshotFromJsonObject(obj)) {
                runtimeLayerSnapshots[snapshot->id] = *snapshot;
                scriptCtx.registerLayerSnapshot(*snapshot);
            }
            if ((obj.contains("text") || obj.contains("font")) &&
                obj.contains("id") && obj.at("id").is_number_integer()) {
                WPTextObject textObject;
                if (textObject.FromJson(obj, vfs)) {
                    runtimeTextObjects[textObject.id] = std::move(textObject);
                }
            }
        }
        const auto refreshRuntimeLayerSnapshot = [&](int32_t layerId,
                                                     const ParseContext::ScriptColorBinding& binding) {
            auto snapshotIt = runtimeLayerSnapshots.find(layerId);
            if (snapshotIt == runtimeLayerSnapshots.end()) {
                return;
            }
            auto& snapshot = snapshotIt->second;
            if (binding.has_origin) {
                snapshot.origin = binding.origin;
            }
            if (binding.has_scale) {
                snapshot.scale = binding.scale;
            }
            if (binding.has_visible) {
                snapshot.visible = binding.visible;
            }
            if (binding.has_max_width && binding.maxWidth > 0.0f) {
                snapshot.size[0] = std::min(snapshot.size[0], binding.maxWidth);
            }
            scriptCtx.registerLayerSnapshot(snapshot);
        };
        {
            // Compute current time-of-day fraction (0.0 = midnight, 0.5 = noon)
            using clock = std::chrono::system_clock;
            auto now = clock::to_time_t(clock::now());
            std::tm lt {};
            localtime_r(&now, &lt);
            double frac = (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) / 86400.0;
            scriptCtx.setTimeOfDay(frac);
        }

        constexpr int kSceneScriptBindingPasses = 2;
        for (int scriptPass = 0; scriptPass < kSceneScriptBindingPasses; ++scriptPass) {
            for (const auto& obj : json.at("objects")) {
                if (! obj.contains("id")) continue;
                int32_t objId = obj.at("id").get<int32_t>();

                // Find script strings anywhere in the object's JSON tree
                std::function<void(const nlohmann::json&,
                                   const std::string&,
                                   bool,
                                   int32_t,
                                   int32_t,
                                   bool)> scanForScripts =
                    [&](const nlohmann::json& node,
                        const std::string& scriptField,
                        bool maxWidthOnly,
                        int32_t effectId,
                        int32_t passId,
                        bool inConstantShaderValues) {
                if (node.is_object() && node.contains("script") &&
                    node.at("script").is_string()) {
                    const bool isMaxWidthScript = scriptField == "maxwidth";
                    if (maxWidthOnly != isMaxWidthScript) {
                        return;
                    }
                    const auto& script = node.at("script").get_ref<const std::string&>();
                    if (script.size() < 50) return; // skip trivial scripts

                    // Get current layer defaults
                    std::array<float, 3> vectorValue { 0, 0, 0 };
                    std::array<float, 3> color { 1, 1, 1 };
                    float alpha = 1.0f;
                    if (scriptField == "scale") {
                        vectorValue = {1.0f, 1.0f, 1.0f};
                        if (obj.contains("scale")) {
                            ParseSceneVec3Value(obj.at("scale"), vectorValue);
                        }
                    } else if (scriptField == "color") {
                        vectorValue = color;
                        ParseSceneVec3Value(node, vectorValue);
                    } else if (obj.contains("origin")) {
                        ParseSceneVec3Value(obj.at("origin"), vectorValue);
                    }
                    if (obj.contains("color")) {
                        ParseSceneVec3Value(obj.at("color"), color);
                    }

                    // Resolve scriptproperties user bindings before evaluating.
                    // Scene JSON scriptproperties can have {"user": "propName", "value": default}
                    // entries that need to be resolved to the actual user property value.
                    std::unordered_map<std::string, double> resolvedScriptProperties;
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
                            if (resolved) {
                                scriptCtx.setScriptProperty(spKey, dval);
                                resolvedScriptProperties[spKey] = dval;
                            }
                        }
                    }
                    if (scriptField == "scale" &&
                        script.find("mediaTimelineChanged") != std::string::npos) {
                        context.media_timeline_scale_scripts[objId] = {
                            script,
                            resolvedScriptProperties
                        };
                    }
                    const bool currentVisible = ResolveObjectVisibleForDebug(obj);
                    auto result = scriptCtx.evaluateLayerScript(
                        script, vectorValue, color, alpha, objId, currentVisible);
                    const bool isMaterialConstantScript =
                        inConstantShaderValues && effectId != 0 && passId != 0;
                    if (scriptPass == 0 && !isMaterialConstantScript &&
                        SceneScriptContainsMediaRuntimeCallback(script)) {
                        const bool handledByTimelineScaleBinding =
                            scriptField == "scale" &&
                            script.find("mediaTimelineChanged") != std::string::npos;
                        if (!handledByTimelineScaleBinding) {
                            if (auto runtimeField =
                                    MediaRuntimeBindingFieldForScriptField(scriptField)) {
                                context.media_runtime_scripts[objId].push_back({
                                    .field = *runtimeField,
                                    .script = script,
                                    .scriptProperties = resolvedScriptProperties
                                });
                            }
                        }
                    }
                    if (isMaterialConstantScript) {
                        if (auto value = ScriptMaterialConstantValue(result, scriptField)) {
                            context.script_material_constant_bindings[
                                ScriptMaterialConstantKey(objId, effectId, passId, scriptField)] = *value;
                            LOG_INFO("QuickJS material binding: id=%d effect=%d pass=%d %s values=%zu",
                                     objId,
                                     effectId,
                                     passId,
                                     scriptField.c_str(),
                                     value->size());
                        }
                        return;
                    }
                    const bool isLayerPropertyScript = scriptField.empty();
                    const bool isColorFieldScript = scriptField == "color";
                    const bool isAlphaFieldScript = scriptField == "alpha";
                    const bool isVisibleFieldScript = scriptField == "visible";
                    bool shouldRefreshRuntimeLayerSnapshot = false;

                    if (result.color && (isColorFieldScript || isLayerPropertyScript)) {
                        context.script_color_bindings[objId].color = *result.color;
                        context.script_color_bindings[objId].has_color = true;
                        shouldRefreshRuntimeLayerSnapshot = true;
                        LOG_INFO("QuickJS binding: id=%d color=(%.3f,%.3f,%.3f)",
                                 objId, (*result.color)[0], (*result.color)[1], (*result.color)[2]);
                    }
                    if (result.alpha && (isAlphaFieldScript || isLayerPropertyScript)) {
                        context.script_color_bindings[objId].alpha = *result.alpha;
                        context.script_color_bindings[objId].has_alpha = true;
                        shouldRefreshRuntimeLayerSnapshot = true;
                        LOG_INFO("QuickJS binding: id=%d alpha=%.3f", objId, *result.alpha);
                    }
                    const auto& vectorResult = result.returnVector ? result.returnVector : result.origin;
                    if (vectorResult) {
                        if (scriptField == "scale") {
                            context.script_color_bindings[objId].scale = *vectorResult;
                            context.script_color_bindings[objId].has_scale = true;
                            shouldRefreshRuntimeLayerSnapshot = true;
                            LOG_INFO("QuickJS binding: id=%d scale=(%.3f,%.3f,%.3f)",
                                     objId, (*vectorResult)[0], (*vectorResult)[1], (*vectorResult)[2]);
                        } else if (scriptField == "color") {
                            context.script_color_bindings[objId].color = *vectorResult;
                            context.script_color_bindings[objId].has_color = true;
                            shouldRefreshRuntimeLayerSnapshot = true;
                            LOG_INFO("QuickJS binding: id=%d color=(%.3f,%.3f,%.3f)",
                                     objId, (*vectorResult)[0], (*vectorResult)[1], (*vectorResult)[2]);
                        } else {
                            context.script_color_bindings[objId].origin = *vectorResult;
                            context.script_color_bindings[objId].has_origin = true;
                            shouldRefreshRuntimeLayerSnapshot = true;
                            LOG_INFO("QuickJS binding: id=%d origin=(%.0f,%.0f,%.0f)",
                                     objId, (*vectorResult)[0], (*vectorResult)[1], (*vectorResult)[2]);
                        }
                    }
                    if (result.visible && (isVisibleFieldScript || isLayerPropertyScript)) {
                        context.script_color_bindings[objId].visible = *result.visible;
                        context.script_color_bindings[objId].has_visible = true;
                        shouldRefreshRuntimeLayerSnapshot = true;
                        LOG_INFO("QuickJS binding: id=%d visible=%d",
                                 objId, *result.visible ? 1 : 0);
                    }
                    if (result.horizontalAlign && !result.horizontalAlign->empty()) {
                        context.script_color_bindings[objId].horizontalAlign = *result.horizontalAlign;
                        context.script_color_bindings[objId].has_horizontal_align = true;
                        shouldRefreshRuntimeLayerSnapshot = true;
                        LOG_INFO("QuickJS binding: id=%d horizontalalign=%s",
                                 objId, result.horizontalAlign->c_str());
                    }
                    if (result.verticalAlign && !result.verticalAlign->empty()) {
                        context.script_color_bindings[objId].verticalAlign = *result.verticalAlign;
                        context.script_color_bindings[objId].has_vertical_align = true;
                        shouldRefreshRuntimeLayerSnapshot = true;
                        LOG_INFO("QuickJS binding: id=%d verticalalign=%s",
                                 objId, result.verticalAlign->c_str());
                    }
                    if (result.scalar && scriptField == "maxwidth") {
                        context.script_color_bindings[objId].maxWidth = *result.scalar;
                        context.script_color_bindings[objId].has_max_width = true;
                        shouldRefreshRuntimeLayerSnapshot = true;
                        LOG_INFO("QuickJS binding: id=%d maxwidth=%.3f",
                                 objId, *result.scalar);
                    }
                    if (result.text) {
                        context.script_text_bindings[objId].text = *result.text;
                        context.script_text_bindings[objId].has_text = true;
                        shouldRefreshRuntimeLayerSnapshot = true;
                        const std::string logText = EscapeSceneScriptLogText(*result.text);
                        LOG_INFO("QuickJS binding: id=%d text=%s", objId, logText.c_str());
                    }
                    if (shouldRefreshRuntimeLayerSnapshot) {
                        refreshRuntimeLayerSnapshot(objId, context.script_color_bindings[objId]);
                    }
                } else if (node.is_object()) {
                    int32_t childEffectId = effectId;
                    int32_t childPassId = passId;
                    if (node.contains("file") && node.contains("passes") &&
                        node.contains("id") && node.at("id").is_number_integer()) {
                        childEffectId = node.at("id").get<int32_t>();
                    }
                    if (node.contains("constantshadervalues") &&
                        node.contains("id") && node.at("id").is_number_integer()) {
                        childPassId = node.at("id").get<int32_t>();
                    }
                    for (const auto& [k, v] : node.items()) {
                        scanForScripts(v,
                                       k,
                                       maxWidthOnly,
                                       childEffectId,
                                       childPassId,
                                       inConstantShaderValues || k == "constantshadervalues");
                    }
                } else if (node.is_array()) {
                    for (const auto& v : node) {
                        scanForScripts(v,
                                       scriptField,
                                       maxWidthOnly,
                                       effectId,
                                       passId,
                                       inConstantShaderValues);
                    }
                }
                };
                scanForScripts(obj, "", false, 0, 0, false);
                scanForScripts(obj, "", true, 0, 0, false);
            }
        }
        std::unordered_set<int32_t> resolvedScriptBindingLayers;
        for (const auto& entry : context.script_color_bindings) {
            resolvedScriptBindingLayers.insert(entry.first);
        }
        for (const auto& entry : context.script_text_bindings) {
            resolvedScriptBindingLayers.insert(entry.first);
        }
        if (! resolvedScriptBindingLayers.empty()) {
            LOG_INFO("QuickJS resolved %zu script bindings from scene JSON",
                     resolvedScriptBindingLayers.size());
        }
    }

    for (auto& obj : json["objects"]) {
        if (! obj.contains("id") || ! obj.at("id").is_number_integer()) {
            continue;
        }
        const int32_t objId = obj.at("id").get<int32_t>();
        auto bindingIt = context.script_color_bindings.find(objId);
        if (bindingIt == context.script_color_bindings.end() ||
            ! bindingIt->second.has_visible) {
            continue;
        }
        obj["visible"] = bindingIt->second.visible;
    }

    for (auto& obj : json["objects"]) {
        ApplyDebugLayerVisibilityOverride(context, obj, m_debug_effect_captures);
    }
    for (auto& obj : json["objects"]) {
        ResolveStructuredObjectFields(obj);
    }
    for (const auto& obj : json.at("objects")) {
        RegisterDebugJsonObjectGraph(context, obj);
        if (m_debug_effect_captures.enabled()) {
            CollectDebugMouseParallaxSidecarLayer(context, obj);
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

    std::unordered_set<int32_t> explicitlyHiddenObjects;
    for (const auto& obj : json.at("objects")) {
        const auto id = JsonObjectId(obj);
        if (!id) {
            continue;
        }
        if (!ResolveObjectVisibleForDebug(obj)) {
            explicitlyHiddenObjects.insert(*id);
            context.hidden_objects.insert(*id);
        }
    }
    for (const auto& obj : json.at("objects")) {
        const auto id = JsonObjectId(obj);
        if (!id) {
            continue;
        }
        int32_t parentId = JsonObjectParentId(obj);
        std::unordered_set<int32_t> visited;
        while (parentId > 0 && !visited.count(parentId)) {
            visited.insert(parentId);
            if (explicitlyHiddenObjects.count(parentId) ||
                context.hidden_objects.count(parentId)) {
                context.hidden_objects.insert(*id);
                break;
            }
            const auto nextParentIt = context.object_parent_ids.find(parentId);
            if (nextParentIt == context.object_parent_ids.end()) {
                break;
            }
            parentId = nextParentIt->second;
        }
    }

    std::vector<WPObjectVar> wp_objs;
    int modelObjectCount = 0;

    for (auto& obj : json.at("objects")) {
        RegisterPuppetPauseDirectives(context, obj);
        if (obj.contains("image") && ! obj.at("image").is_null()) {
            AddWPImageObject(wp_objs, obj, vfs);
        } else if (obj.contains("particle") && ! obj.at("particle").is_null()) {
            AddWPObject<wpscene::WPParticleObject>(wp_objs, obj, vfs);
        } else if (obj.contains("sound") && ! obj.at("sound").is_null()) {
            AddWPObject<wpscene::WPSoundObject>(wp_objs, obj, vfs);
        } else if (obj.contains("light") && ! obj.at("light").is_null()) {
            AddWPObject<wpscene::WPLightObject>(wp_objs, obj, vfs);
        } else if (obj.contains("model") && ! obj.at("model").is_null()) {
            AddWPObject<wpscene::WPModelObject>(wp_objs, obj, vfs);
            ++modelObjectCount;
        } else if (obj.contains("text") || obj.contains("font")) {
            AddWPObject<WPTextObject>(wp_objs, obj, vfs);
        } else if (IsTransformAnchorObject(obj)) {
            AddWPObject<WPSolidAnchorObject>(wp_objs, obj, vfs);
        }
    }

    if (m_debug_effect_captures.enabled()) {
        for (const auto& obj : wp_objs) {
            std::visit([&context](const auto& value) { RegisterDebugObjectGraph(context, value); }, obj);
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
    RegisterSyntheticMediaThumbnailTexture(context, mediaState);
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
    context.scene->debugEffectCaptures = m_debug_effect_captures;
    context.scene->debugEffectCaptures.mouseParallax.cameraEnabled = sc.general.cameraparallax;
    context.scene->debugEffectCaptures.mouseParallax.cameraAmount = sc.general.cameraparallaxamount;
    context.scene->debugEffectCaptures.mouseParallax.cameraDelay = sc.general.cameraparallaxdelay;
    context.scene->debugEffectCaptures.mouseParallax.cameraMouseInfluence =
        sc.general.cameraparallaxmouseinfluence;
    for (const auto& layer : context.debug_mouse_parallax_sidecar_layers) {
        wallpaper::debug::recordMouseParallaxLayer(*context.scene,
                                                   layer.id,
                                                   layer.name,
                                                   "transform-anchor",
                                                   layer.parallaxDepth,
                                                   layer.parent,
                                                   DebugObjectName(context, layer.parent),
                                                   DebugChildLayerIds(context, layer.id),
                                                   true);
    }

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
                       [&context](WPTextObject& obj) {
                           ParseTextObj(context, obj);
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
