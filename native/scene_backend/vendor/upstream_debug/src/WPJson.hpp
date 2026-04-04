#pragma once
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string_view>
#include <type_traits>

#include "Utils/Logging.h"

#define GET_JSON_VALUE(json, value) \
    wallpaper::GetJsonValue(        \
        __SHORT_FILE__, __FUNCTION__, __LINE__, (json), (value), false, "", true)
#define GET_JSON_NAME_VALUE(json, name, value) \
    wallpaper::GetJsonValue(                   \
        __SHORT_FILE__, __FUNCTION__, __LINE__, (json), (value), true, (name), true)

#define GET_JSON_VALUE_NOWARN(json, value) \
    wallpaper::GetJsonValue(               \
        __SHORT_FILE__, __FUNCTION__, __LINE__, (json), (value), false, "", false)
#define GET_JSON_NAME_VALUE_NOWARN(json, name, value) \
    wallpaper::GetJsonValue(                          \
        __SHORT_FILE__, __FUNCTION__, __LINE__, (json), (value), true, (name), false)

#define PARSE_JSON(source, result) \
    wallpaper::ParseJson(__SHORT_FILE__, __FUNCTION__, __LINE__, (source), (result))

namespace wallpaper
{

template<typename T>
struct JsonTemplateTypeCheck {
    using type = bool;
    static_assert(! std::is_const_v<T>, "GetJsonValue need a non const value");
};

template<typename T>
typename wallpaper::JsonTemplateTypeCheck<T>::type
GetJsonValue(const char* file, const char* func, int line, const nlohmann::json& json, T& value,
             bool has_name, std::string_view name, bool warn);

bool ParseJson(const char* file, const char* func, int line, const std::string& source,
               nlohmann::json& result);

void SetActiveScenePropertyState(const nlohmann::json& properties);
void ClearActiveScenePropertyState();
std::optional<nlohmann::json> LookupUserPropertyValue(std::string_view name);

// Resolve a JSON field that may be a conditional user property binding.
std::optional<nlohmann::json> ResolveConditionalProperty(const nlohmann::json& field);

// Evaluate a WE animation curve at the given scene time (seconds).
// Returns the interpolated value, or std::nullopt if the field has no animation.
// Handles {"animation": {"c0": [{frame, value, ...}], "options": {fps, length, mode}}}.
std::optional<double> EvaluateAnimationCurve(const nlohmann::json& field, double sceneTimeSec);
// Evaluate a multi-channel animation (c0, c1, c2) → RGB color.
std::optional<std::array<float, 3>> EvaluateAnimationCurveRGB(const nlohmann::json& field, double sceneTimeSec);

// Get current scene time in seconds (monotonic, starts at 0).
double GetSceneTimeSec();
} // namespace wallpaper
