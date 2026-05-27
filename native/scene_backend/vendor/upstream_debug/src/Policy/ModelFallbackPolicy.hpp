#pragma once

#include <string>

namespace wallpaper::policy {

struct ModelMaterialFallbackInput {
    std::string sourceShader;
    std::string sourceBlending;
    bool hasDiffuseTexture { false };
    bool wantsLightmap { false };
    bool wantsNormalmap { false };
    bool wantsReflection { false };
};

struct ModelMaterialFallbackDecision {
    bool useAuthoredGenericMaterial { false };
    std::string outputShader;
    std::string outputBlending;
};

enum class ModelFallbackStatusKind {
    None,
    MixedSceneDetected,
    ModelOnlyDetected,
    FirstFrameRendered,
};

struct ModelFallbackStatusInput {
    int supportedDrawableObjectCount { 0 };
    int modelObjectCount { 0 };
    bool firstFrameRendered { false };
};

struct ModelFallbackStatusDecision {
    ModelFallbackStatusKind kind { ModelFallbackStatusKind::None };
};

ModelMaterialFallbackDecision decideModelMaterialFallback(const ModelMaterialFallbackInput& input);
ModelFallbackStatusDecision describeModelFallbackSupport(const ModelFallbackStatusInput& input);

} // namespace wallpaper::policy
