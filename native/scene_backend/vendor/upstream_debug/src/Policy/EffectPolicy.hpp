#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace wallpaper::policy {

struct LayerEffectDescriptor {
    std::string name;
    std::string firstMaterialShader;
    std::vector<std::string> materialShaders;
};

struct LayerEffectInput {
    bool sceneHasPuppetObjects { false };
    bool hasVisibleEffects { false };
    bool noEffectsDebug { false };
    bool isComposelayer { false };
    bool fullscreen { false };
    int visibleEffectCount { 0 };
    int colorBlendMode { 0 };
    float alpha { 1.0f };
    std::string layerName;
    std::string imagePath;
    std::vector<LayerEffectDescriptor> effects;
};

struct LayerEffectDecision {
    bool keepLayer { true };
    bool keepEffects { true };
    bool forceAlphaOne { false };
    bool strippedEffects { false };
    std::string_view reason { "keep" };
};

LayerEffectDecision decideLayerEffects(const LayerEffectInput& input);

} // namespace wallpaper::policy
