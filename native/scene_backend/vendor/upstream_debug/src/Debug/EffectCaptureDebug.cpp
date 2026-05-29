#include "Debug/EffectCaptureDebug.hpp"

#include "Scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace wallpaper::debug {
namespace {

std::string trimUnderscores(std::string value)
{
    while (!value.empty() && value.front() == '_') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == '_') {
        value.pop_back();
    }
    return value;
}

std::string collapseUnderscores(std::string value)
{
    std::string out;
    out.reserve(value.size());
    bool lastWasUnderscore = false;
    for (char ch : value) {
        if (ch == '_') {
            if (!lastWasUnderscore) {
                out.push_back(ch);
            }
            lastWasUnderscore = true;
            continue;
        }
        out.push_back(ch);
        lastWasUnderscore = false;
    }
    return out;
}

nlohmann::json candidateChecksToJson(const wallpaper::policy::CandidateChecks& checks)
{
    return {
        {"hasWaterFamily", checks.hasWaterFamily},
        {"waterOnly", checks.waterOnly},
        {"isUtilityCarrier", checks.isUtilityCarrier},
        {"isComposelayer", checks.isComposelayer},
        {"isFullscreen", checks.isFullscreen},
        {"isPuppetLayer", checks.isPuppetLayer},
        {"isProtectedPuppetPath", checks.isProtectedPuppetPath},
    };
}

nlohmann::json layerToJson(const EffectCaptureLayerInfo& layer)
{
    return {
        {"sceneId", layer.sceneId},
        {"sceneType", layer.sceneType},
        {"layerName", layer.layerName},
        {"layerImage", layer.layerImage},
        {"layerId", layer.layerId},
        {"visibleEffectCount", layer.visibleEffectCount},
        {"alpha", layer.alpha},
        {"policy", {
            {"keepLayer", layer.keepLayer},
            {"keepEffects", layer.keepEffects},
            {"strippedEffects", layer.strippedEffects},
            {"forceAlphaOne", layer.forceAlphaOne},
            {"reason", layer.policyReason},
        }},
        {"effectNames", layer.effectNames},
        {"materialShaders", layer.materialShaders},
        {"candidateFamilies", layer.candidateFamilies},
        {"candidateMixFamilies", layer.candidateMixFamilies},
        {"candidateChainShape", layer.candidateChainShape},
        {"candidateRisk", layer.candidateRisk},
        {"candidateBlockedReason", layer.candidateBlockedReason},
        {"candidateChecks", candidateChecksToJson(layer.candidateChecks)},
    };
}

} // namespace

std::filesystem::path EffectCaptureConfig::manifestPath() const
{
    return std::filesystem::path(outputDir) / "manifest.json";
}

std::string sanitizeCapturePathSegment(std::string_view value)
{
    std::string safe;
    safe.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            safe.push_back(static_cast<char>(ch));
        } else {
            safe.push_back('_');
        }
    }
    safe = trimUnderscores(collapseUnderscores(std::move(safe)));
    if (safe.empty()) {
        return "unnamed";
    }
    return safe;
}

std::filesystem::path capturePath(const EffectCaptureConfig& config,
                                  const EffectCaptureLayerInfo& layer,
                                  std::string_view stage)
{
    const std::string layerToken =
        std::to_string(layer.layerId) + "_" + sanitizeCapturePathSegment(layer.layerName);
    const std::string fileToken = sanitizeCapturePathSegment(stage) + ".tga";
    return std::filesystem::path(config.outputDir) / sanitizeCapturePathSegment(layer.sceneId) /
           layerToken / fileToken;
}

void registerEffectCapture(Scene& scene,
                           const EffectCaptureLayerInfo& layer,
                           std::string_view stage,
                           std::string_view renderTarget)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return;
    }

    EffectCaptureRecord record;
    record.stage = std::string(stage);
    record.label = layer.sceneId + ":" + std::to_string(layer.layerId) + ":" + record.stage;
    record.renderTarget = std::string(renderTarget);
    record.path = capturePath(scene.debugEffectCaptures, layer, stage).string();
    record.layer = layer;
    if (auto it = scene.renderTargets.find(record.renderTarget); it != scene.renderTargets.end()) {
        record.renderTargetWidth = it->second.width;
        record.renderTargetHeight = it->second.height;
    }
    scene.debugEffectCaptureRecords.push_back(record);
}

void recordEffectPassState(Scene& scene, const EffectPassState& state)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return;
    }
    scene.debugEffectPassStates.push_back(state);
}

void recordStrippedEffectCandidate(Scene& scene, const EffectCaptureLayerInfo& layer)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return;
    }
    scene.debugEffectStrippedCandidates.push_back(layer);
}

bool writeEffectCaptureManifest(const Scene& scene)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return true;
    }

    bool failed = false;
    nlohmann::json captures = nlohmann::json::array();
    for (const auto& record : scene.debugEffectCaptureRecords) {
        failed = failed || record.failed || !record.completed;
        captures.push_back({
            {"stage", record.stage},
            {"label", record.label},
            {"renderTarget", record.renderTarget},
            {"renderTargetInfo", {
                {"width", record.renderTargetWidth},
                {"height", record.renderTargetHeight},
                {"format", record.renderTargetFormat},
            }},
            {"path", record.path},
            {"completed", record.completed},
            {"failed", record.failed},
            {"failureReason", record.failureReason},
            {"layer", layerToJson(record.layer)},
        });
    }

    nlohmann::json passStates = nlohmann::json::array();
    for (const auto& state : scene.debugEffectPassStates) {
        passStates.push_back({
            {"output", state.output},
            {"loadOp", state.loadOp},
            {"depthLoadOp", state.depthLoadOp},
            {"colorMask", state.colorMask},
            {"blendMode", state.blendMode},
            {"blendEnabled", state.blendEnabled},
            {"preserveOutput", state.preserveOutput},
            {"usesDepth", state.usesDepth},
        });
    }

    nlohmann::json strippedCandidates = nlohmann::json::array();
    for (const auto& candidate : scene.debugEffectStrippedCandidates) {
        strippedCandidates.push_back(layerToJson(candidate));
    }

    nlohmann::json manifest = {
        {"status", failed ? "failed" : "ok"},
        {"sceneId", scene.scene_id},
        {"commandLine", scene.debugEffectCaptures.commandLine},
        {"captureCount", scene.debugEffectCaptureRecords.size()},
        {"captures", captures},
        {"passStates", passStates},
        {"strippedCandidates", strippedCandidates},
    };

    const auto manifestPath = scene.debugEffectCaptures.manifestPath();
    if (manifestPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(manifestPath.parent_path(), ec);
    }

    std::ofstream out(manifestPath);
    if (!out.is_open()) {
        return false;
    }
    out << manifest.dump(2) << '\n';
    return out.good();
}

} // namespace wallpaper::debug
