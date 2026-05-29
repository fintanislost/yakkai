#pragma once

#include "Policy/EffectPolicy.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wallpaper {
class Scene;
}

namespace wallpaper::debug {

struct EffectCaptureConfig {
    std::string outputDir;
    std::string commandLine;

    bool enabled() const { return !outputDir.empty(); }
    std::filesystem::path manifestPath() const;
};

struct EffectCaptureLayerInfo {
    std::string              sceneId;
    std::string              sceneType;
    std::string              layerName;
    std::string              layerImage;
    int                      layerId { 0 };
    int                      visibleEffectCount { 0 };
    float                    alpha { 1.0f };
    bool                     keepLayer { true };
    bool                     keepEffects { true };
    bool                     strippedEffects { false };
    bool                     forceAlphaOne { false };
    std::string              policyReason;
    std::vector<std::string> effectNames;
    std::vector<std::string> materialShaders;
    std::vector<std::string> candidateFamilies;
    std::vector<std::string> candidateMixFamilies;
    std::string              candidateChainShape;
    std::string              candidateRisk;
    std::string              candidateBlockedReason;
    wallpaper::policy::CandidateChecks candidateChecks;
};

struct EffectCaptureRecord {
    std::string            stage;
    std::string            label;
    std::string            renderTarget;
    std::string            path;
    EffectCaptureLayerInfo layer;
    int                    renderTargetWidth { 0 };
    int                    renderTargetHeight { 0 };
    std::string            renderTargetFormat { "RGBA8" };
    bool                   completed { false };
    bool                   failed { false };
    std::string            failureReason;
};

struct EffectPassState {
    std::string output;
    std::string loadOp;
    std::string depthLoadOp;
    std::string colorMask;
    std::string blendMode;
    bool        blendEnabled { false };
    bool        preserveOutput { false };
    bool        usesDepth { false };
};

std::string sanitizeCapturePathSegment(std::string_view value);

std::filesystem::path capturePath(const EffectCaptureConfig& config,
                                  const EffectCaptureLayerInfo& layer,
                                  std::string_view stage);

void registerEffectCapture(Scene& scene,
                           const EffectCaptureLayerInfo& layer,
                           std::string_view stage,
                           std::string_view renderTarget);

void recordEffectPassState(Scene& scene, const EffectPassState& state);

void recordStrippedEffectCandidate(Scene& scene, const EffectCaptureLayerInfo& layer);

bool writeEffectCaptureManifest(const Scene& scene);

} // namespace wallpaper::debug
