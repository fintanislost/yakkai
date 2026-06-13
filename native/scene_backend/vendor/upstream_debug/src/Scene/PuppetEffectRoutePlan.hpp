#pragma once

#include <array>
#include <string>
#include <vector>

namespace wallpaper
{

struct PuppetEffectRoutePlanInput {
    bool puppetLayer { false };
    bool fullscreen { false };
    bool composelayer { false };
    bool effectRouteActive { false };
    bool usePuppetChannelMapPrepass { false };
    bool routePuppetPrepassThroughAuthoredEffects { false };
    bool debugRouteOnly { false };
    std::string puppetFinalMeshOverride;
    std::vector<int> activeChannelBlendSlots;
};

struct PuppetEffectRoutePlan {
    bool useStandalonePuppetFinalDisplay { false };
    bool publishFinalOutput { true };
    bool preservePuppetMeshForEffectPasses { false };
    bool finalMaterialUsesPuppetSkinning { false };
    bool effectInputMaterialPreservesLayerBlendMode { false };
    std::string effectInputMeshKind;
    std::string effectFinalMeshKind;
    std::string standaloneFinalMeshKind;
    std::string finalDisplayRoute;
    std::string standaloneDisplayAttachMode;
    std::string routeRisk;
};

PuppetEffectRoutePlan decidePuppetEffectRoutePlan(const PuppetEffectRoutePlanInput& input);

std::array<float, 2> decideEffectLayerMaterialParallaxDepth(
    const PuppetEffectRoutePlan& routePlan,
    bool isFinalPublishedMaterialNode,
    std::array<float, 2> sourceParallaxDepth);

} // namespace wallpaper
