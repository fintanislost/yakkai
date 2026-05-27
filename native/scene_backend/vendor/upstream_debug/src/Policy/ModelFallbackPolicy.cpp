#include "Policy/ModelFallbackPolicy.hpp"

namespace wallpaper::policy {

ModelMaterialFallbackDecision decideModelMaterialFallback(const ModelMaterialFallbackInput& input)
{
    ModelMaterialFallbackDecision decision;
    if (input.sourceShader == "generic") {
        decision.useAuthoredGenericMaterial = true;
        decision.outputShader = "generic";
        decision.outputBlending = input.sourceBlending.empty() ? "disabled" : input.sourceBlending;
        return decision;
    }

    decision.useAuthoredGenericMaterial = false;
    decision.outputShader = "genericimage";
    decision.outputBlending = "disabled";
    return decision;
}

ModelFallbackStatusDecision describeModelFallbackSupport(const ModelFallbackStatusInput& input)
{
    ModelFallbackStatusDecision decision;
    if (input.modelObjectCount <= 0) {
        return decision;
    }
    if (input.firstFrameRendered) {
        decision.kind = ModelFallbackStatusKind::FirstFrameRendered;
    } else if (input.supportedDrawableObjectCount > 0) {
        decision.kind = ModelFallbackStatusKind::MixedSceneDetected;
    } else {
        decision.kind = ModelFallbackStatusKind::ModelOnlyDetected;
    }
    return decision;
}

} // namespace wallpaper::policy
