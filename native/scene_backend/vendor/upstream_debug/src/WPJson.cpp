#include "WPJson.hpp"
#include <nlohmann/json.hpp>

extern "C" {
#include "quickjs.h"
}

#include "Utils/Identity.hpp"
#include "Utils/String.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <memory>
#include <optional>
#include <regex>
#include <unordered_map>

namespace wallpaper
{

// Forward declaration — defined after the anonymous namespace.
std::optional<nlohmann::json> LookupUserPropertyValue(std::string_view name);

namespace
{
struct ActiveScenePropertyState {
    nlohmann::json                              properties { nlohmann::json::object() };
    std::unordered_map<std::string, nlohmann::json> sharedValues;
    double                                      timeOfDay { 0.0 };
};

thread_local std::optional<ActiveScenePropertyState> s_activeScenePropertyState;

double CurrentTimeOfDayFraction() {
    using clock = std::chrono::system_clock;
    const auto now = clock::to_time_t(clock::now());

    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif

    const double seconds =
        static_cast<double>(((localTime.tm_hour * 60) + localTime.tm_min) * 60 + localTime.tm_sec);
    return seconds / (24.0 * 60.0 * 60.0);
}

nlohmann::json PropertyEntryValue(const nlohmann::json& property) {
    if (property.is_object() && property.contains("value")) {
        return property.at("value");
    }
    // Combo properties may have no "value" — default to first option's value
    if (property.is_object() && property.contains("options")) {
        const auto& opts = property.at("options");
        if (opts.is_array() && ! opts.empty() && opts[0].contains("value")) {
            return opts[0].at("value");
        }
    }
    return property;
}

const nlohmann::json& JsonValueOrSelf(const nlohmann::json& json) {
    if (json.is_object() && json.contains("value")) {
        return json.at("value");
    }
    return json;
}

bool ParseBoolString(const std::string& text, bool& value) {
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lowered == "true" || lowered == "1") {
        value = true;
        return true;
    }
    if (lowered == "false" || lowered == "0") {
        value = false;
        return true;
    }
    return false;
}

bool JsonMatchesCondition(const nlohmann::json& value, const nlohmann::json& condition) {
    if (condition.is_boolean()) {
        if (value.is_boolean()) return value.get<bool>() == condition.get<bool>();
        if (value.is_number_integer()) return (value.get<int64_t>() != 0) == condition.get<bool>();
        if (value.is_string()) {
            const std::string text = value.get<std::string>();
            return (text == "1" || text == "true") == condition.get<bool>();
        }
        return false;
    }

    if (condition.is_number()) {
        if (value.is_number()) {
            return std::abs(value.get<double>() - condition.get<double>()) <= 1.0e-6;
        }
        if (value.is_string()) {
            try {
                return std::abs(std::stod(value.get<std::string>()) - condition.get<double>()) <=
                       1.0e-6;
            } catch (...) {
                return false;
            }
        }
        if (value.is_boolean()) {
            return (value.get<bool>() ? 1.0 : 0.0) == condition.get<double>();
        }
        return false;
    }

    const std::string conditionText = condition.is_string() ? condition.get<std::string>()
                                                            : condition.dump();
    if (value.is_string()) return value.get<std::string>() == conditionText;
    if (value.is_boolean()) {
        return conditionText == (value.get<bool>() ? "true" : "false") ||
               conditionText == (value.get<bool>() ? "1" : "0");
    }
    if (value.is_number_integer()) return std::to_string(value.get<int64_t>()) == conditionText;
    if (value.is_number_unsigned()) return std::to_string(value.get<uint64_t>()) == conditionText;
    if (value.is_number_float()) {
        try {
            return std::abs(value.get<double>() - std::stod(conditionText)) <= 1.0e-6;
        } catch (...) {
            return false;
        }
    }
    return value.dump() == conditionText;
}

std::optional<nlohmann::json> ResolveUserWrappedValue(const nlohmann::json& json) {
    if (! json.is_object() || ! json.contains("user")) {
        return std::nullopt;
    }

    const auto fallback = json.contains("value") ? std::optional<nlohmann::json>(json.at("value"))
                                                  : std::nullopt;
    const auto& user = json.at("user");
    if (user.is_string()) {
        const auto& userName = user.get_ref<const std::string&>();
        if (auto value = LookupUserPropertyValue(userName)) {
            return *value;
        }
        return fallback;
    }

    if (! user.is_object()) {
        return fallback;
    }

    std::string name;
    GET_JSON_NAME_VALUE_NOWARN(user, "name", name);
    if (name.empty()) {
        return fallback;
    }

    auto propertyValue = LookupUserPropertyValue(name);
    if (! propertyValue) {
        return fallback;
    }

    if (user.contains("condition")) {
        return JsonMatchesCondition(*propertyValue, user.at("condition"));
    }

    return *propertyValue;
}

double SmoothStep(double edge0, double edge1, double x) {
    const double delta = edge1 - edge0;
    if (std::abs(delta) <= 1.0e-9) {
        return x < edge0 ? 0.0 : 1.0;
    }

    const double t = std::clamp((x - edge0) / delta, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double UserPropertyNumber(std::string_view name, double fallback = 0.0) {
    if (auto value = LookupUserPropertyValue(name)) {
        if (value->is_number()) return value->get<double>();
        if (value->is_boolean()) return value->get<bool>() ? 1.0 : 0.0;
        if (value->is_string()) {
            try {
                return std::stod(value->get<std::string>());
            } catch (...) {
                return fallback;
            }
        }
    }
    return fallback;
}

bool UserPropertyBool(std::string_view name, bool fallback = false) {
    if (auto value = LookupUserPropertyValue(name)) {
        if (value->is_boolean()) return value->get<bool>();
        if (value->is_number_integer()) return value->get<int64_t>() != 0;
        if (value->is_number_unsigned()) return value->get<uint64_t>() != 0;
        if (value->is_number_float()) return std::abs(value->get<double>()) > 1.0e-6;
        if (value->is_string()) {
            const std::string text = value->get<std::string>();
            return text == "true" || text == "1";
        }
    }
    return fallback;
}

void SetSharedValue(std::string key, nlohmann::json value) {
    if (! s_activeScenePropertyState) return;
    s_activeScenePropertyState->sharedValues[std::move(key)] = std::move(value);
}

std::optional<nlohmann::json> LookupSharedValue(std::string_view key) {
    if (! s_activeScenePropertyState) return std::nullopt;
    const auto it = s_activeScenePropertyState->sharedValues.find(std::string(key));
    if (it == s_activeScenePropertyState->sharedValues.end()) return std::nullopt;
    return it->second;
}

std::optional<double> ExtractJsNumber(std::string_view script, std::string_view name) {
    const std::string scriptText(script);
    const std::regex pattern(
        std::string(R"(const\s+)") + std::string(name) + R"(\s*=\s*([-+]?[0-9]*\.?[0-9]+)\s*;)");
    std::smatch match;
    if (! std::regex_search(scriptText, match, pattern) || match.size() < 2) {
        return std::nullopt;
    }

    try {
        return std::stod(match.str(1));
    } catch (...) {
        return std::nullopt;
    }
}

nlohmann::json CoerceLikeFallback(const nlohmann::json& fallback, double value) {
    if (fallback.is_boolean()) return value > 0.5;
    if (fallback.is_number_integer()) return static_cast<int32_t>(std::lround(value));
    if (fallback.is_number_unsigned()) return static_cast<uint32_t>(std::lround(value));
    return value;
}

std::optional<nlohmann::json> EvaluateTimeOfDayScript(std::string_view script,
                                                      const nlohmann::json& fallback) {
    if (! s_activeScenePropertyState) {
        return std::nullopt;
    }
    if (script.find("engine.userProperties.timeofday") == std::string_view::npos ||
        script.find("engine.timeOfDay") == std::string_view::npos ||
        script.find("smoothStep") == std::string_view::npos) {
        return std::nullopt;
    }

    const double startHour = ExtractJsNumber(script, "START_HOUR").value_or(0.0);
    const double endHour = ExtractJsNumber(script, "END_HOUR").value_or(0.0);
    const double blendDuration = ExtractJsNumber(script, "BLEND_DURATION").value_or(0.0);
    const double dayTime = s_activeScenePropertyState->timeOfDay;
    const int    mode = static_cast<int>(std::lround(UserPropertyNumber("timeofday", 0.0)));

    double value = 0.0;
    if (script.find("engine.userProperties.timeofday == 1 || engine.userProperties.timeofday == 2") !=
        std::string_view::npos) {
        if (mode == 1 || mode == 2) {
            value = 1.0;
        } else if (mode == 0) {
            value = SmoothStep((startHour - blendDuration) / 24.0, startHour / 24.0, dayTime) *
                    SmoothStep(endHour / 24.0, (endHour - blendDuration) / 24.0, dayTime);
        }
    } else if (script.find("engine.userProperties.timeofday == 2") != std::string_view::npos) {
        if (mode == 2) {
            value = 1.0;
        } else if (mode == 0) {
            value = SmoothStep((startHour - blendDuration) / 24.0, startHour / 24.0, dayTime) *
                    SmoothStep(endHour / 24.0, (endHour - blendDuration) / 24.0, dayTime);
        }
        if (script.find("shared.sunset = value;") != std::string_view::npos) {
            SetSharedValue("sunset", value);
            SetSharedValue("showsunset", value > 0.0);
        }
    } else if (script.find("engine.userProperties.timeofday == 3") != std::string_view::npos) {
        if (mode == 3) {
            value = 1.0;
        } else if (mode == 0) {
            value = std::max(
                SmoothStep((startHour - blendDuration) / 24.0, startHour / 24.0, dayTime),
                SmoothStep(endHour / 24.0, (endHour - blendDuration) / 24.0, dayTime));
        }
        if (script.find("shared.night = value;") != std::string_view::npos) {
            SetSharedValue("night", value);
            SetSharedValue("shownight", value > 0.0);
        }
    } else {
        return std::nullopt;
    }

    return CoerceLikeFallback(fallback, value);
}

std::optional<nlohmann::json> EvaluateKnownScriptValue(const nlohmann::json& json) {
    if (! json.is_object() || ! json.contains("script") || ! json.at("script").is_string()) {
        return std::nullopt;
    }

    const std::string& script = json.at("script").get_ref<const std::string&>();
    const nlohmann::json fallback = json.contains("value") ? json.at("value") : nlohmann::json();

    {
        static const std::regex directSharedPattern(R"(value\s*=\s*shared\.([A-Za-z0-9_]+)\s*;)");
        std::smatch match;
        if (std::regex_search(script, match, directSharedPattern) && match.size() >= 2) {
            if (auto value = LookupSharedValue(match.str(1))) {
                return *value;
            }
            return fallback;
        }
    }

    if (script.find("engine.userProperties.loadingintro == true") != std::string::npos) {
        return CoerceLikeFallback(fallback, UserPropertyBool("loadingintro", false) ? 1.0 : 0.0);
    }

    if (auto value = EvaluateTimeOfDayScript(script, fallback)) {
        return value;
    }

    if (script.find("shared.shownight > 0") != std::string::npos &&
        script.find("new Vec3(") != std::string::npos) {
        static const std::regex vecPattern(R"(new\s+Vec3\(([^)]*)\))");
        std::sregex_iterator it(script.begin(), script.end(), vecPattern);
        std::sregex_iterator end;
        std::vector<std::array<float, 3>> colors;
        for (; it != end; ++it) {
            std::array<float, 3> color { 0.0f, 0.0f, 0.0f };
            std::string serialized = (*it)[1].str();
            std::replace(serialized.begin(), serialized.end(), ',', ' ');
            if (utils::StrToArray::Convert(serialized, color)) {
                colors.push_back(color);
            }
        }
        if (colors.size() >= 2) {
            if (auto shownight = LookupSharedValue("shownight")) {
                if (shownight->is_boolean() && shownight->get<bool>()) {
                    return nlohmann::json(colors[0]);
                }
                return nlohmann::json(colors[1]);
            }
            return nlohmann::json(colors[1]);
        }
        if (auto shownight = LookupSharedValue("shownight")) {
            if (shownight->is_boolean() && shownight->get<bool>() && colors.size() >= 1) {
                return nlohmann::json(colors[0]);
            }
            if (colors.size() >= 2) return nlohmann::json(colors[1]);
        }
    }

    // QuickJS fallback for unrecognized scripts that reference userProperties.
    // Evaluates the script and returns the computed value.
    if ((script.find("engine.") != std::string::npos ||
         script.find("scriptProperties") != std::string::npos) &&
        script.size() > 50 && s_activeScenePropertyState) {
        // Build a minimal JS snippet: define userProperties, run the script, capture 'value'
        std::string js = "var engine = { userProperties: {";
        for (const auto& [key, prop] : s_activeScenePropertyState->properties.items()) {
            if (! prop.is_object() || ! prop.contains("value")) continue;
            const auto& val = prop.at("value");
            js += "'" + key + "': ";
            if (val.is_number()) {
                js += std::to_string(val.get<double>());
            } else if (val.is_boolean()) {
                js += val.get<bool>() ? "true" : "false";
            } else if (val.is_string()) {
                js += "'" + val.get<std::string>() + "'";
            } else {
                js += "null";
            }
            js += ", ";
        }
        js += "}, canvasSize: { x: 1920, y: 1080 }, timeOfDay: " +
              std::to_string(s_activeScenePropertyState->timeOfDay) + " };\n";
        js += "function Vec3(x,y,z) { this.x=x||0; this.y=y||0; this.z=z||0; }\n";
        js += "function createScriptProperties() {\n"
              "  var p={}; var b={\n"
              "    addSlider:function(o){p[o.name]=o.value;return b;},\n"
              "    addColor:function(o){p[o.name]=o.value;return b;},\n"
              "    addCheckbox:function(o){p[o.name]=o.value;return b;},\n"
              "    addCombo:function(o){p[o.name]=o.value;return b;},\n"
              "    addText:function(o){p[o.name]=o.value;return b;},\n"
              "    finish:function(){return p;}\n"
              "  }; return b;\n"
              "}\n";
        // Strip ES module syntax so QuickJS can evaluate as a plain script
        std::string scriptSrc(script);
        {
            auto rep = [](std::string& s, const std::string& f, const std::string& t) {
                size_t p = 0;
                while ((p = s.find(f, p)) != std::string::npos) { s.replace(p, f.size(), t); p += t.size(); }
            };
            rep(scriptSrc, "'use strict';", "");
            rep(scriptSrc, "\"use strict\";", "");
            // WE scripts use non-breaking spaces (U+00A0, UTF-8 C2 A0)
            rep(scriptSrc, "\xc2\xa0", " "); // normalize NBSP → space
            rep(scriptSrc, "export var ", "var ");
            rep(scriptSrc, "export function ", "function ");
            rep(scriptSrc, "export default ", "");
            rep(scriptSrc, "export ", ""); // catch remaining exports
        }
        js += "var thisLayer = { color: new Vec3(0,0,0), alpha: 1, visible: true, origin: new Vec3(0,0,0) };\n";
        js += "var thisScene = { clearColor: new Vec3(0,0,0) };\n";
        js += "var value = new Vec3(0, 0, 0);\n";
        js += "(function() {\n";
        js += scriptSrc;
        js += "\nif (typeof update === 'function') { var __r = update(value); if (__r) value = __r; }\n";
        js += "})();\n";

        JSRuntime* rt = JS_NewRuntime();
        JSContext* ctx = JS_NewContext(rt);
        JSValue result = JS_Eval(ctx, js.c_str(), js.size(), "<script>", JS_EVAL_TYPE_GLOBAL);
        if (! JS_IsException(result)) {
            // Try to extract the result
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue jsval = JS_GetPropertyStr(ctx, global, "value");
            if (! JS_IsNull(jsval) && ! JS_IsUndefined(jsval)) {
                double d;
                if (JS_ToFloat64(ctx, &d, jsval) == 0) {
                    JS_FreeValue(ctx, jsval);
                    JS_FreeValue(ctx, global);
                    JS_FreeValue(ctx, result);
                    JS_FreeContext(ctx);
                    JS_FreeRuntime(rt);
                    return CoerceLikeFallback(fallback, d);
                }
                const char* str = JS_ToCString(ctx, jsval);
                if (str) {
                    std::string sv(str);
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, jsval);
                    JS_FreeValue(ctx, global);
                    JS_FreeValue(ctx, result);
                    JS_FreeContext(ctx);
                    JS_FreeRuntime(rt);
                    LOG_INFO("QuickJS eval result: '%s'", sv.c_str());
                    return nlohmann::json(sv);
                }
            }
            JS_FreeValue(ctx, jsval);
            JS_FreeValue(ctx, global);
        } else {
            JSValue exc = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, exc);
            if (msg) {
                LOG_INFO("QuickJS eval error (non-fatal): %s", msg);
                JS_FreeCString(ctx, msg);
            }
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, result);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }

    return std::nullopt;
}

std::optional<nlohmann::json> ResolveStructuredSceneValue(const nlohmann::json& json) {
    if (! s_activeScenePropertyState || ! json.is_object()) {
        return std::nullopt;
    }

    if (auto userValue = ResolveUserWrappedValue(json)) {
        return userValue;
    }

    if (auto scriptValue = EvaluateKnownScriptValue(json)) {
        return scriptValue;
    }

    return std::nullopt;
}
} // namespace

std::optional<nlohmann::json> ResolveConditionalProperty(const nlohmann::json& field) {
    return ResolveStructuredSceneValue(field);
}

static std::chrono::steady_clock::time_point s_sceneStartTime = std::chrono::steady_clock::now();

double GetSceneTimeSec() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - s_sceneStartTime).count();
}

std::optional<double> EvaluateAnimationCurve(const nlohmann::json& field, double sceneTimeSec) {
    if (! field.is_object() || ! field.contains("animation")) return std::nullopt;
    const auto& anim = field.at("animation");
    if (! anim.contains("c0") || ! anim.at("c0").is_array()) return std::nullopt;
    const auto& keyframes = anim.at("c0");
    if (keyframes.empty()) return std::nullopt;

    // Parse animation options
    double fps = 30.0;
    int length = 2700;
    bool loop = true;
    if (anim.contains("options")) {
        const auto& opts = anim.at("options");
        if (opts.contains("fps")) fps = opts.at("fps").get<double>();
        if (opts.contains("length")) length = opts.at("length").get<int>();
        if (opts.contains("mode") && opts.at("mode").is_string() && opts.at("mode").get<std::string>() != "loop") loop = false;
    }

    // For looping day-night animations (long loops at 30fps), map time-of-day
    // to the frame range rather than using elapsed scene time.
    double totalFrames = static_cast<double>(length);
    double frame;
    if (loop && totalFrames >= 2400) {
        // Long loops represent a 24-hour cycle — use current time-of-day
        double dayFrac = CurrentTimeOfDayFraction();
        frame = dayFrac * totalFrames;
    } else {
        frame = sceneTimeSec * fps;
        if (loop && totalFrames > 0) {
            frame = std::fmod(frame, totalFrames);
            if (frame < 0) frame += totalFrames;
        } else {
            frame = std::clamp(frame, 0.0, totalFrames);
        }
    }

    // Find surrounding keyframes and interpolate linearly
    double prevFrame = 0, prevValue = keyframes[0].at("value").get<double>();
    for (size_t i = 0; i < keyframes.size(); i++) {
        double kfFrame = keyframes[i].at("frame").get<double>();
        double kfValue = keyframes[i].at("value").get<double>();
        if (frame <= kfFrame) {
            if (i == 0) return kfValue;
            double t = (kfFrame > prevFrame) ? (frame - prevFrame) / (kfFrame - prevFrame) : 0.0;
            return prevValue + t * (kfValue - prevValue);
        }
        prevFrame = kfFrame;
        prevValue = kfValue;
    }
    return prevValue; // past last keyframe
}

std::optional<std::array<float, 3>> EvaluateAnimationCurveRGB(const nlohmann::json& field, double sceneTimeSec) {
    if (! field.is_object() || ! field.contains("animation")) return std::nullopt;
    const auto& anim = field.at("animation");
    if (! anim.contains("c0")) return std::nullopt;

    // Build a synthetic single-channel field for each component
    std::array<float, 3> result { 1.0f, 1.0f, 1.0f };
    const char* channels[] = { "c0", "c1", "c2" };
    for (int ch = 0; ch < 3; ch++) {
        if (! anim.contains(channels[ch])) continue;
        nlohmann::json singleChannel;
        singleChannel["animation"]["c0"] = anim.at(channels[ch]);
        if (anim.contains("options")) singleChannel["animation"]["options"] = anim.at("options");
        if (auto val = EvaluateAnimationCurve(singleChannel, sceneTimeSec)) {
            result[ch] = static_cast<float>(*val);
        }
    }
    return result;
}

std::optional<nlohmann::json> LookupUserPropertyValue(std::string_view name) {
    if (! s_activeScenePropertyState || name.empty()) {
        return std::nullopt;
    }
    const auto& properties = s_activeScenePropertyState->properties;
    if (! properties.is_object()) {
        return std::nullopt;
    }
    const std::string key(name);
    if (! properties.contains(key)) {
        return std::nullopt;
    }
    return PropertyEntryValue(properties.at(key));
}

bool ParseJson(const char* file, const char* func, int line, const std::string& source,
               nlohmann::json& result) {
    try {
        result = nlohmann::json::parse(source);
    } catch (nlohmann::json::parse_error& e) {
        WallpaperLog(LOGLEVEL_ERROR, file, line, "parse json(%s), %s", func, e.what());
        return false;
    }
    return true;
}

void SetActiveScenePropertyState(const nlohmann::json& properties) {
    ActiveScenePropertyState state;
    if (properties.is_object()) {
        state.properties = properties;
    }
    state.timeOfDay = CurrentTimeOfDayFraction();
    s_activeScenePropertyState = std::move(state);
}

void ClearActiveScenePropertyState() {
    s_activeScenePropertyState.reset();
}

template<typename T>
inline bool _GetJsonValue(const nlohmann::json&                  json,
                          typename utils::is_std_array<T>::type& value) {
    std::optional<nlohmann::json> resolved = ResolveStructuredSceneValue(json);
    const auto* pjson = resolved ? &resolved.value() : &json;
    using Tv          = typename T::value_type;
    const auto& njson = JsonValueOrSelf(*pjson);
    if (njson.is_array()) {
        value = njson.get<T>();
        return true;
    }
    if (njson.is_number()) {
        value = { njson.get<Tv>() };
        return true;
    } else if (njson.is_string()) {
        std::string strvalue = njson.get<std::string>();
        return utils::StrToArray::Convert(strvalue, value);
    } else {
        // Not a number or string — try dump() as fallback
        std::string strvalue = njson.dump();
        return utils::StrToArray::Convert(strvalue, value);
    }
}

template<typename T>
inline bool _GetJsonValue(const nlohmann::json& json, T& value) {
    std::optional<nlohmann::json> resolved = ResolveStructuredSceneValue(json);
    const auto& njson = JsonValueOrSelf(resolved ? resolved.value() : json);

    if constexpr (std::is_same_v<T, bool>) {
        if (njson.is_boolean()) {
            value = njson.get<bool>();
            return true;
        }
        if (njson.is_number_integer()) {
            value = njson.get<int64_t>() != 0;
            return true;
        }
        if (njson.is_number_unsigned()) {
            value = njson.get<uint64_t>() != 0;
            return true;
        }
        if (njson.is_number_float()) {
            value = std::abs(njson.get<double>()) > 1.0e-6;
            return true;
        }
        if (njson.is_string()) {
            return ParseBoolString(njson.get<std::string>(), value);
        }
    } else if constexpr (std::is_integral_v<T>) {
        if (njson.is_number()) {
            value = static_cast<T>(std::llround(njson.get<double>()));
            return true;
        }
        if (njson.is_string()) {
            try { value = static_cast<T>(std::llround(std::stod(njson.get<std::string>()))); return true; }
            catch (...) { return false; }
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        if (njson.is_number()) {
            value = static_cast<T>(njson.get<double>());
            return true;
        }
        if (njson.is_string()) {
            try { value = static_cast<T>(std::stod(njson.get<std::string>())); return true; }
            catch (...) { return false; }
        }
    }

    value = njson.get<T>();
    return true;
}

template<typename T>
inline bool _GetJsonValue(const char* file, const char* func, int line, const nlohmann::json& json,
                          T& value, bool warn, const char* name) {
    (void)warn;

    using njson = nlohmann::json;
    std::string nameinfo;
    if (name != nullptr) nameinfo = std::string("(key: ") + name + ")";
    try {
        return _GetJsonValue<T>(json, value);
    } catch (const njson::type_error& e) {
        WallpaperLog(LOGLEVEL_INFO,
                     file,
                     line,
                     "%s %s at %s\n%s",
                     e.what(),
                     nameinfo.c_str(),
                     func,
                     json.dump(4).c_str());
    } catch (const std::invalid_argument& e) {
        WallpaperLog(LOGLEVEL_ERROR, file, line, "%s %s at %s", e.what(), nameinfo.c_str(), func);
    } catch (const std::out_of_range& e) {
        WallpaperLog(LOGLEVEL_ERROR, file, line, "%s %s at %s", e.what(), nameinfo.c_str(), func);
    } catch (const utils::StrToArray::WrongSizeExp& e) {
        WallpaperLog(LOGLEVEL_ERROR, file, line, "%s %s at %s", e.what(), nameinfo.c_str(), func);
    }
    return false;
}

template<typename T>
typename JsonTemplateTypeCheck<T>::type
GetJsonValue(const char* file, const char* func, int line, const nlohmann::json& json, T& value,
             bool has_name, std::string_view name_view, bool warn) {
    std::string name { name_view };
    if (has_name) {
        if (! json.contains(name)) {
            if (warn)
                WallpaperLog(LOGLEVEL_INFO,
                             "",
                             0,
                             "read json \"%s\" not a key at %s(%s:%d)",
                             name.data(),
                             func,
                             file,
                             line);
            return false;
        } else if (json.at(name).is_null()) {
            if (warn)
                WallpaperLog(LOGLEVEL_INFO,
                             "",
                             0,
                             "read json \"%s\" is null at %s(%s:%d)",
                             name.data(),
                             func,
                             file,
                             line);
            return false;
        }
    }
    return _GetJsonValue<T>(file,
                            func,
                            line,
                            has_name ? json.at(name) : json,
                            value,
                            warn,
                            name.empty() ? nullptr : name.c_str());
}

#define T_IMPL_GET_JSON(TYPE)                                                            \
    template JsonTemplateTypeCheck<TYPE>::type GetJsonValue<TYPE>(const char*,           \
                                                                  const char*,           \
                                                                  int,                   \
                                                                  const nlohmann::json&, \
                                                                  TYPE&,                 \
                                                                  bool,                  \
                                                                  std::string_view,      \
                                                                  bool);

T_IMPL_GET_JSON(bool);
T_IMPL_GET_JSON(int32_t);
T_IMPL_GET_JSON(uint32_t);
T_IMPL_GET_JSON(float);
T_IMPL_GET_JSON(double);
T_IMPL_GET_JSON(std::string);
T_IMPL_GET_JSON(std::vector<float>);
T_IMPL_GET_JSON(std::vector<std::string>);

template<std::size_t N>
using iarray = std::array<int, N>;
T_IMPL_GET_JSON(iarray<3>);

template<std::size_t N>
using farray = std::array<float, N>;
T_IMPL_GET_JSON(farray<2>);
T_IMPL_GET_JSON(farray<3>);

// template bool GetJsonValue();
} // namespace wallpaper
