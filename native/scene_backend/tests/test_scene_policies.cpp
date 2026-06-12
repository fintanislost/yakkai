#include "Debug/EffectCaptureDebug.hpp"
#include "Policy/EffectPolicy.hpp"
#include "Policy/ModelFallbackPolicy.hpp"
#include "Policy/SceneScriptRuntimePolicy.hpp"
#include "Policy/VideoTexturePolicy.hpp"
#include "Scene/PuppetEffectRoutePlan.hpp"
#include "Scene/PuppetFinalDisplayBuilder.hpp"
#include "Scene/Scene.h"
#include "Scene/SceneImageEffectLayer.h"
#include "Scene/SceneMesh.h"
#include "Scene/SceneNode.h"
#include "Puppet/PuppetSimulation.hpp"
#include "Shader/ShaderCompatPatches.hpp"
#include "SpecTexs.hpp"
#include "VulkanRender/CustomShaderPass.hpp"
#include "VulkanRender/CopyPass.hpp"
#include "VulkanRender/FinPass.hpp"
#include "VulkanRender/PassCommon.hpp"
#include "WPMdlParser.hpp"
#include "WPSceneParser.hpp"
#include "WPSceneScript.hpp"
#include "WPShaderParser.hpp"
#include "WPTexImageParser.hpp"
#include "Audio/SoundManager.h"
#include "Fs/PhysicalFs.h"
#include "Fs/VFS.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

bool equalsStrings(const std::vector<std::string>& actual, const std::vector<std::string>& expected)
{
    return actual == expected;
}

bool equalsInts(const std::vector<int>& actual, const std::vector<int>& expected)
{
    return actual == expected;
}

std::array<float, 2> readTexCoord(const wallpaper::SceneVertexArray& vertex,
                                  size_t vertexIndex)
{
    const auto attrs = vertex.GetAttrOffsetMap();
    const auto texcoordIt = attrs.find(std::string(wallpaper::WE_IN_TEXCOORD));
    check(texcoordIt != attrs.end(), "mesh has texture coordinate attribute");
    if (texcoordIt == attrs.end()) {
        return {0.0f, 0.0f};
    }

    const size_t offsetFloats = texcoordIt->second.offset / sizeof(float);
    const float* data = vertex.Data() + vertexIndex * vertex.OneSize() + offsetFloats;
    return {data[0], data[1]};
}

bool nearFloat(float actual, float expected)
{
    return std::abs(actual - expected) < 1.0e-6f;
}

wallpaper::SceneNode* findNodeByTranslate(wallpaper::SceneNode* node,
                                          const std::array<float, 3>& translate)
{
    if (!node) {
        return nullptr;
    }
    const auto& nodeTranslate = node->Translate();
    if (nearFloat(nodeTranslate.x(), translate[0]) &&
        nearFloat(nodeTranslate.y(), translate[1]) &&
        nearFloat(nodeTranslate.z(), translate[2])) {
        return node;
    }
    for (const auto& child : node->GetChildren()) {
        if (auto* match = findNodeByTranslate(child.get(), translate)) {
            return match;
        }
    }
    return nullptr;
}

void appendI32(std::vector<uint8_t>& bytes, int32_t value)
{
    uint8_t raw[sizeof(value)];
    std::memcpy(raw, &value, sizeof(value));
    bytes.insert(bytes.end(), std::begin(raw), std::end(raw));
}

void appendF32(std::vector<uint8_t>& bytes, float value)
{
    uint8_t raw[sizeof(value)];
    std::memcpy(raw, &value, sizeof(value));
    bytes.insert(bytes.end(), std::begin(raw), std::end(raw));
}

void appendTexVersion(std::vector<uint8_t>& bytes, const char* prefix, int version)
{
    char raw[9] {};
    std::snprintf(raw, sizeof(raw), "%.4s%.4d", prefix, version);
    bytes.insert(bytes.end(), raw, raw + sizeof(raw));
}

std::vector<uint8_t> makeTexbV4SpriteFixture()
{
    std::vector<uint8_t> bytes;
    appendTexVersion(bytes, "TEXV", 5);
    appendTexVersion(bytes, "TEXI", 1);
    appendI32(bytes, 9); // R8
    appendI32(bytes, 1 << 2); // sprite
    appendI32(bytes, 1024);
    appendI32(bytes, 1024);
    appendI32(bytes, 4);
    appendI32(bytes, 4);
    appendI32(bytes, static_cast<int32_t>(0xff000000u));
    appendTexVersion(bytes, "TEXB", 4);
    appendI32(bytes, 1); // one image slot

    // TEXB v4 flat image record. The old ParseHeader() path treated this as
    // a v1-v3 mipmap loop. With width=height=1024 and lz4=1, the misaligned
    // first frame id became 0x01000004.
    appendI32(bytes, -1);
    appendI32(bytes, 0);
    appendI32(bytes, 1);
    appendI32(bytes, 1024);
    appendI32(bytes, 1024);
    appendI32(bytes, 1);
    appendI32(bytes, 4096);
    appendI32(bytes, 4);
    bytes.insert(bytes.end(), {0xff, 0xff, 0xff, 0xff});

    appendTexVersion(bytes, "TEXS", 3);
    appendI32(bytes, 1); // one sprite frame
    appendI32(bytes, 204);
    appendI32(bytes, 102);
    appendI32(bytes, 0); // image slot id
    appendF32(bytes, 0.02f);
    appendF32(bytes, 0.0f);
    appendF32(bytes, 0.0f);
    appendF32(bytes, 204.0f);
    appendF32(bytes, 0.0f);
    appendF32(bytes, 0.0f);
    appendF32(bytes, 102.0f);
    return bytes;
}

void checkDecisionStableAfterClassification(const wallpaper::policy::LayerEffectInput& input,
                                            const std::string& label)
{
    const auto before = wallpaper::policy::decideLayerEffects(input);
    const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
    const auto after = wallpaper::policy::decideLayerEffects(input);

    check(before.keepLayer == after.keepLayer, label + " keepLayer is stable");
    check(before.keepEffects == after.keepEffects, label + " keepEffects is stable");
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

void testEffectFinalOutputDebugPlaceholderTracksPublishedOutput()
{
    const std::string pingpongA = "_rt_effect_pingpong_a_publish";
    const std::string pingpongB = "_rt_effect_pingpong_b_publish";
    const std::string debugFinalPlaceholder =
        std::string(wallpaper::WE_DEBUG_EFFECT_FINAL_OUTPUT_PREFIX) + "publish";

    wallpaper::SceneNode worldNode;
    wallpaper::SceneImageEffectLayer layer(&worldNode, 100.0f, 100.0f, pingpongA, pingpongB);

    auto effect = std::make_shared<wallpaper::SceneImageEffect>();
    effect->commands.push_back({ .cmd = wallpaper::SceneImageEffect::CmdType::Copy,
                                 .dst = "_rt_debug_material_output_publish",
                                 .src = debugFinalPlaceholder,
                                 .afterpos = 1,
                                 .srcFinalEffectOutput = true });
    effect->nodes.push_back({ std::string(wallpaper::WE_EFFECT_PPONG_PREFIX_B), effectNode(), false });
    layer.AddEffect(effect);

    wallpaper::SceneMesh defaultMesh;
    layer.ResolveEffect(defaultMesh, "effect");

    check(layer.GetEffect(0)->nodes.front().output == std::string(wallpaper::SpecTex_Default),
          "published final effect node writes SpecTex_Default");
    check(layer.GetEffect(0)->commands[0].src == std::string(wallpaper::SpecTex_Default),
          "debug final-output placeholder tracks published SpecTex_Default target");
}

void testEffectFinalOutputDebugPlaceholderOnlyTracksFinalNode()
{
    const std::string pingpongA = "_rt_effect_pingpong_a_publish_partial";
    const std::string pingpongB = "_rt_effect_pingpong_b_publish_partial";
    const std::string debugFinalPlaceholder =
        std::string(wallpaper::WE_DEBUG_EFFECT_FINAL_OUTPUT_PREFIX) + "publish_partial";

    wallpaper::SceneNode worldNode;
    wallpaper::SceneImageEffectLayer layer(&worldNode, 100.0f, 100.0f, pingpongA, pingpongB);

    auto effect = std::make_shared<wallpaper::SceneImageEffect>();
    effect->commands.push_back({ .cmd = wallpaper::SceneImageEffect::CmdType::Copy,
                                 .dst = "_rt_debug_material_output_intermediate",
                                 .src = debugFinalPlaceholder,
                                 .afterpos = 1,
                                 .srcFinalEffectOutput = true });
    effect->commands.push_back({ .cmd = wallpaper::SceneImageEffect::CmdType::Copy,
                                 .dst = "_rt_debug_material_output_final",
                                 .src = debugFinalPlaceholder,
                                 .afterpos = 2,
                                 .srcFinalEffectOutput = true });
    effect->nodes.push_back({ std::string(wallpaper::WE_EFFECT_PPONG_PREFIX_B), effectNode(), false });
    effect->nodes.push_back({ std::string(wallpaper::WE_EFFECT_PPONG_PREFIX_B), effectNode(), false });
    layer.AddEffect(effect);

    wallpaper::SceneMesh defaultMesh;
    layer.ResolveEffect(defaultMesh, "effect");

    auto nodeIt = layer.GetEffect(0)->nodes.begin();
    check(nodeIt->output == pingpongB,
          "intermediate final-output node stays on current ping-pong target");
    ++nodeIt;
    check(nodeIt->output == std::string(wallpaper::SpecTex_Default),
          "final final-output node writes SpecTex_Default target");
    check(layer.GetEffect(0)->commands[0].src == pingpongB,
          "intermediate final-output placeholder stays on current ping-pong target");
    check(layer.GetEffect(0)->commands[1].src == std::string(wallpaper::SpecTex_Default),
          "final final-output placeholder tracks published SpecTex_Default target");
}

void testCopyPassExtentClampsToOverlappingRegion()
{
    const VkExtent3D largeSource { 3840, 2160, 1 };
    const VkExtent3D smallDestination { 256, 144, 1 };
    const auto clamped = wallpaper::vulkan::chooseCopyPassExtent(largeSource, smallDestination);

    check(clamped.clamped, "copy pass extent reports clamped mismatched copy");
    check(clamped.extent.width == 256, "copy pass extent clamps width to destination");
    check(clamped.extent.height == 144, "copy pass extent clamps height to destination");
    check(clamped.extent.depth == 1, "copy pass extent preserves overlapping depth");

    const VkExtent3D sameSize { 640, 360, 1 };
    const auto unclamped = wallpaper::vulkan::chooseCopyPassExtent(sameSize, sameSize);
    check(!unclamped.clamped, "copy pass extent does not clamp equal extents");
    check(unclamped.extent.width == sameSize.width && unclamped.extent.height == sameSize.height,
          "copy pass extent preserves equal extents");
}

void testCustomShaderPassSynchronizesPreviousTargetWrites()
{
    const auto dependency = wallpaper::vulkan::customShaderPassExternalDependency(false);
    check((dependency.srcStageMask & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) != 0,
          "custom shader pass waits for prior color-attachment writes");
    check((dependency.srcStageMask & VK_PIPELINE_STAGE_TRANSFER_BIT) != 0,
          "custom shader pass waits for prior transfer writes");
    check((dependency.dstStageMask & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) != 0,
          "custom shader pass dependency covers shader sampling");
    check((dependency.srcAccessMask & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) != 0,
          "custom shader pass makes prior color writes available");
    check((dependency.srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) != 0,
          "custom shader pass makes prior transfer writes available");
    check((dependency.dstAccessMask & VK_ACCESS_SHADER_READ_BIT) != 0,
          "custom shader pass makes writes visible to shader reads");

    const VkImageSubresourceRange range {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    const auto barrier = wallpaper::vulkan::customShaderPassTextureReadBarrier({}, range);
    check((barrier.srcAccessMask & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) != 0,
          "custom shader texture-read barrier includes prior color writes");
    check((barrier.srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) != 0,
          "custom shader texture-read barrier includes prior transfer writes");
    check((barrier.dstAccessMask & VK_ACCESS_SHADER_READ_BIT) != 0,
          "custom shader texture-read barrier publishes to shader reads");
}

void testFinPassSynchronizesFinalRenderTargetRead()
{
    const VkImageSubresourceRange range {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    const auto sync = wallpaper::vulkan::finPassResultTextureReadBarrier({}, range);
    check((sync.srcStageMask & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) != 0,
          "fin pass waits for previous color-attachment writes");
    check((sync.srcStageMask & VK_PIPELINE_STAGE_TRANSFER_BIT) != 0,
          "fin pass waits for previous transfer writes");
    check((sync.dstStageMask & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) != 0,
          "fin pass publishes result texture to fragment sampling");
    check((sync.barrier.srcAccessMask & VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) != 0,
          "fin pass result barrier includes prior color writes");
    check((sync.barrier.srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT) != 0,
          "fin pass result barrier includes prior transfer writes");
    check((sync.barrier.dstAccessMask & VK_ACCESS_SHADER_READ_BIT) != 0,
          "fin pass result barrier makes final target visible to shader reads");
}

void testEffectSourceUsesLocalSpaceWhileFinalOutputKeepsParent()
{
    const std::string pingpongA = "_rt_effect_pingpong_a_parented";
    const std::string pingpongB = "_rt_effect_pingpong_b_parented";

    auto parent = std::make_shared<wallpaper::SceneNode>(
        Eigen::Vector3f { 100.0f, 50.0f, 0.0f },
        Eigen::Vector3f { 1.0f, 1.0f, 1.0f },
        Eigen::Vector3f::Zero());
    auto worldNode = std::make_shared<wallpaper::SceneNode>();
    parent->AppendChild(worldNode);

    wallpaper::SceneImageEffectLayer layer(worldNode.get(), 100.0f, 100.0f, pingpongA, pingpongB);
    layer.SetFinalBlend(wallpaper::BlendMode::Normal);
    auto effect = std::make_shared<wallpaper::SceneImageEffect>();
    auto outputNode = effectNode();
    effect->nodes.push_back({ std::string(wallpaper::WE_EFFECT_PPONG_PREFIX_B), outputNode, false });
    layer.AddEffect(effect);

    wallpaper::SceneMesh defaultMesh;
    layer.ResolveEffect(defaultMesh, "effect");

    check(worldNode->Parent() == nullptr,
          "effect source node is detached from scene parent for local offscreen render");
    check(outputNode->Parent() == parent.get(),
          "effect final output keeps the original scene parent");
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
        check(classification.candidateMixFamilies.empty(),
              "simple waterwaves candidate has no mix families");
        check(classification.candidateChainShape == "simple-water",
              "simple waterwaves candidate reports simple-water chain shape");
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
        check(containsString(classification.candidateMixFamilies, "opacity"),
              "water plus opacity records opacity mix family");
        check(classification.candidateChainShape == "water+opacity",
              "water plus opacity reports water+opacity chain shape");
        check(classification.candidateChecks.hasWaterFamily,
              "mixed-chain keeps water-family signal");
        check(!classification.candidateChecks.waterOnly,
              "mixed-chain is not water-only");
        checkDecisionStableAfterClassification(input, "mixed-chain");
    }

    {
        auto input = baseEffectInput();
        input.effects.push_back({
            .name = "shine",
            .visible = true,
            .firstMaterialShader = "effects/shine",
            .materialShaders = {"effects/shine"},
        });
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(containsString(classification.candidateMixFamilies, "shine"),
              "water plus shine records shine mix family");
        check(classification.candidateChainShape == "water+shine",
              "water plus shine reports water+shine chain shape");
        check(classification.candidateRisk == "mixed-chain",
              "water plus shine remains mixed-chain risk");
        checkDecisionStableAfterClassification(input, "water-plus-shine");
    }

    {
        auto input = baseEffectInput();
        input.effects.push_back({
            .name = "iris",
            .visible = true,
            .firstMaterialShader = "effects/iris",
            .materialShaders = {"effects/iris"},
        });
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(containsString(classification.candidateMixFamilies, "iris"),
              "water plus iris records iris mix family");
        check(classification.candidateChainShape == "water+iris",
              "water plus iris reports water+iris chain shape");
        check(classification.candidateRisk == "mixed-chain",
              "water plus iris remains mixed-chain risk");
        checkDecisionStableAfterClassification(input, "water-plus-iris");
    }

    {
        auto input = baseEffectInput();
        input.effects.push_back({
            .name = "opacity",
            .visible = true,
            .firstMaterialShader = "effects/opacity",
            .materialShaders = {"effects/opacity"},
        });
        input.effects.push_back({
            .name = "shine",
            .visible = true,
            .firstMaterialShader = "effects/shine",
            .materialShaders = {"effects/shine"},
        });
        input.effects.push_back({
            .name = "iris",
            .visible = true,
            .firstMaterialShader = "effects/iris",
            .materialShaders = {"effects/iris"},
        });
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(equalsStrings(classification.candidateMixFamilies, {"opacity", "shine", "iris"}),
              "water plus opacity shine iris records deterministic mix families");
        check(classification.candidateChainShape == "water+opacity+shine+iris",
              "water plus opacity shine iris reports combined chain shape");
        check(classification.candidateRisk == "mixed-chain",
              "water plus opacity shine iris remains mixed-chain risk");
        checkDecisionStableAfterClassification(input, "water-plus-opacity-shine-iris");
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
        check(classification.candidateChainShape == "water-composelayer",
              "water composelayer reports a stable water carrier shape");
        check(classification.candidateEffectClass == "composelayer-water-only",
              "water composelayer reports a stable water carrier class");
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
        check(classification.candidateChainShape == "water-utility",
              "solidlayer water candidate reports stable water utility shape");
        check(classification.candidateEffectClass == "utility-water-only",
              "solidlayer water candidate reports stable water utility class");
        check(classification.candidateChecks.isUtilityCarrier,
              "utility carrier check is true");
        checkDecisionStableAfterClassification(input, "utility-carrier");
    }

    {
        auto input = baseEffectInput();
        input.imagePath = "models/util/solidlayer.json";
        input.effects.push_back({
            .name = "audio",
            .visible = true,
            .firstMaterialShader = "effects/audio",
            .materialShaders = {"effects/audio"},
        });
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "utility-carrier",
              "audio utility keeps utility-carrier risk");
        check(containsString(classification.candidateMixFamilies, "audio"),
              "audio utility records audio mix family");
        check(classification.candidateChainShape == "audio-utility",
              "audio utility reports audio-utility chain shape");
        checkDecisionStableAfterClassification(input, "audio-utility");
    }

    {
        auto input = baseEffectInput();
        input.imagePath = "models/util/solidlayer.json";
        input.effects.push_back({
            .name = "blur",
            .visible = true,
            .firstMaterialShader = "effects/blur",
            .materialShaders = {"effects/blur"},
        });
        input.effects.push_back({
            .name = "lut",
            .visible = true,
            .firstMaterialShader = "effects/lut",
            .materialShaders = {"effects/lut"},
        });
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "utility-carrier",
              "blur LUT utility keeps utility-carrier risk");
        check(equalsStrings(classification.candidateMixFamilies, {"blur", "lut"}),
              "blur LUT utility records deterministic mix families");
        check(classification.candidateChainShape == "blur-lut-utility",
              "blur LUT utility reports blur-lut-utility chain shape");
        checkDecisionStableAfterClassification(input, "blur-lut-utility");
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
        check(classification.candidateChainShape == "puppet-mixed",
              "generic puppet water layer reports puppet-mixed chain shape");
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
        check(containsString(classification.candidateMixFamilies, "pulse"),
              "crop-sheet puppet candidate records pulse mix family");
        check(classification.candidateChainShape == "protected-puppet-mixed",
              "crop-sheet puppet candidate reports protected-puppet-mixed chain shape");
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
        check(containsString(classification.candidateMixFamilies, "blur"),
              "non-water candidate records known non-water mix family");
        check(classification.candidateChainShape == "blur-only",
              "non-water blur candidate reports blur-only chain shape");
        check(classification.candidateEffectClass == "regular-blur-only",
              "non-carrier blur-only candidate reports stable blur class");
        check(classification.candidateChecks.hasBlurFamily,
              "non-water blur candidate records blur-family check");
        check(!classification.candidateChecks.hasLutFamily,
              "non-water blur candidate does not record LUT-family check");
        check(!classification.candidateChecks.hasColorGradingFamily,
              "non-water blur candidate does not record color-grade-family check");
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(decision.keepEffects,
              "non-carrier blur-only keeps effects under the narrow predicate");
        check(!decision.strippedEffects,
              "non-carrier blur-only is not marked stripped");
        check(decision.reason == "regular-blur-only-effect",
              "non-carrier blur-only uses the regular blur-only policy reason");
        checkDecisionStableAfterClassification(input, "non-water");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "lut loader",
            .visible = true,
            .firstMaterialShader = "workshop/3165346237/effects/lut_loader",
            .materialShaders = {"workshop/3165346237/effects/lut_loader"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "non-water",
              "non-water LUT candidate risk is non-water");
        check(equalsStrings(classification.candidateMixFamilies, {"lut"}),
              "non-water LUT candidate records canonical LUT mix family");
        check(classification.candidateChainShape == "lut-only",
              "non-water LUT candidate reports lut-only chain shape");
        check(classification.candidateEffectClass == "regular-lut-only",
              "regular image-layer LUT candidate reports stable LUT class");
        check(classification.candidateChecks.hasLutFamily,
              "non-water LUT candidate records LUT-family check");
        checkDecisionStableAfterClassification(input, "non-water-lut");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "color grading",
            .visible = true,
            .firstMaterialShader = "workshop/2795521260/effects/color_grading",
            .materialShaders = {"workshop/2795521260/effects/color_grading"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "non-water",
              "non-water color-grade candidate risk is non-water");
        check(equalsStrings(classification.candidateMixFamilies, {"color-grade"}),
              "non-water color-grade candidate records canonical color-grade mix family");
        check(classification.candidateChainShape == "color-grade-only",
              "non-water color-grade candidate reports color-grade-only chain shape");
        check(classification.candidateEffectClass == "regular-color-grade-only",
              "regular image-layer color-grade candidate reports stable color class");
        check(classification.candidateChecks.hasColorGradingFamily,
              "non-water color-grade candidate records color-grade-family check");
        checkDecisionStableAfterClassification(input, "non-water-color-grade");
    }

    {
        auto input = baseEffectInput();
        input.fullscreen = true;
        input.effects = {{
            .name = "blur",
            .visible = true,
            .firstMaterialShader = "effects/blur_precise_gaussian",
            .materialShaders = {"effects/blur_precise_gaussian"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "non-water",
              "non-water fullscreen blur keeps current risk");
        check(classification.candidateChecks.isFullscreen,
              "non-water fullscreen blur records fullscreen check");
        check(classification.candidateChainShape == "blur-fullscreen",
              "non-water fullscreen blur reports carrier-aware chain shape");
        checkDecisionStableAfterClassification(input, "non-water-fullscreen-blur");
    }

    {
        auto input = baseEffectInput();
        input.isComposelayer = true;
        input.imagePath = "models/util/composelayer.json";
        input.effects = {{
            .name = "blur",
            .visible = true,
            .firstMaterialShader = "effects/blur_precise_gaussian",
            .materialShaders = {"effects/blur_precise_gaussian"},
        }, {
            .name = "color grading",
            .visible = true,
            .firstMaterialShader = "workshop/2795521260/effects/color_grading",
            .materialShaders = {"workshop/2795521260/effects/color_grading"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        check(classification.candidateRisk == "non-water",
              "non-water composelayer blur color-grade keeps current risk");
        check(equalsStrings(classification.candidateMixFamilies, {"blur", "color-grade"}),
              "non-water composelayer records blur and color-grade families");
        check(classification.candidateChecks.isComposelayer,
              "non-water composelayer records composelayer check");
        check(classification.candidateChainShape == "blur-color-grade-composelayer",
              "non-water composelayer reports blur color-grade carrier-aware shape");
        check(classification.candidateEffectClass == "composelayer-color-grade",
              "composelayer color-grade candidate reports stable color class");
        checkDecisionStableAfterClassification(input, "non-water-blur-color-grade-composelayer");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "LUT Loader",
            .visible = true,
            .firstMaterialShader = "workshop/3165346237/effects/lut_loader",
            .materialShaders = {"workshop/3165346237/effects/lut_loader"},
        }};
        input.isPuppetLayer = true;
        input.layerName = "ARONA_CROP_SHEET";
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateEffectClass == "protected-puppet-lut",
              "protected crop-sheet LUT candidate reports protected-puppet-lut class");
        check(classification.candidateChainShape == "protected-puppet-mixed",
              "protected crop-sheet LUT candidate keeps protected chain shape");
        check(decision.keepEffects,
              "protected crop-sheet LUT keeps effects after route validation");
        check(!decision.strippedEffects,
              "protected crop-sheet LUT is not marked stripped after route validation");
        check(decision.reason == "protected-puppet-effect",
              "protected crop-sheet LUT uses generic protected puppet policy reason");
    }

    {
        auto input = baseEffectInput();
        input.effects.push_back({
            .name = "pulse",
            .visible = true,
            .firstMaterialShader = "effects/pulse",
            .materialShaders = {"effects/pulse"},
        });
        input.effects.push_back({
            .name = "shake",
            .visible = true,
            .firstMaterialShader = "effects/puppet_shake",
            .materialShaders = {"effects/puppet_shake"},
        });
        input.isPuppetLayer = true;
        input.layerName = "ARONA_CROP_SHEET";
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateRisk == "protected-puppet-path",
              "protected crop-sheet water pulse layer is still classified as protected");
        check(classification.candidateEffectClass == "none",
              "protected crop-sheet water pulse layer has no high-risk effect class");
        check(decision.keepEffects,
              "protected crop-sheet water pulse layer keeps effects after route validation");
        check(!decision.strippedEffects,
              "protected crop-sheet water pulse layer is not marked stripped after route validation");
        check(decision.reason == "protected-puppet-effect",
              "protected crop-sheet water pulse layer uses generic protected puppet policy reason");
    }

    {
        auto input = baseEffectInput();
        input.effects.push_back({
            .name = "audio ripple",
            .visible = true,
            .firstMaterialShader = "effects/audio_response",
            .materialShaders = {"effects/audio_response"},
        });
        input.isPuppetLayer = true;
        input.layerName = "ARONA_CROP_SHEET";
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(!decision.keepEffects,
              "protected crop-sheet audio mix remains stripped");
        check(decision.reason == "puppet-alpha-strip",
              "protected crop-sheet audio mix keeps strip reason");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "Blur",
            .visible = true,
            .firstMaterialShader = "effects/blur_precise_gaussian",
            .materialShaders = {"effects/blur_precise_gaussian"},
        }};
        input.isPuppetLayer = true;
        input.layerName = "ARONA_CROP_SHEET";
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateEffectClass == "protected-puppet-blur",
              "protected crop-sheet blur candidate reports protected-puppet-blur class");
        check(!decision.keepEffects,
              "protected crop-sheet blur remains stripped");
        check(decision.reason == "puppet-alpha-strip",
              "protected crop-sheet blur keeps current strip reason");
    }

    {
        auto input = baseEffectInput();
        input.effects = {{
            .name = "LUT Loader",
            .visible = true,
            .firstMaterialShader = "workshop/3165346237/effects/lut_loader",
            .materialShaders = {"workshop/3165346237/effects/lut_loader"},
        }};
        input.isPuppetLayer = true;
        input.layerName = "Character LUT";
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateEffectClass == "mixed-puppet-lut",
              "generic puppet LUT candidate reports mixed-puppet-lut class");
        check(classification.candidateChainShape == "puppet-mixed",
              "generic puppet LUT candidate keeps puppet-mixed chain shape");
        check(!decision.keepEffects,
              "generic puppet LUT remains stripped");
        check(decision.reason == "puppet-alpha-strip",
              "generic puppet LUT keeps current strip reason");
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
        check(classification.candidateChainShape == "unknown-mixed",
              "unknown shader mixed with water reports unknown-mixed chain shape");
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
        input.effects = {{
            .name = "LUT Loader",
            .visible = true,
            .firstMaterialShader = "workshop/3165346237/effects/lut_loader",
            .materialShaders = {"workshop/3165346237/effects/lut_loader"},
        }};
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(decision.keepLayer, "lut-only image layers remain visible");
        check(decision.keepEffects, "lut-only image layers keep effects");
        check(!decision.strippedEffects, "lut-only image layers are not marked stripped");
        check(decision.reason == "lut-only-effect", "lut-only image layers use the lut-only policy reason");
    }

    {
        auto input = baseEffectInput();
        input.imagePath = "models/util/solidlayer.json";
        input.effects = {{
            .name = "blur",
            .visible = true,
            .firstMaterialShader = "effects/blur_precise_gaussian",
            .materialShaders = {"effects/blur_precise_gaussian"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateEffectClass == "utility-blur",
              "utility solidlayer blur is classified as utility-blur");
        check(decision.keepLayer, "utility blur carriers remain visible");
        check(decision.keepEffects, "utility blur carriers keep effects");
        check(!decision.strippedEffects, "utility blur carriers are not marked stripped");
        check(decision.reason == "utility-blur-effect",
              "utility blur carriers use the utility blur policy reason");
    }

    {
        auto input = baseEffectInput();
        input.isComposelayer = true;
        input.imagePath = "models/util/composelayer.json";
        input.effects = {{
            .name = "blur",
            .visible = true,
            .firstMaterialShader = "effects/blur_precise_gaussian",
            .materialShaders = {"effects/blur_precise_gaussian"},
        }, {
            .name = "color grading",
            .visible = true,
            .firstMaterialShader = "workshop/2795521260/effects/color_grading",
            .materialShaders = {"workshop/2795521260/effects/color_grading"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateEffectClass == "composelayer-color-grade",
              "composelayer blur color-grade is classified as composelayer-color-grade");
        check(decision.keepLayer, "composelayer color-grade carriers remain visible");
        check(decision.keepEffects, "composelayer color-grade carriers keep effects");
        check(!decision.strippedEffects, "composelayer color-grade carriers are not marked stripped");
        check(decision.reason == "composelayer-color-grade-effect",
              "composelayer color-grade carriers use the composelayer color-grade policy reason");
    }

    {
        auto input = baseEffectInput();
        input.isComposelayer = true;
        input.imagePath = "models/util/composelayer.json";
        input.effects = {{
            .name = "waterripple",
            .visible = true,
            .firstMaterialShader = "effects/waterripple",
            .materialShaders = {"effects/waterripple"},
        }, {
            .name = "waterflow",
            .visible = true,
            .firstMaterialShader = "effects/waterflow",
            .materialShaders = {"effects/waterflow"},
        }};
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateEffectClass == "composelayer-water-only",
              "composelayer water-only carrier is classified as composelayer-water-only");
        check(decision.keepLayer, "composelayer water-only carriers remain visible");
        check(decision.keepEffects, "composelayer water-only carriers keep effects");
        check(!decision.strippedEffects,
              "composelayer water-only carriers are not marked stripped");
        check(decision.reason == "composelayer-water-effect",
              "composelayer water-only carriers use the composelayer water policy reason");
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
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateRisk == "puppet-layer",
              "generic puppet water layer is still classified as puppet-layer risk");
        check(decision.keepEffects,
              "generic puppet water layer keeps effects after route validation");
        check(!decision.strippedEffects,
              "generic puppet water layer is not marked stripped after route validation");
        check(decision.reason == "puppet-water-effect",
              "generic puppet water layer uses generic puppet water policy reason");
    }

    {
        auto input = baseEffectInput();
        input.isPuppetLayer = true;
        input.layerName = "Character mixed water layer";
        input.effects.push_back({
            .name = "opacity",
            .visible = true,
            .firstMaterialShader = "effects/opacity",
            .materialShaders = {"effects/opacity"},
        });
        input.effects.push_back({
            .name = "shine",
            .visible = true,
            .firstMaterialShader = "effects/shine",
            .materialShaders = {"effects/shine"},
        });
        input.effects.push_back({
            .name = "iris",
            .visible = true,
            .firstMaterialShader = "effects/iris",
            .materialShaders = {"effects/iris"},
        });
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(decision.keepEffects,
              "generic puppet water opacity shine iris layer keeps effects");
        check(decision.reason == "puppet-water-effect",
              "generic puppet water opacity shine iris layer uses puppet water policy reason");
    }

    {
        auto input = baseEffectInput();
        input.isPuppetLayer = true;
        input.layerName = "Character audio water layer";
        input.effects.push_back({
            .name = "audio ripple",
            .visible = true,
            .firstMaterialShader = "effects/audio_response",
            .materialShaders = {"effects/audio_response"},
        });
        checkPuppetAlphaStripped(input, "puppet-layer", "generic puppet audio water layer");
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
        input.effects.push_back({
            .name = "LUT Loader",
            .visible = true,
            .firstMaterialShader = "workshop/3165346237/effects/lut_loader",
            .materialShaders = {"workshop/3165346237/effects/lut_loader"},
        });
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateRisk == "protected-puppet-path",
              "protected crop-sheet water LUT layer is still classified as protected");
        check(decision.keepEffects,
              "protected crop-sheet water LUT layer keeps effects after route validation");
        check(decision.reason == "protected-puppet-effect",
              "protected crop-sheet water LUT layer uses generic protected puppet policy reason");
    }

    {
        auto input = baseEffectInput();
        input.imagePath = "models/util/solidlayer.json";
        const auto classification = wallpaper::policy::classifyStrippedEffectCandidate(input);
        const auto decision = wallpaper::policy::decideLayerEffects(input);
        check(classification.candidateEffectClass == "utility-water-only",
              "water-only solidlayer remains classified as utility-water-only");
        check(!decision.keepLayer, "stripped utility effect carriers are dropped");
        check(!decision.keepEffects, "water-only utility carriers still strip effects");
        check(decision.strippedEffects,
              "water-only utility carriers remain marked stripped");
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
        check(equalsInts(wallpaper::debug::parseProbeLayerIdList("22,168"), {22, 168}),
              "probe layer id parser preserves normalized id order");
        check(equalsInts(wallpaper::debug::parseProbeLayerIdList(" 22, 168,22 "), {22, 168}),
              "probe layer id parser trims whitespace and removes duplicates");
        check(wallpaper::debug::parseProbeLayerIdList("abc,22,-1,0").empty(),
              "probe layer id parser rejects invalid lists");
        check(equalsInts(wallpaper::debug::parseCaptureLayerIdList("405,239,405"), {405, 239}),
              "capture layer id parser trims duplicates");
        check(wallpaper::debug::parseCaptureLayerIdList("abc,405,-1,0").empty(),
              "capture layer id parser rejects invalid lists");
        check(equalsInts(wallpaper::debug::parseProbeChannelMapSlotList("0, 2,2,15"), {0, 2, 15}),
              "probe channelmap slot parser accepts zero and removes duplicates");
        check(wallpaper::debug::parseProbeChannelMapSlotList("-1,2").empty(),
              "probe channelmap slot parser rejects negative slots");
        check(wallpaper::debug::parseProbeChannelMapSlotList("64").empty(),
              "probe channelmap slot parser rejects out-of-range slots");
    }

    {
        const auto overrides = wallpaper::debug::parsePuppetAnimationLayerOverrideList(
            "405:478:visible=true,paused=false,blend=0.25; 405:781:visible=0,rate=0,curTime=2.5");
        check(overrides.has_value(),
              "puppet animation override parser accepts semicolon-separated numeric rules");
        check(overrides && overrides->size() == 2,
              "puppet animation override parser preserves rule count");
        check(overrides && overrides->at(0).layerId == 405 &&
                  overrides->at(0).animationId == 478,
              "puppet animation override parser records target layer and animation ids");
        check(overrides && overrides->at(0).visible && *overrides->at(0).visible,
              "puppet animation override parser records visible=true");
        check(overrides && overrides->at(0).paused && !*overrides->at(0).paused,
              "puppet animation override parser records paused=false");
        check(overrides && overrides->at(0).blend && *overrides->at(0).blend == 0.25,
              "puppet animation override parser records blend values");
        check(overrides && overrides->at(1).visible && !*overrides->at(1).visible,
              "puppet animation override parser accepts boolean 0");
        check(overrides && overrides->at(1).rate && *overrides->at(1).rate == 0.0,
              "puppet animation override parser records rate values");
        check(overrides && overrides->at(1).curTime && *overrides->at(1).curTime == 2.5,
              "puppet animation override parser records current time values");

        check(!wallpaper::debug::parsePuppetAnimationLayerOverrideList("405:478:unknown=true"),
              "puppet animation override parser rejects unknown keys");
        check(!wallpaper::debug::parsePuppetAnimationLayerOverrideList("405:478:visible=maybe"),
              "puppet animation override parser rejects invalid booleans");
        check(!wallpaper::debug::parsePuppetAnimationLayerOverrideList("405:0:visible=true"),
              "puppet animation override parser rejects non-positive animation ids");
    }

    {
        wallpaper::debug::PuppetAnimationLayerInfo hidden;
        hidden.animationId = 10;
        hidden.animationName = "Hidden";
        hidden.visible = false;
        hidden.blend = 1.0;
        hidden.matchedAnimation = true;
        hidden.visibleAndWeighted = hidden.visible && hidden.blend > 1.0e-6;
        check(!hidden.visibleAndWeighted,
              "hidden matched puppet animation layer is not active weighted");

        wallpaper::debug::PuppetAnimationLayerInfo active;
        active.animationId = 11;
        active.animationName = "Active";
        active.visible = true;
        active.blend = 0.5;
        active.matchedAnimation = true;
        active.visibleAndWeighted = active.visible && active.blend > 1.0e-6;
        active.activeBoneSlots = {1, 3};
        check(active.visibleAndWeighted,
              "visible blended puppet animation layer is active weighted");
        check(active.activeBoneSlots.size() == 2,
              "active puppet animation layer records contribution slots");

        wallpaper::debug::PuppetCutoutSlotCoverageInfo inactiveSlot {
            .slot = 1,
            .active = false,
            .vertexCount = 3,
            .triangleCount = 1,
        };
        wallpaper::debug::PuppetCutoutSlotCoverageInfo activeSlot {
            .slot = 2,
            .active = true,
            .vertexCount = 9,
            .triangleCount = 4,
        };
        check(!inactiveSlot.active && inactiveSlot.vertexCount == 3,
              "inactive puppet cutout slot coverage keeps geometry counts");
        check(activeSlot.active && activeSlot.slot == 2 && activeSlot.triangleCount == 4,
              "active puppet cutout slot coverage records active geometry");
    }

    {
        const wallpaper::debug::EffectCaptureConfig config {
            .outputDir = "/tmp/yakkai-effect-debug",
            .commandLine = "yakkai_scene_harness --debug-effect-captures /tmp/yakkai-effect-debug",
            .probeLayerIds = {22, 168},
            .captureDelayMs = 8000,
        };
        check(config.enabled(), "effect capture config with output directory enables captures");
        check(config.manifestPath().string() == "/tmp/yakkai-effect-debug/manifest.json",
              "effect capture manifest path is under the output directory");
        check(config.shouldProbeLayer(22), "effect capture config probes listed layer ids");
        check(config.shouldProbeLayer(168), "effect capture config probes every listed layer id");
        check(!config.shouldProbeLayer(42), "effect capture config does not probe unlisted layer ids");
        check(!config.shouldProbeHighRiskLayer(22),
              "effect capture config keeps regular probe ids separate from high-risk probe ids");
        check(!wallpaper::debug::shouldDumpEffectCaptures(config, 7.0, 1.0 / 30.0),
              "delayed effect capture waits before the requested scene time");
        check(wallpaper::debug::shouldDumpEffectCaptures(config, 7.99, 1.0 / 30.0),
              "delayed effect capture can dump on the frame that reaches the requested scene time");
        check(wallpaper::debug::shouldDumpEffectCaptures(config, 8.0, 0.0),
              "delayed effect capture dumps once elapsed scene time reaches the requested delay");
    }

    {
        const wallpaper::debug::EffectCaptureConfig config {
            .outputDir = "/tmp/yakkai-effect-debug",
            .commandLine = "unit",
            .probeLayerIds = {22},
            .highRiskProbeLayerIds = {53, 155},
            .probeChannelMapSlots = {0, 2, 15},
            .puppetAnimationLayerOverrides = {{
                .layerId = 405,
                .animationId = 478,
                .visible = true,
            }},
            .probeMaxEffects = 2,
        };
        check(config.shouldProbeLayer(22),
              "effect capture config still probes listed puppet layer ids");
        check(!config.shouldProbeLayer(53),
              "effect capture config keeps high-risk ids out of regular probe ids");
        check(config.shouldProbeHighRiskLayer(53),
              "effect capture config probes listed high-risk layer ids");
        check(config.shouldProbeHighRiskLayer(155),
              "effect capture config probes every listed high-risk layer id");
        check(!config.shouldProbeHighRiskLayer(22),
              "effect capture config keeps regular probe ids out of high-risk ids");
        check(config.probeMaxEffects == 2,
              "effect capture config stores forced-probe effect prefix limits");
        check(!config.puppetEffectRouteOnly,
              "effect capture config defaults route-only puppet probes off");
        check(equalsInts(config.probeChannelMapSlots, {0, 2, 15}),
              "effect capture config stores diagnostic channelmap probe slots");
        check(config.puppetAnimationLayerOverrides.size() == 1 &&
                  config.puppetAnimationLayerOverrides.front().layerId == 405,
              "effect capture config stores puppet animation layer overrides");
    }

    {
        const wallpaper::debug::EffectCaptureConfig unrestricted {
            .outputDir = "unit",
            .commandLine = "unit --debug-effect-captures unit",
        };
        check(unrestricted.shouldCaptureLayer(405),
              "empty capture layer filter captures listed layers by default");
        check(unrestricted.shouldCaptureLayer(42),
              "empty capture layer filter captures every layer by default");

        const wallpaper::debug::EffectCaptureConfig filtered {
            .outputDir = "unit",
            .commandLine = "unit --debug-effect-captures unit --debug-effect-capture-layers 405,239",
            .captureLayerIds = {405, 239},
        };
        check(filtered.shouldCaptureLayer(405),
              "capture layer filter includes explicitly listed layer ids");
        check(filtered.shouldCaptureLayer(239),
              "capture layer filter includes every explicitly listed layer id");
        check(!filtered.shouldCaptureLayer(42),
              "capture layer filter excludes unlisted layer ids");

        wallpaper::Scene scene;
        scene.scene_id = "unit-scene";
        scene.debugEffectCaptures = {
            .outputDir = "unit",
            .commandLine = "unit",
            .captureLayerIds = {405},
        };
        scene.renderTargets["_rt_debug_effect_input"] = {
            .width = 256,
            .height = 16,
            .allowReuse = false,
        };

        wallpaper::debug::EffectCaptureLayerInfo skippedLayer;
        skippedLayer.sceneId = "unit-scene";
        skippedLayer.layerName = "skipped";
        skippedLayer.layerId = 42;
        wallpaper::debug::registerEffectCapture(
            scene, skippedLayer, "effect-input", "_rt_debug_effect_input");
        check(scene.debugEffectCaptureRecords.empty(),
              "capture layer filter skips nonmatching layer records");

        wallpaper::debug::EffectCaptureLayerInfo keptLayer;
        keptLayer.sceneId = "unit-scene";
        keptLayer.layerName = "ARONA_CROP_SHEET";
        keptLayer.layerId = 405;
        wallpaper::debug::registerEffectCapture(
            scene, keptLayer, "effect-input", "_rt_debug_effect_input");
        check(scene.debugEffectCaptureRecords.size() == 1,
              "capture layer filter keeps matching layer records");
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
        wallpaper::debug::EffectCaptureConfig config {
            .outputDir = "/tmp/yakkai-effect-debug",
            .commandLine = "unit",
            .probeLayerIds = {22},
        };

        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.layerId = 22;
        layer.policyReason = "puppet-alpha-strip";
        layer.candidateChainShape = "puppet-mixed";

        check(wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "listed puppet-mixed stripped layer is eligible for debug probe");

        layer.candidateChainShape = "protected-puppet-mixed";
        check(wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "listed protected-puppet-mixed stripped layer is eligible for debug probe");

        layer.candidateChainShape = "water-composelayer";
        check(wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "listed water composelayer stripped layer is eligible for debug probe");

        layer.candidateChainShape = "unknown-mixed";
        check(!wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "listed unknown-mixed stripped layer is not eligible for puppet debug probe");

        layer.candidateChainShape = "puppet-mixed";
        layer.layerId = 23;
        check(!wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "unlisted puppet-mixed stripped layer is not eligible for debug probe");

        layer.layerId = 22;
        layer.policyReason = "simple-water-effect";
        check(!wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "non-stripped layer is not eligible for stripped-layer debug probe");

        config.puppetEffectRouteOnly = true;
        layer.policyReason = "puppet-water-effect";
        layer.candidateChecks.isPuppetLayer = true;
        check(wallpaper::debug::shouldLimitRequestedEffectProbeLayer(config, layer),
              "listed allowed puppet-mixed layer is eligible for route-only probe limiting");

        config.puppetEffectRouteOnly = false;
        config.probeMaxEffects = 1;
        check(wallpaper::debug::shouldLimitRequestedEffectProbeLayer(config, layer),
              "listed allowed puppet-mixed layer is eligible for prefix probe limiting");

        config.outputDir.clear();
        layer.policyReason = "puppet-alpha-strip";
        check(!wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "disabled effect capture config cannot probe stripped layers");
    }

    {
        const auto maxZero =
            wallpaper::debug::decideEffectProbeLimit(3, 0, false);
        check(maxZero.keptVisibleEffectCount == 0,
              "max-zero effect probe keeps no visible effects");
        check(!maxZero.keepEffectRouteActive,
              "max-zero effect probe keeps old non-route behavior by default");
        check(maxZero.effectLimitTruncated,
              "max-zero effect probe records truncation");

        const auto routeOnly =
            wallpaper::debug::decideEffectProbeLimit(3, 0, true);
        check(routeOnly.keptVisibleEffectCount == 0,
              "route-only effect probe keeps no visible effects");
        check(routeOnly.keepEffectRouteActive,
              "route-only effect probe keeps the offscreen route active");
        check(routeOnly.routeOnly,
              "route-only effect probe records the diagnostic mode");
    }

    {
        wallpaper::debug::EffectCaptureConfig config {
            .outputDir = "/tmp/yakkai-effect-debug",
            .commandLine = "unit",
            .probeLayerIds = {53},
        };

        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.layerId = 53;
        layer.policyReason = "puppet-alpha-strip";
        layer.candidateChainShape = "blur-utility";
        layer.candidateChecks.hasBlurFamily = true;
        layer.candidateChecks.isUtilityCarrier = true;

        check(!wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "regular puppet probe list does not probe high-risk blur utility layers");

        config.probeLayerIds.clear();
        config.highRiskProbeLayerIds = {53};

        check(wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "high-risk probe list can probe listed stripped blur utility layers");
        check(wallpaper::debug::strippedEffectProbeReason(config, layer) == "high-risk-layer-id-probe",
              "high-risk probe reason is distinct from puppet probe reason");

        layer.candidateChecks.hasBlurFamily = false;
        layer.candidateChecks.hasLutFamily = true;
        layer.candidateChainShape = "lut-only";
        check(wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "high-risk probe list can probe listed stripped LUT layers");

        layer.candidateChecks.hasLutFamily = false;
        layer.candidateChecks.hasColorGradingFamily = true;
        layer.candidateChainShape = "blur-color-grade-composelayer";
        check(wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "high-risk probe list can probe listed stripped color-grading layers");

        layer.layerId = 54;
        check(!wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "unlisted high-risk stripped layer is not eligible for high-risk probe");

        layer.layerId = 53;
        layer.policyReason = "simple-water-effect";
        check(!wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "high-risk probe does not override non-stripped policy decisions");

        layer.policyReason = "puppet-alpha-strip";
        layer.candidateChecks.hasColorGradingFamily = false;
        check(!wallpaper::debug::shouldProbeStrippedEffectLayer(config, layer),
              "high-risk probe requires blur, LUT, or color-grading diagnostics");
    }

    {
        check(wallpaper::debug::shouldRegisterMaterialOutputCaptureForShader(
                  "workshop/3165346237/effects/lut_loader"),
              "LUT effect materials stay eligible for material-output captures");
        check(wallpaper::debug::shouldRegisterMaterialOutputCaptureForShader(
                  "effects/godrays_cast"),
              "godrays effect materials are eligible for material-output captures");
        check(wallpaper::debug::shouldRegisterMaterialOutputCaptureForShader(
                  "effects/pulse"),
              "pulse effect materials are eligible for material-output captures");
        check(!wallpaper::debug::shouldRegisterMaterialOutputCaptureForShader(""),
              "empty material shader names are not eligible for material-output captures");
    }

    {
        const auto outDir =
            std::filesystem::temp_directory_path() / "yakkai-effect-material-policy-test";
        std::filesystem::remove_all(outDir);

        wallpaper::Scene scene;
        scene.scene_id = "unit-scene";
        scene.elapsingTime = 9.5;
        scene.frameTime = 1.0 / 15.0;
        scene.debugEffectCaptures = {
            .outputDir = outDir.string(),
            .commandLine = "unit --debug-effect-captures " + outDir.string(),
        };
        scene.renderTargets["_rt_debug_effect_input"] = {
            .width = 256,
            .height = 16,
            .allowReuse = false,
        };
        scene.renderTargets["_rt_debug_material_output"] = {
            .width = 256,
            .height = 16,
            .allowReuse = false,
        };

        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.sceneId = "unit-scene";
        layer.sceneType = "Puppet";
        layer.layerName = "WALL";
        layer.layerImage = "models/WALL.json";
        layer.layerId = 82;
        layer.policyReason = "lut-only-effect";
        layer.candidateChainShape = "lut-only";
        layer.candidateMixFamilies = {"lut"};
        layer.candidateChecks.hasLutFamily = true;
        layer.publish.enabled = true;
        layer.publish.parentId = 44;
        layer.publish.hasParsedParentNode = true;
        layer.publish.objectSize = {4160.0f, 2923.0f};
        layer.publish.origin = {12.0f, 24.0f, 0.0f};
        layer.publish.scale = {1.0f, 1.0f, 1.0f};
        layer.publish.angles = {0.0f, 0.0f, 0.0f};
        layer.publish.finalBlendMode = 0;
        layer.publish.fullscreen = false;
        layer.publish.composelayer = false;
        layer.publish.standalonePuppetFinalDisplay = false;
        layer.publish.publishFinalOutput = true;
        layer.publish.finalNodeUsesOriginalParent = true;
        layer.publish.effectInputNodeReset = true;
        layer.publish.effectInputRenderTarget = "_rt_effect_ppong_a";
        layer.publish.effectPingPongA = "_rt_effect_ppong_a";
        layer.publish.effectPingPongB = "_rt_effect_ppong_b";
        layer.publish.effectOutputSourceTarget = "_rt_default";
        layer.publish.finalPublishRenderTarget = "_rt_default";
        layer.publish.materialOutputCaptureTiming = "effect-command-copy-after-layer-node";
        layer.publish.finalPublishCaptureTiming = "post-frame-render-target-dump";
        layer.publish.channelMapPrepassMode = "source-explicit";
        layer.publish.channelMapMaterialPath = "materials/ARONA_CROP_SHEET_channelmap.json";
        layer.publish.activePuppetChannelBlendSlots = {1, 3, 4};
        layer.publish.effectInputLocalTransform.origin = {0.0f, 0.0f, 0.0f};
        layer.publish.effectInputLocalTransform.scale = {1.0f, 1.0f, 1.0f};
        layer.publish.effectInputLocalTransform.angles = {0.0f, 0.0f, 0.0f};
        layer.publish.standaloneDisplayLocalTransform.origin = {12.0f, 24.0f, 0.0f};
        layer.publish.standaloneDisplayLocalTransform.scale = {1.0f, 1.0f, 1.0f};
        layer.publish.standaloneDisplayLocalTransform.angles = {0.0f, 0.0f, 0.0f};
        layer.publish.standaloneDisplayParentId = 44;
        layer.publish.standaloneDisplayHasParsedParentNode = true;
        layer.publish.standaloneDisplayNodeOrdinal = 1;
        layer.publish.standaloneFinalMaterialBlendMode =
            static_cast<int>(wallpaper::BlendMode::PremultipliedTranslucent);
        layer.publish.standaloneFinalTexture = "_rt_effect_ppong_b";
        layer.publish.effectInputMeshBounds.vertexArrayCount = 1;
        layer.publish.effectInputMeshBounds.vertexCount = 120;
        layer.publish.effectInputMeshBounds.positionMin = {-2080.0f, -1461.5f, 0.0f};
        layer.publish.effectInputMeshBounds.positionMax = {2080.0f, 1461.5f, 0.0f};
        layer.publish.effectFinalMeshBounds.vertexArrayCount = 1;
        layer.publish.effectFinalMeshBounds.vertexCount = 4;
        layer.publish.effectFinalMeshBounds.positionMin = {-1.0f, -1.0f, 0.0f};
        layer.publish.effectFinalMeshBounds.positionMax = {1.0f, 1.0f, 0.0f};
        layer.publish.standaloneFinalMeshBounds.vertexArrayCount = 1;
        layer.publish.standaloneFinalMeshBounds.vertexCount = 4;
        layer.publish.standaloneFinalMeshBounds.positionMin = {-2080.0f, -1461.5f, 0.0f};
        layer.publish.standaloneFinalMeshBounds.positionMax = {2080.0f, 1461.5f, 0.0f};

        wallpaper::debug::registerEffectCapture(scene, layer, "effect-input", "_rt_debug_effect_input");
        wallpaper::debug::registerEffectCapture(
            scene, layer, "material-output-1-0", "_rt_debug_material_output");

        wallpaper::debug::EffectCaptureMaterialInfo material;
        material.effectIndex = 1;
        material.materialIndex = 0;
        material.shader = "workshop/3165346237/effects/lut_loader";
        material.authoredTextures = {"effects/lut/night.png"};
        material.resolvedTextures = {"effects/lut/night.png"};
        material.textureBindings.push_back({
            .slot = 0,
            .authored = "effects/lut/night.png",
            .resolved = "effects/lut/night.png",
        });
        material.authoredCombos = {{"LUTMODE", "1"}};
        material.resolvedCombos = {{"LUTMODE", "1"}};
        material.materialValues = {{"strength", {0.75f}}};
        material.resolvedConstValues = {{"g_Texture0Resolution", {256.0f, 16.0f, 256.0f, 16.0f}}};
        material.defines = {"g_Texture0"};
        material.authoredOutputRenderTarget = "_rt_effect_ppong_b";
        material.resolvedOutputRenderTarget = "_rt_default";
        material.finalPublishedMaterial = true;
        material.debugMaterialOutputSourceRenderTarget = "_rt_default";
        material.debugMaterialOutputCommandSource = "_rt_debug_effect_final_output";
        material.debugSourceFinalEffectOutput = true;
        material.materialOutputCaptureStage = "material-output-1-0";
        layer.effectMaterials.push_back(material);

        wallpaper::debug::recordEffectPassState(scene, {
            .output = "_rt_default",
            .loadOp = "LOAD",
            .depthLoadOp = "DONT_CARE",
            .colorMask = "RGB",
            .colorMaskBits = 7,
            .blendMode = "0",
            .blendEnabled = true,
            .preserveOutput = true,
            .usesDepth = false,
            .camera = "global",
            .nodeId = 405,
            .materialName = "ARONA_CROP_SHEET",
            .debugPurpose = "effect-pass",
        });

        wallpaper::debug::refreshEffectCaptureLayerInfo(scene, layer);

        check(wallpaper::debug::writeEffectCaptureManifest(scene),
              "manifest writes effect material diagnostics");

        const std::string manifest = readTextFile(scene.debugEffectCaptures.manifestPath());
        check(manifest.find("\"captureDelayMs\": 0") != std::string::npos,
              "manifest includes effect capture delay");
        check(manifest.find("\"shaderTimeSeconds\": 9.5") != std::string::npos,
              "manifest includes shader time used for g_Time");
        check(manifest.find("\"frameTimeSeconds\": 0.06666666666666667") != std::string::npos,
              "manifest includes current frame time");
        check(manifest.find("\"effectiveCaptureTimeSeconds\": 9.566666666666666") != std::string::npos,
              "manifest includes effective capture readiness time");
        check(manifest.find("\"effectMaterials\"") != std::string::npos,
              "manifest includes effect material diagnostics");
        check(manifest.find("\"effectIndex\": 1") != std::string::npos,
              "manifest includes material effect index");
        check(manifest.find("\"materialIndex\": 0") != std::string::npos,
              "manifest includes material index");
        check(manifest.find("\"shader\": \"workshop/3165346237/effects/lut_loader\"") != std::string::npos,
              "manifest includes material shader path");
        check(manifest.find("\"authoredTextures\"") != std::string::npos,
              "manifest includes authored textures");
        check(manifest.find("\"resolvedTextures\"") != std::string::npos,
              "manifest includes resolved textures");
        check(manifest.find("\"textureBindings\"") != std::string::npos,
              "manifest includes texture binding diagnostics");
        check(manifest.find("\"materialValues\"") != std::string::npos,
              "manifest includes authored material values");
        check(manifest.find("\"resolvedConstValues\"") != std::string::npos,
              "manifest includes resolved shader const values");
        check(manifest.find("\"g_Texture0Resolution\"") != std::string::npos,
              "manifest includes resolved texture resolution const value");
        check(manifest.find("\"stage\": \"material-output-1-0\"") != std::string::npos,
              "manifest includes material output capture stage");
        check(manifest.find("\"captureIndex\": 0") != std::string::npos,
              "manifest includes first capture index");
        check(manifest.find("\"captureIndex\": 1") != std::string::npos,
              "manifest includes second capture index");
        check(manifest.find("\"renderTarget\": \"_rt_debug_material_output\"") != std::string::npos,
              "manifest includes material output render target");
        check(manifest.find("\"publish\"") != std::string::npos,
              "manifest includes publish diagnostics");
        check(manifest.find("\"finalPublishCaptureTiming\": \"post-frame-render-target-dump\"") != std::string::npos,
              "manifest includes final publish timing");
        check(manifest.find("\"materialOutputCaptureTiming\": \"effect-command-copy-after-layer-node\"") != std::string::npos,
              "manifest includes material output timing");
        check(manifest.find("\"parentId\": 44") != std::string::npos,
              "manifest includes publish parent id");
        check(manifest.find("\"finalBlendMode\": 0") != std::string::npos,
              "manifest includes final blend mode");
        check(manifest.find("\"effectOutputSourceTarget\": \"_rt_default\"") != std::string::npos,
              "manifest includes effect output source target");
        check(manifest.find("\"finalPublishedMaterial\": true") != std::string::npos,
              "manifest includes final material publish marker");
        check(manifest.find("\"debugSourceFinalEffectOutput\": true") != std::string::npos,
              "manifest includes debug material source routing");
        check(manifest.find("\"channelMapPrepassMode\": \"source-explicit\"") != std::string::npos,
              "manifest includes channelmap prepass mode");
        check(manifest.find("\"channelMapMaterialPath\": \"materials/ARONA_CROP_SHEET_channelmap.json\"") != std::string::npos,
              "manifest includes channelmap material path");
        check(manifest.find("\"activePuppetChannelBlendSlots\"") != std::string::npos,
              "manifest includes active puppet channel blend slots");
        check(manifest.find("\"effectInputLocalTransform\"") != std::string::npos,
              "manifest includes effect input local transform");
        check(manifest.find("\"standaloneDisplayLocalTransform\"") != std::string::npos,
              "manifest includes standalone display local transform");
        check(manifest.find("\"standaloneDisplayParentId\": 44") != std::string::npos,
              "manifest includes standalone display parent id");
        check(manifest.find("\"standaloneDisplayNodeOrdinal\": 1") != std::string::npos,
              "manifest includes standalone display draw ordinal");
        check(manifest.find("\"standaloneFinalMaterialBlendMode\": 4") != std::string::npos,
              "manifest includes standalone final material blend mode");
        check(manifest.find("\"standaloneFinalTexture\": \"_rt_effect_ppong_b\"") != std::string::npos,
              "manifest includes standalone final source texture");
        check(manifest.find("\"effectInputMeshBounds\"") != std::string::npos,
              "manifest includes effect input mesh bounds");
        check(manifest.find("\"effectFinalMeshBounds\"") != std::string::npos,
              "manifest includes effect final mesh bounds");
        check(manifest.find("\"standaloneFinalMeshBounds\"") != std::string::npos,
              "manifest includes standalone final mesh bounds");
        check(manifest.find("\"debugEffectPassStates\"") != std::string::npos,
              "manifest includes debug effect pass states");
        check(manifest.find("\"colorMaskBits\": 7") != std::string::npos,
              "manifest includes pass state color mask bits");
        check(manifest.find("\"camera\": \"global\"") != std::string::npos,
              "manifest includes pass state camera");
        check(manifest.find("\"nodeId\": 405") != std::string::npos,
              "manifest includes pass state node id");
        check(manifest.find("\"materialName\": \"ARONA_CROP_SHEET\"") != std::string::npos,
              "manifest includes pass state material name");
        check(manifest.find("\"debugPurpose\": \"effect-pass\"") != std::string::npos,
              "manifest includes pass state debug purpose");

        std::filesystem::remove_all(outDir);
    }

    {
        const auto outDir =
            std::filesystem::temp_directory_path() / "yakkai-default-rt-boundary-capture-test";
        std::filesystem::remove_all(outDir);

        wallpaper::Scene scene;
        scene.scene_id = "unit-scene";
        scene.debugEffectCaptures = {
            .outputDir = outDir.string(),
            .commandLine = "unit --debug-effect-captures " + outDir.string(),
        };
        scene.renderTargets[wallpaper::SpecTex_Default.data()] = {
            .width = 1280,
            .height = 720,
            .allowReuse = true,
        };

        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.sceneId = "unit-scene";
        layer.layerName = "1st flare";
        layer.layerId = 469;
        layer.publish.enabled = true;
        layer.publish.finalPublishRenderTarget = wallpaper::SpecTex_Default;
        layer.publish.defaultRtBoundaryCaptureTiming = "effect-command-copy-around-effect-layer";

        const std::string target = wallpaper::debug::registerDefaultRtBoundaryCapture(
            scene, layer, "default-before-effect", "node/test");

        check(target == "_rt_debug_default_before_effect_node_test",
              "default RT boundary capture target is stable and sanitized");
        check(scene.renderTargets.count(target) == 1,
              "default RT boundary capture creates a debug render target");
        check(scene.renderTargets.at(target).width == 1280 &&
                  scene.renderTargets.at(target).height == 720,
              "default RT boundary capture inherits default render target size");
        check(!scene.renderTargets.at(target).allowReuse,
              "default RT boundary capture target is not reusable");
        check(scene.debugEffectCaptureRecords.size() == 1,
              "default RT boundary capture registers one capture record");
        check(scene.debugEffectCaptureRecords.front().stage == "default-before-effect",
              "default RT boundary capture stores the requested stage");
        check(scene.debugEffectCaptureRecords.front().renderTarget == target,
              "default RT boundary capture stores the debug target");

        check(wallpaper::debug::writeEffectCaptureManifest(scene),
              "manifest writes default RT boundary capture");

        const std::string manifest = readTextFile(scene.debugEffectCaptures.manifestPath());
        check(manifest.find("\"stage\": \"default-before-effect\"") != std::string::npos,
              "manifest includes default RT boundary capture stage");
        check(manifest.find("\"defaultRtBoundaryCaptureTiming\": \"effect-command-copy-around-effect-layer\"") != std::string::npos,
              "manifest includes default RT boundary timing");

        std::filesystem::remove_all(outDir);
    }

    {
        const auto outDir =
            std::filesystem::temp_directory_path() / "yakkai-final-display-boundary-capture-test";
        std::filesystem::remove_all(outDir);

        wallpaper::Scene scene;
        scene.scene_id = "unit-scene";
        scene.debugEffectCaptures = {
            .outputDir = outDir.string(),
            .commandLine = "unit --debug-effect-captures " + outDir.string(),
        };
        scene.renderTargets[wallpaper::SpecTex_Default.data()] = {
            .width = 1280,
            .height = 720,
            .allowReuse = true,
        };

        auto finalDisplayNode = std::make_shared<wallpaper::SceneNode>();

        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.sceneId = "unit-scene";
        layer.layerName = "protected puppet final display";
        layer.layerId = 405;
        layer.publish.enabled = true;
        layer.publish.finalPublishRenderTarget = wallpaper::SpecTex_Default;
        layer.publish.finalDisplayRoute = "effect-layer-node-final-publish";

        const auto targets = wallpaper::debug::registerFinalDisplayBoundaryCapture(
            scene, layer, *finalDisplayNode, "node/final");

        check(targets.beforeTarget == "_rt_debug_final_display_before_node_final",
              "final display boundary before target is stable and sanitized");
        check(targets.afterTarget == "_rt_debug_final_display_after_node_final",
              "final display boundary after target is stable and sanitized");
        check(scene.renderTargets.count(targets.beforeTarget) == 1,
              "final display boundary creates a before debug render target");
        check(scene.renderTargets.count(targets.afterTarget) == 1,
              "final display boundary creates an after debug render target");
        check(!scene.renderTargets.at(targets.beforeTarget).allowReuse &&
                  !scene.renderTargets.at(targets.afterTarget).allowReuse,
              "final display boundary render targets are not reusable");
        check(scene.debugEffectCaptureRecords.size() == 2,
              "final display boundary registers before and after capture records");
        check(scene.debugEffectFinalDisplayBoundaryCaptures.size() == 1,
              "final display boundary stores one render-graph node hook");
        check(scene.debugEffectFinalDisplayBoundaryCaptures.front().node == finalDisplayNode.get(),
              "final display boundary hook references the tagged display node");
        check(scene.debugEffectFinalDisplayBoundaryCaptures.front().beforeTarget == targets.beforeTarget,
              "final display boundary hook stores the before target");
        check(scene.debugEffectFinalDisplayBoundaryCaptures.front().afterTarget == targets.afterTarget,
              "final display boundary hook stores the after target");

        layer.publish.finalDisplayBoundaryCaptureTiming = "render-graph-copy-around-final-display-node";
        layer.publish.finalDisplayBeforeRenderTarget = targets.beforeTarget;
        layer.publish.finalDisplayAfterRenderTarget = targets.afterTarget;
        wallpaper::debug::refreshEffectCaptureLayerInfo(scene, layer);

        check(wallpaper::debug::writeEffectCaptureManifest(scene),
              "manifest writes final display boundary captures");

        const std::string manifest = readTextFile(scene.debugEffectCaptures.manifestPath());
        check(manifest.find("\"stage\": \"final-display-before\"") != std::string::npos,
              "manifest includes final display before stage");
        check(manifest.find("\"stage\": \"final-display-after\"") != std::string::npos,
              "manifest includes final display after stage");
        check(manifest.find("\"finalDisplayBoundaryCaptureTiming\": \"render-graph-copy-around-final-display-node\"") != std::string::npos,
              "manifest includes final display boundary timing");
        check(manifest.find("\"finalDisplayBeforeRenderTarget\": \"_rt_debug_final_display_before_node_final\"") != std::string::npos,
              "manifest includes final display before render target");
        check(manifest.find("\"finalDisplayAfterRenderTarget\": \"_rt_debug_final_display_after_node_final\"") != std::string::npos,
              "manifest includes final display after render target");

        std::filesystem::remove_all(outDir);
    }

    {
        const auto outDir =
            std::filesystem::temp_directory_path() / "yakkai-effect-layer-final-publish-boundary-capture-test";
        std::filesystem::remove_all(outDir);

        wallpaper::Scene scene;
        scene.scene_id = "unit-scene";
        scene.debugEffectCaptures = {
            .outputDir = outDir.string(),
            .commandLine = "unit --debug-effect-captures " + outDir.string(),
        };
        scene.renderTargets[wallpaper::SpecTex_Default.data()] = {
            .width = 1280,
            .height = 720,
            .allowReuse = true,
        };

        auto finalPublishNode = std::make_shared<wallpaper::SceneNode>();

        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.sceneId = "unit-scene";
        layer.layerName = "effect layer final publish";
        layer.layerId = 405;
        layer.publish.enabled = true;
        layer.publish.finalPublishRenderTarget = wallpaper::SpecTex_Default;
        layer.publish.finalDisplayRoute = "effect-layer-node-final-publish";

        const auto targets = wallpaper::debug::registerEffectLayerFinalPublishBoundaryCapture(
            scene, layer, *finalPublishNode, "node/final");

        check(targets.beforeTarget == "_rt_debug_final_display_before_node_final",
              "effect layer final publish before target is stable and sanitized");
        check(targets.afterTarget == "_rt_debug_final_display_after_node_final",
              "effect layer final publish after target is stable and sanitized");
        check(layer.publish.finalDisplayBoundaryCaptureTiming ==
                  "render-graph-copy-around-effect-layer-final-publish-node",
              "effect layer final publish boundary timing is explicit");
        check(layer.publish.finalDisplayBeforeRenderTarget == targets.beforeTarget,
              "effect layer final publish stores before render target");
        check(layer.publish.finalDisplayAfterRenderTarget == targets.afterTarget,
              "effect layer final publish stores after render target");

        wallpaper::debug::refreshEffectCaptureLayerInfo(scene, layer);
        check(wallpaper::debug::writeEffectCaptureManifest(scene),
              "manifest writes effect layer final publish boundary captures");

        const std::string manifest = readTextFile(scene.debugEffectCaptures.manifestPath());
        check(manifest.find("\"stage\": \"final-display-before\"") != std::string::npos,
              "manifest includes effect layer final publish before stage");
        check(manifest.find("\"stage\": \"final-display-after\"") != std::string::npos,
              "manifest includes effect layer final publish after stage");
        check(manifest.find("\"finalDisplayBoundaryCaptureTiming\": \"render-graph-copy-around-effect-layer-final-publish-node\"") != std::string::npos,
              "manifest includes effect layer final publish boundary timing");

        std::filesystem::remove_all(outDir);
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
            .probeLayerIds = {42},
            .highRiskProbeLayerIds = {53},
            .probeChannelMapSlots = {0, 2, 15},
            .puppetAnimationLayerOverrides = {{
                .layerId = 405,
                .animationId = 478,
                .visible = true,
            }},
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
        layer.candidateMixFamilies = {"opacity"};
        layer.candidateChainShape = "water+opacity";
        layer.candidateRisk = "mixed-chain";
        layer.candidateBlockedReason = "water-effect-mixed-chain";
        layer.candidateChecks.hasWaterFamily = true;
        layer.candidateChecks.waterOnly = false;
        layer.candidateChecks.isUtilityCarrier = false;
        layer.candidateChecks.isComposelayer = false;
        layer.candidateChecks.isFullscreen = false;
        layer.candidateChecks.isPuppetLayer = false;
        layer.candidateChecks.isProtectedPuppetPath = false;
        layer.debugProbeRequested = true;
        layer.debugProbeOverrodePolicy = true;
        layer.debugProbeReason = "layer-id-probe";
        layer.debugProbeMaxEffects = 1;
        layer.debugProbeOriginalVisibleEffectCount = 2;
        layer.debugProbeKeptVisibleEffectCount = 1;
        layer.debugProbeEffectLimitTruncated = true;

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
        check(manifest.find("\"probeLayerIds\"") != std::string::npos,
              "manifest includes configured probe layer ids");
        check(manifest.find("\"highRiskProbeLayerIds\"") != std::string::npos,
              "manifest includes configured high-risk probe layer ids");
        check(manifest.find("\"probeChannelMapSlots\"") != std::string::npos,
              "manifest includes configured channelmap probe slots");
        check(manifest.find("\"puppetAnimationLayerOverrides\"") != std::string::npos,
              "manifest includes configured puppet animation layer overrides");
        check(manifest.find("42") != std::string::npos,
              "manifest includes configured probe layer id value");
        check(manifest.find("53") != std::string::npos,
              "manifest includes configured high-risk probe layer id value");
        check(manifest.find("\"debugProbe\"") != std::string::npos,
              "manifest includes stripped candidate debug probe metadata");
        check(manifest.find("\"requested\": true") != std::string::npos,
              "manifest includes stripped candidate probe request state");
        check(manifest.find("\"overrodePolicy\": true") != std::string::npos,
              "manifest includes stripped candidate probe override state");
        check(manifest.find("\"reason\": \"layer-id-probe\"") != std::string::npos,
              "manifest includes stripped candidate probe reason");
        check(manifest.find("\"maxEffects\": 1") != std::string::npos,
              "manifest includes stripped candidate effect prefix limit");
        check(manifest.find("\"originalVisibleEffectCount\": 2") != std::string::npos,
              "manifest includes stripped candidate original effect count");
        check(manifest.find("\"keptVisibleEffectCount\": 1") != std::string::npos,
              "manifest includes stripped candidate kept effect count");
        check(manifest.find("\"effectLimitTruncated\": true") != std::string::npos,
              "manifest includes stripped candidate effect truncation state");
        check(manifest.find("\"layerName\": \"Water background\"") != std::string::npos,
              "manifest includes stripped candidate layer name");
        check(manifest.find("\"reason\": \"puppet-alpha-strip\"") != std::string::npos,
              "manifest includes stripped candidate policy reason");
        check(manifest.find("\"candidateFamilies\"") != std::string::npos,
              "manifest includes stripped candidate families");
        check(manifest.find("\"candidateMixFamilies\"") != std::string::npos,
              "manifest includes stripped candidate mix families");
        check(manifest.find("\"candidateChainShape\": \"water+opacity\"") != std::string::npos,
              "manifest includes stripped candidate chain shape");
        check(manifest.find("\"candidateRisk\": \"mixed-chain\"") != std::string::npos,
              "manifest includes stripped candidate risk");
        check(manifest.find("\"candidateBlockedReason\": \"water-effect-mixed-chain\"") != std::string::npos,
              "manifest includes stripped candidate blocked reason");
        check(manifest.find("\"candidateChecks\"") != std::string::npos,
              "manifest includes stripped candidate checks");
        check(manifest.find("\"hasWaterFamily\": true") != std::string::npos,
              "manifest includes stripped candidate water-family check");
        check(manifest.find("\"hasBlurFamily\": false") != std::string::npos,
              "manifest includes stripped candidate blur-family check");
        check(manifest.find("\"hasLutFamily\": false") != std::string::npos,
              "manifest includes stripped candidate LUT-family check");
        check(manifest.find("\"hasColorGradingFamily\": false") != std::string::npos,
              "manifest includes stripped candidate color-grade-family check");
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

    {
        const auto outDir =
            std::filesystem::temp_directory_path() / "yakkai-protected-puppet-diagnostic-test";
        std::filesystem::remove_all(outDir);

        wallpaper::Scene scene;
        scene.scene_id = "3228578419";
        scene.debugEffectCaptures = {
            .outputDir = outDir.string(),
            .commandLine = "unit --debug-effect-captures " + outDir.string(),
        };

        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.sceneId = "3228578419";
        layer.sceneType = "Puppet";
        layer.layerName = "ARONA_CROP_SHEET";
        layer.layerImage = "models/ARONA_CROP_SHEET.png";
        layer.layerId = 2143;
        layer.visibleEffectCount = 3;
        layer.alpha = 0.84f;
        layer.keepLayer = true;
        layer.keepEffects = false;
        layer.strippedEffects = true;
        layer.policyReason = "puppet-alpha-strip";
        layer.effectNames = {"lut", "pulse", "shake"};
        layer.materialShaders = {"effects/lut_loader", "effects/pulse", "effects/shake"};
        layer.candidateMixFamilies = {"lut", "pulse", "shake"};
        layer.candidateChainShape = "protected-puppet-mixed";
        layer.candidateEffectClass = "protected-puppet-lut";
        layer.candidateRisk = "protected-puppet-path";
        layer.candidateBlockedReason = "protected-puppet-path";
        layer.candidateChecks.hasLutFamily = true;
        layer.candidateChecks.isPuppetLayer = true;
        layer.candidateChecks.isProtectedPuppetPath = true;
        layer.puppetAnimationLayers.push_back({
            .animationId = 151,
            .animationName = "Idle",
            .rate = 1.0,
            .blend = 1.0,
            .visible = true,
            .paused = false,
            .additive = false,
            .curTime = 0.0,
            .matchedAnimation = true,
            .visibleAndWeighted = true,
            .activeBoneSlots = {2, 5, 9},
        });
        layer.puppetAnimationLayers.push_back({
            .animationId = 405,
            .animationName = "Arona Drool",
            .rate = 0.0,
            .blend = 0.35,
            .visible = true,
            .paused = true,
            .additive = true,
            .curTime = 2.5,
        });
        const auto route = wallpaper::policy::decideEffectPublishRoute({
            .puppetLayer = true,
            .standalonePuppetFinalDisplay = true,
            .usePuppetChannelMapPrepass = false,
        });
        layer.publish.puppetLayer = true;
        layer.publish.standalonePuppetFinalDisplay = true;
        layer.publish.publishFinalOutput = false;
        layer.publish.effectInputMeshKind = route.effectInputMeshKind;
        layer.publish.effectFinalMeshKind = route.effectFinalMeshKind;
        layer.publish.standaloneFinalMeshKind = route.standaloneFinalMeshKind;
        layer.publish.finalDisplayRoute = route.finalDisplayRoute;
        layer.publish.standaloneDisplayAttachMode = route.standaloneDisplayAttachMode;
        layer.publish.routeRisk = route.routeRisk;
        layer.publish.puppetCutoutSlotCoverage.push_back({
            .slot = 2,
            .active = true,
            .vertexCount = 42,
            .triangleCount = 17,
        });
        layer.publish.puppetCutoutSlotCoverage.push_back({
            .slot = 5,
            .active = false,
            .vertexCount = 11,
            .triangleCount = 4,
        });
        wallpaper::debug::EffectCaptureMaterialInfo material;
        material.effectIndex = 1;
        material.materialIndex = 0;
        material.shader = "effects/lut_loader";
        material.authoredOutputRenderTarget = "_rt_effect_ppong_b";
        material.authoredTextures = {"materials/arona_lut.tex"};
        material.materialValues = {{"strength", {0.5f}}};
        layer.effectMaterials.push_back(material);

        wallpaper::debug::recordStrippedEffectCandidate(scene, layer);
        wallpaper::debug::recordPuppetAnimationLayerInventory(scene, layer);

        check(scene.debugEffectCaptureRecords.empty(),
              "protected puppet diagnostics do not create normal capture records");
        check(wallpaper::debug::writeEffectCaptureManifest(scene),
              "manifest writes protected puppet diagnostics");

        const std::string manifest = readTextFile(scene.debugEffectCaptures.manifestPath());
        check(manifest.find("\"protectedPuppetDiagnostics\"") != std::string::npos,
              "manifest includes protected puppet diagnostics separately");
        check(manifest.find("\"diagnosticKind\": \"protected-puppet-chain\"") != std::string::npos,
              "manifest classifies protected puppet diagnostic kind");
        check(manifest.find("\"captureMode\": \"metadata-only\"") != std::string::npos,
              "manifest marks protected puppet diagnostics as metadata-only");
        check(manifest.find("\"finalPublishRenderTarget\": \"_rt_default\"") != std::string::npos,
              "manifest includes protected puppet final publish routing");
        check(manifest.find("\"alphaEvidence\"") != std::string::npos,
              "manifest includes protected puppet alpha evidence");
        check(manifest.find("\"layerName\": \"ARONA_CROP_SHEET\"") != std::string::npos,
              "manifest includes protected puppet layer identity");
        check(manifest.find("\"materialShaders\"") != std::string::npos,
              "manifest includes protected puppet material shader evidence");
        check(manifest.find("\"effectIndex\": 1") != std::string::npos,
              "manifest includes protected puppet authored material pass order");
        check(manifest.find("\"strength\"") != std::string::npos,
              "manifest includes protected puppet material constants");
        check(manifest.find("\"protected-puppet-mixed\"") != std::string::npos,
              "manifest includes protected puppet chain shape");
        check(manifest.find("\"candidateEffectClass\": \"protected-puppet-lut\"") != std::string::npos,
              "manifest includes protected puppet LUT class");
        check(manifest.find("\"puppetLayer\": true") != std::string::npos,
              "manifest includes generic puppet route flag");
        check(manifest.find("\"effectInputMeshKind\": \"puppet-skinned-mesh\"") != std::string::npos,
              "manifest includes effect input mesh route");
        check(manifest.find("\"standaloneFinalMeshKind\": \"layer-card\"") != std::string::npos,
              "manifest includes layer-card standalone final mesh route");
        check(manifest.find("\"standaloneDisplayAttachMode\": \"original-parent-sibling\"") != std::string::npos,
              "manifest includes standalone final display attach mode");
        check(manifest.find("\"routeRisk\": \"\"") != std::string::npos,
              "manifest includes empty route risk for fixed generic route");
        check(manifest.find("\"puppetAnimationLayers\"") != std::string::npos,
              "manifest includes puppet animation layer state");
        check(manifest.find("\"puppetAnimationLayerInventory\"") != std::string::npos,
              "manifest includes top-level puppet animation layer inventory");
        check(manifest.find("\"animationName\": \"Arona Drool\"") != std::string::npos,
              "manifest includes named puppet animation layer");
        check(manifest.find("\"animationId\": 405") != std::string::npos,
              "manifest includes puppet animation id");
        check(manifest.find("\"paused\": true") != std::string::npos,
              "manifest includes script-resolved paused state");
        check(manifest.find("\"additive\": true") != std::string::npos,
              "manifest includes additive animation layer state");
        check(manifest.find("\"matchedAnimation\": true") != std::string::npos,
              "manifest includes puppet animation match state");
        check(manifest.find("\"visibleAndWeighted\": true") != std::string::npos,
              "manifest includes active puppet animation weighting state");
        check(manifest.find("\"activeBoneSlotCount\": 3") != std::string::npos,
              "manifest includes active puppet bone slot count");
        check(manifest.find("\"activeBoneSlots\"") != std::string::npos &&
                  manifest.find("2") != std::string::npos &&
                  manifest.find("5") != std::string::npos &&
                  manifest.find("9") != std::string::npos,
              "manifest includes active puppet bone slot ids");
        check(manifest.find("\"puppetCutoutSlotCoverage\"") != std::string::npos,
              "manifest includes puppet cutout slot coverage");
        check(manifest.find("\"slot\": 2") != std::string::npos &&
                  manifest.find("\"active\": true") != std::string::npos &&
                  manifest.find("\"vertexCount\": 42") != std::string::npos &&
                  manifest.find("\"triangleCount\": 17") != std::string::npos,
              "manifest includes active puppet cutout slot coverage");
        check(manifest.find("\"slot\": 5") != std::string::npos &&
                  manifest.find("\"active\": false") != std::string::npos,
              "manifest includes inactive puppet cutout slot coverage");
        check(manifest.find("\"primaryVertexCount\": 42") != std::string::npos &&
                  manifest.find("\"weightedVertexCount\": 42") != std::string::npos &&
                  manifest.find("\"secondaryOnly\": false") != std::string::npos,
              "manifest includes named primary and weighted puppet slot coverage");
        check(manifest.find("\"boneName\"") != std::string::npos &&
                  manifest.find("\"simulationMetadataPresent\"") != std::string::npos,
              "manifest includes puppet bone metadata fields");

        std::filesystem::remove_all(outDir);
    }
}

void testEffectPublishRoutePolicy()
{
    {
        const auto route = wallpaper::policy::decideEffectPublishRoute({
            .puppetLayer = true,
            .standalonePuppetFinalDisplay = false,
            .usePuppetChannelMapPrepass = false,
        });
        check(route.effectInputMeshKind == "card",
              "non-channelmap puppet route starts effects from the flat crop-sheet source");
        check(route.effectFinalMeshKind == "puppet-skinned-mesh",
              "non-channelmap puppet route publishes authored effects through the puppet mesh");
        check(route.finalDisplayRoute == "effect-layer-node-final-publish",
              "non-channelmap puppet route uses effect-layer final publish");
        check(route.standaloneFinalMeshKind.empty(),
              "non-channelmap puppet default route does not create a standalone layer-card display");
        check(!route.effectInputMaterialPreservesLayerBlendMode,
              "flat crop-sheet puppet effect input uses normal offscreen composition");
        check(route.routeRisk.empty(),
              "non-channelmap puppet deferred route has no standalone flat-card route risk");
    }

    {
        const auto route = wallpaper::policy::decideEffectPublishRoute({
            .fullscreen = false,
            .composelayer = true,
        });
        check(route.effectInputMeshKind == "card",
              "non-fullscreen composelayers render from a local card input");
        check(route.effectFinalMeshKind == "card",
              "non-fullscreen composelayers publish through the authored layer card");
        check(route.finalDisplayRoute == "effect-layer-node-final-publish",
              "non-fullscreen composelayers use local effect-layer final publish");
    }

    {
        const auto route = wallpaper::policy::decideEffectPublishRoute({
            .fullscreen = true,
            .composelayer = false,
        });
        check(route.effectFinalMeshKind == "fullscreen-card",
              "fullscreen image effects still publish through a fullscreen card");
        check(route.finalDisplayRoute == "effect-layer-fullscreen-final-publish",
              "fullscreen image effects keep fullscreen final publish");
    }

    {
        const auto route = wallpaper::policy::decideEffectPublishRoute({
            .puppetLayer = true,
            .standalonePuppetFinalDisplay = true,
            .usePuppetChannelMapPrepass = false,
        });
        check(route.effectInputMeshKind == "puppet-skinned-mesh",
              "non-channelmap puppet route starts from skinned effect input");
        check(route.standaloneFinalMeshKind == "layer-card",
              "non-channelmap puppet route publishes the rendered effect texture as a layer card");
        check(!route.standaloneFinalMaterialUsesPuppetSkinning,
              "non-channelmap puppet final material avoids double-applying puppet skinning");
        check(route.effectInputMaterialPreservesLayerBlendMode,
              "puppet offscreen input preserves layer blending for overlapping cutouts");
        check(route.standaloneDisplayAttachMode == "original-parent-sibling",
              "standalone puppet final display attaches beside the effect input node");
        check(route.routeRisk.empty(),
              "non-channelmap puppet route no longer reports flat-card route risk");
    }

    {
        const auto route = wallpaper::policy::decideEffectPublishRoute({
            .puppetLayer = true,
            .standalonePuppetFinalDisplay = true,
            .usePuppetChannelMapPrepass = true,
            .hasActivePuppetChannelBlendSlots = true,
        });
        check(route.standaloneFinalMeshKind ==
                  "puppet-image-space-filtered-overlay-or-puppet-skinned-mesh",
              "channelmap puppet route keeps filtered overlay mesh classification");
        check(route.standaloneFinalMaterialUsesPuppetSkinning,
              "channelmap puppet final material keeps puppet skinning");
    }

    {
        const auto route = wallpaper::policy::decideEffectPublishRoute({
            .puppetLayer = true,
            .standalonePuppetFinalDisplay = true,
            .usePuppetChannelMapPrepass = false,
            .puppetFinalMeshOverride = "image-space",
        });
        check(route.standaloneFinalMeshKind == "puppet-image-space-mesh",
              "debug non-channelmap puppet route can publish through image-space mesh");
        check(!route.standaloneFinalMaterialUsesPuppetSkinning,
              "debug image-space final mesh still avoids double-applying puppet skinning");
    }

    {
        const auto route = wallpaper::policy::decideEffectPublishRoute({
            .puppetLayer = true,
            .standalonePuppetFinalDisplay = false,
            .usePuppetChannelMapPrepass = false,
            .puppetFinalMeshOverride = "deferred-puppet-final",
        });
        check(route.effectInputMeshKind == "card",
              "debug deferred puppet-final route starts effects from the flat card source");
        check(route.effectFinalMeshKind == "puppet-skinned-mesh",
              "debug deferred puppet-final route publishes effects through the puppet mesh");
        check(route.finalDisplayRoute == "effect-layer-node-final-publish",
              "debug deferred puppet-final route uses effect-layer final publish");
        check(route.routeRisk.empty(),
              "debug deferred puppet-final route has no standalone flat-card route risk");
    }
}

void testPuppetEffectRoutePlan()
{
    wallpaper::PuppetEffectRoutePlanInput input;
    input.puppetLayer = true;
    input.effectRouteActive = true;
    input.routePuppetPrepassThroughAuthoredEffects = true;

    const auto plan = wallpaper::decidePuppetEffectRoutePlan(input);
    check(!plan.useStandalonePuppetFinalDisplay,
          "non-channelmap puppet effect route defers final display to the effect-layer output");
    check(plan.effectInputMeshKind == "card",
          "non-channelmap puppet effect route samples the flat crop-sheet before authored effects");
    check(plan.effectFinalMeshKind == "puppet-skinned-mesh",
          "non-channelmap puppet effect route publishes through the puppet final mesh");
    check(plan.standaloneFinalMeshKind.empty(),
          "non-channelmap puppet effect route avoids the legacy standalone final display");
    check(plan.preservePuppetMeshForEffectPasses,
          "authored puppet prepass route preserves puppet meshes for effect passes");
    check(plan.publishFinalOutput,
          "non-channelmap puppet effect route publishes from the effect-layer final node");
    check(!plan.effectInputMaterialPreservesLayerBlendMode,
          "flat crop-sheet puppet route does not preserve layer blend mode on the effect input");

    input.puppetFinalMeshOverride = "layer-card";
    const auto legacyPlan = wallpaper::decidePuppetEffectRoutePlan(input);
    check(legacyPlan.useStandalonePuppetFinalDisplay,
          "debug layer-card override keeps the legacy standalone final display route available");
    check(legacyPlan.standaloneFinalMeshKind == "layer-card",
          "debug layer-card override preserves existing route manifest string");
    check(legacyPlan.effectInputMaterialPreservesLayerBlendMode,
          "debug layer-card override keeps legacy overlapping-cutout blend preservation");

    input.puppetFinalMeshOverride = "image-space";
    const auto overridePlan = wallpaper::decidePuppetEffectRoutePlan(input);
    check(overridePlan.standaloneFinalMeshKind == "puppet-image-space-mesh",
          "debug final mesh override preserves existing route manifest string");

    wallpaper::PuppetEffectRoutePlanInput ordinaryInput;
    ordinaryInput.puppetLayer = true;
    ordinaryInput.effectRouteActive = true;
    const auto ordinaryPlan = wallpaper::decidePuppetEffectRoutePlan(ordinaryInput);
    check(!ordinaryPlan.useStandalonePuppetFinalDisplay,
          "ordinary puppet effect route defaults to effect-layer final publish");
    check(!ordinaryPlan.preservePuppetMeshForEffectPasses,
          "ordinary puppet effect route does not preserve puppet meshes through effect passes");

    wallpaper::PuppetEffectRoutePlanInput routeOnlyInput;
    routeOnlyInput.puppetLayer = true;
    routeOnlyInput.debugRouteOnly = true;
    const auto routeOnlyPlan = wallpaper::decidePuppetEffectRoutePlan(routeOnlyInput);
    check(!routeOnlyPlan.useStandalonePuppetFinalDisplay,
          "route-only puppet probe follows the default deferred puppet final route");
    check(routeOnlyPlan.publishFinalOutput,
          "route-only puppet probe keeps effect-layer final publish active");

    wallpaper::PuppetEffectRoutePlanInput deferredInput;
    deferredInput.puppetLayer = true;
    deferredInput.effectRouteActive = true;
    deferredInput.puppetFinalMeshOverride = "deferred-puppet-final";
    const auto deferredPlan = wallpaper::decidePuppetEffectRoutePlan(deferredInput);
    check(!deferredPlan.useStandalonePuppetFinalDisplay,
          "deferred puppet-final debug route skips standalone final display");
    check(deferredPlan.effectFinalMeshKind == "puppet-skinned-mesh",
          "deferred puppet-final debug route preserves puppet final mesh classification");

    wallpaper::PuppetEffectRoutePlanInput channelmapInput;
    channelmapInput.puppetLayer = true;
    channelmapInput.effectRouteActive = true;
    channelmapInput.usePuppetChannelMapPrepass = true;
    const auto suppressedChannelmapPlan = wallpaper::decidePuppetEffectRoutePlan(channelmapInput);
    check(suppressedChannelmapPlan.standaloneFinalMeshKind == "suppressed",
          "channelmap route with no active slots suppresses standalone final mesh");
    channelmapInput.activeChannelBlendSlots = {2};
    const auto activeChannelmapPlan = wallpaper::decidePuppetEffectRoutePlan(channelmapInput);
    check(activeChannelmapPlan.standaloneFinalMeshKind ==
              "puppet-image-space-filtered-overlay-or-puppet-skinned-mesh",
          "channelmap route with active slots keeps filtered overlay classification");
}

void testPuppetFinalDisplayBlendInvariant()
{
    check(static_cast<int>(wallpaper::BlendMode::PremultipliedTranslucent) !=
              static_cast<int>(wallpaper::BlendMode::Translucent),
          "premultiplied puppet final display blend remains distinct from translucent blend");
    check(wallpaper::decidePuppetFinalDisplayBlend() ==
              wallpaper::BlendMode::PremultipliedTranslucent,
          "non-channelmap puppet final display publishes premultiplied offscreen output");
}

void testLayerEffectViewportPolicy()
{
    {
        const auto viewport = wallpaper::policy::decideLayerEffectViewport({
            .objectWidth = 3050.0f,
            .objectHeight = 2650.0f,
            .hasMeshBounds = true,
            .meshPositionMinX = 81.79f,
            .meshPositionMinY = -1299.36f,
            .meshPositionMaxX = 1895.96f,
            .meshPositionMaxY = 913.17f,
        });

        check(viewport.width == 3792,
              "puppet effect viewport expands horizontally to include mesh beyond object bounds");
        check(viewport.height == 2650,
              "puppet effect viewport keeps object height when mesh fits vertically");
        check(viewport.expandedToMeshBounds,
              "puppet effect viewport reports mesh-bound expansion");
    }

    {
        const auto viewport = wallpaper::policy::decideLayerEffectViewport({
            .objectWidth = 4160.0f,
            .objectHeight = 2923.0f,
            .hasMeshBounds = true,
            .meshPositionMinX = -2087.0f,
            .meshPositionMinY = -1403.5f,
            .meshPositionMaxX = 2062.0f,
            .meshPositionMaxY = 1527.5f,
        });

        check(viewport.width == 4160,
              "small puppet mesh bleed keeps authored effect viewport width");
        check(viewport.height == 2923,
              "small puppet mesh bleed keeps authored effect viewport height");
        check(!viewport.expandedToMeshBounds,
              "small puppet mesh bleed does not report viewport expansion");
    }

    {
        const auto viewport = wallpaper::policy::decideLayerEffectViewport({
            .objectWidth = 100.0f,
            .objectHeight = 80.0f,
            .hasMeshBounds = true,
            .meshPositionMinX = -40.0f,
            .meshPositionMinY = -30.0f,
            .meshPositionMaxX = 45.0f,
            .meshPositionMaxY = 35.0f,
        });

        check(viewport.width == 100,
              "in-bounds puppet effect viewport keeps object width");
        check(viewport.height == 80,
              "in-bounds puppet effect viewport keeps object height");
        check(!viewport.expandedToMeshBounds,
              "in-bounds puppet effect viewport does not report expansion");
    }

    {
        const auto viewport = wallpaper::policy::decideLayerEffectViewport({
            .objectWidth = 100.0f,
            .objectHeight = 80.0f,
            .hasMeshBounds = true,
            .meshPositionMinX = -70.0f,
            .meshPositionMinY = -55.0f,
            .meshPositionMaxX = 40.0f,
            .meshPositionMaxY = 35.0f,
        });

        check(viewport.width == 140,
              "puppet effect viewport expands for negative x overflow");
        check(viewport.height == 110,
              "puppet effect viewport expands for negative y overflow");
        check(viewport.expandedToMeshBounds,
              "negative overflow reports viewport expansion");
    }

    {
        const auto viewport = wallpaper::policy::decideLayerEffectViewport({
            .objectWidth = 25.25f,
            .objectHeight = 10.5f,
        });

        check(viewport.width == 25,
              "effect viewport keeps existing fractional object width truncation");
        check(viewport.height == 10,
              "effect viewport keeps existing fractional object height truncation");
        check(!viewport.expandedToMeshBounds,
              "missing mesh bounds uses object size only");
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
        check(stubs.find("Vec3.prototype.add") != std::string::npos,
              "runtime stubs include Vec3.add");
        check(stubs.find("Vec3.prototype.subtract") != std::string::npos,
              "runtime stubs include Vec3.subtract");
        check(stubs.find("Vec3.prototype.multiply") != std::string::npos,
              "runtime stubs include Vec3.multiply");
        check(stubs.find("function __makeSceneScriptLayer") != std::string::npos,
              "runtime stubs include generic layer objects");
        check(stubs.find("getTransformMatrix") != std::string::npos,
              "runtime stubs include layer transform matrices");
        check(stubs.find("getParent") != std::string::npos,
              "runtime stubs include layer parent lookup");
        check(stubs.find("getLayer") != std::string::npos,
              "runtime stubs include scene layer lookup");
        check(stubs.find("smoothStep") != std::string::npos,
              "runtime stubs include WEMath.smoothStep");
        check(stubs.find("mix") != std::string::npos,
              "runtime stubs include WEMath.mix");
        check(stubs.find("lerp") != std::string::npos,
              "runtime stubs include WEMath.lerp");
        check(stubs.find("clamp") != std::string::npos,
              "runtime stubs include WEMath.clamp");
        check(stubs.find("localStorage") != std::string::npos,
              "runtime stubs include localStorage");
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

    {
        const auto gap = wallpaper::policy::classifySceneScriptRuntimeGap(
            "ReferenceError: 'scene' is not defined", "");
        check(gap.kind == wallpaper::policy::SceneScriptRuntimeGapKind::Visible,
              "missing scene object is a visible script gap");
        check(gap.api == "scene", "missing scene object records API name");
    }

    {
        const auto gap = wallpaper::policy::classifySceneScriptRuntimeGap(
            "ReferenceError: 'MediaPlaybackEvent' is not defined", "");
        check(gap.kind == wallpaper::policy::SceneScriptRuntimeGapKind::MediaRuntimeOnly,
              "missing MediaPlaybackEvent is media/runtime-only");
        check(gap.api == "MediaPlaybackEvent", "missing media event records API name");
    }

    {
        const auto gap = wallpaper::policy::classifySceneScriptRuntimeGap(
            "ReferenceError: 'console' is not defined", "");
        check(gap.kind == wallpaper::policy::SceneScriptRuntimeGapKind::Harmless,
              "missing console logging is harmless");
        check(gap.api == "console", "missing console records API name");
    }

    {
        const auto gap = wallpaper::policy::classifySceneScriptRuntimeGap(
            "TypeError: cannot read property 'scale' of undefined", "");
        check(gap.kind == wallpaper::policy::SceneScriptRuntimeGapKind::Visible,
              "missing scale vector/object property is visible");
        check(gap.api == "undefined.scale", "missing scale records property API name");
    }
}

void testSceneScriptEvaluatorRuntimeStubs()
{
    wallpaper::SceneScriptContext ctx;
    ctx.setUserProperties(nlohmann::json::object());
    ctx.setCanvasSize(1920, 1080);

    {
        const std::string script = R"(
            export function update(value) {
                const layer = thisScene.getLayer("day");
                layer.getVideoTexture().play();
                scene.getLayer("myLayer").visible = false;
                return value.add(new Vec3(2, 3, 0));
            }
        )";

        const auto result = ctx.evaluateLayerScript(script, {10.0f, 20.0f, 0.0f});
        check(result.origin.has_value(),
              "SceneScript evaluator resolves scene layer lookup without throwing");
        if (result.origin) {
            check(nearFloat((*result.origin)[0], 12.0f),
                  "SceneScript Vec3.add updates x");
            check(nearFloat((*result.origin)[1], 23.0f),
                  "SceneScript Vec3.add updates y");
        }
    }

    {
        const std::string script = R"(
            let parentLayer;
            let initialOrigin;

            export function init(value) {
                parentLayer = thisLayer.getParent();
                initialOrigin = value;
            }

            export function update(value) {
                const matrix = thisLayer.getTransformMatrix().m;
                return initialOrigin
                    .add(new Vec3(matrix[12], matrix[13], 0))
                    .add(parentLayer.origin)
                    .subtract(new Vec3(5, 5, 0))
                    .multiply(new Vec3(1, 1, 0));
            }
        )";

        const auto result = ctx.evaluateLayerScript(script, {10.0f, 20.0f, 0.0f});
        check(result.origin.has_value(),
              "SceneScript evaluator passes the current value into init and exposes transform stubs");
        if (result.origin) {
            check(nearFloat((*result.origin)[0], 15.0f),
                  "SceneScript transform matrix and init value update x");
            check(nearFloat((*result.origin)[1], 35.0f),
                  "SceneScript transform matrix and init value update y");
        }
    }

    {
        wallpaper::SceneScriptContext comboCtx;
        comboCtx.setUserProperties(nlohmann::json::object());
        comboCtx.setScriptProperty("mode", 2.0);

        const std::string script = R"(
            export var scriptProperties = createScriptProperties()
                .addCombo({
                    name: 'mode',
                    options: [
                        { label: 'One', value: '1' },
                        { label: 'Two', value: '2' }
                    ]
                })
                .finish();

            export function update(value) {
                if (scriptProperties.mode == 2) {
                    return value.add(new Vec3(4, 0, 0));
                }
                return missingComboOverride.x;
            }
        )";

        const auto result = comboCtx.evaluateLayerScript(script, {10.0f, 20.0f, 0.0f});
        check(result.origin.has_value(),
              "SceneScript combo builder uses script property overrides");
        if (result.origin) {
            check(nearFloat((*result.origin)[0], 14.0f),
                  "SceneScript combo override updates x");
        }
    }

    {
        wallpaper::SceneScriptContext comboCtx;
        comboCtx.setUserProperties(nlohmann::json::object());

        const std::string script = R"(
            export var scriptProperties = createScriptProperties()
                .addCombo({
                    name: 'mode',
                    options: [
                        { label: 'Two', value: '2' },
                        { label: 'Three', value: '3' }
                    ]
                })
                .finish();

            export function update(value) {
                if (scriptProperties.mode == 2) {
                    return value.add(new Vec3(5, 0, 0));
                }
                return missingComboDefault.x;
            }
        )";

        const auto result = comboCtx.evaluateLayerScript(script, {10.0f, 20.0f, 0.0f});
        check(result.origin.has_value(),
              "SceneScript combo builder falls back to the first option value");
        if (result.origin) {
            check(nearFloat((*result.origin)[0], 15.0f),
                  "SceneScript combo default option updates x");
        }
    }

    {
        wallpaper::SceneScriptContext textCtx;
        textCtx.setUserProperties({
            {"clock", {{"type", "bool"}, {"value", true}}}
        });
        textCtx.setScriptProperty("showTime", 1.0);

        const std::string script = R"(
            export var scriptProperties = createScriptProperties()
                .addCheckbox({
                    name: 'showTime',
                    value: false
                })
                .finish();

            export function update(value) {
                if (scriptProperties.showTime) {
                    return '12:34';
                }
                return value;
            }
        )";

        const auto result = textCtx.evaluateLayerScript(script, {0.0f, 0.0f, 0.0f});
        check(result.text.has_value(),
              "SceneScript evaluator captures string return values for text objects");
        if (result.text) {
            check(*result.text == "12:34",
                  "SceneScript text result uses script property override");
        }
    }
}

void testTextSceneScriptOriginBindingAppliesToTextNode()
{
    const auto root =
        std::filesystem::temp_directory_path() / "yakkai-text-scenescript-origin-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    wallpaper::fs::VFS vfs;
    check(vfs.Mount("/assets", wallpaper::fs::CreatePhysicalFs(root.string())),
          "test VFS mounts text SceneScript fixture");

    const std::string script = R"(
        export function update(value) {
            thisLayer.origin = new Vec3(12, 34, 0);
            return 'scripted text';
        }
    )";

    nlohmann::json sceneJson = {
        {"camera", {
            {"center", "0 0 0"},
            {"eye", "0 0 1"},
            {"up", "0 1 0"}
        }},
        {"general", {
            {"ambientcolor", "0.2 0.2 0.2"},
            {"skylightcolor", "0.3 0.3 0.3"},
            {"clearcolor", "0 0 0"},
            {"cameraparallax", false},
            {"cameraparallaxamount", 0.0},
            {"cameraparallaxdelay", 0.0},
            {"cameraparallaxmouseinfluence", 0.0},
            {"orthogonalprojection", {
                {"width", 1280},
                {"height", 720}
            }}
        }},
        {"objects", nlohmann::json::array({
            {
                {"id", 101},
                {"name", "ScriptedText"},
                {"origin", "1 2 0"},
                {"scale", "1 1 1"},
                {"angles", "0 0 0"},
                {"size", "256 64"},
                {"visible", true},
                {"text", {
                    {"value", "fallback"},
                    {"script", script}
                }}
            }
        })}
    };

    wallpaper::audio::SoundManager soundManager;
    wallpaper::WPSceneParser parser;
    const auto scene = parser.Parse("text_scenescript_origin_test", sceneJson.dump(), vfs, soundManager);
    check(scene != nullptr, "text SceneScript fixture parses");
    if (!scene || !scene->sceneGraph) {
        return;
    }

    check(findNodeByTranslate(scene->sceneGraph.get(), {12.0f, 34.0f, 0.0f}) != nullptr,
          "text SceneScript origin binding moves the generated text node");
    check(findNodeByTranslate(scene->sceneGraph.get(), {1.0f, 2.0f, 0.0f}) == nullptr,
          "text node does not keep authored origin after script origin binding resolves");
}

void testPuppetMeshTexcoordsApplyTextureMapRate()
{
    wallpaper::WPMdl mdl;
    mdl.mat_json_file = "materials/test.json";
    mdl.vertexs.resize(3);
    mdl.indices.push_back({0, 1, 2});

    mdl.vertexs[0].position = {0.0f, 0.0f, 0.0f};
    mdl.vertexs[0].texcoord = {0.25f, 0.50f};
    mdl.vertexs[1].position = {1.0f, 0.0f, 0.0f};
    mdl.vertexs[1].texcoord = {1.0f, 1.0f};
    mdl.vertexs[2].position = {0.0f, 1.0f, 0.0f};
    mdl.vertexs[2].texcoord = {0.0f, 0.75f};

    wallpaper::SceneMesh mesh;
    wallpaper::WPMdlParser::GenPuppetMesh(mesh, mdl, std::array<float, 2> {0.5f, 0.25f});

    check(mesh.VertexCount() == 1, "puppet mesh has one vertex array");
    const auto uv0 = readTexCoord(mesh.GetVertexArray(0), 0);
    const auto uv1 = readTexCoord(mesh.GetVertexArray(0), 1);
    const auto uv2 = readTexCoord(mesh.GetVertexArray(0), 2);

    check(nearFloat(uv0[0], 0.125f) && nearFloat(uv0[1], 0.125f),
          "puppet mesh scales first UV by texture map rate");
    check(nearFloat(uv1[0], 0.5f) && nearFloat(uv1[1], 0.25f),
          "puppet mesh scales full-range UV by texture map rate");
    check(nearFloat(uv2[0], 0.0f) && nearFloat(uv2[1], 0.1875f),
          "puppet mesh scales partial V UV by texture map rate");
}

void testPuppetFilteredMeshIncludesSecondaryOnlyWeightedChildren()
{
    wallpaper::WPMdl mdl;
    mdl.mat_json_file = "materials/test.json";
    mdl.puppet = std::make_shared<wallpaper::WPPuppet>();
    mdl.puppet->bones.resize(14);
    mdl.puppet->bones[0].parent = 0xFFFFFFFFu;
    mdl.puppet->bones[3].parent = 0;
    mdl.puppet->bones[8].parent = 0;
    mdl.puppet->bones[13].parent = 3;

    mdl.vertexs.resize(6);
    for (size_t i = 0; i < mdl.vertexs.size(); ++i) {
        mdl.vertexs[i].position = {
            static_cast<float>(i % 3),
            static_cast<float>(i / 3),
            0.0f,
        };
        mdl.vertexs[i].texcoord = {0.0f, 0.0f};
        mdl.vertexs[i].weight = {1.0f, 0.0f, 0.0f, 0.0f};
    }

    for (size_t i = 0; i < 3; ++i) {
        mdl.vertexs[i].blend_indices = {8, 13, 0, 0};
        mdl.vertexs[i].weight = {0.4f, 0.6f, 0.0f, 0.0f};
    }
    for (size_t i = 3; i < 6; ++i) {
        mdl.vertexs[i].blend_indices = {8, 0, 0, 0};
    }
    mdl.indices.push_back({0, 1, 2});
    mdl.indices.push_back({3, 4, 5});

    wallpaper::SceneMesh mesh;
    const std::array<uint32_t, 1> activeParentSlots {3};
    const bool filtered =
        wallpaper::WPMdlParser::GenPuppetMesh(mesh, mdl, activeParentSlots);

    check(filtered, "puppet filtered mesh treats secondary-only weighted child as active");
    check(mesh.VertexCount() == 1, "filtered puppet mesh has one vertex array");
    check(mesh.GetVertexArray(0).VertexCount() == 3,
          "filtered puppet mesh keeps only the child-weighted triangle");
}

std::shared_ptr<wallpaper::WPPuppet> makeTwoBoneSimulationPuppet()
{
    auto puppet = std::make_shared<wallpaper::WPPuppet>();
    puppet->bones.resize(2);
    puppet->bones[0].name = "root";
    puppet->bones[0].parent = 0xFFFFFFFFu;
    puppet->bones[0].transform = Eigen::Affine3f::Identity();

    puppet->bones[1].name = "child";
    puppet->bones[1].parent = 0;
    puppet->bones[1].transform = Eigen::Affine3f::Identity();
    puppet->bones[1].transform.pretranslate(Eigen::Vector3f(0.0f, 10.0f, 0.0f));
    puppet->bones[1].simulationMetadata =
        R"({"t":true,"tm":100.0,"tp":"0.00000 200.00000 0.00000"})";
    puppet->bones[1].parsedSimulationMetadata =
        wallpaper::WPMdlParser::ParseBoneSimulationMetadata(
            puppet->bones[1].simulationMetadata);

    wallpaper::WPPuppet::Animation animation;
    animation.id = 1;
    animation.name = "RootMove";
    animation.fps = 30.0;
    animation.length = 2;
    animation.mode = wallpaper::WPPuppet::PlayMode::Loop;
    animation.bframes_array.resize(2);

    for (auto& boneFrames : animation.bframes_array) {
        boneFrames.frames.resize(2);
        for (auto& frame : boneFrames.frames) {
            frame.position = Eigen::Vector3f::Zero();
            frame.angle = Eigen::Vector3f::Zero();
            frame.scale = Eigen::Vector3f::Ones();
        }
    }
    animation.bframes_array[0].frames[1].position = Eigen::Vector3f(20.0f, 0.0f, 0.0f);

    puppet->anims.push_back(animation);
    puppet->prepared();
    return puppet;
}

void testPuppetSimulationOffModePreservesOutput()
{
    auto puppet = makeTwoBoneSimulationPuppet();
    wallpaper::WPPuppetLayer::AnimationLayer layer;
    layer.id = 1;
    layer.visible = true;
    layer.blend = 1.0;

    std::array<wallpaper::WPPuppetLayer::AnimationLayer, 1> layers { layer };
    wallpaper::WPPuppetLayer puppetLayer(puppet);
    puppetLayer.prepared(layers);
    puppetLayer.setSimulationMode(wallpaper::PuppetSimulationMode::Off);

    const auto frame = puppetLayer.genFrame(1.0 / 30.0);
    check(frame.size() == 2, "off-mode synthetic puppet returns two bone transforms");
    check(std::isfinite(frame[1].matrix()(0, 3)) &&
              std::isfinite(frame[1].matrix()(1, 3)),
          "off-mode synthetic puppet returns finite child transform");
}

void testPuppetSimulationEligibilitySkipsAuthoredAnimatedBones()
{
    auto puppet = makeTwoBoneSimulationPuppet();
    wallpaper::WPPuppetLayer::AnimationLayer layer;
    layer.id = 1;
    layer.visible = true;
    layer.blend = 1.0;

    std::array<wallpaper::WPPuppetLayer::AnimationLayer, 1> layers { layer };
    wallpaper::WPPuppetLayer puppetLayer(puppet);
    puppetLayer.prepared(layers);

    check(!puppetLayer.isBoneEligibleForSimulationForTests(0),
          "root bone without simulation metadata is not eligible");
    check(puppetLayer.isBoneEligibleForSimulationForTests(1),
          "inactive simulated child bone is eligible");
}

void testPuppetSimulationRuntimeMovesEligibleChild()
{
    auto puppet = makeTwoBoneSimulationPuppet();
    wallpaper::WPPuppetLayer::AnimationLayer layer;
    layer.id = 1;
    layer.visible = true;
    layer.blend = 1.0;

    std::array<wallpaper::WPPuppetLayer::AnimationLayer, 1> layers { layer };
    wallpaper::WPPuppetLayer offLayer(puppet);
    offLayer.prepared(layers);
    offLayer.setSimulationMode(wallpaper::PuppetSimulationMode::Off);
    offLayer.genFrame(1.0 / 30.0);
    const auto offSecond = offLayer.genFrame(1.0 / 30.0);
    const Eigen::Vector3f offTranslation = offSecond[1].translation();

    wallpaper::WPPuppetLayer runtimeLayer(puppet);
    runtimeLayer.prepared(layers);
    runtimeLayer.setSimulationMode(wallpaper::PuppetSimulationMode::Runtime);
    runtimeLayer.genFrame(1.0 / 30.0);
    const auto runtimeSecond = runtimeLayer.genFrame(1.0 / 30.0);

    const Eigen::Vector3f runtimeTranslation = runtimeSecond[1].translation();
    const float runtimeDelta = (runtimeTranslation - offTranslation).norm();

    check(std::isfinite(runtimeDelta), "runtime puppet simulation keeps finite child output");
    check(runtimeDelta > 1.0e-5f,
          "runtime puppet simulation differs from off-mode for eligible child");
    check(runtimeDelta < 25.0f,
          "runtime puppet simulation child movement stays bounded against off-mode");
}

void testPuppetSimulationModeReadsEnvironmentWhenUnset()
{
    const char* previous = std::getenv("YAKKAI_PUPPET_SIMULATION");
    const bool hadPrevious = previous != nullptr;
    const std::string previousValue = hadPrevious ? std::string(previous) : std::string();
    const auto restoreEnvironment = [&]() {
        if (hadPrevious) {
            setenv("YAKKAI_PUPPET_SIMULATION", previousValue.c_str(), 1);
        } else {
            unsetenv("YAKKAI_PUPPET_SIMULATION");
        }
    };

    auto puppet = makeTwoBoneSimulationPuppet();
    wallpaper::WPPuppetLayer::AnimationLayer layer;
    layer.id = 1;
    layer.visible = true;
    layer.blend = 1.0;
    std::array<wallpaper::WPPuppetLayer::AnimationLayer, 1> layers { layer };

    unsetenv("YAKKAI_PUPPET_SIMULATION");
    wallpaper::WPPuppetLayer defaultLayer(puppet);
    defaultLayer.prepared(layers);
    check(defaultLayer.simulationMode() == wallpaper::PuppetSimulationMode::Off,
          "unset puppet simulation environment keeps mode off");

    setenv("YAKKAI_PUPPET_SIMULATION", "runtime", 1);
    wallpaper::WPPuppetLayer runtimeLayer(puppet);
    runtimeLayer.prepared(layers);
    check(runtimeLayer.simulationMode() == wallpaper::PuppetSimulationMode::Runtime,
          "runtime puppet simulation environment enables runtime mode");

    setenv("YAKKAI_PUPPET_SIMULATION", "diagnostic", 1);
    wallpaper::WPPuppetLayer explicitLayer(puppet);
    explicitLayer.setSimulationMode(wallpaper::PuppetSimulationMode::Off);
    explicitLayer.prepared(layers);
    check(explicitLayer.simulationMode() == wallpaper::PuppetSimulationMode::Off,
          "explicit puppet simulation mode overrides environment");

    restoreEnvironment();
}

void testPuppetSimulationMetadataParser()
{
    const auto parsed = wallpaper::WPMdlParser::ParseBoneSimulationMetadata(
        R"({"a":null,"s":null,"tm":100.0,"tp":"0.00000 200.00000 0.00000"})");

    check(parsed.present, "puppet simulation parser marks non-empty metadata present");
    check(parsed.valid, "puppet simulation parser accepts WE JSON object metadata");
    check(parsed.targetPointPresent, "puppet simulation parser extracts target point");
    check(nearFloat(parsed.targetPoint[0], 0.0f) &&
          nearFloat(parsed.targetPoint[1], 200.0f) &&
          nearFloat(parsed.targetPoint[2], 0.0f),
          "puppet simulation parser parses target point vector");
    check(parsed.targetMassPresent, "puppet simulation parser extracts target mass");
    check(nearFloat(parsed.targetMass, 100.0f),
          "puppet simulation parser parses target mass");
    check(!parsed.physicsActive,
          "target-only puppet metadata is not active physics metadata");

    const auto active = wallpaper::WPMdlParser::ParseBoneSimulationMetadata(
        R"({"r":true,"t":false,"s":0,"tm":200.0,"tp":"100.00000 0.00000 0.00000"})");
    check(active.valid, "active puppet simulation parser accepts WE JSON metadata");
    check(active.physicsActive, "active puppet simulation parser detects enabled physics fields");

    const auto empty = wallpaper::WPMdlParser::ParseBoneSimulationMetadata("");
    check(!empty.present, "empty puppet simulation metadata is not present");
    check(!empty.valid, "empty puppet simulation metadata is not valid");
    check(!empty.targetPointPresent, "empty puppet simulation metadata has no target point");

    const auto invalid = wallpaper::WPMdlParser::ParseBoneSimulationMetadata("{not-json");
    check(invalid.present, "invalid non-empty puppet simulation metadata is still present");
    check(!invalid.valid, "invalid puppet simulation metadata is not valid");
    check(!invalid.targetPointPresent, "invalid puppet simulation metadata has no target point");
}

void testPuppetSimulationHelperRequiresActivePhysics()
{
    wallpaper::PuppetSimulationBoneInput bone;
    bone.hasParent = true;
    bone.parent = 0;
    bone.metadata.valid = true;
    bone.metadata.targetPointPresent = true;
    bone.metadata.targetPoint = {0.0f, 200.0f, 0.0f};
    bone.metadata.targetMass = 100.0f;

    check(!wallpaper::IsBoneEligibleForRuntimeSimulation(bone, false),
          "runtime simulation helper rejects target-only metadata without active physics");

    bone.metadata.physicsActive = true;
    check(wallpaper::IsBoneEligibleForRuntimeSimulation(bone, false),
          "runtime simulation helper accepts active physics metadata without authored deltas");
    check(!wallpaper::IsBoneEligibleForRuntimeSimulation(bone, true),
          "runtime simulation helper rejects authored animated bones");
}

wallpaper::PuppetSimulationBoneInput simulationBoneForTest(bool hasParent, uint32_t parent)
{
    wallpaper::PuppetSimulationBoneInput bone;
    bone.hasParent = hasParent;
    bone.parent = parent;
    bone.metadata.valid = true;
    bone.metadata.physicsActive = true;
    bone.metadata.targetPointPresent = true;
    bone.metadata.targetPoint = {0.0f, 200.0f, 0.0f};
    bone.metadata.targetMass = 100.0f;
    return bone;
}

void testPuppetSimulationRejectsInvalidParentOrder()
{
    std::vector<wallpaper::PuppetSimulationBoneInput> bones {
        simulationBoneForTest(false, 0xFFFFFFFFu),
        simulationBoneForTest(true, 1),
        simulationBoneForTest(true, 1),
    };
    std::vector<bool> authoredDeltaBones(bones.size(), false);
    std::vector<wallpaper::PuppetSimulationBoneState> states(bones.size());
    std::vector<Eigen::Affine3f> worldAffines(bones.size(), Eigen::Affine3f::Identity());
    worldAffines[1].pretranslate(Eigen::Vector3f(0.0f, 10.0f, 0.0f));
    worldAffines[2].pretranslate(Eigen::Vector3f(0.0f, 20.0f, 0.0f));

    wallpaper::ApplyRuntimePuppetSimulationStep(
        1.0 / 30.0, bones, authoredDeltaBones, states, worldAffines);

    check(!states[1].initialized,
          "runtime simulation rejects self-parented bones");

    bones[1].parent = 2;
    wallpaper::ApplyRuntimePuppetSimulationStep(
        1.0 / 30.0, bones, authoredDeltaBones, states, worldAffines);

    check(!states[1].initialized,
          "runtime simulation rejects forward-parented bones");
}

void testPuppetSimulationPropagatesSimulatedParentToDescendants()
{
    std::vector<wallpaper::PuppetSimulationBoneInput> bones {
        simulationBoneForTest(false, 0xFFFFFFFFu),
        simulationBoneForTest(true, 0),
        simulationBoneForTest(true, 1),
    };
    bones[0].metadata = {};
    bones[2].metadata = {};

    std::vector<bool> authoredDeltaBones(bones.size(), false);
    std::vector<wallpaper::PuppetSimulationBoneState> states(bones.size());

    std::vector<Eigen::Affine3f> worldAffines(bones.size(), Eigen::Affine3f::Identity());
    worldAffines[1].pretranslate(Eigen::Vector3f(0.0f, 10.0f, 0.0f));
    worldAffines[2].pretranslate(Eigen::Vector3f(0.0f, 15.0f, 0.0f));
    wallpaper::ApplyRuntimePuppetSimulationStep(
        1.0 / 30.0, bones, authoredDeltaBones, states, worldAffines);

    std::vector<Eigen::Affine3f> movedAffines(bones.size(), Eigen::Affine3f::Identity());
    movedAffines[0].pretranslate(Eigen::Vector3f(20.0f, 0.0f, 0.0f));
    movedAffines[1].pretranslate(Eigen::Vector3f(20.0f, 10.0f, 0.0f));
    movedAffines[2].pretranslate(Eigen::Vector3f(20.0f, 15.0f, 0.0f));
    wallpaper::ApplyRuntimePuppetSimulationStep(
        1.0 / 30.0, bones, authoredDeltaBones, states, movedAffines);

    const Eigen::Vector3f parentDelta =
        movedAffines[1].translation() - Eigen::Vector3f(20.0f, 10.0f, 0.0f);
    const Eigen::Vector3f childDelta =
        movedAffines[2].translation() - Eigen::Vector3f(20.0f, 15.0f, 0.0f);

    check(parentDelta.norm() > 1.0e-5f,
          "runtime simulation moves eligible non-leaf parent in the synthetic hierarchy");
    check((childDelta - parentDelta).norm() < 1.0e-4f,
          "runtime simulation propagates simulated parent movement to child bones");
}

void testPuppetSimulationModeParser()
{
    check(wallpaper::ParsePuppetSimulationMode("") ==
              wallpaper::PuppetSimulationMode::Off,
          "empty puppet simulation mode defaults off");
    check(wallpaper::ParsePuppetSimulationMode("off") ==
              wallpaper::PuppetSimulationMode::Off,
          "off puppet simulation mode parses");
    check(wallpaper::ParsePuppetSimulationMode("diagnostic") ==
              wallpaper::PuppetSimulationMode::Diagnostic,
          "diagnostic puppet simulation mode parses");
    check(wallpaper::ParsePuppetSimulationMode("runtime") ==
              wallpaper::PuppetSimulationMode::Runtime,
          "runtime puppet simulation mode parses");
    check(wallpaper::ParsePuppetSimulationMode("bad-value") ==
              wallpaper::PuppetSimulationMode::Off,
          "invalid puppet simulation mode falls back off");
}

void testPuppetCutoutSlotCoverageJsonIncludesParsedSimulationFields()
{
    const auto outDir =
        std::filesystem::temp_directory_path() / "yakkai-puppet-simulation-metadata-test";
    std::filesystem::remove_all(outDir);

    wallpaper::debug::PuppetCutoutSlotCoverageInfo slot;
    slot.slot = 3;
    slot.active = false;
    slot.boneName = "ribbon";
    slot.simulationMetadataPresent = true;
    slot.simulationMetadataValid = true;
    slot.simulationPhysicsActive = true;
    slot.simulationTargetPointPresent = true;
    slot.simulationTargetPoint = {0.0f, 200.0f, 0.0f};
    slot.simulationTargetMassPresent = true;
    slot.simulationTargetMass = 100.0f;
    slot.simulatedInactive = true;
    slot.layerLocalBounds = {-10.0f, -20.0f, 30.0f, 40.0f};
    slot.layerLocalCentroid = {5.0f, 10.0f};

    wallpaper::Scene scene;
    scene.scene_id = "unit-scene";
    scene.debugEffectCaptures = {
        .outputDir = outDir.string(),
        .commandLine = "unit --debug-effect-captures " + outDir.string(),
    };

    wallpaper::debug::EffectCaptureLayerInfo layer;
    layer.sceneId = "unit-scene";
    layer.layerId = 405;
    layer.publish.enabled = true;
    layer.publish.puppetCutoutSlotCoverage.push_back(slot);

    wallpaper::debug::recordStrippedEffectCandidate(scene, layer);
    check(wallpaper::debug::writeEffectCaptureManifest(scene),
          "manifest writes puppet simulation coverage fields");

    const nlohmann::json manifest =
        nlohmann::json::parse(readTextFile(scene.debugEffectCaptures.manifestPath()));
    const auto& coverage = manifest.at("strippedCandidates").at(0)
        .at("publish").at("puppetCutoutSlotCoverage").at(0);

    check(coverage.at("simulationMetadataValid").get<bool>(),
          "puppet cutout coverage serializes parsed simulation metadata validity");
    check(coverage.at("simulationPhysicsActive").get<bool>(),
          "puppet cutout coverage serializes active physics metadata state");
    check(coverage.at("simulationTargetPointPresent").get<bool>(),
          "puppet cutout coverage serializes target point presence");
    check(nearFloat(coverage.at("simulationTargetPoint").at(1).get<float>(), 200.0f),
          "puppet cutout coverage serializes parsed target point");
    check(coverage.at("simulationTargetMassPresent").get<bool>(),
          "puppet cutout coverage serializes target mass presence");
    check(nearFloat(coverage.at("simulationTargetMass").get<float>(), 100.0f),
          "puppet cutout coverage serializes parsed target mass");
    check(coverage.at("simulatedInactive").get<bool>(),
          "puppet cutout coverage flags inactive slots with active physics metadata");
    check(nearFloat(coverage.at("layerLocalBounds").at(0).get<float>(), -10.0f) &&
              nearFloat(coverage.at("layerLocalBounds").at(3).get<float>(), 40.0f),
          "puppet cutout coverage serializes layer-local bounds");
    check(nearFloat(coverage.at("layerLocalCentroid").at(0).get<float>(), 5.0f) &&
              nearFloat(coverage.at("layerLocalCentroid").at(1).get<float>(), 10.0f),
          "puppet cutout coverage serializes layer-local centroid");

    std::filesystem::remove_all(outDir);
}

void testTranslucentBlendPreservesSourceAlpha()
{
    VkPipelineColorBlendAttachmentState blend {};
    wallpaper::vulkan::SetBlend(wallpaper::BlendMode::Translucent, blend);

    check(blend.blendEnable == VK_TRUE, "translucent blending is enabled");
    check(blend.srcColorBlendFactor == VK_BLEND_FACTOR_SRC_ALPHA,
          "translucent color uses source alpha");
    check(blend.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          "translucent color preserves destination inverse alpha");
    check(blend.srcAlphaBlendFactor == VK_BLEND_FACTOR_ONE,
          "translucent alpha preserves the source alpha channel");
    check(blend.dstAlphaBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          "translucent alpha fades existing destination alpha");
}

void testPremultipliedTranslucentBlendUsesStoredColor()
{
    VkPipelineColorBlendAttachmentState blend {};
    wallpaper::vulkan::SetBlend(wallpaper::BlendMode::PremultipliedTranslucent, blend);

    check(blend.blendEnable == VK_TRUE, "premultiplied translucent blending is enabled");
    check(blend.srcColorBlendFactor == VK_BLEND_FACTOR_ONE,
          "premultiplied translucent color uses stored source RGB");
    check(blend.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          "premultiplied translucent color fades destination inverse alpha");
    check(blend.srcAlphaBlendFactor == VK_BLEND_FACTOR_ONE,
          "premultiplied translucent alpha preserves source alpha");
    check(blend.dstAlphaBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          "premultiplied translucent alpha fades existing destination alpha");
}

void testSourceAlphaPreservePatch()
{
    wallpaper::Combos combos;
    combos["YAKKAI_PRESERVE_SOURCE_ALPHA"] = "1";

    const std::string source = R"(
varying vec4 v_TexCoord;
uniform sampler2D g_Texture0;
void main() {
    vec2 texCoord = v_TexCoord.xy + vec2(0.1, 0.0);
    gl_FragColor = texSample2D(g_Texture0, texCoord);
}
)";

    const std::string patched =
        wallpaper::ApplySourceAlphaPreservePatch(
            source, combos, wallpaper::ShaderType::FRAGMENT);

    check(patched.find("yakkaiPreserveSourceAlphaColor") != std::string::npos,
          "alpha-preserve patch introduces a named temporary");
    check(patched.find("yakkaiPreserveSourceAlphaSource = texSample2D(g_Texture0, v_TexCoord.xy)") !=
              std::string::npos,
          "alpha-preserve patch samples source color and alpha from undisplaced UV");
    check(patched.find("yakkaiPreserveSourceAlphaColor.rgb = mix(yakkaiPreserveSourceAlphaSource.rgb") !=
              std::string::npos,
          "alpha-preserve patch avoids displaced transparent RGB at restored-alpha edges");
    check(patched.find(
              "yakkaiPreserveSourceAlphaColor.a = yakkaiPreserveSourceAlphaSource.a") !=
              std::string::npos,
          "alpha-preserve patch samples alpha from the undisplaced source UV");

    const std::string shakeSource = R"(
varying vec4 v_TexCoord;
uniform sampler2D g_Texture0;
void main() {
    vec2 texCoordOffset = vec2(0.1, 0.0);
    gl_FragColor = texSample2D(g_Texture0, texCoordOffset + v_TexCoord.xy);
}
)";

    const std::string patchedShake =
        wallpaper::ApplySourceAlphaPreservePatch(
            shakeSource, combos, wallpaper::ShaderType::FRAGMENT);
    check(patchedShake.find("yakkaiPreserveSourceAlphaColor") != std::string::npos,
          "displaced direct-sample alpha-preserve patch introduces a named temporary");
    check(patchedShake.find("yakkaiPreserveSourceAlphaSource = texSample2D(g_Texture0, v_TexCoord.xy)") !=
              std::string::npos,
          "displaced direct-sample alpha-preserve patch samples source color and alpha from undisplaced UV");
    check(patchedShake.find("yakkaiPreserveSourceAlphaColor.rgb = mix(yakkaiPreserveSourceAlphaSource.rgb") !=
              std::string::npos,
          "displaced direct-sample alpha-preserve patch avoids displaced transparent RGB at restored-alpha edges");
    check(patchedShake.find(
              "yakkaiPreserveSourceAlphaColor.a = yakkaiPreserveSourceAlphaSource.a") !=
              std::string::npos,
          "displaced direct-sample alpha-preserve patch samples alpha from the undisplaced source UV");

    wallpaper::Combos disabledCombos;
    const std::string unchanged =
        wallpaper::ApplySourceAlphaPreservePatch(
            source, disabledCombos, wallpaper::ShaderType::FRAGMENT);
    check(unchanged == source,
          "alpha-preserve patch leaves shaders unchanged without the combo");

    check(wallpaper::ApplySourceAlphaPreservePatch(source, combos, wallpaper::ShaderType::VERTEX) ==
              source,
          "alpha-preserve patch leaves vertex shaders unchanged");

    auto unrelated = wallpaper::ApplySourceAlphaPreservePatch(
        "vec4 color = texSample2D(g_Texture0, v_TexCoord.xy);",
        combos,
        wallpaper::ShaderType::FRAGMENT);
    check(unrelated.find("yakkaiPreserveSourceAlphaColor") == std::string::npos,
          "alpha-preserve patch leaves unrelated fragment shaders unchanged");
}

void testPuppetSourceAlphaPreserveShaderPolicy()
{
    check(wallpaper::ShouldPreservePuppetSourceAlphaForShader("effects/shake"),
          "puppet source-alpha patch remains enabled for shake");
    check(wallpaper::ShouldPreservePuppetSourceAlphaForShader("workshop/effects/shake"),
          "puppet source-alpha patch matches nested shake shader paths");
    check(!wallpaper::ShouldPreservePuppetSourceAlphaForShader("effects/waterwaves"),
          "puppet source-alpha patch is not enabled for waterwaves after Windows pass evidence");
    check(!wallpaper::ShouldPreservePuppetSourceAlphaForShader("workshop/effects/waterwaves"),
          "puppet source-alpha patch does not match nested waterwaves shader paths");
}

void testTexbV4SpriteHeaderPolicy()
{
    const auto root =
        std::filesystem::temp_directory_path() / "yakkai-texb-v4-sprite-header-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "materials/particle");

    const auto fixture = makeTexbV4SpriteFixture();
    {
        std::ofstream out(root / "materials/particle/v4_sprite.tex", std::ios::binary);
        out.write(reinterpret_cast<const char*>(fixture.data()),
                  static_cast<std::streamsize>(fixture.size()));
    }

    wallpaper::fs::VFS vfs;
    check(vfs.Mount("/assets", wallpaper::fs::CreatePhysicalFs(root.string())),
          "test VFS mounts TEXB v4 sprite fixture");

    wallpaper::WPTexImageParser parser(&vfs);
    wallpaper::ImageHeader header;
    bool threw = false;
    try {
        header = parser.ParseHeader("particle/v4_sprite");
    } catch (const std::exception&) {
        threw = true;
    }

    check(!threw,
          "TEXB v4 sprite headers skip flat image records before reading frame ids");
    if (threw) {
        return;
    }

    check(header.isSprite, "TEXB v4 sprite fixture remains marked as sprite");
    check(header.extraHeader.at("texb").val == 4, "TEXB v4 fixture records texb version");
    check(header.spriteAnim.numFrames() == 1, "TEXB v4 sprite fixture parses one frame");
    if (header.spriteAnim.numFrames() == 1) {
        const auto& frame = header.spriteAnim.GetCurFrame();
        check(frame.imageId == 0, "TEXB v4 sprite frame keeps image slot zero");
    }
}

void testShaderCompatPolicy()
{
    wallpaper::fs::VFS vfs;
    wallpaper::WPShaderInfo shaderInfo;
    std::vector<wallpaper::ShaderCode> spvs;
    std::vector<wallpaper::WPShaderUnit> units {
        {
            .stage = wallpaper::ShaderType::VERTEX,
            .src = R"(
attribute vec3 a_Position;
attribute vec2 a_TexCoord;
varying vec2 v_TexCoord;
void main() {
    v_TexCoord = a_TexCoord;
    gl_Position = vec4(a_Position, 1.0);
}
)",
        },
        {
            .stage = wallpaper::ShaderType::FRAGMENT,
            .src = R"(
varying vec2 v_TexCoord;
uniform sampler2D g_Texture0;
void main() {
    vec4 color = texture(g_Texture0, v_TexCoord);
    clip(color.a - 0.001);
    gl_FragColor = color;
}
)",
        },
    };

    wallpaper::WPShaderParser::InitGlslang();
    const bool compiled = wallpaper::WPShaderParser::CompileToSpv(
        "unit-clip-shader", units, spvs, vfs, &shaderInfo, {});
    wallpaper::WPShaderParser::FinalGlslang();

    check(compiled, "fragment shader compatibility translates HLSL clip() to GLSL discard");
    check(spvs.size() == 2, "clip shader produces both vertex and fragment SPIR-V units");
}

} // namespace

int main()
{
    testEffectCaptureDebug();
    testEffectFinalOutputDebugPlaceholder();
    testEffectFinalOutputDebugPlaceholderTracksPublishedOutput();
    testEffectFinalOutputDebugPlaceholderOnlyTracksFinalNode();
    testCopyPassExtentClampsToOverlappingRegion();
    testCustomShaderPassSynchronizesPreviousTargetWrites();
    testFinPassSynchronizesFinalRenderTargetRead();
    testEffectSourceUsesLocalSpaceWhileFinalOutputKeepsParent();
    testEffectCandidateClassification();
    testEffectPolicy();
    testEffectPublishRoutePolicy();
    testPuppetEffectRoutePlan();
    testPuppetFinalDisplayBlendInvariant();
    testLayerEffectViewportPolicy();
    testVideoTexturePolicy();
    testModelFallbackPolicy();
    testSceneScriptRuntimePolicy();
    testSceneScriptEvaluatorRuntimeStubs();
    testTextSceneScriptOriginBindingAppliesToTextNode();
    testPuppetMeshTexcoordsApplyTextureMapRate();
    testPuppetFilteredMeshIncludesSecondaryOnlyWeightedChildren();
    testPuppetSimulationModeParser();
    testPuppetSimulationOffModePreservesOutput();
    testPuppetSimulationEligibilitySkipsAuthoredAnimatedBones();
    testPuppetSimulationRuntimeMovesEligibleChild();
    testPuppetSimulationModeReadsEnvironmentWhenUnset();
    testPuppetSimulationMetadataParser();
    testPuppetSimulationHelperRequiresActivePhysics();
    testPuppetSimulationRejectsInvalidParentOrder();
    testPuppetSimulationPropagatesSimulatedParentToDescendants();
    testPuppetCutoutSlotCoverageJsonIncludesParsedSimulationFields();
    testTranslucentBlendPreservesSourceAlpha();
    testPremultipliedTranslucentBlendUsesStoredColor();
    testSourceAlphaPreservePatch();
    testPuppetSourceAlphaPreserveShaderPolicy();
    testTexbV4SpriteHeaderPolicy();
    testShaderCompatPolicy();
    if (g_failures != 0) {
        return EXIT_FAILURE;
    }
    std::cout << "scene policy tests passed\n";
    return EXIT_SUCCESS;
}
