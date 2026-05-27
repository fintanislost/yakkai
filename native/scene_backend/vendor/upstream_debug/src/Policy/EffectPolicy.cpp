#include "Policy/EffectPolicy.hpp"

#include <algorithm>

namespace wallpaper::policy {
namespace {

bool containsToken(const std::string& value, std::string_view token)
{
    return value.find(token) != std::string::npos;
}

bool anyEffectNameContains(const std::vector<LayerEffectDescriptor>& effects, std::string_view token)
{
    return std::any_of(effects.begin(), effects.end(), [token](const LayerEffectDescriptor& effect) {
        return containsToken(effect.name, token);
    });
}

bool anyMaterialShaderContains(const std::vector<LayerEffectDescriptor>& effects, std::string_view token)
{
    return std::any_of(effects.begin(), effects.end(), [token](const LayerEffectDescriptor& effect) {
        return std::any_of(effect.materialShaders.begin(), effect.materialShaders.end(),
                           [token](const std::string& shader) {
                               return containsToken(shader, token);
                           });
    });
}

bool anyFirstMaterialShaderContains(const std::vector<LayerEffectDescriptor>& effects, std::string_view token)
{
    return std::any_of(effects.begin(), effects.end(), [token](const LayerEffectDescriptor& effect) {
        return containsToken(effect.firstMaterialShader, token);
    });
}

bool isUtilityLayer(std::string_view imagePath)
{
    return imagePath.find("solidlayer") != std::string_view::npos ||
           imagePath.find("projectlayer") != std::string_view::npos ||
           imagePath.find("fullscreenlayer") != std::string_view::npos;
}

bool isFlareOrLensLayer(std::string_view layerName)
{
    return layerName.find("flare") != std::string_view::npos ||
           layerName.find("lense") != std::string_view::npos ||
           layerName.find("lens") != std::string_view::npos;
}

} // namespace

LayerEffectDecision decideLayerEffects(const LayerEffectInput& input)
{
    LayerEffectDecision decision;
    decision.keepEffects = input.hasVisibleEffects;

    if (!input.hasVisibleEffects && input.fullscreen) {
        decision.keepLayer = false;
        decision.keepEffects = false;
        decision.reason = "effectless-fullscreen";
        return decision;
    }

    if (!input.hasVisibleEffects && input.isComposelayer) {
        decision.keepLayer = false;
        decision.keepEffects = false;
        decision.reason = "effectless-composelayer";
        return decision;
    }

    if (input.noEffectsDebug) {
        decision.keepEffects = false;
        decision.strippedEffects = input.hasVisibleEffects;
        if (input.isComposelayer) {
            decision.keepLayer = false;
            decision.reason = "debug-no-effects-composelayer";
        } else {
            decision.reason = "debug-no-effects";
        }
        return decision;
    }

    if (!input.sceneHasPuppetObjects || !input.hasVisibleEffects) {
        return decision;
    }

    const bool hasColorkey = anyEffectNameContains(input.effects, "colorkey") ||
                             anyMaterialShaderContains(input.effects, "colorkey");
    const bool hasHeavyEffect = anyEffectNameContains(input.effects, "audio") ||
                                anyEffectNameContains(input.effects, "lightshaft") ||
                                anyFirstMaterialShaderContains(input.effects, "audio") ||
                                anyFirstMaterialShaderContains(input.effects, "Audio") ||
                                anyFirstMaterialShaderContains(input.effects, "lightshaft");
    const bool isEssentialEffect = hasColorkey ||
        (!hasHeavyEffect && (isFlareOrLensLayer(input.layerName) || input.colorBlendMode != 0));

    if (!isEssentialEffect) {
        decision.keepEffects = false;
        decision.strippedEffects = true;
        decision.reason = "puppet-alpha-strip";
        if (input.isComposelayer || input.fullscreen || isUtilityLayer(input.imagePath)) {
            decision.keepLayer = false;
        }
        return decision;
    }

    if (input.alpha == 0.0f) {
        decision.forceAlphaOne = true;
    }
    decision.reason = "essential-effect";
    return decision;
}

} // namespace wallpaper::policy
