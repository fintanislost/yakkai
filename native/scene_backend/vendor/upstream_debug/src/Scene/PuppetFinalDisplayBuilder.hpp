#pragma once

#include "Debug/EffectCaptureDebug.hpp"
#include "Policy/EffectPolicy.hpp"
#include "Scene/PuppetEffectRoutePlan.hpp"
#include "Scene/SceneMaterial.h"
#include "Scene/SceneMesh.h"
#include "Scene/SceneNode.h"
#include "Type.hpp"
#include "WPMdlParser.hpp"
#include "WPShaderParser.hpp"
#include "WPShaderValueUpdater.hpp"
#include "wpscene/WPMaterial.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wallpaper
{

using PuppetFinalDisplayLoadMaterialCallback =
    std::function<bool(const wpscene::WPMaterial&,
                       SceneNode*,
                       SceneMaterial*,
                       WPShaderValueData*,
                       WPShaderInfo*)>;

using PuppetFinalDisplayLoadConstValuesCallback =
    std::function<void(SceneMaterial&, const wpscene::WPMaterial&, const WPShaderInfo&)>;

struct PuppetFinalDisplayBuildInput {
    Scene* scene { nullptr };
    debug::EffectCaptureLayerInfo* effectCaptureInfo { nullptr };
    std::string imageName;
    int imageParentId { 0 };
    bool imageParentParsed { false };
    std::string nodeAddr;
    std::size_t existingStandaloneDisplayNodeCount { 0 };
    bool debugEffectCaptures { false };
    bool usePuppetChannelMapPrepass { false };
    std::vector<uint32_t> activePuppetChannelBlendSlots;
    WPMdl* puppet { nullptr };
    wpscene::WPMaterial imageMaterial;
    wpscene::WPMaterial sourceMaterial;
    std::array<float, 2> imageSize { 0.0f, 0.0f };
    policy::LayerEffectViewportDecision effectViewport;
    PuppetEffectRoutePlan routePlan;
    const SceneNode* standaloneDisplayTransform { nullptr };
    ShaderValueMap baseConstSvs;
    std::array<float, 2> parallaxDepth { 0.0f, 0.0f };
    std::vector<WPPuppetLayer::AnimationLayer> renderPuppetLayers;
    std::string finalEffectTexture;
    int authoredEffectCount { 0 };
    PuppetFinalDisplayLoadMaterialCallback loadMaterial;
    PuppetFinalDisplayLoadConstValuesCallback loadConstValues;
};

struct PuppetFinalDisplayBuildResult {
    bool success { false };
    std::shared_ptr<SceneNode> node;
    std::shared_ptr<SceneMesh> mesh;
    WPShaderValueData shaderValueData;
    std::string sourceTexture;
    int finalBlendMode { 0 };
    std::string finalMeshKind;
    std::string attachMode;
    debug::EffectCaptureMeshBoundsInfo standaloneFinalMeshBounds;
    debug::EffectCaptureTransformInfo standaloneDisplayLocalTransform;
    std::string finalDisplayBeforeRenderTarget;
    std::string finalDisplayAfterRenderTarget;
    std::string finalDisplayBoundaryCaptureTiming;
    int nodeOrdinal { -1 };
    bool usingFilteredOverlayMesh { false };
};

BlendMode decidePuppetFinalDisplayBlend();

PuppetFinalDisplayBuildResult buildPuppetFinalDisplay(
    const PuppetFinalDisplayBuildInput& input);

} // namespace wallpaper
