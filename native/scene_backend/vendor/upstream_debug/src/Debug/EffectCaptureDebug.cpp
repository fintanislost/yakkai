#include "Debug/EffectCaptureDebug.hpp"

#include "Interface/IShaderValueUpdater.h"
#include "Scene/Scene.h"
#include "SpecTexs.hpp"
#include "WPJson.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <tuple>
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

nlohmann::json layerVisibilityOverridesToJson(const std::unordered_map<int, bool>& rules)
{
    std::vector<std::pair<int, bool>> sortedRules(rules.begin(), rules.end());
    std::sort(sortedRules.begin(), sortedRules.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });

    nlohmann::json out = nlohmann::json::array();
    for (const auto& [layerId, visible] : sortedRules) {
        out.push_back({
            {"layerId", layerId},
            {"visible", visible},
        });
    }
    return out;
}

nlohmann::json mousePositionToJson(const std::array<float, 2>& value)
{
    return nlohmann::json::array({value[0], value[1]});
}

nlohmann::json mouseParallaxLayerInventoryToJson(const Scene& scene)
{
    nlohmann::json out = nlohmann::json::array();
    const auto snapshot = scene.shaderValueUpdater
        ? scene.shaderValueUpdater->mouseParallaxDebugSnapshot()
        : MouseParallaxDebugSnapshot {};

    std::vector<EffectCaptureMouseParallaxLayerInfo> layers =
        scene.debugMouseParallaxLayerInventory;
    std::sort(layers.begin(), layers.end(), [](const auto& left, const auto& right) {
        return std::tie(left.layerId, left.layerKind, left.layerName) <
               std::tie(right.layerId, right.layerKind, right.layerName);
    });

    const float mouseVecX = 0.5f - snapshot.effectivePosition[0];
    const float mouseVecY = snapshot.effectivePosition[1] - 0.5f;
    for (const auto& layer : layers) {
        const std::array<float, 2> expectedOffset {
            mouseVecX * static_cast<float>(scene.ortho[0]) *
                snapshot.cameraMouseInfluence * layer.parallaxDepth[0] * snapshot.cameraAmount,
            mouseVecY * static_cast<float>(scene.ortho[1]) *
                snapshot.cameraMouseInfluence * layer.parallaxDepth[1] * snapshot.cameraAmount,
        };
        nlohmann::json row = {
            {"layerId", layer.layerId},
            {"layerName", layer.layerName},
            {"layerKind", layer.layerKind},
            {"parallaxDepth", mousePositionToJson(layer.parallaxDepth)},
            {"expectedOffset", mousePositionToJson(expectedOffset)},
            {"expectedOffsetAvailable", true},
        };
        if (layer.parentLayerId > 0) {
            row["parentLayerId"] = layer.parentLayerId;
            row["parentLayerName"] = layer.parentLayerName;
        }
        if (layer.childLookupAvailable) {
            row["hasChildren"] = !layer.childLayerIds.empty();
            row["childLayerIds"] = layer.childLayerIds;
            row["propagationExpectation"] = "parent-offset-affects-children";
        } else {
            row["propagationExpectation"] = "unknown-parent-graph";
        }
        out.push_back(std::move(row));
    }
    return out;
}

nlohmann::json mouseParallaxToJson(const Scene& scene)
{
    const auto snapshot = scene.shaderValueUpdater
        ? scene.shaderValueUpdater->mouseParallaxDebugSnapshot()
        : MouseParallaxDebugSnapshot {};
    const auto& config = scene.debugEffectCaptures.mouseParallax;

    nlohmann::json out = {
        {"inputSource", config.inputSource},
        {"inputPosition", mousePositionToJson(snapshot.inputPosition)},
        {"effectivePosition", mousePositionToJson(snapshot.effectivePosition)},
        {"parallaxUniformPosition", mousePositionToJson(snapshot.parallaxUniformPosition)},
        {"camera", {
            {"enabled", snapshot.cameraEnabled},
            {"amount", snapshot.cameraAmount},
            {"delay", snapshot.cameraDelay},
            {"mouseInfluence", snapshot.cameraMouseInfluence},
        }},
        {"authoredCamera", {
            {"enabled", config.cameraEnabled},
            {"amount", config.cameraAmount},
            {"delay", config.cameraDelay},
            {"mouseInfluence", config.cameraMouseInfluence},
        }},
        {"parallaxLayers", mouseParallaxLayerInventoryToJson(scene)},
    };
    if (config.hasRequestedPosition) {
        out["requestedPosition"] = mousePositionToJson(config.requestedPosition);
    }
    if (!config.timeline.empty()) {
        nlohmann::json timeline = nlohmann::json::array();
        for (const auto& point : config.timeline) {
            timeline.push_back({
                {"timeMs", point.timeMs},
                {"position", mousePositionToJson(point.position)},
            });
        }
        out["timeline"] = timeline;
    }
    if (config.timelineElapsedMsAtCapture) {
        out["timelineElapsedMsAtCapture"] = *config.timelineElapsedMsAtCapture;
    }
    return out;
}

nlohmann::json mediaStateTimelineToJson(const std::string& raw)
{
    if (raw.empty()) {
        return nlohmann::json::array();
    }

    nlohmann::json parsed;
    if (PARSE_JSON(raw, parsed) && parsed.is_array()) {
        return parsed;
    }

    return nlohmann::json::array();
}

nlohmann::json generatedTextParentToJson(const GeneratedTextParentInfo& parent)
{
    return {
        {"layerId", parent.layerId},
        {"layerName", parent.layerName},
        {"translate", parent.translate},
        {"scale", parent.scale},
    };
}

nlohmann::json generatedTextParentChainToJson(
    const std::vector<GeneratedTextParentInfo>& parents)
{
    nlohmann::json out = nlohmann::json::array();
    for (const auto& parent : parents) {
        out.push_back(generatedTextParentToJson(parent));
    }
    return out;
}

nlohmann::json generatedTextDiagnosticToJson(const GeneratedTextDiagnostic& info)
{
    return {
        {"layerId", info.layerId},
        {"layerName", info.layerName},
        {"text", info.text},
        {"textureName", info.textureName},
        {"font", info.font},
        {"rasterizer", info.rasterizer},
        {"fontLoaded", info.fontLoaded},
        {"fontFamily", info.fontFamily},
        {"fontLoadStatus", info.fontLoadStatus},
        {"horizontalAlign", info.horizontalAlign},
        {"verticalAlign", info.verticalAlign},
        {"pointSize", info.pointSize},
        {"effectivePixelSize", info.effectivePixelSize},
        {"parentId", info.parentId},
        {"parentChain", generatedTextParentChainToJson(info.parentChain)},
        {"cardSize", info.cardSize},
        {"textureSize", info.textureSize},
        {"color", info.color},
        {"alpha", info.alpha},
        {"nodeTranslate", info.nodeTranslate},
        {"nodeScale", info.nodeScale},
        {"localBounds", info.localBounds},
        {"worldBounds", info.worldBounds},
        {"alphaBounds", info.alphaBounds},
        {"visibility", info.visibility},
        {"classificationReason", info.classificationReason},
    };
}

nlohmann::json generatedTextDiagnosticsToJson(const Scene& scene)
{
    std::vector<GeneratedTextDiagnostic> diagnostics = scene.debugGeneratedTextDiagnostics;
    std::sort(diagnostics.begin(), diagnostics.end(), [](const auto& left, const auto& right) {
        return std::tie(left.layerId, left.layerName) <
               std::tie(right.layerId, right.layerName);
    });

    nlohmann::json out = nlohmann::json::array();
    for (const auto& info : diagnostics) {
        out.push_back(generatedTextDiagnosticToJson(info));
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
    nlohmann::json debugLayerVisibilityOverride = {
        {"requested", layer.debugLayerVisibilityOverrideRequested},
        {"visible", layer.debugLayerVisibilityOverrideVisible},
        {"originalVisible", layer.debugLayerVisibilityOverrideOriginalVisible},
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
        {"parallaxDepth", mousePositionToJson(layer.parallaxDepth)},
        {"parallaxDepthNonzero", layer.parallaxDepthNonzero},
        {"debugProbe", debugProbe},
        {"debugLayerVisibilityOverride", debugLayerVisibilityOverride},
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

std::optional<bool> EffectCaptureConfig::layerVisibilityOverrideFor(int layerId) const
{
    const auto it = layerVisibilityOverrides.find(layerId);
    if (it == layerVisibilityOverrides.end()) {
        return std::nullopt;
    }
    return it->second;
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

std::unordered_map<int, bool> parseLayerVisibilityOverrideList(std::string_view value)
{
    std::unordered_map<int, bool> overrides;
    value = trimAsciiWhitespace(value);
    if (value.empty()) {
        return overrides;
    }

    while (true) {
        const std::size_t comma = value.find(',');
        const std::string_view rule = trimAsciiWhitespace(value.substr(0, comma));
        const std::size_t colon = rule.find(':');
        if (rule.empty() || colon == std::string_view::npos) {
            return {};
        }

        const auto layerId = parsePositiveInt(rule.substr(0, colon));
        const auto visible = parseDebugBool(rule.substr(colon + 1));
        if (!layerId || !visible) {
            return {};
        }
        overrides[*layerId] = *visible;

        if (comma == std::string_view::npos) {
            return overrides;
        }
        value.remove_prefix(comma + 1);
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

void recordMouseParallaxLayer(Scene& scene,
                              int layerId,
                              std::string_view layerName,
                              std::string_view layerKind,
                              std::array<float, 2> parallaxDepth,
                              int parentLayerId,
                              std::string_view parentLayerName,
                              const std::vector<int>& childLayerIds,
                              bool childLookupAvailable)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return;
    }
    if (std::abs(parallaxDepth[0]) <= 1.0e-6f &&
        std::abs(parallaxDepth[1]) <= 1.0e-6f) {
        return;
    }

    std::vector<int> sortedChildLayerIds = childLayerIds;
    std::sort(sortedChildLayerIds.begin(), sortedChildLayerIds.end());
    sortedChildLayerIds.erase(
        std::unique(sortedChildLayerIds.begin(), sortedChildLayerIds.end()),
        sortedChildLayerIds.end());

    EffectCaptureMouseParallaxLayerInfo info {
        .layerId = layerId,
        .layerName = std::string(layerName),
        .layerKind = std::string(layerKind),
        .parallaxDepth = parallaxDepth,
        .parentLayerId = parentLayerId,
        .parentLayerName = std::string(parentLayerName),
        .childLookupAvailable = childLookupAvailable,
        .childLayerIds = std::move(sortedChildLayerIds),
    };

    for (auto& existing : scene.debugMouseParallaxLayerInventory) {
        if (existing.layerId == layerId && existing.layerKind == info.layerKind) {
            existing = std::move(info);
            return;
        }
    }
    scene.debugMouseParallaxLayerInventory.push_back(std::move(info));
}

void recordGeneratedTextDiagnostic(Scene& scene, const GeneratedTextDiagnostic& info)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return;
    }

    for (auto& existing : scene.debugGeneratedTextDiagnostics) {
        if (existing.layerId == info.layerId) {
            existing = info;
            return;
        }
    }
    scene.debugGeneratedTextDiagnostics.push_back(info);
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
            {"localTransform", transformToJson(state.localTransform)},
            {"meshBounds", meshBoundsToJson(state.meshBounds)},
            {"worldBounds", state.worldBounds},
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
        {"sceneOrtho", {scene.ortho[0], scene.ortho[1]}},
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
        {"layerVisibilityOverrides", layerVisibilityOverridesToJson(
            scene.debugEffectCaptures.layerVisibilityOverrides)},
        {"probeMaxEffects", nullableInt(scene.debugEffectCaptures.probeMaxEffects)},
        {"puppetFinalMeshOverride", scene.debugEffectCaptures.puppetFinalMeshOverride},
        {"puppetEffectRouteOnly", scene.debugEffectCaptures.puppetEffectRouteOnly},
        {"mouseParallax", mouseParallaxToJson(scene)},
        {"mediaStateTimeline", mediaStateTimelineToJson(scene.debugEffectCaptures.mediaStateTimelineJson)},
        {"generatedTextDiagnostics", generatedTextDiagnosticsToJson(scene)},
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
