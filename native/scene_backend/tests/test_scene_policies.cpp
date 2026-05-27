#include "Policy/EffectPolicy.hpp"
#include "Policy/ModelFallbackPolicy.hpp"
#include "Policy/SceneScriptRuntimePolicy.hpp"
#include "Policy/VideoTexturePolicy.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

wallpaper::policy::LayerEffectInput baseEffectInput()
{
    wallpaper::policy::LayerEffectInput input;
    input.sceneHasPuppetObjects = true;
    input.hasVisibleEffects = true;
    input.visibleEffectCount = 1;
    input.layerName = "regular layer";
    input.imagePath = "materials/layer.png";
    input.alpha = 1.0f;
    input.effects = {{
        .name = "waterwaves",
        .firstMaterialShader = "effects/waterwaves",
        .materialShaders = {"effects/waterwaves"},
    }};
    return input;
}

void testEffectPolicy()
{
    {
        auto input = baseEffectInput();
        input.hasVisibleEffects = false;
        input.fullscreen = true;
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(!decision.keepLayer, "effectless fullscreen layers are skipped");
        check(!decision.keepEffects, "effectless fullscreen layers keep no effects");
        check(decision.reason == "effectless-fullscreen", "effectless fullscreen reason is parser-visible");
    }

    {
        auto input = baseEffectInput();
        input.hasVisibleEffects = false;
        input.isComposelayer = true;
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(!decision.keepLayer, "effectless composelayers are skipped");
        check(!decision.keepEffects, "effectless composelayers keep no effects");
        check(decision.reason == "effectless-composelayer", "effectless composelayer reason is parser-visible");
    }

    {
        auto input = baseEffectInput();
        input.noEffectsDebug = true;
        input.isComposelayer = true;
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(!decision.keepLayer, "YAKKAI_NO_EFFECTS drops composelayers");
        check(decision.reason == "debug-no-effects-composelayer", "debug composelayer reason is parser-visible");
    }

    {
        auto input = baseEffectInput();
        input.noEffectsDebug = true;
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(decision.keepLayer, "YAKKAI_NO_EFFECTS keeps non-composelayers");
        check(!decision.keepEffects, "YAKKAI_NO_EFFECTS strips non-composelayer effects");
        check(decision.reason == "debug-no-effects", "debug strip reason is parser-visible");
    }

    {
        auto input = baseEffectInput();
        input.imagePath = "models/util/solidlayer.json";
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(!decision.keepLayer, "stripped utility effect carriers are dropped");
        check(!decision.keepEffects, "nonessential puppet-scene effects are stripped");
        check(decision.reason == "puppet-alpha-strip", "puppet strip reason is parser-visible");
    }

    {
        auto input = baseEffectInput();
        input.layerName = "lens flare";
        input.effects = {{
            .name = "flare",
            .firstMaterialShader = "effects/flare",
            .materialShaders = {"effects/flare"},
        }};
        input.alpha = 0.0f;
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(decision.keepLayer, "flare layers remain visible");
        check(decision.keepEffects, "flare layers keep effects");
        check(decision.forceAlphaOne, "flare layers with alpha zero force alpha one");
        check(decision.reason == "essential-effect", "essential effect reason is parser-visible");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "effect",
            .firstMaterialShader = "effects/base",
            .materialShaders = {"effects/base", "effects/colorkey"},
        }};
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(decision.keepEffects, "colorkey in any material shader is preserved");
    }

    {
        auto input = baseEffectInput();
        input.layerName = "lens flare";
        input.effects = {{
            .name = "flare",
            .firstMaterialShader = "effects/base",
            .materialShaders = {"effects/base", "effects/lightshaft"},
        }};
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(decision.keepEffects, "heavy detection preserves current first-material-only behavior");
    }
}

void testVideoTexturePolicy()
{
    {
        const auto decision = wallpaper::policy::decideVideoTexturePolicy({
            .sourceSize = 200,
            .expectedRawSize = 400,
            .hasVideoMagic = false,
            .decodedWidth = 0,
        });
        check(!decision.shouldAttemptDecode, "small non-video raw mismatch does not decode");
        check(!decision.enablePlayback, "non-decoded texture never enables playback");
    }

    {
        const auto decision = wallpaper::policy::decideVideoTexturePolicy({
            .sourceSize = 9000000,
            .expectedRawSize = 400,
            .hasVideoMagic = true,
            .decodedWidth = 3840,
        });
        check(decision.shouldAttemptDecode, "video magic attempts decode");
        check(decision.enablePlayback, "decoded width >= 1920 enables playback");
    }

    {
        const auto decision = wallpaper::policy::decideVideoTexturePolicy({
            .sourceSize = 9000000,
            .expectedRawSize = 400,
            .hasVideoMagic = false,
            .decodedWidth = 1920,
        });
        check(decision.shouldAttemptDecode, "size mismatch without video magic attempts decode");
        check(decision.enablePlayback, "size mismatch video plays at exact width threshold");
    }

    {
        const auto decision = wallpaper::policy::decideVideoTexturePolicy({
            .sourceSize = 9000000,
            .expectedRawSize = 400,
            .hasVideoMagic = false,
            .decodedWidth = 1919,
        });
        check(decision.shouldAttemptDecode, "size mismatch decode trigger does not need video magic");
        check(!decision.enablePlayback, "decoded width just below threshold stays static");
    }

    {
        const auto decision = wallpaper::policy::decideVideoTexturePolicy({
            .sourceSize = 9000000,
            .expectedRawSize = 400,
            .hasVideoMagic = true,
            .decodedWidth = 1280,
        });
        check(decision.shouldAttemptDecode, "small video magic attempts decode");
        check(!decision.enablePlayback, "decoded width below 1920 stays static");
    }
}

void testModelFallbackPolicy()
{
    {
        const auto decision = wallpaper::policy::decideModelMaterialFallback({
            .sourceShader = "generic",
            .sourceBlending = "",
            .hasDiffuseTexture = true,
            .wantsLightmap = true,
            .wantsNormalmap = false,
            .wantsReflection = true,
        });
        check(decision.useAuthoredGenericMaterial, "generic model materials are preserved");
        check(decision.outputShader == "generic", "generic shader remains generic");
        check(decision.outputBlending == "disabled", "empty generic blending becomes disabled");
    }

    {
        const auto decision = wallpaper::policy::decideModelMaterialFallback({
            .sourceShader = "generic",
            .sourceBlending = "translucent",
            .hasDiffuseTexture = true,
        });
        check(decision.useAuthoredGenericMaterial, "generic model materials remain authored");
        check(decision.outputBlending == "translucent", "non-empty generic blending is preserved");
    }

    {
        const auto decision = wallpaper::policy::decideModelMaterialFallback({
            .sourceShader = "somecustomshader",
            .sourceBlending = "translucent",
            .hasDiffuseTexture = true,
        });
        check(!decision.useAuthoredGenericMaterial, "custom model materials use diffuse fallback");
        check(decision.outputShader == "genericimage", "custom fallback uses genericimage");
        check(decision.outputBlending == "disabled", "diffuse fallback forces opaque blending");
    }

    {
        const auto status = wallpaper::policy::describeModelFallbackSupport({
            .supportedDrawableObjectCount = 2,
            .modelObjectCount = 3,
            .firstFrameRendered = false,
        });
        check(status.kind == wallpaper::policy::ModelFallbackStatusKind::MixedSceneDetected,
              "mixed model scenes report detected fallback");
    }

    {
        const auto status = wallpaper::policy::describeModelFallbackSupport({
            .supportedDrawableObjectCount = 0,
            .modelObjectCount = 3,
            .firstFrameRendered = false,
        });
        check(status.kind == wallpaper::policy::ModelFallbackStatusKind::ModelOnlyDetected,
              "model-only scenes report model-only fallback");
    }

    {
        const auto status = wallpaper::policy::describeModelFallbackSupport({
            .supportedDrawableObjectCount = 2,
            .modelObjectCount = 3,
            .firstFrameRendered = true,
        });
        check(status.kind == wallpaper::policy::ModelFallbackStatusKind::FirstFrameRendered,
              "first frame status wins when model fallback rendered");
    }

    {
        const auto status = wallpaper::policy::describeModelFallbackSupport({
            .supportedDrawableObjectCount = 2,
            .modelObjectCount = 0,
            .firstFrameRendered = false,
        });
        check(status.kind == wallpaper::policy::ModelFallbackStatusKind::None,
              "scenes without model fallback keep no model status");
    }
}

void testSceneScriptRuntimePolicy()
{
    {
        std::string script = "'use strict';\nexport var value = 1;\nexport function update() { return value; }\nimport { x } from 'y';\n";
        const auto sanitized = wallpaper::policy::sanitizeSceneScriptModule(script);
        const std::string expected = "\nvar value = 1;\nfunction update() { return value; }\n/* import stripped */\n";
        check(sanitized == expected, "SceneScript sanitizer preserves exact legacy output");
        check(sanitized.find("export ") == std::string::npos, "export syntax is stripped");
        check(sanitized.find("import { x } from 'y';") == std::string::npos, "import lines are stripped");
        check(sanitized.find("/* import stripped */") != std::string::npos, "import lines use legacy stripped marker");
        check(sanitized.find("var value = 1") != std::string::npos, "export var becomes var");
        check(sanitized.find("function update()") != std::string::npos, "export function becomes function");
    }

    {
        const auto stubs = wallpaper::policy::sceneScriptRuntimeStubSource();
        check(stubs.find("function createScriptProperties()") != std::string::npos,
              "runtime stubs include createScriptProperties");
        check(stubs.find("function Vec3") != std::string::npos,
              "runtime stubs include Vec3");
        check(stubs.find("smoothStep") != std::string::npos,
              "runtime stubs include WEMath.smoothStep");
        check(stubs.find("lerp") != std::string::npos,
              "runtime stubs include WEMath.lerp");
        check(stubs.find("clamp") != std::string::npos,
              "runtime stubs include WEMath.clamp");
        check(stubs.find("engine.setTimeout") != std::string::npos,
              "runtime stubs include setTimeout");
        check(stubs.find("engine.runtime") != std::string::npos,
              "runtime stubs include runtime");
        check(stubs.find("engine.frametime") != std::string::npos,
              "runtime stubs include frametime");
        check(stubs.find("engine.registerAnimation") != std::string::npos,
              "runtime stubs include registerAnimation");
        check(stubs.find("engine.AUDIO_RESOLUTION_16") != std::string::npos,
              "runtime stubs include AUDIO_RESOLUTION_16");
        check(stubs.find("engine.AUDIO_RESOLUTION_32") != std::string::npos,
              "runtime stubs include AUDIO_RESOLUTION_32");
        check(stubs.find("engine.AUDIO_RESOLUTION_64") != std::string::npos,
              "runtime stubs include AUDIO_RESOLUTION_64");
        check(stubs.find("engine.registerAudioBuffers") != std::string::npos,
              "runtime stubs include registerAudioBuffers");
        check(stubs.find("cursorPosition") != std::string::npos,
              "runtime stubs include cursorPosition");
        check(stubs.find("cursorWorldPosition") != std::string::npos,
              "runtime stubs include cursorWorldPosition");
        check(stubs.find("var shared = {}") != std::string::npos,
              "runtime stubs include shared object");
    }
}

} // namespace

int main()
{
    testEffectPolicy();
    testVideoTexturePolicy();
    testModelFallbackPolicy();
    testSceneScriptRuntimePolicy();
    if (g_failures != 0) {
        return EXIT_FAILURE;
    }
    std::cout << "scene policy tests passed\n";
    return EXIT_SUCCESS;
}
