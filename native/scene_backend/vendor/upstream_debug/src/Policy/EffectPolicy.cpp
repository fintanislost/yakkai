#include "Policy/EffectPolicy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <limits>
#include <utility>

namespace wallpaper::policy {
namespace {

bool containsToken(const std::string& value, std::string_view token)
{
    return value.find(token) != std::string::npos;
}

std::string asciiLower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool containsTokenInsensitive(std::string_view value, std::string_view token)
{
    return asciiLower(value).find(asciiLower(token)) != std::string::npos;
}

constexpr std::array<std::string_view, 3> kWaterFamilies {
    "waterwaves",
    "waterflow",
    "waterripple",
};

constexpr std::array<std::string_view, 12> kKnownNonWaterTokens {
    "opacity",
    "shine",
    "iris",
    "pulse",
    "shake",
    "lut",
    "color_grading",
    "colorgrading",
    "blur",
    "audio",
    "lightshaft",
    "effectpassthrough",
};

constexpr std::array<std::pair<std::string_view, std::string_view>, 16> kDiagnosticMixFamilyAliases {{
    {"opacity", "opacity"},
    {"shine", "shine"},
    {"iris", "iris"},
    {"audio", "audio"},
    {"blur", "blur"},
    {"lut_loader", "lut"},
    {"lut", "lut"},
    {"color_grading", "color-grade"},
    {"color grading", "color-grade"},
    {"colorgrading", "color-grade"},
    {"colorgrade", "color-grade"},
    {"colorcorrection", "color-grade"},
    {"pulse", "pulse"},
    {"shake", "shake"},
    {"lightshaft", "lightshaft"},
    {"effectpassthrough", "effectpassthrough"},
}};

bool containsAnyToken(std::string_view value, const auto& tokens)
{
    return std::any_of(tokens.begin(), tokens.end(), [value](std::string_view token) {
        return containsTokenInsensitive(value, token);
    });
}

bool equalsWaterFamily(std::string_view value)
{
    const std::string lowered = asciiLower(value);
    return std::any_of(kWaterFamilies.begin(), kWaterFamilies.end(), [&lowered](std::string_view family) {
        return lowered == family;
    });
}

std::string_view editorEffectTitleWaterFamily(std::string_view value)
{
    const std::string lowered = asciiLower(value);
    if (lowered == "ui_editor_effect_water_waves_title") {
        return "waterwaves";
    }
    if (lowered == "ui_editor_effect_water_flow_title") {
        return "waterflow";
    }
    if (lowered == "ui_editor_effect_water_ripple_title") {
        return "waterripple";
    }
    return {};
}

bool pathEndsWithWaterFamily(std::string_view value)
{
    const std::string lowered = asciiLower(value);
    return std::any_of(kWaterFamilies.begin(), kWaterFamilies.end(), [&lowered](std::string_view family) {
        const std::string suffix = "/" + std::string(family);
        return lowered.size() >= suffix.size() &&
               lowered.compare(lowered.size() - suffix.size(), suffix.size(), suffix) == 0;
    });
}

void appendUnique(std::vector<std::string>& values, std::string_view value)
{
    const std::string owned(value);
    if (std::find(values.begin(), values.end(), owned) == values.end()) {
        values.push_back(owned);
    }
}

void collectFamiliesFromText(std::vector<std::string>& families, std::string_view value)
{
    if (const std::string_view family = editorEffectTitleWaterFamily(value); !family.empty()) {
        appendUnique(families, family);
    }
    for (std::string_view family : kWaterFamilies) {
        if (containsTokenInsensitive(value, family)) {
            appendUnique(families, family);
        }
    }
}

std::vector<std::string> collectCandidateFamilies(const std::vector<LayerEffectDescriptor>& effects)
{
    std::vector<std::string> families;
    for (const auto& effect : effects) {
        if (!effect.visible) {
            continue;
        }
        collectFamiliesFromText(families, effect.name);
        collectFamiliesFromText(families, effect.firstMaterialShader);
        for (const auto& shader : effect.materialShaders) {
            collectFamiliesFromText(families, shader);
        }
    }
    return families;
}

void collectMixFamiliesFromText(std::vector<std::string>& families, std::string_view value)
{
    std::vector<std::pair<std::size_t, std::string_view>> matches;
    const std::string lowered = asciiLower(value);
    for (const auto& [alias, family] : kDiagnosticMixFamilyAliases) {
        const std::size_t pos = lowered.find(alias);
        if (pos != std::string::npos) {
            matches.push_back({pos, family});
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
        }
        return lhs.second < rhs.second;
    });
    for (const auto& match : matches) {
        appendUnique(families, match.second);
    }
}

std::vector<std::string> collectCandidateMixFamilies(const std::vector<LayerEffectDescriptor>& effects)
{
    std::vector<std::string> families;
    for (const auto& effect : effects) {
        if (!effect.visible) {
            continue;
        }
        collectMixFamiliesFromText(families, effect.name);
        collectMixFamiliesFromText(families, effect.firstMaterialShader);
        for (const auto& shader : effect.materialShaders) {
            collectMixFamiliesFromText(families, shader);
        }
    }
    return families;
}

bool hasFamily(const std::vector<std::string>& families, std::string_view family)
{
    return std::find(families.begin(), families.end(), std::string(family)) != families.end();
}

std::string waterMixShape(const std::vector<std::string>& families)
{
    const bool opacity = hasFamily(families, "opacity");
    const bool shine = hasFamily(families, "shine");
    const bool iris = hasFamily(families, "iris");
    if (opacity && shine && iris) {
        return "water+opacity+shine+iris";
    }
    if (opacity && shine) {
        return "water+opacity+shine";
    }
    if (opacity && iris) {
        return "water+opacity+iris";
    }
    if (opacity) {
        return "water+opacity";
    }
    if (shine) {
        return "water+shine";
    }
    if (iris) {
        return "water+iris";
    }
    return {};
}

std::string joinShapeParts(const std::vector<std::string_view>& parts)
{
    std::string shape;
    for (std::string_view part : parts) {
        if (!shape.empty()) {
            shape += "-";
        }
        shape += part;
    }
    return shape;
}

std::string highRiskShape(const CandidateClassification& classification)
{
    std::vector<std::string_view> parts;
    if (classification.candidateChecks.hasBlurFamily) {
        parts.push_back("blur");
    }
    if (classification.candidateChecks.hasLutFamily) {
        parts.push_back("lut");
    }
    if (classification.candidateChecks.hasColorGradingFamily) {
        parts.push_back("color-grade");
    }
    return joinShapeParts(parts);
}

std::string carrierShape(std::string_view base, std::string_view carrier)
{
    if (base.empty()) {
        return {};
    }
    std::string shape(base);
    shape += "-";
    shape += carrier;
    return shape;
}

std::string diagnosticChainShape(const CandidateClassification& classification)
{
    const std::string riskyShape = highRiskShape(classification);

    if (!classification.candidateChecks.hasWaterFamily) {
        if (classification.candidateChecks.isProtectedPuppetPath) {
            return "protected-puppet-mixed";
        }
        if (classification.candidateChecks.isPuppetLayer) {
            return "puppet-mixed";
        }
        if (classification.candidateChecks.isComposelayer) {
            if (const std::string shape = carrierShape(riskyShape, "composelayer"); !shape.empty()) {
                return shape;
            }
            return classification.candidateMixFamilies.empty() ? "non-water" : "carrier-mixed";
        }
        if (classification.candidateChecks.isUtilityCarrier) {
            if (hasFamily(classification.candidateMixFamilies, "audio")) {
                return "audio-utility";
            }
            if (const std::string shape = carrierShape(riskyShape, "utility"); !shape.empty()) {
                return shape;
            }
            return classification.candidateMixFamilies.empty() ? "non-water" : "carrier-mixed";
        }
        if (classification.candidateChecks.isFullscreen) {
            if (const std::string shape = carrierShape(riskyShape, "fullscreen"); !shape.empty()) {
                return shape;
            }
            return classification.candidateMixFamilies.empty() ? "non-water" : "carrier-mixed";
        }
        if (!riskyShape.empty()) {
            return riskyShape + "-only";
        }
        return classification.candidateMixFamilies.empty() ? "non-water" : "unknown-mixed";
    }
    if (classification.candidateChecks.isProtectedPuppetPath) {
        return "protected-puppet-mixed";
    }
    if (classification.candidateChecks.isPuppetLayer) {
        return "puppet-mixed";
    }
    if (classification.candidateChecks.isComposelayer) {
        if (const std::string shape = carrierShape(riskyShape, "composelayer"); !shape.empty()) {
            return shape;
        }
        if (classification.candidateChecks.waterOnly) {
            return "water-composelayer";
        }
        return "carrier-mixed";
    }
    if (classification.candidateChecks.isUtilityCarrier) {
        if (hasFamily(classification.candidateMixFamilies, "audio")) {
            return "audio-utility";
        }
        if (const std::string shape = carrierShape(riskyShape, "utility"); !shape.empty()) {
            return shape;
        }
        if (classification.candidateChecks.waterOnly) {
            return "water-utility";
        }
        return "carrier-mixed";
    }
    if (classification.candidateChecks.isFullscreen) {
        if (const std::string shape = carrierShape(riskyShape, "fullscreen"); !shape.empty()) {
            return shape;
        }
        if (classification.candidateChecks.waterOnly) {
            return "water-fullscreen";
        }
        return "carrier-mixed";
    }
    if (!classification.candidateChecks.waterOnly) {
        if (const std::string shape = waterMixShape(classification.candidateMixFamilies); !shape.empty()) {
            return shape;
        }
        return "unknown-mixed";
    }
    return "simple-water";
}

std::string diagnosticEffectClass(const CandidateClassification& classification)
{
    const bool hasBlur = classification.candidateChecks.hasBlurFamily;
    const bool hasLut = classification.candidateChecks.hasLutFamily;
    const bool hasColorGrade = classification.candidateChecks.hasColorGradingFamily;
    if (classification.candidateChainShape == "water-composelayer") {
        return "composelayer-water-only";
    }
    if (classification.candidateChainShape == "water-utility") {
        return "utility-water-only";
    }
    if (classification.candidateChainShape == "water-fullscreen") {
        return "fullscreen-water-only";
    }
    if (!hasBlur && !hasLut && !hasColorGrade) {
        return "none";
    }

    if (hasBlur && !hasLut && !hasColorGrade) {
        if (classification.candidateChecks.isProtectedPuppetPath) {
            return "protected-puppet-blur";
        }
        if (classification.candidateChecks.isPuppetLayer) {
            return "mixed-puppet-blur";
        }
        if (classification.candidateChecks.isComposelayer) {
            return "composelayer-blur";
        }
        if (classification.candidateChecks.isUtilityCarrier) {
            return "utility-blur";
        }
        if (classification.candidateChecks.isFullscreen) {
            return "fullscreen-blur";
        }
        if (classification.candidateChainShape == "blur-only") {
            return "regular-blur-only";
        }
        return "mixed-blur";
    }

    if (classification.candidateChecks.isProtectedPuppetPath) {
        if (hasLut) {
            return "protected-puppet-lut";
        }
        return "protected-puppet-color-grade";
    }
    if (classification.candidateChecks.isPuppetLayer) {
        if (hasLut) {
            return "mixed-puppet-lut";
        }
        return "mixed-puppet-color-grade";
    }
    if (classification.candidateChecks.isComposelayer) {
        if (hasColorGrade) {
            return "composelayer-color-grade";
        }
        return "composelayer-lut";
    }

    if (classification.candidateChainShape == "lut-only") {
        return "regular-lut-only";
    }
    if (classification.candidateChainShape == "color-grade-only") {
        return "regular-color-grade-only";
    }
    if (hasLut && hasColorGrade) {
        return "mixed-lut-color-grade";
    }
    if (hasLut) {
        return "mixed-lut";
    }
    return "mixed-color-grade";
}

bool fieldIsStrictWater(std::string_view value)
{
    if (value.empty()) {
        return true;
    }
    return (equalsWaterFamily(value) ||
            !editorEffectTitleWaterFamily(value).empty() ||
            pathEndsWithWaterFamily(value)) &&
           !containsAnyToken(value, kKnownNonWaterTokens);
}

bool descriptorIsStrictWater(const LayerEffectDescriptor& effect)
{
    if (!effect.visible) {
        return true;
    }

    bool hasWater = containsAnyToken(effect.name, kWaterFamilies) ||
                    containsAnyToken(effect.firstMaterialShader, kWaterFamilies);
    bool fieldsAreStrictWater = fieldIsStrictWater(effect.name) &&
                                fieldIsStrictWater(effect.firstMaterialShader);
    for (const auto& shader : effect.materialShaders) {
        hasWater = hasWater || containsAnyToken(shader, kWaterFamilies);
        fieldsAreStrictWater = fieldsAreStrictWater && fieldIsStrictWater(shader);
    }
    return hasWater && fieldsAreStrictWater;
}

bool visibleEffectsAreStrictWater(const std::vector<LayerEffectDescriptor>& effects)
{
    bool sawVisible = false;
    for (const auto& effect : effects) {
        if (!effect.visible) {
            continue;
        }
        sawVisible = true;
        if (!descriptorIsStrictWater(effect)) {
            return false;
        }
    }
    return sawVisible;
}

bool anyEffectNameContains(const std::vector<LayerEffectDescriptor>& effects, std::string_view token)
{
    return std::any_of(effects.begin(), effects.end(), [token](const LayerEffectDescriptor& effect) {
        return containsToken(effect.name, token);
    });
}

bool anyMaterialShaderContains(const std::vector<LayerEffectDescriptor>& effects, std::string_view token)
{
    return std::any_of(effects.begin(), effects.end(), [token](const LayerEffectDescriptor& effect) {
        return std::any_of(effect.materialShaders.begin(), effect.materialShaders.end(),
                           [token](const std::string& shader) {
                               return containsToken(shader, token);
                           });
    });
}

bool anyFirstMaterialShaderContains(const std::vector<LayerEffectDescriptor>& effects, std::string_view token)
{
    return std::any_of(effects.begin(), effects.end(), [token](const LayerEffectDescriptor& effect) {
        return containsToken(effect.firstMaterialShader, token);
    });
}

bool isUtilityLayer(std::string_view imagePath)
{
    return imagePath.find("solidlayer") != std::string_view::npos ||
           imagePath.find("projectlayer") != std::string_view::npos ||
           imagePath.find("fullscreenlayer") != std::string_view::npos;
}

bool isComposelayerPath(std::string_view imagePath)
{
    return imagePath.find("composelayer") != std::string_view::npos;
}

bool isProtectedPuppetPath(const LayerEffectInput& input)
{
    if (!input.isPuppetLayer) {
        return false;
    }
    return containsTokenInsensitive(input.layerName, "crop_sheet") ||
           containsTokenInsensitive(input.layerName, "cropsheet") ||
           containsTokenInsensitive(input.layerName, "crop-sheet") ||
           containsTokenInsensitive(input.imagePath, "crop_sheet") ||
           containsTokenInsensitive(input.imagePath, "cropsheet") ||
           containsTokenInsensitive(input.imagePath, "crop-sheet");
}

bool isFlareOrLensLayer(std::string_view layerName)
{
    return layerName.find("flare") != std::string_view::npos ||
           layerName.find("lense") != std::string_view::npos ||
           layerName.find("lens") != std::string_view::npos;
}

bool isProtectedPuppetSafeMixFamily(std::string_view family)
{
    return family == "lut" || family == "pulse" || family == "shake";
}

bool isPuppetWaterSafeMixFamily(std::string_view family)
{
    return family == "opacity" || family == "shine" || family == "iris";
}

bool hasOnlyProtectedPuppetSafeFamilies(const CandidateClassification& classification)
{
    const auto& checks = classification.candidateChecks;
    const bool hasSafeFamily = checks.hasWaterFamily ||
                               std::any_of(classification.candidateMixFamilies.begin(),
                                           classification.candidateMixFamilies.end(),
                                           isProtectedPuppetSafeMixFamily);
    if (!hasSafeFamily) {
        return false;
    }
    return std::all_of(classification.candidateMixFamilies.begin(),
                       classification.candidateMixFamilies.end(),
                       isProtectedPuppetSafeMixFamily);
}

bool isPromotableProtectedPuppetEffect(const CandidateClassification& classification)
{
    const auto& checks = classification.candidateChecks;
    return checks.isProtectedPuppetPath &&
           checks.isPuppetLayer &&
           hasOnlyProtectedPuppetSafeFamilies(classification) &&
           !checks.hasBlurFamily &&
           !checks.hasColorGradingFamily &&
           !checks.isComposelayer &&
           !checks.isFullscreen &&
           !checks.isUtilityCarrier;
}

bool isPromotablePuppetWaterEffect(const CandidateClassification& classification)
{
    const auto& checks = classification.candidateChecks;
    return checks.isPuppetLayer &&
           !checks.isProtectedPuppetPath &&
           checks.hasWaterFamily &&
           std::all_of(classification.candidateMixFamilies.begin(),
                       classification.candidateMixFamilies.end(),
                       isPuppetWaterSafeMixFamily) &&
           !checks.hasBlurFamily &&
           !checks.hasLutFamily &&
           !checks.hasColorGradingFamily &&
           !checks.isComposelayer &&
           !checks.isFullscreen &&
           !checks.isUtilityCarrier;
}

int viewportExtentFromObject(float objectExtent)
{
    if (! std::isfinite(objectExtent) || objectExtent <= 0.0f) {
        return 1;
    }
    const float clamped = std::min(objectExtent, static_cast<float>(std::numeric_limits<uint16_t>::max()));
    return std::max(1, static_cast<int>(clamped));
}

int viewportExtentFromBounds(float objectExtent, float minPosition, float maxPosition)
{
    constexpr float kMeaningfulMeshOverflowRatio = 0.03f;
    constexpr float kMeshOverflowEpsilonPx = 0.5f;
    const int baseExtent = viewportExtentFromObject(objectExtent);
    const float meaningfulOverflow =
        std::max(kMeshOverflowEpsilonPx, static_cast<float>(baseExtent) * kMeaningfulMeshOverflowRatio);
    const float baseHalfExtent = baseExtent / 2.0f;
    float requiredHalfExtent = baseHalfExtent;
    if (std::isfinite(minPosition)) {
        requiredHalfExtent = std::max(requiredHalfExtent, std::abs(minPosition));
    }
    if (std::isfinite(maxPosition)) {
        requiredHalfExtent = std::max(requiredHalfExtent, std::abs(maxPosition));
    }
    const float overflow = requiredHalfExtent - baseHalfExtent;
    if (overflow <= meaningfulOverflow) {
        return baseExtent;
    }
    const float requiredExtent =
        std::min(requiredHalfExtent * 2.0f, static_cast<float>(std::numeric_limits<uint16_t>::max()));
    return std::max(1, static_cast<int>(std::ceil(requiredExtent)));
}

} // namespace

CandidateClassification classifyStrippedEffectCandidate(const LayerEffectInput& input)
{
    CandidateClassification classification;
    classification.candidateFamilies = collectCandidateFamilies(input.effects);
    classification.candidateMixFamilies = collectCandidateMixFamilies(input.effects);

    classification.candidateChecks.hasWaterFamily = !classification.candidateFamilies.empty();
    classification.candidateChecks.hasBlurFamily =
        hasFamily(classification.candidateMixFamilies, "blur");
    classification.candidateChecks.hasLutFamily =
        hasFamily(classification.candidateMixFamilies, "lut");
    classification.candidateChecks.hasColorGradingFamily =
        hasFamily(classification.candidateMixFamilies, "color-grade");
    classification.candidateChecks.waterOnly =
        classification.candidateChecks.hasWaterFamily && visibleEffectsAreStrictWater(input.effects);
    classification.candidateChecks.isUtilityCarrier = isUtilityLayer(input.imagePath);
    classification.candidateChecks.isComposelayer = input.isComposelayer || isComposelayerPath(input.imagePath);
    classification.candidateChecks.isFullscreen = input.fullscreen;
    classification.candidateChecks.isPuppetLayer = input.isPuppetLayer;
    classification.candidateChecks.isProtectedPuppetPath = isProtectedPuppetPath(input);

    if (!classification.candidateChecks.hasWaterFamily) {
        classification.candidateRisk = "non-water";
        classification.candidateBlockedReason = "no-water-effect-family";
    } else if (classification.candidateChecks.isProtectedPuppetPath) {
        classification.candidateRisk = "protected-puppet-path";
        classification.candidateBlockedReason = "protected-puppet-path";
    } else if (classification.candidateChecks.isPuppetLayer) {
        classification.candidateRisk = "puppet-layer";
        classification.candidateBlockedReason = "puppet-layer";
    } else if (classification.candidateChecks.isComposelayer) {
        classification.candidateRisk = "composelayer-carrier";
        classification.candidateBlockedReason = "composelayer-carrier";
    } else if (classification.candidateChecks.isUtilityCarrier) {
        classification.candidateRisk = "utility-carrier";
        classification.candidateBlockedReason = "utility-carrier";
    } else if (classification.candidateChecks.isFullscreen) {
        classification.candidateRisk = "fullscreen-carrier";
        classification.candidateBlockedReason = "fullscreen-carrier";
    } else if (!classification.candidateChecks.waterOnly) {
        classification.candidateRisk = "mixed-chain";
        classification.candidateBlockedReason = "water-effect-mixed-chain";
    } else {
        classification.candidateRisk = "simple-water";
        classification.candidateBlockedReason = "water-effect-candidate";
    }

    classification.candidateChainShape = diagnosticChainShape(classification);
    classification.candidateEffectClass = diagnosticEffectClass(classification);

    return classification;
}

LayerEffectDecision decideLayerEffects(const LayerEffectInput& input)
{
    LayerEffectDecision decision;
    decision.keepEffects = input.hasVisibleEffects;

    if (!input.hasVisibleEffects && input.fullscreen) {
        decision.keepLayer = false;
        decision.keepEffects = false;
        decision.reason = "effectless-fullscreen";
        return decision;
    }

    if (!input.hasVisibleEffects && input.isComposelayer) {
        decision.keepLayer = false;
        decision.keepEffects = false;
        decision.reason = "effectless-composelayer";
        return decision;
    }

    if (input.noEffectsDebug) {
        decision.keepEffects = false;
        decision.strippedEffects = input.hasVisibleEffects;
        if (input.isComposelayer) {
            decision.keepLayer = false;
            decision.reason = "debug-no-effects-composelayer";
        } else {
            decision.reason = "debug-no-effects";
        }
        return decision;
    }

    if (!input.sceneHasPuppetObjects || !input.hasVisibleEffects) {
        return decision;
    }

    const bool hasColorkey = anyEffectNameContains(input.effects, "colorkey") ||
                             anyMaterialShaderContains(input.effects, "colorkey");
    const bool hasHeavyEffect = anyEffectNameContains(input.effects, "audio") ||
                                anyEffectNameContains(input.effects, "lightshaft") ||
                                anyFirstMaterialShaderContains(input.effects, "audio") ||
                                anyFirstMaterialShaderContains(input.effects, "Audio") ||
                                anyFirstMaterialShaderContains(input.effects, "lightshaft");
    const bool isEssentialEffect = hasColorkey ||
        (!hasHeavyEffect && (isFlareOrLensLayer(input.layerName) || input.colorBlendMode != 0));

    if (!isEssentialEffect) {
        const auto candidateClassification = classifyStrippedEffectCandidate(input);
        if (candidateClassification.candidateRisk == "simple-water") {
            decision.reason = "simple-water-effect";
            return decision;
        }
        if (candidateClassification.candidateChainShape == "lut-only") {
            decision.reason = "lut-only-effect";
            return decision;
        }
        if (candidateClassification.candidateEffectClass == "regular-blur-only") {
            decision.reason = "regular-blur-only-effect";
            return decision;
        }
        if (candidateClassification.candidateEffectClass == "utility-blur") {
            decision.reason = "utility-blur-effect";
            return decision;
        }
        if (candidateClassification.candidateEffectClass == "composelayer-color-grade") {
            decision.reason = "composelayer-color-grade-effect";
            return decision;
        }
        if (candidateClassification.candidateEffectClass == "composelayer-water-only") {
            decision.reason = "composelayer-water-effect";
            return decision;
        }
        if (isPromotablePuppetWaterEffect(candidateClassification)) {
            decision.reason = "puppet-water-effect";
            return decision;
        }
        if (isPromotableProtectedPuppetEffect(candidateClassification)) {
            decision.reason = "protected-puppet-effect";
            return decision;
        }

        decision.keepEffects = false;
        decision.strippedEffects = true;
        decision.reason = "puppet-alpha-strip";
        if (input.isComposelayer || input.fullscreen || isUtilityLayer(input.imagePath)) {
            decision.keepLayer = false;
        }
        return decision;
    }

    decision.reason = "essential-effect";
    return decision;
}

EffectPublishRouteDecision decideEffectPublishRoute(const EffectPublishRouteInput& input)
{
    EffectPublishRouteDecision route;

    if (input.puppetLayer) {
        const bool deferredPuppetFinal =
            input.puppetFinalMeshOverride == "deferred-puppet-final" ||
            (! input.usePuppetChannelMapPrepass &&
             ! input.standalonePuppetFinalDisplay);
        route.effectInputMeshKind =
            input.usePuppetChannelMapPrepass
                ? "puppet-channelmap-base-uv-mesh"
                : deferredPuppetFinal ? "card" : "puppet-skinned-mesh";
        route.effectInputMaterialPreservesLayerBlendMode =
            input.standalonePuppetFinalDisplay;
        route.effectFinalMeshKind =
            (input.usePuppetChannelMapPrepass || deferredPuppetFinal)
                ? "puppet-skinned-mesh"
                : "flat-card";
        if (input.standalonePuppetFinalDisplay) {
            if (input.usePuppetChannelMapPrepass) {
                route.standaloneFinalMeshKind =
                    input.hasActivePuppetChannelBlendSlots
                        ? "puppet-image-space-filtered-overlay-or-puppet-skinned-mesh"
                        : "suppressed";
                route.standaloneFinalMaterialUsesPuppetSkinning =
                    route.standaloneFinalMeshKind != "suppressed";
            } else {
                route.standaloneFinalMeshKind =
                    input.puppetFinalMeshOverride == "image-space"
                        ? "puppet-image-space-mesh"
                        : "layer-card";
            }
        }
    } else {
        route.effectInputMeshKind = "card";
        route.effectFinalMeshKind =
            (input.fullscreen || input.composelayer) ? "fullscreen-card" : "card";
    }

    if (input.standalonePuppetFinalDisplay) {
        route.finalDisplayRoute = "standalone-puppet-final-display";
        route.standaloneDisplayAttachMode = "original-parent-sibling";
    } else if (input.composelayer) {
        route.finalDisplayRoute = "effect-layer-composite-final-publish";
    } else if (input.fullscreen) {
        route.finalDisplayRoute = "effect-layer-fullscreen-final-publish";
    } else {
        route.finalDisplayRoute = "effect-layer-node-final-publish";
    }

    if (input.puppetLayer && input.standalonePuppetFinalDisplay &&
        route.standaloneFinalMeshKind == "flat-card") {
        route.routeRisk = "puppet-effect-output-displayed-as-flat-card";
    }

    return route;
}

LayerEffectViewportDecision decideLayerEffectViewport(const LayerEffectViewportInput& input)
{
    LayerEffectViewportDecision viewport;
    viewport.width = viewportExtentFromObject(input.objectWidth);
    viewport.height = viewportExtentFromObject(input.objectHeight);

    if (! input.hasMeshBounds) {
        return viewport;
    }

    viewport.width =
        viewportExtentFromBounds(input.objectWidth, input.meshPositionMinX, input.meshPositionMaxX);
    viewport.height =
        viewportExtentFromBounds(input.objectHeight, input.meshPositionMinY, input.meshPositionMaxY);
    viewport.expandedToMeshBounds =
        viewport.width > viewportExtentFromObject(input.objectWidth) ||
        viewport.height > viewportExtentFromObject(input.objectHeight);
    return viewport;
}

} // namespace wallpaper::policy
