#include "WPJson.hpp"
#include <nlohmann/json.hpp>

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
        if (auto value = LookupUserPropertyValue(user.get_ref<const std::string&>())) {
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
    } else {
        std::string strvalue;
        strvalue = njson.get<std::string>();
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
            value = static_cast<T>(std::llround(std::stod(njson.get<std::string>())));
            return true;
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        if (njson.is_number()) {
            value = static_cast<T>(njson.get<double>());
            return true;
        }
        if (njson.is_string()) {
            value = static_cast<T>(std::stod(njson.get<std::string>()));
            return true;
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
