#pragma once
#include "Core/Literals.hpp"
#include "Core/NoCopyMove.hpp"
#include "Core/MapSet.hpp"

#include <array>
#include <functional>
#include <string_view>

namespace wallpaper
{
class SceneNode;
class SceneShader;
class ShaderValue;
class SpriteAnimation;

using sprite_map_t    = Map<usize, SpriteAnimation>;
using UpdateUniformOp = std::function<void(std::string_view, ShaderValue)>;
using ExistsUniformOp = std::function<bool(std::string_view)>;

struct MouseParallaxDebugSnapshot {
    std::array<float, 2> inputPosition { 0.5f, 0.5f };
    std::array<float, 2> effectivePosition { 0.5f, 0.5f };
    std::array<float, 2> parallaxUniformPosition { 0.5f, 0.5f };
    bool cameraEnabled { false };
    float cameraAmount { 0.0f };
    float cameraDelay { 0.0f };
    float cameraMouseInfluence { 0.0f };
};

class IShaderValueUpdater : NoCopy, NoMove {
public:
    IShaderValueUpdater()          = default;
    virtual ~IShaderValueUpdater() = default;

    virtual void FrameBegin()                                                      = 0;
    virtual void InitUniforms(SceneNode*, const ExistsUniformOp&)                  = 0;
    virtual void UpdateUniforms(SceneNode*, sprite_map_t&, const UpdateUniformOp&) = 0;
    virtual void FrameEnd()                                                        = 0;

    virtual void MouseInput(double x, double y) = 0;
    virtual void SetTexelSize(float x, float y) = 0;
    virtual void SetScreenSize(i32 w, i32 h)    = 0;
    virtual MouseParallaxDebugSnapshot mouseParallaxDebugSnapshot() const = 0;
};
} // namespace wallpaper
