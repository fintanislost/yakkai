#include "Debug/EffectCaptureDebug.hpp"

#include "Scene/Scene.h"
#include "SpecTexs.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
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

std::string_view trimAsciiWhitespace(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<int> parsePositiveInt(std::string_view value)
{
    value = trimAsciiWhitespace(value);
    if (value.empty()) {
        return std::nullopt;
    }

    int parsed = 0;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(first, last, parsed);
    if (ec != std::errc() || ptr != last || parsed <= 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<bool> parseDebugBool(std::string_view value)
{
    value = trimAsciiWhitespace(value);
    if (value == "1" || value == "true" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off") {
        return false;
    }
    return std::nullopt;
}

std::optional<double> parseNonNegativeDouble(std::string_view value)
{
    value = trimAsciiWhitespace(value);
    if (value.empty()) {
        return std::nullopt;
    }

    std::string owned(value);
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(owned.c_str(), &end);
    if (errno != 0 || end == owned.c_str() || *end != '\0' ||
        !std::isfinite(parsed) || parsed < 0.0) {
        return std::nullopt;
    }
    return parsed;
}

nlohmann::json candidateChecksToJson(const wallpaper::policy::CandidateChecks& checks)
{
    return {
        {"hasWaterFamily", checks.hasWaterFamily},
        {"hasBlurFamily", checks.hasBlurFamily},
        {"hasLutFamily", checks.hasLutFamily},
        {"hasColorGradingFamily", checks.hasColorGradingFamily},
        {"waterOnly", checks.waterOnly},
        {"isUtilityCarrier", checks.isUtilityCarrier},
        {"isComposelayer", checks.isComposelayer},
        {"isFullscreen", checks.isFullscreen},
        {"isPuppetLayer", checks.isPuppetLayer},
        {"isProtectedPuppetPath", checks.isProtectedPuppetPath},
    };
}

nlohmann::json nullableInt(int value)
{
    if (value < 0) {
        return nullptr;
    }
    return value;
}

nlohmann::json optionalBool(std::optional<bool> value)
{
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json optionalDouble(std::optional<double> value)
{
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json puppetAnimationLayerOverrideToJson(
    const PuppetAnimationLayerOverride& rule)
{
    return {
        {"layerId", rule.layerId},
        {"animationId", rule.animationId},
        {"visible", optionalBool(rule.visible)},
        {"paused", optionalBool(rule.paused)},
        {"additive", optionalBool(rule.additive)},
        {"blend", optionalDouble(rule.blend)},
        {"rate", optionalDouble(rule.rate)},
        {"curTime", optionalDouble(rule.curTime)},
    };
}

nlohmann::json puppetAnimationLayerOverridesToJson(
    const std::vector<PuppetAnimationLayerOverride>& rules)
{
    nlohmann::json out = nlohmann::json::array();
    for (const auto& rule : rules) {
        out.push_back(puppetAnimationLayerOverrideToJson(rule));
    }
    return out;
}

nlohmann::json materialToJson(const EffectCaptureMaterialInfo& material)
{
    nlohmann::json textureBindings = nlohmann::json::array();
    for (const auto& binding : material.textureBindings) {
        textureBindings.push_back({
            {"slot", binding.slot},
            {"authored", binding.authored},
            {"resolved", binding.resolved},
        });
    }

    return {
        {"effectIndex", material.effectIndex},
        {"materialIndex", material.materialIndex},
        {"shader", material.shader},
        {"authoredOutputRenderTarget", material.authoredOutputRenderTarget},
        {"resolvedOutputRenderTarget", material.resolvedOutputRenderTarget},
        {"finalPublishedMaterial", material.finalPublishedMaterial},
        {"debugMaterialOutputSourceRenderTarget", material.debugMaterialOutputSourceRenderTarget},
        {"debugMaterialOutputCommandSource", material.debugMaterialOutputCommandSource},
        {"debugSourceFinalEffectOutput", material.debugSourceFinalEffectOutput},
        {"localMaterialOutputCaptureStage", material.localMaterialOutputCaptureStage},
        {"materialOutputCaptureStage", material.materialOutputCaptureStage},
        {"materialOutputCopyAfterPos", material.materialOutputCopyAfterPos},
        {"authoredTextures", material.authoredTextures},
        {"resolvedTextures", material.resolvedTextures},
        {"textureBindings", textureBindings},
        {"authoredCombos", material.authoredCombos},
        {"resolvedCombos", material.resolvedCombos},
        {"materialValues", material.materialValues},
        {"resolvedConstValues", material.resolvedConstValues},
        {"defines", material.defines},
    };
}

nlohmann::json transformToJson(const EffectCaptureTransformInfo& transform)
{
    return {
        {"origin", transform.origin},
        {"scale", transform.scale},
        {"angles", transform.angles},
    };
}

nlohmann::json meshBoundsToJson(const EffectCaptureMeshBoundsInfo& bounds)
{
    return {
        {"vertexArrayCount", bounds.vertexArrayCount},
        {"indexArrayCount", bounds.indexArrayCount},
        {"vertexCount", bounds.vertexCount},
        {"indexDataCount", bounds.indexDataCount},
        {"indexRenderDataCount", bounds.indexRenderDataCount},
        {"positionMin", bounds.positionMin},
        {"positionMax", bounds.positionMax},
    };
}

nlohmann::json puppetCutoutSlotCoverageToJson(
    const std::vector<PuppetCutoutSlotCoverageInfo>& coverage)
{
    nlohmann::json out = nlohmann::json::array();
    for (const auto& slot : coverage) {
        const int primaryVertexCount =
            slot.primaryVertexCount > 0 ? slot.primaryVertexCount : slot.vertexCount;
        const int primaryTriangleCount =
            slot.primaryTriangleCount > 0 ? slot.primaryTriangleCount : slot.triangleCount;
        const int weightedVertexCount =
            slot.weightedVertexCount > 0 ? slot.weightedVertexCount : primaryVertexCount;
        const int weightedTriangleCount =
            slot.weightedTriangleCount > 0 ? slot.weightedTriangleCount : primaryTriangleCount;
        out.push_back({
            {"slot", slot.slot},
            {"active", slot.active},
            {"vertexCount", slot.vertexCount},
            {"triangleCount", slot.triangleCount},
            {"boneName", slot.boneName},
            {"parentSlot", slot.parentSlot},
            {"parentBoneName", slot.parentBoneName},
            {"primaryVertexCount", primaryVertexCount},
            {"primaryTriangleCount", primaryTriangleCount},
            {"weightedVertexCount", weightedVertexCount},
            {"weightedTriangleCount", weightedTriangleCount},
            {"weightedVertexWeightSum", slot.weightedVertexWeightSum},
            {"layerLocalBounds", slot.layerLocalBounds},
            {"layerLocalCentroid", slot.layerLocalCentroid},
            {"secondaryOnly", slot.secondaryOnly},
            {"simulationMetadataPresent", slot.simulationMetadataPresent},
            {"simulationMetadata", slot.simulationMetadata},
            {"simulationMetadataValid", slot.simulationMetadataValid},
            {"simulationPhysicsActive", slot.simulationPhysicsActive},
            {"simulationTargetPointPresent", slot.simulationTargetPointPresent},
            {"simulationTargetPoint", slot.simulationTargetPoint},
            {"simulationTargetMassPresent", slot.simulationTargetMassPresent},
            {"simulationTargetMass", slot.simulationTargetMass},
            {"simulatedInactive", slot.simulatedInactive},
        });
    }
    return out;
}

nlohmann::json publishToJson(const EffectCapturePublishInfo& publish)
{
    return {
        {"enabled", publish.enabled},
        {"parentId", publish.parentId},
        {"hasParsedParentNode", publish.hasParsedParentNode},
        {"objectSize", publish.objectSize},
        {"origin", publish.origin},
        {"scale", publish.scale},
        {"angles", publish.angles},
        {"finalBlendMode", publish.finalBlendMode},
        {"fullscreen", publish.fullscreen},
        {"composelayer", publish.composelayer},
        {"puppetLayer", publish.puppetLayer},
        {"effectInputViewportSize", publish.effectInputViewportSize},
        {"effectInputViewportExpanded", publish.effectInputViewportExpanded},
        {"standalonePuppetFinalDisplay", publish.standalonePuppetFinalDisplay},
        {"publishFinalOutput", publish.publishFinalOutput},
        {"finalNodeUsesOriginalParent", publish.finalNodeUsesOriginalParent},
        {"effectInputNodeReset", publish.effectInputNodeReset},
        {"effectInputMaterialPreservesLayerBlendMode",
         publish.effectInputMaterialPreservesLayerBlendMode},
        {"effectInputMeshKind", publish.effectInputMeshKind},
        {"effectFinalMeshKind", publish.effectFinalMeshKind},
        {"standaloneFinalMeshKind", publish.standaloneFinalMeshKind},
        {"finalDisplayRoute", publish.finalDisplayRoute},
        {"standaloneDisplayAttachMode", publish.standaloneDisplayAttachMode},
        {"routeRisk", publish.routeRisk},
        {"effectInputRenderTarget", publish.effectInputRenderTarget},
        {"effectPingPongA", publish.effectPingPongA},
        {"effectPingPongB", publish.effectPingPongB},
        {"effectOutputSourceTarget", publish.effectOutputSourceTarget},
        {"finalPublishRenderTarget", publish.finalPublishRenderTarget},
        {"materialOutputCaptureTiming", publish.materialOutputCaptureTiming},
        {"finalPublishCaptureTiming", publish.finalPublishCaptureTiming},
        {"defaultRtBoundaryCaptureTiming", publish.defaultRtBoundaryCaptureTiming},
        {"finalDisplayBoundaryCaptureTiming", publish.finalDisplayBoundaryCaptureTiming},
        {"finalDisplayBeforeRenderTarget", publish.finalDisplayBeforeRenderTarget},
        {"finalDisplayAfterRenderTarget", publish.finalDisplayAfterRenderTarget},
        {"channelMapPrepassMode", publish.channelMapPrepassMode},
        {"channelMapMaterialPath", publish.channelMapMaterialPath},
        {"activePuppetChannelBlendSlots", publish.activePuppetChannelBlendSlots},
        {"puppetCutoutSlotCoverage", puppetCutoutSlotCoverageToJson(publish.puppetCutoutSlotCoverage)},
        {"effectInputLocalTransform", transformToJson(publish.effectInputLocalTransform)},
        {"standaloneDisplayLocalTransform",
         transformToJson(publish.standaloneDisplayLocalTransform)},
        {"standaloneDisplayParentId", publish.standaloneDisplayParentId},
        {"standaloneDisplayHasParsedParentNode",
         publish.standaloneDisplayHasParsedParentNode},
        {"standaloneDisplayNodeOrdinal", publish.standaloneDisplayNodeOrdinal},
        {"standaloneFinalMaterialBlendMode", publish.standaloneFinalMaterialBlendMode},
        {"standaloneFinalTexture", publish.standaloneFinalTexture},
        {"effectInputMeshBounds", meshBoundsToJson(publish.effectInputMeshBounds)},
        {"effectFinalMeshBounds", meshBoundsToJson(publish.effectFinalMeshBounds)},
        {"standaloneFinalMeshBounds", meshBoundsToJson(publish.standaloneFinalMeshBounds)},
    };
}

nlohmann::json puppetAnimationLayerToJson(const PuppetAnimationLayerInfo& layer)
{
    nlohmann::json activeBoneSlots = nlohmann::json::array();
    for (const int slot : layer.activeBoneSlots) {
        activeBoneSlots.push_back(slot);
    }

    return {
        {"animationId", layer.animationId},
        {"animationName", layer.animationName},
        {"rate", layer.rate},
        {"blend", layer.blend},
        {"visible", layer.visible},
        {"paused", layer.paused},
        {"additive", layer.additive},
        {"curTime", layer.curTime},
        {"matchedAnimation", layer.matchedAnimation},
        {"visibleAndWeighted", layer.visibleAndWeighted},
        {"activeBoneSlotCount", layer.activeBoneSlots.size()},
        {"activeBoneSlots", activeBoneSlots},
    };
}

nlohmann::json puppetAnimationLayersToJson(
    const std::vector<PuppetAnimationLayerInfo>& layers)
{
    nlohmann::json out = nlohmann::json::array();
    for (const auto& layer : layers) {
        out.push_back(puppetAnimationLayerToJson(layer));
    }
    return out;
}

nlohmann::json materialsToJson(const std::vector<EffectCaptureMaterialInfo>& materials)
{
    nlohmann::json out = nlohmann::json::array();
    for (const auto& material : materials) {
        out.push_back(materialToJson(material));
    }
    return out;
}

nlohmann::json layerToJson(const EffectCaptureLayerInfo& layer)
{
    nlohmann::json debugProbe = {
        {"requested", layer.debugProbeRequested},
        {"overrodePolicy", layer.debugProbeOverrodePolicy},
        {"reason", layer.debugProbeReason},
        {"maxEffects", nullableInt(layer.debugProbeMaxEffects)},
        {"originalVisibleEffectCount", nullableInt(layer.debugProbeOriginalVisibleEffectCount)},
        {"keptVisibleEffectCount", nullableInt(layer.debugProbeKeptVisibleEffectCount)},
        {"effectLimitTruncated", layer.debugProbeEffectLimitTruncated},
        {"routeOnly", layer.debugProbeRouteOnly},
    };

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
            {"reason", layer.policyReason},
        }},
        {"effectNames", layer.effectNames},
        {"materialShaders", layer.materialShaders},
        {"effectMaterials", materialsToJson(layer.effectMaterials)},
        {"puppetAnimationLayers", puppetAnimationLayersToJson(layer.puppetAnimationLayers)},
        {"publish", publishToJson(layer.publish)},
        {"candidateFamilies", layer.candidateFamilies},
        {"candidateMixFamilies", layer.candidateMixFamilies},
        {"candidateChainShape", layer.candidateChainShape},
        {"candidateEffectClass", layer.candidateEffectClass},
        {"candidateRisk", layer.candidateRisk},
        {"candidateBlockedReason", layer.candidateBlockedReason},
        {"candidateChecks", candidateChecksToJson(layer.candidateChecks)},
        {"debugProbe", debugProbe},
    };
}

nlohmann::json puppetAnimationLayerInventoryToJson(const EffectCaptureLayerInfo& layer)
{
    return {
        {"sceneId", layer.sceneId},
        {"sceneType", layer.sceneType},
        {"layerName", layer.layerName},
        {"layerImage", layer.layerImage},
        {"layerId", layer.layerId},
        {"policyReason", layer.policyReason},
        {"candidateChainShape", layer.candidateChainShape},
        {"candidateRisk", layer.candidateRisk},
        {"puppetAnimationLayers", puppetAnimationLayersToJson(layer.puppetAnimationLayers)},
    };
}

nlohmann::json protectedPuppetDiagnosticToJson(const EffectCaptureLayerInfo& layer)
{
    nlohmann::json diagnostic = layerToJson(layer);
    diagnostic["diagnosticKind"] = "protected-puppet-chain";
    diagnostic["captureMode"] = "metadata-only";
    diagnostic["normalCaptureRecordCreated"] = false;
    diagnostic["effectOrder"] = layer.effectNames;
    diagnostic["alphaEvidence"] = {
        {"layerAlpha", layer.alpha},
    };
    diagnostic["finalPublishRenderTarget"] =
        layer.publish.finalPublishRenderTarget.empty()
            ? std::string(SpecTex_Default)
            : layer.publish.finalPublishRenderTarget;
    diagnostic["routing"] = {
        {"metadataOnly", true},
        {"normalPolicyReason", layer.policyReason},
        {"normalPolicyKeepsEffects", layer.keepEffects},
        {"normalPolicyStripsEffects", layer.strippedEffects},
        {"finalPublishRenderTarget", diagnostic["finalPublishRenderTarget"]},
        {"puppetLayer", layer.publish.puppetLayer},
        {"effectInputMeshKind", layer.publish.effectInputMeshKind},
        {"effectFinalMeshKind", layer.publish.effectFinalMeshKind},
        {"standaloneFinalMeshKind", layer.publish.standaloneFinalMeshKind},
        {"finalDisplayRoute", layer.publish.finalDisplayRoute},
        {"routeRisk", layer.publish.routeRisk},
    };
    return diagnostic;
}

bool containsLayerId(const std::vector<int>& layerIds, int layerId)
{
    return std::find(layerIds.begin(), layerIds.end(), layerId) != layerIds.end();
}

bool hasHighRiskEffectFamily(const EffectCaptureLayerInfo& layer)
{
    return layer.candidateChecks.hasBlurFamily ||
           layer.candidateChecks.hasLutFamily ||
           layer.candidateChecks.hasColorGradingFamily;
}

bool hasPuppetMixedChainShape(const EffectCaptureLayerInfo& layer)
{
    return layer.candidateChainShape == "puppet-mixed" ||
           layer.candidateChainShape == "protected-puppet-mixed";
}

bool hasProbeLayerChainShape(const EffectCaptureLayerInfo& layer)
{
    return hasPuppetMixedChainShape(layer) ||
           layer.candidateChainShape == "water-composelayer" ||
           layer.candidateChainShape == "water-utility" ||
           layer.candidateChainShape == "water-fullscreen";
}

} // namespace

std::filesystem::path EffectCaptureConfig::manifestPath() const
{
    return std::filesystem::path(outputDir) / "manifest.json";
}

bool EffectCaptureConfig::shouldProbeLayer(int layerId) const
{
    return containsLayerId(probeLayerIds, layerId);
}

bool EffectCaptureConfig::shouldCaptureLayer(int layerId) const
{
    return captureLayerIds.empty() || containsLayerId(captureLayerIds, layerId);
}

bool EffectCaptureConfig::shouldProbeHighRiskLayer(int layerId) const
{
    return containsLayerId(highRiskProbeLayerIds, layerId);
}

bool shouldDumpEffectCaptures(const EffectCaptureConfig& config,
                              double elapsedSeconds,
                              double frameTimeSeconds)
{
    if (!config.enabled()) {
        return false;
    }
    if (config.captureDelayMs <= 0) {
        return true;
    }

    const double effectiveElapsedMs =
        std::max(0.0, elapsedSeconds + std::max(0.0, frameTimeSeconds)) * 1000.0;
    return effectiveElapsedMs + 1.0e-3 >= static_cast<double>(config.captureDelayMs);
}

bool shouldRegisterMaterialOutputCaptureForShader(std::string_view shader)
{
    return !trimAsciiWhitespace(shader).empty();
}

EffectProbeLimitDecision decideEffectProbeLimit(int originalVisibleEffectCount,
                                                int maxEffects,
                                                bool routeOnly)
{
    const int original = std::max(0, originalVisibleEffectCount);
    if (routeOnly) {
        return {
            .keptVisibleEffectCount = 0,
            .keepEffectRouteActive = original > 0,
            .effectLimitTruncated = original > 0,
            .routeOnly = original > 0,
        };
    }

    const int limit = std::max(0, maxEffects);
    const int kept = std::min(original, limit);
    return {
        .keptVisibleEffectCount = kept,
        .keepEffectRouteActive = kept > 0,
        .effectLimitTruncated = kept < original,
        .routeOnly = false,
    };
}

bool shouldProbeStrippedEffectLayer(const EffectCaptureConfig& config,
                                    const EffectCaptureLayerInfo& layer)
{
    return !strippedEffectProbeReason(config, layer).empty();
}

bool shouldLimitRequestedEffectProbeLayer(const EffectCaptureConfig& config,
                                          const EffectCaptureLayerInfo& layer)
{
    if (!config.enabled() ||
        (config.probeMaxEffects < 0 && !config.puppetEffectRouteOnly)) {
        return false;
    }

    const bool regularProbe = config.shouldProbeLayer(layer.layerId);
    const bool highRiskProbe = config.shouldProbeHighRiskLayer(layer.layerId);
    if (!regularProbe && !highRiskProbe) {
        return false;
    }

    if (config.puppetEffectRouteOnly &&
        config.probeMaxEffects < 0 &&
        !layer.candidateChecks.isPuppetLayer) {
        return false;
    }

    if (layer.policyReason == "puppet-alpha-strip") {
        return shouldProbeStrippedEffectLayer(config, layer);
    }

    if (regularProbe && hasProbeLayerChainShape(layer)) {
        return true;
    }

    return highRiskProbe && hasHighRiskEffectFamily(layer);
}

std::string strippedEffectProbeReason(const EffectCaptureConfig& config,
                                      const EffectCaptureLayerInfo& layer)
{
    if (!config.enabled() || layer.policyReason != "puppet-alpha-strip") {
        return {};
    }

    if (config.shouldProbeLayer(layer.layerId) && hasProbeLayerChainShape(layer)) {
        return "layer-id-probe";
    }

    if (config.shouldProbeHighRiskLayer(layer.layerId) && hasHighRiskEffectFamily(layer)) {
        return "high-risk-layer-id-probe";
    }

    return {};
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

std::vector<int> parsePositiveLayerIdList(std::string_view value)
{
    std::vector<int> ids;
    value = trimAsciiWhitespace(value);
    if (value.empty()) {
        return ids;
    }

    while (true) {
        const std::size_t comma = value.find(',');
        std::string_view token = trimAsciiWhitespace(value.substr(0, comma));
        if (token.empty()) {
            return {};
        }

        int parsed = 0;
        const char* first = token.data();
        const char* last = token.data() + token.size();
        const auto [ptr, ec] = std::from_chars(first, last, parsed);
        if (ec != std::errc() || ptr != last || parsed <= 0) {
            return {};
        }
        if (std::find(ids.begin(), ids.end(), parsed) == ids.end()) {
            ids.push_back(parsed);
        }

        if (comma == std::string_view::npos) {
            return ids;
        }
        value.remove_prefix(comma + 1);
    }
}

std::vector<int> parseProbeLayerIdList(std::string_view value)
{
    return parsePositiveLayerIdList(value);
}

std::vector<int> parseCaptureLayerIdList(std::string_view value)
{
    return parsePositiveLayerIdList(value);
}

std::vector<int> parseProbeChannelMapSlotList(std::string_view value)
{
    constexpr int kMaxDiagnosticChannelMapSlot = 63;

    std::vector<int> slots;
    value = trimAsciiWhitespace(value);
    if (value.empty()) {
        return slots;
    }

    while (true) {
        const std::size_t comma = value.find(',');
        std::string_view token = trimAsciiWhitespace(value.substr(0, comma));
        if (token.empty()) {
            return {};
        }

        int parsed = 0;
        const char* first = token.data();
        const char* last = token.data() + token.size();
        const auto [ptr, ec] = std::from_chars(first, last, parsed);
        if (ec != std::errc() || ptr != last || parsed < 0 ||
            parsed > kMaxDiagnosticChannelMapSlot) {
            return {};
        }
        if (std::find(slots.begin(), slots.end(), parsed) == slots.end()) {
            slots.push_back(parsed);
        }

        if (comma == std::string_view::npos) {
            return slots;
        }
        value.remove_prefix(comma + 1);
    }
}

int parseProbeMaxEffects(std::string_view value)
{
    value = trimAsciiWhitespace(value);
    if (value.empty()) {
        return -1;
    }

    int parsed = 0;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(first, last, parsed);
    if (ec != std::errc() || ptr != last || parsed < 0) {
        return -1;
    }
    return parsed;
}

std::optional<std::vector<PuppetAnimationLayerOverride>>
parsePuppetAnimationLayerOverrideList(std::string_view value)
{
    std::vector<PuppetAnimationLayerOverride> overrides;
    value = trimAsciiWhitespace(value);
    if (value.empty()) {
        return overrides;
    }

    while (true) {
        const std::size_t semicolon = value.find(';');
        std::string_view rule = trimAsciiWhitespace(value.substr(0, semicolon));
        if (rule.empty()) {
            return std::nullopt;
        }

        const std::size_t firstColon = rule.find(':');
        if (firstColon == std::string_view::npos) {
            return std::nullopt;
        }
        const std::size_t secondColon = rule.find(':', firstColon + 1);
        if (secondColon == std::string_view::npos) {
            return std::nullopt;
        }

        const auto layerId = parsePositiveInt(rule.substr(0, firstColon));
        const auto animationId =
            parsePositiveInt(rule.substr(firstColon + 1, secondColon - firstColon - 1));
        if (!layerId || !animationId) {
            return std::nullopt;
        }

        PuppetAnimationLayerOverride override;
        override.layerId = *layerId;
        override.animationId = *animationId;

        std::string_view assignments = trimAsciiWhitespace(rule.substr(secondColon + 1));
        if (assignments.empty()) {
            return std::nullopt;
        }

        while (true) {
            const std::size_t comma = assignments.find(',');
            const std::string_view assignment =
                trimAsciiWhitespace(assignments.substr(0, comma));
            const std::size_t equals = assignment.find('=');
            if (assignment.empty() || equals == std::string_view::npos) {
                return std::nullopt;
            }

            const std::string_view key = trimAsciiWhitespace(assignment.substr(0, equals));
            const std::string_view raw = trimAsciiWhitespace(assignment.substr(equals + 1));
            if (key == "visible") {
                auto parsed = parseDebugBool(raw);
                if (!parsed) return std::nullopt;
                override.visible = *parsed;
            } else if (key == "paused") {
                auto parsed = parseDebugBool(raw);
                if (!parsed) return std::nullopt;
                override.paused = *parsed;
            } else if (key == "additive") {
                auto parsed = parseDebugBool(raw);
                if (!parsed) return std::nullopt;
                override.additive = *parsed;
            } else if (key == "blend") {
                auto parsed = parseNonNegativeDouble(raw);
                if (!parsed) return std::nullopt;
                override.blend = *parsed;
            } else if (key == "rate") {
                auto parsed = parseNonNegativeDouble(raw);
                if (!parsed) return std::nullopt;
                override.rate = *parsed;
            } else if (key == "curTime") {
                auto parsed = parseNonNegativeDouble(raw);
                if (!parsed) return std::nullopt;
                override.curTime = *parsed;
            } else {
                return std::nullopt;
            }

            if (comma == std::string_view::npos) {
                break;
            }
            assignments.remove_prefix(comma + 1);
        }

        overrides.push_back(std::move(override));

        if (semicolon == std::string_view::npos) {
            return overrides;
        }
        value.remove_prefix(semicolon + 1);
    }
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
    if (!scene.debugEffectCaptures.enabled() ||
        !scene.debugEffectCaptures.shouldCaptureLayer(layer.layerId)) {
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

std::string registerDefaultRtBoundaryCapture(Scene& scene,
                                             const EffectCaptureLayerInfo& layer,
                                             std::string_view stage,
                                             std::string_view suffix)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return {};
    }

    const auto defaultTarget = scene.renderTargets.find(std::string(SpecTex_Default));
    if (defaultTarget == scene.renderTargets.end()) {
        return {};
    }

    const std::string renderTarget = "_rt_debug_" + sanitizeCapturePathSegment(stage) + "_" +
                                     sanitizeCapturePathSegment(suffix);
    scene.renderTargets[renderTarget] = defaultTarget->second;
    scene.renderTargets[renderTarget].allowReuse = false;
    registerEffectCapture(scene, layer, stage, renderTarget);
    return renderTarget;
}

EffectCaptureFinalDisplayBoundaryTargets
registerFinalDisplayBoundaryCapture(Scene& scene,
                                    const EffectCaptureLayerInfo& layer,
                                    const SceneNode& node,
                                    std::string_view suffix)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return {};
    }

    const auto defaultTarget = scene.renderTargets.find(std::string(SpecTex_Default));
    if (defaultTarget == scene.renderTargets.end()) {
        return {};
    }

    const std::string safeSuffix = sanitizeCapturePathSegment(suffix);
    EffectCaptureFinalDisplayBoundaryTargets targets {
        .beforeTarget = "_rt_debug_final_display_before_" + safeSuffix,
        .afterTarget = "_rt_debug_final_display_after_" + safeSuffix,
    };

    scene.renderTargets[targets.beforeTarget] = defaultTarget->second;
    scene.renderTargets[targets.beforeTarget].allowReuse = false;
    scene.renderTargets[targets.afterTarget] = defaultTarget->second;
    scene.renderTargets[targets.afterTarget].allowReuse = false;

    registerEffectCapture(scene, layer, "final-display-before", targets.beforeTarget);
    registerEffectCapture(scene, layer, "final-display-after", targets.afterTarget);

    scene.debugEffectFinalDisplayBoundaryCaptures.push_back({
        .node = &node,
        .beforeTarget = targets.beforeTarget,
        .afterTarget = targets.afterTarget,
    });
    return targets;
}

EffectCaptureFinalDisplayBoundaryTargets
registerEffectLayerFinalPublishBoundaryCapture(Scene& scene,
                                               EffectCaptureLayerInfo& layer,
                                               const SceneNode& node,
                                               std::string_view suffix)
{
    auto targets = registerFinalDisplayBoundaryCapture(scene, layer, node, suffix);
    layer.publish.finalDisplayBoundaryCaptureTiming =
        targets.beforeTarget.empty()
            ? std::string()
            : "render-graph-copy-around-effect-layer-final-publish-node";
    layer.publish.finalDisplayBeforeRenderTarget = targets.beforeTarget;
    layer.publish.finalDisplayAfterRenderTarget = targets.afterTarget;
    return targets;
}

void refreshEffectCaptureLayerInfo(Scene& scene, const EffectCaptureLayerInfo& layer)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return;
    }

    for (auto& record : scene.debugEffectCaptureRecords) {
        if (record.layer.sceneId == layer.sceneId &&
            record.layer.layerId == layer.layerId) {
            record.layer = layer;
        }
    }
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
    if (layer.candidateChecks.isProtectedPuppetPath) {
        scene.debugEffectProtectedPuppetDiagnostics.push_back(layer);
    }
}

void recordPuppetAnimationLayerInventory(Scene& scene, const EffectCaptureLayerInfo& layer)
{
    if (!scene.debugEffectCaptures.enabled() || layer.puppetAnimationLayers.empty()) {
        return;
    }

    for (auto& existing : scene.debugPuppetAnimationLayerInventory) {
        if (existing.sceneId == layer.sceneId && existing.layerId == layer.layerId) {
            existing = layer;
            return;
        }
    }
    scene.debugPuppetAnimationLayerInventory.push_back(layer);
}

bool writeEffectCaptureManifest(const Scene& scene)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return true;
    }

    bool failed = false;
    nlohmann::json captures = nlohmann::json::array();
    int captureIndex = 0;
    for (const auto& record : scene.debugEffectCaptureRecords) {
        failed = failed || record.failed || !record.completed;
        captures.push_back({
            {"captureIndex", captureIndex++},
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
            {"colorMaskBits", state.colorMaskBits},
            {"blendMode", state.blendMode},
            {"blendEnabled", state.blendEnabled},
            {"preserveOutput", state.preserveOutput},
            {"usesDepth", state.usesDepth},
            {"camera", state.camera},
            {"nodeId", state.nodeId},
            {"materialName", state.materialName},
            {"debugPurpose", state.debugPurpose},
        });
    }

    nlohmann::json strippedCandidates = nlohmann::json::array();
    for (const auto& candidate : scene.debugEffectStrippedCandidates) {
        strippedCandidates.push_back(layerToJson(candidate));
    }

    nlohmann::json protectedPuppetDiagnostics = nlohmann::json::array();
    for (const auto& candidate : scene.debugEffectProtectedPuppetDiagnostics) {
        protectedPuppetDiagnostics.push_back(protectedPuppetDiagnosticToJson(candidate));
    }

    nlohmann::json puppetAnimationLayerInventory = nlohmann::json::array();
    for (const auto& layer : scene.debugPuppetAnimationLayerInventory) {
        puppetAnimationLayerInventory.push_back(puppetAnimationLayerInventoryToJson(layer));
    }

    nlohmann::json manifest = {
        {"status", failed ? "failed" : "ok"},
        {"sceneId", scene.scene_id},
        {"commandLine", scene.debugEffectCaptures.commandLine},
        {"captureLayerIds", scene.debugEffectCaptures.captureLayerIds},
        {"probeLayerIds", scene.debugEffectCaptures.probeLayerIds},
        {"captureDelayMs", scene.debugEffectCaptures.captureDelayMs},
        {"shaderTimeSeconds", scene.elapsingTime},
        {"frameTimeSeconds", scene.frameTime},
        {"effectiveCaptureTimeSeconds", scene.elapsingTime + std::max(0.0, scene.frameTime)},
        {"highRiskProbeLayerIds", scene.debugEffectCaptures.highRiskProbeLayerIds},
        {"probeChannelMapSlots", scene.debugEffectCaptures.probeChannelMapSlots},
        {"puppetAnimationLayerOverrides", puppetAnimationLayerOverridesToJson(
            scene.debugEffectCaptures.puppetAnimationLayerOverrides)},
        {"probeMaxEffects", nullableInt(scene.debugEffectCaptures.probeMaxEffects)},
        {"puppetFinalMeshOverride", scene.debugEffectCaptures.puppetFinalMeshOverride},
        {"puppetEffectRouteOnly", scene.debugEffectCaptures.puppetEffectRouteOnly},
        {"captureCount", scene.debugEffectCaptureRecords.size()},
        {"captures", captures},
        {"passStates", passStates},
        {"debugEffectPassStates", passStates},
        {"strippedCandidates", strippedCandidates},
        {"protectedPuppetDiagnostics", protectedPuppetDiagnostics},
        {"puppetAnimationLayerInventory", puppetAnimationLayerInventory},
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
