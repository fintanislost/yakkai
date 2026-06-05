#include "Scene/PuppetEffectRoutePlan.hpp"

#include "Policy/EffectPolicy.hpp"

namespace wallpaper
{

PuppetEffectRoutePlan decidePuppetEffectRoutePlan(const PuppetEffectRoutePlanInput& input)
{
    PuppetEffectRoutePlan plan;

    const bool activeEffectRoute = input.effectRouteActive || input.debugRouteOnly;
    const bool forceStandalonePuppetFinal =
        input.puppetFinalMeshOverride == "layer-card" ||
        input.puppetFinalMeshOverride == "image-space";
    const bool deferredPuppetFinal =
        input.puppetLayer &&
        activeEffectRoute &&
        ! input.usePuppetChannelMapPrepass &&
        ! forceStandalonePuppetFinal;
    plan.useStandalonePuppetFinalDisplay =
        input.puppetLayer && activeEffectRoute && !deferredPuppetFinal;
    plan.publishFinalOutput = !plan.useStandalonePuppetFinalDisplay;
    plan.preservePuppetMeshForEffectPasses =
        input.routePuppetPrepassThroughAuthoredEffects;
    const std::string routeMeshOverride =
        deferredPuppetFinal ? "deferred-puppet-final" : input.puppetFinalMeshOverride;

    const auto route = policy::decideEffectPublishRoute({
        .puppetLayer = input.puppetLayer,
        .fullscreen = input.fullscreen,
        .composelayer = input.composelayer,
        .standalonePuppetFinalDisplay = plan.useStandalonePuppetFinalDisplay,
        .usePuppetChannelMapPrepass = input.usePuppetChannelMapPrepass,
        .hasActivePuppetChannelBlendSlots = !input.activeChannelBlendSlots.empty(),
        .puppetFinalMeshOverride = routeMeshOverride,
    });

    plan.finalMaterialUsesPuppetSkinning =
        route.standaloneFinalMaterialUsesPuppetSkinning;
    plan.effectInputMaterialPreservesLayerBlendMode =
        route.effectInputMaterialPreservesLayerBlendMode;
    plan.effectInputMeshKind = route.effectInputMeshKind;
    plan.effectFinalMeshKind = route.effectFinalMeshKind;
    plan.standaloneFinalMeshKind = route.standaloneFinalMeshKind;
    plan.finalDisplayRoute = route.finalDisplayRoute;
    plan.standaloneDisplayAttachMode = route.standaloneDisplayAttachMode;
    plan.routeRisk = route.routeRisk;

    return plan;
}

} // namespace wallpaper
