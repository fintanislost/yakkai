#include "Debug/EffectCaptureDebug.hpp"
#include "Policy/EffectPolicy.hpp"
#include "Policy/ModelFallbackPolicy.hpp"
#include "Policy/SceneScriptRuntimePolicy.hpp"
#include "Policy/VideoTexturePolicy.hpp"
#include "Scene/Scene.h"
#include "Scene/SceneImageEffectLayer.h"
#include "Scene/SceneMesh.h"
#include "Scene/SceneNode.h"
#include "SpecTexs.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

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

std::shared_ptr<wallpaper::SceneNode> effectNode()
{
    auto node = std::make_shared<wallpaper::SceneNode>();
    auto mesh = std::make_shared<wallpaper::SceneMesh>();
    mesh->AddMaterial(wallpaper::SceneMaterial {});
    node->AddMesh(mesh);
    return node;
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

bool containsString(const std::vector<std::string>& values, const std::string& expected)
{
    return std::find(values.begin(), values.end(), expected) != values.end();
}

void checkDecisionStableAfterClassification(const wallpaper::policy::LayerEffectInput& input,
                                            const std::string& label)
{
    const auto before = wallpaper::policy::decideLayerEffects(input);
    const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
    const auto after = wallpaper::policy::decideLayerEffects(input);

    check(before.keepLayer == after.keepLayer, label + " keepLayer is stable");
    check(before.keepEffects == after.keepEffects, label + " keepEffects is stable");
    check(before.forceAlphaOne == after.forceAlphaOne, label + " forceAlphaOne is stable");
    check(before.strippedEffects == after.strippedEffects, label + " strippedEffects is stable");
    check(before.reason == after.reason, label + " reason is stable");
    check(!classification.candidateRisk.empty(), label + " classification has risk");
}

void checkSimpleWaterPreserved(wallpaper::policy::LayerEffectInput input,
                               const std::string& label)
{
    const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
    const auto decision = wallpaper::policy::decideLayerEffects(input);

    check(classification.candidateRisk == "simple-water",
          label + " is classified as simple-water");
    check(decision.keepLayer,
          label + " keeps the layer");
    check(decision.keepEffects,
          label + " keeps effects");
    check(!decision.strippedEffects,
          label + " is not marked stripped");
    check(decision.reason == "simple-water-effect",
          label + " uses the simple-water policy reason");
}

void checkPuppetAlphaStripped(wallpaper::policy::LayerEffectInput input,
                              const std::string& expectedRisk,
                              const std::string& label)
{
    const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
    const auto decision = wallpaper::policy::decideLayerEffects(input);

    check(classification.candidateRisk == expectedRisk,
          label + " has expected classifier risk");
    check(!decision.keepEffects,
          label + " strips effects");
    check(decision.strippedEffects,
          label + " is marked stripped");
    check(decision.reason == "puppet-alpha-strip",
          label + " keeps puppet-alpha-strip reason");
}

void testEffectFinalOutputDebugPlaceholder()
{
    const std::string pingpongA = "_rt_effect_pingpong_a_test";
    const std::string pingpongB = "_rt_effect_pingpong_b_test";
    const std::string debugFinalPlaceholder =
        std::string(wallpaper::WE_DEBUG_EFFECT_FINAL_OUTPUT_PREFIX) + "test";

    wallpaper::SceneNode worldNode;
    wallpaper::SceneImageEffectLayer layer(&worldNode, 100.0f, 100.0f, pingpongA, pingpongB);
    layer.SetPublishFinalOutput(false);

    auto first = std::make_shared<wallpaper::SceneImageEffect>();
    first->nodes.push_back({ std::string(wallpaper::WE_EFFECT_PPONG_PREFIX_B), effectNode(), false });
    layer.AddEffect(first);

    auto second = std::make_shared<wallpaper::SceneImageEffect>();
    second->commands.push_back({ .cmd = wallpaper::SceneImageEffect::CmdType::Copy,
                                 .dst = "_rt_debug_effect_output_test",
                                 .src = debugFinalPlaceholder,
                                 .afterpos = 1 });
    second->commands.push_back({ .cmd = wallpaper::SceneImageEffect::CmdType::Copy,
                                 .dst = "_rt_debug_effect_input_test",
                                 .src = std::string(wallpaper::WE_EFFECT_PPONG_PREFIX_A) + "_test",
                                 .afterpos = 0 });
    second->nodes.push_back({ std::string(wallpaper::WE_EFFECT_PPONG_PREFIX_B), effectNode(), false });
    layer.AddEffect(second);

    wallpaper::SceneMesh defaultMesh;
    layer.ResolveEffect(defaultMesh, "effect");
    layer.ResolveEffect(defaultMesh, "effect");

    check(layer.GetEffect(1)->commands[0].src == pingpongA,
          "debug final-output placeholder resolves to final ping-pong target on even chains");
    check(layer.GetEffect(1)->commands[1].src == pingpongB,
          "previous-input placeholder still resolves to current ping-pong input");
}

void testEffectCandidateClassification()
{
    {
        auto input = baseEffectInput();
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(containsString(classification.candidateFamilies, "waterwaves"),
              "simple waterwaves candidate records waterwaves family");
        check(classification.candidateRisk == "simple-water",
              "simple waterwaves candidate is simple-water");
        check(classification.candidateBlockedReason == "water-effect-candidate",
              "simple waterwaves candidate records candidate reason");
        check(classification.candidateChecks.hasWaterFamily,
              "simple waterwaves candidate has water family");
        check(classification.candidateChecks.waterOnly,
              "simple waterwaves candidate is water-only");
        check(!classification.candidateChecks.isComposelayer,
              "simple waterwaves candidate is not composelayer");
        checkDecisionStableAfterClassification(input, "simple-water");
    }

    {
        auto input = baseEffectInput();
        input.effects.push_back({
            .name = "opacity",
            .visible = true,
            .firstMaterialShader = "effects/opacity",
            .materialShaders = {"effects/opacity"},
        });
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "mixed-chain",
              "water plus opacity is mixed-chain");
        check(classification.candidateBlockedReason == "water-effect-mixed-chain",
              "mixed-chain records mixed reason");
        check(classification.candidateChecks.hasWaterFamily,
              "mixed-chain keeps water-family signal");
        check(!classification.candidateChecks.waterOnly,
              "mixed-chain is not water-only");
        checkDecisionStableAfterClassification(input, "mixed-chain");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "waterflow",
            .visible = true,
            .firstMaterialShader = "effects/waterflow",
            .materialShaders = {"effects/waterflow"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(containsString(classification.candidateFamilies, "waterflow"),
              "waterflow candidate records waterflow family");
        check(classification.candidateRisk == "simple-water",
              "waterflow candidate is simple-water");
        checkDecisionStableAfterClassification(input, "waterflow");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "ui_editor_effect_water_flow_title",
            .visible = true,
            .firstMaterialShader = "effects/waterflow",
            .materialShaders = {"effects/waterflow"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "simple-water",
              "WE editor water-flow effect title with waterflow shader is simple-water");
        check(classification.candidateChecks.waterOnly,
              "WE editor water-flow effect title is water-only");
        checkDecisionStableAfterClassification(input, "we-editor-water-flow-title");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "ui_editor_effect_water_waves_title",
            .visible = true,
            .firstMaterialShader = "effects/waterwaves",
            .materialShaders = {"effects/waterwaves"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "simple-water",
              "WE editor water-waves effect title with waterwaves shader is simple-water");
        check(classification.candidateChecks.waterOnly,
              "WE editor water-waves effect title is water-only");
        checkDecisionStableAfterClassification(input, "we-editor-water-waves-title");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "waterwaves",
            .visible = true,
            .firstMaterialShader = "workshop/123/effects/waterwaves",
            .materialShaders = {"workshop/123/effects/waterwaves"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "simple-water",
              "workshop waterwaves path ending in water family is simple-water");
        check(classification.candidateChecks.waterOnly,
              "workshop waterwaves path ending in water family is water-only");
        checkDecisionStableAfterClassification(input, "workshop-waterwaves-path");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "waterwaves_custom",
            .visible = true,
            .firstMaterialShader = "effects/waterwaves_custom_unknown",
            .materialShaders = {"effects/waterwaves_custom_unknown"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "mixed-chain",
              "custom waterwaves-like fields are mixed-chain");
        check(!classification.candidateChecks.waterOnly,
              "custom waterwaves-like fields are not water-only");
        checkDecisionStableAfterClassification(input, "custom-waterwaves-like-fields");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "custom_waterwaves",
            .visible = true,
            .firstMaterialShader = "effects/custom_waterwaves",
            .materialShaders = {"effects/custom_waterwaves"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "mixed-chain",
              "custom_waterwaves prefix fields are mixed-chain");
        check(!classification.candidateChecks.waterOnly,
              "custom_waterwaves prefix fields are not water-only");
        checkDecisionStableAfterClassification(input, "custom-waterwaves-prefix");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "waterripple",
            .visible = true,
            .firstMaterialShader = "effects/waterripple",
            .materialShaders = {"effects/waterripple"},
        }};
        input.isComposelayer = true;
        input.imagePath = "models/util/composelayer.json";
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(containsString(classification.candidateFamilies, "waterripple"),
              "waterripple candidate records waterripple family");
        check(classification.candidateRisk == "composelayer-carrier",
              "composelayer carrier wins over utility-style path checks");
        check(classification.candidateChecks.isComposelayer,
              "composelayer check is true");
        checkDecisionStableAfterClassification(input, "composelayer-carrier");
    }

    {
        auto input = baseEffectInput();
        input.imagePath = "models/util/solidlayer.json";
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "utility-carrier",
              "solidlayer water candidate is utility-carrier");
        check(classification.candidateChecks.isUtilityCarrier,
              "utility carrier check is true");
        checkDecisionStableAfterClassification(input, "utility-carrier");
    }

    {
        auto input = baseEffectInput();
        input.fullscreen = true;
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "fullscreen-carrier",
              "fullscreen water candidate is fullscreen-carrier");
        check(classification.candidateChecks.isFullscreen,
              "fullscreen check is true");
        checkDecisionStableAfterClassification(input, "fullscreen-carrier");
    }

    {
        auto input = baseEffectInput();
        input.isPuppetLayer = true;
        input.layerName = "Character water layer";
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "puppet-layer",
              "generic puppet water layer is blocked before simple-water");
        check(classification.candidateBlockedReason == "puppet-layer",
              "generic puppet water layer records puppet-layer reason");
        check(classification.candidateChecks.isPuppetLayer,
              "generic puppet water layer records puppet-layer check");
        check(classification.candidateChecks.waterOnly,
              "generic puppet water layer can still be water-only");
        checkDecisionStableAfterClassification(input, "generic-puppet-water-layer");
    }

    {
        auto input = baseEffectInput();
        input.isPuppetLayer = true;
        input.layerName = "ARONA_CROP_SHEET";
        input.effects.push_back({
            .name = "pulse",
            .visible = true,
            .firstMaterialShader = "effects/pulse",
            .materialShaders = {"effects/pulse"},
        });
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "protected-puppet-path",
              "crop-sheet puppet candidate is protected");
        check(classification.candidateChecks.isProtectedPuppetPath,
              "protected puppet path check is true");
        checkDecisionStableAfterClassification(input, "protected-puppet-path");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "blur",
            .visible = true,
            .firstMaterialShader = "effects/blur_precise_gaussian",
            .materialShaders = {"effects/blur_precise_gaussian"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateFamilies.empty(),
              "non-water candidate has no water families");
        check(classification.candidateRisk == "non-water",
              "non-water candidate risk is non-water");
        check(classification.candidateBlockedReason == "no-water-effect-family",
              "non-water candidate records no-water reason");
        checkDecisionStableAfterClassification(input, "non-water");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "waterwaves",
            .visible = true,
            .firstMaterialShader = "effects/waterwaves",
            .materialShaders = {"effects/waterwaves", "effects/custom_unknown"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "mixed-chain",
              "unknown shader mixed with water blocks simple-water");
        check(!classification.candidateChecks.waterOnly,
              "unknown shader mixed with water is not water-only");
        checkDecisionStableAfterClassification(input, "unknown-water-mix");
    }
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
        checkSimpleWaterPreserved(input, "simple waterwaves");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "waterflow",
            .visible = true,
            .firstMaterialShader = "effects/waterflow",
            .materialShaders = {"effects/waterflow"},
        }};
        checkSimpleWaterPreserved(input, "simple waterflow");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "waterripple",
            .visible = true,
            .firstMaterialShader = "effects/waterripple",
            .materialShaders = {"effects/waterripple"},
        }};
        checkSimpleWaterPreserved(input, "simple waterripple");
    }

    {
        auto input = baseEffectInput();
        input.effects.push_back({
            .name = "opacity",
            .visible = true,
            .firstMaterialShader = "effects/opacity",
            .materialShaders = {"effects/opacity"},
        });
        checkPuppetAlphaStripped(input, "mixed-chain", "water plus opacity");
    }

    {
        auto input = baseEffectInput();
        input.isPuppetLayer = true;
        input.layerName = "Character water layer";
        checkPuppetAlphaStripped(input, "puppet-layer", "generic puppet water layer");
    }

    {
        auto input = baseEffectInput();
        input.isPuppetLayer = true;
        input.layerName = "ARONA_CROP_SHEET";
        input.effects.push_back({
            .name = "pulse",
            .visible = true,
            .firstMaterialShader = "effects/pulse",
            .materialShaders = {"effects/pulse"},
        });
        checkPuppetAlphaStripped(input, "protected-puppet-path", "protected crop-sheet water layer");
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

void testEffectCaptureDebug()
{
    check(wallpaper::debug::sanitizeCapturePathSegment("ARONA/CROP: SHEET") == "ARONA_CROP_SHEET",
          "capture path segments replace separators and whitespace");
    check(wallpaper::debug::sanitizeCapturePathSegment("  flare layer  ") == "flare_layer",
          "capture path segments trim outer whitespace");
    check(wallpaper::debug::sanitizeCapturePathSegment("") == "unnamed",
          "empty capture path segments use unnamed");

    {
        const wallpaper::debug::EffectCaptureConfig config;
        check(!config.enabled(), "empty effect capture config disables captures");
    }

    {
        const wallpaper::debug::EffectCaptureConfig config {
            .outputDir = "/tmp/yakkai-effect-debug",
            .commandLine = "yakkai_scene_harness --debug-effect-captures /tmp/yakkai-effect-debug",
        };
        check(config.enabled(), "effect capture config with output directory enables captures");
        check(config.manifestPath().string() == "/tmp/yakkai-effect-debug/manifest.json",
              "effect capture manifest path is under the output directory");
    }

    {
        wallpaper::Scene scene;
        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.sceneId = "unit-scene";
        layer.layerName = "disabled candidate";

        wallpaper::debug::recordStrippedEffectCandidate(scene, layer);

        check(scene.debugEffectStrippedCandidates.empty(),
              "disabled effect capture config ignores stripped candidates");
    }

    {
        const auto outDir =
            std::filesystem::temp_directory_path() / "yakkai-stripped-candidate-policy-test";
        std::filesystem::remove_all(outDir);

        wallpaper::Scene scene;
        scene.scene_id = "unit-scene";
        scene.debugEffectCaptures = {
            .outputDir = outDir.string(),
            .commandLine = "unit --debug-effect-captures " + outDir.string(),
        };

        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.sceneId = "unit-scene";
        layer.sceneType = "Puppet";
        layer.layerName = "Water background";
        layer.layerImage = "materials/water.png";
        layer.layerId = 42;
        layer.visibleEffectCount = 2;
        layer.alpha = 0.75f;
        layer.keepLayer = true;
        layer.keepEffects = false;
        layer.strippedEffects = true;
        layer.policyReason = "puppet-alpha-strip";
        layer.effectNames = {"waterwaves", "opacity"};
        layer.materialShaders = {"effects/waterwaves", "effects/opacity"};
        layer.candidateFamilies = {"waterwaves"};
        layer.candidateRisk = "mixed-chain";
        layer.candidateBlockedReason = "water-effect-mixed-chain";
        layer.candidateChecks.hasWaterFamily = true;
        layer.candidateChecks.waterOnly = false;
        layer.candidateChecks.isUtilityCarrier = false;
        layer.candidateChecks.isComposelayer = false;
        layer.candidateChecks.isFullscreen = false;
        layer.candidateChecks.isPuppetLayer = false;
        layer.candidateChecks.isProtectedPuppetPath = false;

        wallpaper::debug::recordStrippedEffectCandidate(scene, layer);

        check(scene.debugEffectStrippedCandidates.size() == 1,
              "stripped candidate is stored separately from capture records");
        check(scene.debugEffectCaptureRecords.empty(),
              "stripped candidate does not create dump capture records");
        check(wallpaper::debug::writeEffectCaptureManifest(scene),
              "manifest writes stripped candidates");

        const std::string manifest = readTextFile(scene.debugEffectCaptures.manifestPath());
        check(manifest.find("\"status\": \"ok\"") != std::string::npos,
              "stripped candidates do not fail manifest status");
        check(manifest.find("\"strippedCandidates\"") != std::string::npos,
              "manifest includes strippedCandidates array");
        check(manifest.find("\"layerName\": \"Water background\"") != std::string::npos,
              "manifest includes stripped candidate layer name");
        check(manifest.find("\"reason\": \"puppet-alpha-strip\"") != std::string::npos,
              "manifest includes stripped candidate policy reason");
        check(manifest.find("\"candidateFamilies\"") != std::string::npos,
              "manifest includes stripped candidate families");
        check(manifest.find("\"candidateRisk\": \"mixed-chain\"") != std::string::npos,
              "manifest includes stripped candidate risk");
        check(manifest.find("\"candidateBlockedReason\": \"water-effect-mixed-chain\"") != std::string::npos,
              "manifest includes stripped candidate blocked reason");
        check(manifest.find("\"candidateChecks\"") != std::string::npos,
              "manifest includes stripped candidate checks");
        check(manifest.find("\"hasWaterFamily\": true") != std::string::npos,
              "manifest includes stripped candidate water-family check");
        check(manifest.find("\"waterOnly\": false") != std::string::npos,
              "manifest includes stripped candidate water-only check");
        check(manifest.find("\"isUtilityCarrier\": false") != std::string::npos,
              "manifest includes stripped candidate utility-carrier check");
        check(manifest.find("\"isComposelayer\": false") != std::string::npos,
              "manifest includes stripped candidate composelayer check");
        check(manifest.find("\"isFullscreen\": false") != std::string::npos,
              "manifest includes stripped candidate fullscreen check");
        check(manifest.find("\"isPuppetLayer\": false") != std::string::npos,
              "manifest includes stripped candidate puppet-layer check");
        check(manifest.find("\"isProtectedPuppetPath\": false") != std::string::npos,
              "manifest includes stripped candidate protected-puppet-path check");

        std::filesystem::remove_all(outDir);
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
    testEffectCaptureDebug();
    testEffectFinalOutputDebugPlaceholder();
    testEffectCandidateClassification();
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
