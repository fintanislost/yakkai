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
    bool supportedMediaWidgetUtility { false };
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
    bool strippedEffects { false };
    std::string_view reason { "keep" };
};

struct EffectPublishRouteInput {
    bool puppetLayer { false };
    bool fullscreen { false };
    bool composelayer { false };
    bool standalonePuppetFinalDisplay { false };
    bool usePuppetChannelMapPrepass { false };
    bool hasActivePuppetChannelBlendSlots { false };
    std::string puppetFinalMeshOverride;
};

struct EffectPublishRouteDecision {
    std::string effectInputMeshKind;
    std::string effectFinalMeshKind;
    std::string standaloneFinalMeshKind;
    std::string finalDisplayRoute;
    std::string standaloneDisplayAttachMode;
    std::string routeRisk;
    bool standaloneFinalMaterialUsesPuppetSkinning { false };
    bool effectInputMaterialPreservesLayerBlendMode { false };
};

struct LayerEffectViewportInput {
    float objectWidth { 0.0f };
    float objectHeight { 0.0f };
    bool  hasMeshBounds { false };
    float meshPositionMinX { 0.0f };
    float meshPositionMinY { 0.0f };
    float meshPositionMaxX { 0.0f };
    float meshPositionMaxY { 0.0f };
};

struct LayerEffectViewportDecision {
    int  width { 1 };
    int  height { 1 };
    bool expandedToMeshBounds { false };
};

struct CandidateChecks {
    bool hasWaterFamily { false };
    bool hasBlurFamily { false };
    bool hasLutFamily { false };
    bool hasColorGradingFamily { false };
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
    std::string candidateEffectClass { "none" };
    std::string candidateRisk { "non-water" };
    std::string candidateBlockedReason { "no-water-effect-family" };
    CandidateChecks candidateChecks;
};

CandidateClassification classifyStrippedEffectCandidate(const LayerEffectInput& input);
LayerEffectDecision decideLayerEffects(const LayerEffectInput& input);
EffectPublishRouteDecision decideEffectPublishRoute(const EffectPublishRouteInput& input);
LayerEffectViewportDecision decideLayerEffectViewport(const LayerEffectViewportInput& input);

} // namespace wallpaper::policy
