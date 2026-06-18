#include "Scene/Scene.h"

#include "WPSceneScript.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace wallpaper
{

namespace
{
std::optional<std::array<float, 3>> RuntimeVectorResult(const SceneScriptResult& result)
{
    if (result.returnVector) return result.returnVector;
    if (result.origin) return result.origin;
    return std::nullopt;
}

void ApplyMaterialColor(SceneNode& node, const std::array<float, 3>& color)
{
    auto* mesh = node.Mesh();
    if (mesh == nullptr || mesh->Material() == nullptr) {
        return;
    }

    auto& constValues = mesh->Material()->customShader.constValues;
    constValues["g_Color"] = color;
    auto color4 = std::array<float, 4> { color[0], color[1], color[2], 1.0f };
    if (auto it = constValues.find("g_Color4"); it != constValues.end()) {
        color4[3] = it->second.size() >= 4 ? it->second[3] : 1.0f;
    }
    constValues["g_Color4"] = color4;
}

void ApplyMaterialAlpha(SceneNode& node, float alpha)
{
    auto* mesh = node.Mesh();
    if (mesh == nullptr || mesh->Material() == nullptr) {
        return;
    }

    auto& constValues = mesh->Material()->customShader.constValues;
    constValues["g_Alpha"] = alpha;
    constValues["g_UserAlpha"] = alpha;
    auto color4 = std::array<float, 4> { 1.0f, 1.0f, 1.0f, alpha };
    if (auto it = constValues.find("g_Color4"); it != constValues.end()) {
        color4[0] = it->second.size() >= 1 ? it->second[0] : color4[0];
        color4[1] = it->second.size() >= 2 ? it->second[1] : color4[1];
        color4[2] = it->second.size() >= 3 ? it->second[2] : color4[2];
    }
    constValues["g_Color4"] = color4;
}

void ApplyMediaRuntimeBinding(const Scene::MediaRuntimeBinding& binding,
                              const SceneScriptMediaState& mediaState)
{
    const auto node = binding.node.lock();
    if (!node || binding.script.empty()) {
        return;
    }

    SceneScriptContext scriptContext;
    scriptContext.setUserProperties(binding.userProperties);
    scriptContext.setCanvasSize(binding.canvasWidth, binding.canvasHeight);
    scriptContext.setMediaState(mediaState);
    for (const auto& [name, value] : binding.scriptProperties) {
        scriptContext.setScriptProperty(name, value);
    }

    auto currentValue = binding.authoredOrigin;
    if (binding.field == Scene::MediaRuntimeBindingField::Scale) {
        currentValue = binding.authoredScale;
    } else if (binding.field == Scene::MediaRuntimeBindingField::Color) {
        currentValue = binding.authoredColor;
    }

    const auto result = scriptContext.evaluateLayerScript(
        binding.script,
        currentValue,
        binding.authoredColor,
        binding.authoredAlpha,
        binding.layerId,
        binding.authoredVisible);

    switch (binding.field) {
        case Scene::MediaRuntimeBindingField::Origin:
        case Scene::MediaRuntimeBindingField::Layer: {
            if (auto vectorResult = RuntimeVectorResult(result)) {
                node->SetTranslate(
                    Eigen::Vector3f((*vectorResult)[0], (*vectorResult)[1], (*vectorResult)[2]));
            }
            if (binding.field == Scene::MediaRuntimeBindingField::Layer) {
                if (result.color) ApplyMaterialColor(*node, *result.color);
                if (result.alpha) ApplyMaterialAlpha(*node, *result.alpha);
                if (result.visible) node->SetVisible(*result.visible);
            }
            break;
        }
        case Scene::MediaRuntimeBindingField::Scale: {
            if (auto vectorResult = RuntimeVectorResult(result)) {
                node->SetScale(
                    Eigen::Vector3f((*vectorResult)[0], (*vectorResult)[1], (*vectorResult)[2]));
            }
            break;
        }
        case Scene::MediaRuntimeBindingField::Color: {
            if (auto vectorResult = RuntimeVectorResult(result)) {
                ApplyMaterialColor(*node, *vectorResult);
            } else if (result.color) {
                ApplyMaterialColor(*node, *result.color);
            }
            break;
        }
        case Scene::MediaRuntimeBindingField::Alpha: {
            if (result.scalar) {
                ApplyMaterialAlpha(*node, *result.scalar);
            } else if (result.alpha) {
                ApplyMaterialAlpha(*node, *result.alpha);
            }
            break;
        }
        case Scene::MediaRuntimeBindingField::Visible: {
            if (result.visible) {
                node->SetVisible(*result.visible);
            }
            break;
        }
    }
}
} // namespace

SceneScriptMediaState InterpolatedSceneMediaState(const SceneScriptMediaState& state,
                                                  double elapsedSeconds)
{
    SceneScriptMediaState result = state;
    if (!result.available || !result.playing || result.duration <= 0.0) {
        return result;
    }

    const double elapsed = std::max(0.0, elapsedSeconds);
    result.position = std::clamp(result.position + elapsed, 0.0, result.duration);
    return result;
}

void ApplySceneMediaTimelineState(Scene& scene, const SceneScriptMediaState& mediaState)
{
    for (const auto& binding : scene.mediaRuntimeBindings) {
        ApplyMediaRuntimeBinding(binding, mediaState);
    }

    for (const auto& binding : scene.mediaTimelineScaleBindings) {
        const auto node = binding.node.lock();
        if (!node || binding.script.empty()) {
            continue;
        }

        SceneScriptContext scriptContext;
        scriptContext.setUserProperties(binding.userProperties);
        scriptContext.setCanvasSize(binding.canvasWidth, binding.canvasHeight);
        scriptContext.setMediaState(mediaState);
        for (const auto& [name, value] : binding.scriptProperties) {
            scriptContext.setScriptProperty(name, value);
        }
        const auto result = scriptContext.evaluateLayerScript(
            binding.script,
            binding.authoredScale,
            { 1.0f, 1.0f, 1.0f },
            1.0f,
            binding.layerId,
            true);
        const auto vectorResult = result.returnVector ? result.returnVector : result.origin;
        if (!vectorResult) {
            continue;
        }

        Eigen::Vector3f scale((*vectorResult)[0], (*vectorResult)[1], (*vectorResult)[2]);
        node->SetScale(scale);

        Eigen::Vector3f translate(
            binding.authoredOrigin[0],
            binding.authoredOrigin[1],
            binding.authoredOrigin[2]);
        if (binding.leadingEdgeAnchored) {
            const float scaleX = scale.x();
            if (std::isfinite(scaleX)) {
                const float absScaleX = std::abs(scaleX);
                if (absScaleX < 1.0f) {
                    const float compensation = (1.0f - absScaleX) * binding.size[0] * 0.5f;
                    translate.x() -= binding.parentHorizontalSign * compensation;
                }
            }
        }
        node->SetTranslate(translate);
    }
}

} // namespace wallpaper
