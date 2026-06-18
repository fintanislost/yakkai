#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

#include "Debug/EffectCaptureDebug.hpp"
#include "SceneTexture.h"
#include "SceneRenderTarget.h"
#include "SceneNode.h"
#include "SceneLight.hpp"

#include "Core/NoCopyMove.hpp"

namespace wallpaper
{
class ParticleSystem;
class IShaderValueUpdater;
class IImageParser;
struct SceneScriptMediaState;

namespace fs
{
class VFS;
}
class Scene : NoCopy, NoMove {
public:
    Scene();
    ~Scene();

    std::unordered_map<std::string, SceneTexture>      textures;
    std::unordered_map<std::string, SceneRenderTarget> renderTargets;

    std::unordered_map<std::string, std::shared_ptr<SceneCamera>> cameras;
    std::unordered_map<std::string, std::vector<std::string>>     linkedCameras;

    std::vector<std::unique_ptr<SceneLight>> lights;

    std::shared_ptr<SceneNode>           sceneGraph;
    std::unique_ptr<IShaderValueUpdater> shaderValueUpdater;
    std::unique_ptr<IImageParser>        imageParser;
    std::unique_ptr<fs::VFS>             vfs;

    struct MediaTimelineScaleBinding {
        int32_t layerId { 0 };
        std::string script;
        std::array<float, 3> authoredOrigin { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> authoredScale { 1.0f, 1.0f, 1.0f };
        std::array<float, 2> size { 0.0f, 0.0f };
        float parentHorizontalSign { 1.0f };
        int canvasWidth { 1920 };
        int canvasHeight { 1080 };
        bool leadingEdgeAnchored { false };
        nlohmann::json userProperties;
        std::unordered_map<std::string, double> scriptProperties;
        std::weak_ptr<SceneNode> node;
    };
    std::vector<MediaTimelineScaleBinding> mediaTimelineScaleBindings;

    std::string scene_id { "unknown_id" };

    bool first_frame_ok { false };
    wallpaper::debug::EffectCaptureConfig              debugEffectCaptures;
    std::vector<wallpaper::debug::EffectCaptureRecord> debugEffectCaptureRecords;
    std::vector<wallpaper::debug::EffectPassState>     debugEffectPassStates;
    std::vector<wallpaper::debug::EffectCaptureLayerInfo> debugEffectStrippedCandidates;
    std::vector<wallpaper::debug::EffectCaptureLayerInfo> debugEffectProtectedPuppetDiagnostics;
    std::vector<wallpaper::debug::EffectCaptureLayerInfo> debugPuppetAnimationLayerInventory;
    std::vector<wallpaper::debug::EffectCaptureMouseParallaxLayerInfo>
        debugMouseParallaxLayerInventory;
    std::vector<wallpaper::debug::GeneratedTextDiagnostic>
        debugGeneratedTextDiagnostics;
    std::vector<wallpaper::debug::EffectCaptureFinalDisplayBoundaryHook>
        debugEffectFinalDisplayBoundaryCaptures;

    SceneMesh default_effect_mesh;

    std::unique_ptr<ParticleSystem> paritileSys;

    SceneCamera* activeCamera;

    i32                  ortho[2] { 1920, 1080 }; // w, h
    std::array<float, 3> clearColor { 1.0f, 1.0f, 1.0f };

    double elapsingTime { 0.0f }, frameTime { 0.0f };
    void   PassFrameTime(double t) {
          frameTime = t;
          elapsingTime += t;
    }

    void UpdateLinkedCamera(const std::string& name) {
        if (linkedCameras.count(name) != 0) {
            auto& cams = linkedCameras.at(name);
            for (auto& cam : cams) {
                if (cameras.count(cam) != 0) {
                    cameras.at(cam)->Clone(*cameras.at(name));
                    cameras.at(cam)->Update();
                }
            }
        }
    }
};

SceneScriptMediaState InterpolatedSceneMediaState(const SceneScriptMediaState& state,
                                                  double elapsedSeconds);
void ApplySceneMediaTimelineState(Scene& scene, const SceneScriptMediaState& mediaState);
} // namespace wallpaper
