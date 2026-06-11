#pragma once

#include "Policy/EffectPolicy.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wallpaper {
class Scene;
class SceneNode;
}

namespace wallpaper::debug {

struct PuppetAnimationLayerOverride {
    int layerId { 0 };
    int animationId { 0 };
    std::optional<bool> visible;
    std::optional<bool> paused;
    std::optional<bool> additive;
    std::optional<double> blend;
    std::optional<double> rate;
    std::optional<double> curTime;
};

struct EffectCaptureConfig {
    std::string outputDir;
    std::string commandLine;
    std::vector<int> probeLayerIds;
    int captureDelayMs { 0 };
    std::vector<int> highRiskProbeLayerIds;
    std::vector<int> probeChannelMapSlots;
    std::vector<PuppetAnimationLayerOverride> puppetAnimationLayerOverrides;
    int probeMaxEffects { -1 };
    std::string puppetFinalMeshOverride;
    bool puppetEffectRouteOnly { false };

    bool enabled() const { return !outputDir.empty(); }
    bool shouldProbeLayer(int layerId) const;
    bool shouldProbeHighRiskLayer(int layerId) const;
    std::filesystem::path manifestPath() const;
};

struct EffectCaptureTextureBindingInfo {
    int         slot { 0 };
    std::string authored;
    std::string resolved;
};

struct EffectCaptureMaterialInfo {
    int effectIndex { 0 };
    int materialIndex { 0 };
    std::string shader;
    std::string authoredOutputRenderTarget;
    std::string resolvedOutputRenderTarget;
    bool        finalPublishedMaterial { false };
    std::string debugMaterialOutputSourceRenderTarget;
    std::string debugMaterialOutputCommandSource;
    bool        debugSourceFinalEffectOutput { false };
    std::string localMaterialOutputCaptureStage;
    std::string materialOutputCaptureStage;
    int         materialOutputCopyAfterPos { -1 };
    std::vector<std::string> authoredTextures;
    std::vector<std::string> resolvedTextures;
    std::vector<EffectCaptureTextureBindingInfo> textureBindings;
    std::unordered_map<std::string, std::string> authoredCombos;
    std::unordered_map<std::string, std::string> resolvedCombos;
    std::unordered_map<std::string, std::vector<float>> materialValues;
    std::unordered_map<std::string, std::vector<float>> resolvedConstValues;
    std::vector<std::string> defines;
};

struct EffectCaptureTransformInfo {
    std::vector<float> origin;
    std::vector<float> scale;
    std::vector<float> angles;
};

struct EffectCaptureMeshBoundsInfo {
    int vertexArrayCount { 0 };
    int indexArrayCount { 0 };
    int vertexCount { 0 };
    int indexDataCount { 0 };
    int indexRenderDataCount { 0 };
    std::vector<float> positionMin;
    std::vector<float> positionMax;
};

struct PuppetCutoutSlotCoverageInfo {
    int  slot { 0 };
    bool active { false };
    int  vertexCount { 0 };
    int  triangleCount { 0 };
    std::string boneName;
    int         parentSlot { -1 };
    std::string parentBoneName;
    int  primaryVertexCount { 0 };
    int  primaryTriangleCount { 0 };
    int  weightedVertexCount { 0 };
    int  weightedTriangleCount { 0 };
    double weightedVertexWeightSum { 0.0 };
    std::vector<float> layerLocalBounds;
    std::vector<float> layerLocalCentroid;
    bool secondaryOnly { false };
    bool simulationMetadataPresent { false };
    std::string simulationMetadata;
    bool simulationMetadataValid { false };
    bool simulationPhysicsActive { false };
    bool simulationTargetPointPresent { false };
    std::vector<float> simulationTargetPoint;
    bool simulationTargetMassPresent { false };
    float simulationTargetMass { 0.0f };
    bool simulatedInactive { false };
};

struct EffectCapturePublishInfo {
    bool enabled { false };
    int  parentId { 0 };
    bool hasParsedParentNode { false };
    std::vector<float> objectSize;
    std::vector<float> origin;
    std::vector<float> scale;
    std::vector<float> angles;
    int  finalBlendMode { 0 };
    bool fullscreen { false };
    bool composelayer { false };
    bool puppetLayer { false };
    std::vector<float> effectInputViewportSize;
    bool effectInputViewportExpanded { false };
    bool standalonePuppetFinalDisplay { false };
    bool publishFinalOutput { true };
    bool finalNodeUsesOriginalParent { false };
    bool effectInputNodeReset { false };
    bool effectInputMaterialPreservesLayerBlendMode { false };
    std::string effectInputMeshKind;
    std::string effectFinalMeshKind;
    std::string standaloneFinalMeshKind;
    std::string finalDisplayRoute;
    std::string standaloneDisplayAttachMode;
    std::string routeRisk;
    std::string effectInputRenderTarget;
    std::string effectPingPongA;
    std::string effectPingPongB;
    std::string effectOutputSourceTarget;
    std::string finalPublishRenderTarget;
    std::string materialOutputCaptureTiming;
    std::string finalPublishCaptureTiming;
    std::string defaultRtBoundaryCaptureTiming;
    std::string finalDisplayBoundaryCaptureTiming;
    std::string finalDisplayBeforeRenderTarget;
    std::string finalDisplayAfterRenderTarget;
    std::string channelMapPrepassMode;
    std::string channelMapMaterialPath;
    std::vector<int> activePuppetChannelBlendSlots;
    std::vector<PuppetCutoutSlotCoverageInfo> puppetCutoutSlotCoverage;
    EffectCaptureTransformInfo effectInputLocalTransform;
    EffectCaptureTransformInfo standaloneDisplayLocalTransform;
    int  standaloneDisplayParentId { 0 };
    bool standaloneDisplayHasParsedParentNode { false };
    int  standaloneDisplayNodeOrdinal { -1 };
    int  standaloneFinalMaterialBlendMode { -1 };
    std::string standaloneFinalTexture;
    EffectCaptureMeshBoundsInfo effectInputMeshBounds;
    EffectCaptureMeshBoundsInfo effectFinalMeshBounds;
    EffectCaptureMeshBoundsInfo standaloneFinalMeshBounds;
};

struct EffectCaptureFinalDisplayBoundaryTargets {
    std::string beforeTarget;
    std::string afterTarget;
};

struct EffectCaptureFinalDisplayBoundaryHook {
    const SceneNode* node { nullptr };
    std::string      beforeTarget;
    std::string      afterTarget;
};

struct PuppetAnimationLayerInfo {
    int         animationId { 0 };
    std::string animationName;
    double      rate { 1.0 };
    double      blend { 1.0 };
    bool        visible { true };
    bool        paused { false };
    bool        additive { false };
    double      curTime { 0.0 };
    bool        matchedAnimation { false };
    bool        visibleAndWeighted { false };
    std::vector<int> activeBoneSlots;
};

struct EffectCaptureLayerInfo {
    std::string              sceneId;
    std::string              sceneType;
    std::string              layerName;
    std::string              layerImage;
    int                      layerId { 0 };
    int                      visibleEffectCount { 0 };
    float                    alpha { 1.0f };
    bool                     keepLayer { true };
    bool                     keepEffects { true };
    bool                     strippedEffects { false };
    std::string              policyReason;
    std::vector<std::string> effectNames;
    std::vector<std::string> materialShaders;
    std::vector<EffectCaptureMaterialInfo> effectMaterials;
    std::vector<PuppetAnimationLayerInfo> puppetAnimationLayers;
    EffectCapturePublishInfo publish;
    std::vector<std::string> candidateFamilies;
    std::vector<std::string> candidateMixFamilies;
    std::string              candidateChainShape;
    std::string              candidateEffectClass;
    std::string              candidateRisk;
    std::string              candidateBlockedReason;
    wallpaper::policy::CandidateChecks candidateChecks;
    bool                     debugProbeRequested { false };
    bool                     debugProbeOverrodePolicy { false };
    std::string              debugProbeReason;
    int                      debugProbeMaxEffects { -1 };
    int                      debugProbeOriginalVisibleEffectCount { -1 };
    int                      debugProbeKeptVisibleEffectCount { -1 };
    bool                     debugProbeEffectLimitTruncated { false };
    bool                     debugProbeRouteOnly { false };
};

struct EffectCaptureRecord {
    std::string            stage;
    std::string            label;
    std::string            renderTarget;
    std::string            path;
    EffectCaptureLayerInfo layer;
    int                    renderTargetWidth { 0 };
    int                    renderTargetHeight { 0 };
    std::string            renderTargetFormat { "RGBA8" };
    bool                   completed { false };
    bool                   failed { false };
    std::string            failureReason;
};

struct EffectPassState {
    std::string output;
    std::string loadOp;
    std::string depthLoadOp;
    std::string colorMask;
    uint32_t    colorMaskBits { 0 };
    std::string blendMode;
    bool        blendEnabled { false };
    bool        preserveOutput { false };
    bool        usesDepth { false };
    std::string camera;
    int         nodeId { -1 };
    std::string materialName;
    std::string debugPurpose;
};

std::string sanitizeCapturePathSegment(std::string_view value);

std::vector<int> parseProbeLayerIdList(std::string_view value);

std::vector<int> parseProbeChannelMapSlotList(std::string_view value);

int parseProbeMaxEffects(std::string_view value);

bool shouldDumpEffectCaptures(const EffectCaptureConfig& config,
                              double elapsedSeconds,
                              double frameTimeSeconds);

bool shouldRegisterMaterialOutputCaptureForShader(std::string_view shader);

struct EffectProbeLimitDecision {
    int  keptVisibleEffectCount { 0 };
    bool keepEffectRouteActive { false };
    bool effectLimitTruncated { false };
    bool routeOnly { false };
};

EffectProbeLimitDecision decideEffectProbeLimit(int originalVisibleEffectCount,
                                                int maxEffects,
                                                bool routeOnly);

std::optional<std::vector<PuppetAnimationLayerOverride>>
parsePuppetAnimationLayerOverrideList(std::string_view value);

std::filesystem::path capturePath(const EffectCaptureConfig& config,
                                  const EffectCaptureLayerInfo& layer,
                                  std::string_view stage);

void registerEffectCapture(Scene& scene,
                           const EffectCaptureLayerInfo& layer,
                           std::string_view stage,
                           std::string_view renderTarget);

std::string registerDefaultRtBoundaryCapture(Scene& scene,
                                             const EffectCaptureLayerInfo& layer,
                                             std::string_view stage,
                                             std::string_view suffix);

EffectCaptureFinalDisplayBoundaryTargets
registerFinalDisplayBoundaryCapture(Scene& scene,
                                    const EffectCaptureLayerInfo& layer,
                                    const SceneNode& node,
                                    std::string_view suffix);

void refreshEffectCaptureLayerInfo(Scene& scene, const EffectCaptureLayerInfo& layer);

void recordEffectPassState(Scene& scene, const EffectPassState& state);

void recordStrippedEffectCandidate(Scene& scene, const EffectCaptureLayerInfo& layer);

void recordPuppetAnimationLayerInventory(Scene& scene, const EffectCaptureLayerInfo& layer);

bool shouldProbeStrippedEffectLayer(const EffectCaptureConfig& config,
                                    const EffectCaptureLayerInfo& layer);

bool shouldLimitRequestedEffectProbeLayer(const EffectCaptureConfig& config,
                                          const EffectCaptureLayerInfo& layer);

std::string strippedEffectProbeReason(const EffectCaptureConfig& config,
                                      const EffectCaptureLayerInfo& layer);

bool writeEffectCaptureManifest(const Scene& scene);

} // namespace wallpaper::debug
