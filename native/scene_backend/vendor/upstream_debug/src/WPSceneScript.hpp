#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp>

namespace wallpaper {

// Lightweight WE SceneScript evaluator using QuickJS.
// Evaluates script modules that bind scene properties to layer properties
// (e.g., thisLayer.color = engine.userProperties.newproperty13).
//
// Usage:
//   SceneScriptContext ctx;
//   ctx.setUserProperties(sceneProperties);
//   ctx.setCanvasSize(width, height);
//   auto result = ctx.evaluateLayerScript(scriptSource);
//   if (result.color) layer.color = *result.color;

struct SceneScriptResult {
    std::optional<std::array<float, 3>> color;
    std::optional<float>                alpha;
    std::optional<bool>                 visible;
    std::optional<std::array<float, 3>> origin;
};

class SceneScriptContext {
public:
    SceneScriptContext();
    ~SceneScriptContext();

    SceneScriptContext(const SceneScriptContext&) = delete;
    SceneScriptContext& operator=(const SceneScriptContext&) = delete;

    // Set up the engine.userProperties object from scene property defaults.
    void setUserProperties(const nlohmann::json& properties);

    // Set engine.canvasSize.
    void setCanvasSize(int width, int height);

    // Set engine.timeOfDay (0.0 = midnight, 0.5 = noon, 1.0 = midnight).
    void setTimeOfDay(double fraction);

    // Override a script property value (resolved from scene JSON user bindings).
    void setScriptProperty(const std::string& name, double value);

    // Evaluate a SceneScript module and return any layer property modifications.
    // The script is expected to export an `update(value)` function.
    SceneScriptResult evaluateLayerScript(std::string_view script,
                                          const std::array<float, 3>& currentOrigin = {0,0,0},
                                          const std::array<float, 3>& currentColor = {1,1,1},
                                          float currentAlpha = 1.0f,
                                          int32_t layerId = -1);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace wallpaper
