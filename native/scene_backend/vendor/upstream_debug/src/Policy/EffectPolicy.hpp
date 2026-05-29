#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace wallpaper::policy {

struct LayerEffectDescriptor {
    std::string name;
    bool visible { true };
    std::string firstMaterialShader;
    std::vector<std::string> materialShaders;
};

struct LayerEffectInput {
    bool sceneHasPuppetObjects { false };
    bool hasVisibleEffects { false };
    bool noEffectsDebug { false };
    bool isComposelayer { false };
    bool fullscreen { false };
    bool isPuppetLayer { false };
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

struct CandidateChecks {
    bool hasWaterFamily { false };
    bool waterOnly { false };
    bool isUtilityCarrier { false };
    bool isComposelayer { false };
    bool isFullscreen { false };
    bool isPuppetLayer { false };
    bool isProtectedPuppetPath { false };
};

struct CandidateClassification {
    std::vector<std::string> candidateFamilies;
    std::vector<std::string> candidateMixFamilies;
    std::string candidateChainShape { "non-water" };
    std::string candidateRisk { "non-water" };
    std::string candidateBlockedReason { "no-water-effect-family" };
    CandidateChecks candidateChecks;
};

CandidateClassification classifyStrippedEffectCandidate(const LayerEffectInput& input);
LayerEffectDecision decideLayerEffects(const LayerEffectInput& input);

} // namespace wallpaper::policy
