#include "SceneScriptMediaState.hpp"

#include <algorithm>
#include <ctime>
#include <optional>
#include <sstream>

namespace wallpaper {
namespace {

bool BoolValueOr(const nlohmann::json& object, const char* key, bool fallback)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

double NumberValueOr(const nlohmann::json& object, const char* key, double fallback)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return fallback;
    }
    return it->get<double>();
}

std::string StringValueOr(const nlohmann::json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

double ClampNonNegative(double value)
{
    return std::max(0.0, value);
}

std::optional<std::array<float, 3>> ColorValue(const nlohmann::json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return std::nullopt;
    }

    std::array<float, 3> color { 0.0f, 0.0f, 0.0f };
    if (it->is_array() && it->size() >= 3) {
        for (size_t i = 0; i < 3; ++i) {
            if (!(*it)[i].is_number()) {
                return std::nullopt;
            }
            color[i] = (*it)[i].get<float>();
        }
        return color;
    }

    if (it->is_string()) {
        std::istringstream in(it->get<std::string>());
        if (in >> color[0] >> color[1] >> color[2]) {
            return color;
        }
    }

    return std::nullopt;
}

std::optional<double> FixedClockEpochMs(const nlohmann::json& media)
{
    if (const auto it = media.find("clockEpochMs");
        it != media.end() && it->is_number()) {
        return it->get<double>();
    }

    int hours = -1;
    int minutes = -1;
    int seconds = 0;
    if (const auto it = media.find("clockTime");
        it != media.end() && it->is_string()) {
        char delimiter = 0;
        std::istringstream in(it->get<std::string>());
        if (!(in >> hours >> delimiter >> minutes) || delimiter != ':') {
            return std::nullopt;
        }
        if (in >> delimiter >> seconds) {
            if (delimiter != ':') {
                return std::nullopt;
            }
        }
    } else {
        const auto hoursIt = media.find("clockHours");
        const auto minutesIt = media.find("clockMinutes");
        if (hoursIt == media.end() || minutesIt == media.end() ||
            !hoursIt->is_number_integer() || !minutesIt->is_number_integer()) {
            return std::nullopt;
        }
        hours = hoursIt->get<int>();
        minutes = minutesIt->get<int>();
        if (const auto secondsIt = media.find("clockSeconds");
            secondsIt != media.end() && secondsIt->is_number_integer()) {
            seconds = secondsIt->get<int>();
        }
    }

    if (hours < 0 || hours > 23 ||
        minutes < 0 || minutes > 59 ||
        seconds < 0 || seconds > 59) {
        return std::nullopt;
    }

    std::time_t now = std::time(nullptr);
    std::tm local {};
    localtime_r(&now, &local);
    local.tm_hour = hours;
    local.tm_min = minutes;
    local.tm_sec = seconds;
    local.tm_isdst = -1;
    return static_cast<double>(std::mktime(&local)) * 1000.0;
}

} // namespace

SceneScriptMediaState SceneScriptMediaStateFromSceneProperties(const nlohmann::json& sceneProperties)
{
    SceneScriptMediaState state;
    const auto it = sceneProperties.find("__yakkaiMedia");
    if (it == sceneProperties.end() || !it->is_object()) {
        return state;
    }

    const nlohmann::json& media = *it;
    state.available = BoolValueOr(media, "available", false);
    state.playing = BoolValueOr(media, "playing", false);
    state.title = StringValueOr(media, "title");
    state.artist = StringValueOr(media, "artist");
    state.album = StringValueOr(media, "album");
    state.albumArtPath = StringValueOr(media, "albumArtPath");
    state.duration = ClampNonNegative(NumberValueOr(media, "duration", 0.0));
    state.position = ClampNonNegative(NumberValueOr(media, "position", 0.0));
    state.settleSeconds = ClampNonNegative(NumberValueOr(media, "settleSeconds", 0.0));
    if (auto fixedClock = FixedClockEpochMs(media)) {
        state.hasFixedClock = true;
        state.fixedClockEpochMs = *fixedClock;
    }
    if (auto textColor = ColorValue(media, "textColor")) {
        state.textColor = *textColor;
        state.hasThumbnailColors = true;
    }
    if (auto primaryColor = ColorValue(media, "primaryColor")) {
        state.primaryColor = *primaryColor;
        state.hasThumbnailColors = true;
    }
    if (auto secondaryColor = ColorValue(media, "secondaryColor")) {
        state.secondaryColor = *secondaryColor;
        state.hasThumbnailColors = true;
    }
    if (auto tertiaryColor = ColorValue(media, "tertiaryColor")) {
        state.tertiaryColor = *tertiaryColor;
        state.hasThumbnailColors = true;
    }
    if (state.duration > 0.0 && state.position > state.duration) {
        state.position = state.duration;
    }
    return state;
}

} // namespace wallpaper
