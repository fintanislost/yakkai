#include "Scene/Scene.h"

#include "WPSceneScript.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace wallpaper
{

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
