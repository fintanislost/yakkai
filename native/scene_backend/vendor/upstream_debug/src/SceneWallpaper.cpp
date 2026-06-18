#include "SceneWallpaper.hpp"
#include "SceneWallpaperSurface.hpp"

#include "Utils/Logging.h"
#include "Looper/Looper.hpp"

#include "Timer/FrameTimer.hpp"
#include "Utils/FpsCounter.h"
#include "WPJson.hpp"
#include "WPSceneParser.hpp"
#include "SceneScriptMediaState.hpp"
#include "Scene/Scene.h"
#include "Particle/ParticleSystem.h"
#include "Interface/IShaderValueUpdater.h"

#include "Fs/VFS.h"
#include "Fs/PhysicalFs.h"
#include "WPPkgFs.hpp"

#include "Audio/SoundManager.h"

#include "RenderGraph/RenderGraph.hpp"

#include "VulkanRender/SceneToRenderGraph.hpp"
#include "VulkanRender/VulkanRender.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <vector>

using namespace wallpaper;

#define CASE_CMD(cmd)      \
    case CMD::CMD_##cmd:   \
        handle_##cmd(msg); \
        break;
#define MHANDLER_CMD(cmd) void handle_##cmd(const std::shared_ptr<looper::Message>& msg)
#define MHANDLER_CMD_IMPL(cl, cmd) \
    void impl_##cl::handle_##cmd(const std::shared_ptr<looper::Message>& msg)
#define CALL_MHANDLER_CMD(cmd, msg) handle_##cmd(msg)

namespace
{
nlohmann::json LoadScenePropertiesFromProjectFile(const std::filesystem::path& projectPath) {
    std::ifstream input(projectPath);
    if (! input) {
        return nlohmann::json::object();
    }

    const std::string projectSource((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    nlohmann::json projectJson;
    if (! PARSE_JSON(projectSource, projectJson) || ! projectJson.is_object()) {
        return nlohmann::json::object();
    }

    const auto generalIt = projectJson.find("general");
    if (generalIt == projectJson.end() || ! generalIt->is_object()) {
        return nlohmann::json::object();
    }

    const auto propertiesIt = generalIt->find("properties");
    if (propertiesIt == generalIt->end() || ! propertiesIt->is_object()) {
        return nlohmann::json::object();
    }

    LOG_INFO("scene property defaults loaded from %s: count=%zu",
             projectPath.string().c_str(),
             propertiesIt->size());
    return *propertiesIt;
}

std::string ResolveScenePropertiesJson(const std::string& explicitJson, const std::string& pkgDir) {
    if (! explicitJson.empty()) {
        nlohmann::json parsedProperties;
        if (PARSE_JSON(explicitJson, parsedProperties) && parsedProperties.is_object() &&
            ! parsedProperties.empty()) {
            return explicitJson;
        }
    }

    const std::filesystem::path projectPath = std::filesystem::path(pkgDir) / "project.json";
    const nlohmann::json        properties  = LoadScenePropertiesFromProjectFile(projectPath);
    if (! properties.empty()) {
        return properties.dump();
    }

    return explicitJson;
}

nlohmann::json ParseMediaStateJson(const std::string& mediaStateJson)
{
    if (mediaStateJson.empty()) {
        return nlohmann::json::object();
    }

    nlohmann::json parsed;
    if (PARSE_JSON(mediaStateJson, parsed) && parsed.is_object()) {
        return parsed;
    }

    return nlohmann::json::object();
}

SceneScriptMediaState SceneScriptMediaStateFromJsonString(const std::string& mediaStateJson)
{
    return SceneScriptMediaStateFromSceneProperties(ParseMediaStateJson(mediaStateJson));
}

bool ShouldLogHighFrequency(std::atomic<uint64_t>& counter,
                            uint64_t               initial_burst = 6,
                            uint64_t               interval = 180) {
    const uint64_t count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return count <= initial_burst || (count % interval) == 0;
}

std::atomic<uint64_t> s_render_draw_begin_log_counter { 0 };
std::atomic<uint64_t> s_render_draw_finish_log_counter { 0 };

struct DebugMouseTimelinePoint {
    int timeMs = 0;
    float x = 0.5f;
    float y = 0.5f;
};

std::string_view TrimAsciiWhitespace(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<std::array<float, 2>> ParseDebugMousePosition(std::string_view value) {
    value = TrimAsciiWhitespace(value);
    if (value.empty()) {
        return std::nullopt;
    }

    const std::size_t comma = value.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view rawX = TrimAsciiWhitespace(value.substr(0, comma));
    std::string_view rawY = TrimAsciiWhitespace(value.substr(comma + 1));
    if (rawX.empty() || rawY.empty() || rawY.find(',') != std::string_view::npos) {
        return std::nullopt;
    }

    float x = 0.5f;
    float y = 0.5f;
    const auto parseFloat = [](std::string_view raw, float& out) {
        const char* first = raw.data();
        const char* last = raw.data() + raw.size();
        const auto [ptr, ec] = std::from_chars(first, last, out);
        return ec == std::errc() && ptr == last && std::isfinite(out);
    };
    if (!parseFloat(rawX, x) || !parseFloat(rawY, y) ||
        x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f) {
        return std::nullopt;
    }
    return std::array<float, 2> { x, y };
}

std::optional<int> ParseDebugNonNegativeInt(std::string_view value) {
    value = TrimAsciiWhitespace(value);
    if (value.empty()) {
        return std::nullopt;
    }

    int out = 0;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(first, last, out);
    if (ec != std::errc() || ptr != last || out < 0) {
        return std::nullopt;
    }
    return out;
}

bool ParseDebugNormalizedFloat(std::string_view value, float& out) {
    value = TrimAsciiWhitespace(value);
    if (value.empty()) {
        return false;
    }

    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc() && ptr == last && std::isfinite(out) &&
           out >= 0.0f && out <= 1.0f;
}

std::optional<std::vector<DebugMouseTimelinePoint>> ParseDebugMouseTimeline(std::string_view value) {
    value = TrimAsciiWhitespace(value);
    if (value.empty()) {
        return std::vector<DebugMouseTimelinePoint> {};
    }

    std::vector<DebugMouseTimelinePoint> timeline;
    std::optional<int> previousTimeMs;
    while (!value.empty()) {
        const std::size_t separator = value.find(';');
        std::string_view keyframe = separator == std::string_view::npos
            ? value
            : value.substr(0, separator);
        value = separator == std::string_view::npos
            ? std::string_view {}
            : value.substr(separator + 1);

        keyframe = TrimAsciiWhitespace(keyframe);
        const std::size_t colon = keyframe.find(':');
        if (colon == std::string_view::npos || keyframe.find(':', colon + 1) != std::string_view::npos) {
            return std::nullopt;
        }

        const std::optional<int> timeMs = ParseDebugNonNegativeInt(keyframe.substr(0, colon));
        if (!timeMs || (previousTimeMs && *timeMs <= *previousTimeMs)) {
            return std::nullopt;
        }

        const std::string_view position = keyframe.substr(colon + 1);
        const std::size_t comma = position.find(',');
        if (comma == std::string_view::npos || position.find(',', comma + 1) != std::string_view::npos) {
            return std::nullopt;
        }

        float x = 0.5f;
        float y = 0.5f;
        if (!ParseDebugNormalizedFloat(position.substr(0, comma), x) ||
            !ParseDebugNormalizedFloat(position.substr(comma + 1), y)) {
            return std::nullopt;
        }

        timeline.push_back(DebugMouseTimelinePoint { *timeMs, x, y });
        previousTimeMs = *timeMs;
    }

    if (timeline.size() < 2) {
        return std::nullopt;
    }
    return timeline;
}

std::array<float, 2> EvaluateDebugMouseTimeline(const std::vector<DebugMouseTimelinePoint>& timeline,
                                                double elapsedMs) {
    if (timeline.empty()) {
        return { 0.5f, 0.5f };
    }
    if (elapsedMs <= static_cast<double>(timeline.front().timeMs)) {
        return { timeline.front().x, timeline.front().y };
    }
    for (std::size_t i = 1; i < timeline.size(); ++i) {
        const auto& previous = timeline[i - 1];
        const auto& current = timeline[i];
        if (elapsedMs <= static_cast<double>(current.timeMs)) {
            const double span = static_cast<double>(current.timeMs - previous.timeMs);
            const double t = span > 0.0
                ? (elapsedMs - static_cast<double>(previous.timeMs)) / span
                : 0.0;
            return {
                static_cast<float>(static_cast<double>(previous.x) +
                                   (static_cast<double>(current.x) - static_cast<double>(previous.x)) * t),
                static_cast<float>(static_cast<double>(previous.y) +
                                   (static_cast<double>(current.y) - static_cast<double>(previous.y)) * t),
            };
        }
    }
    return { timeline.back().x, timeline.back().y };
}

void ClearDebugMouseTimelineManifest(wallpaper::debug::EffectCaptureConfig::MouseParallax& mouseParallax) {
    mouseParallax.timeline.clear();
    mouseParallax.timelineElapsedMsAtCapture.reset();
}

void ApplyDefaultCenterMouseManifest(wallpaper::debug::EffectCaptureConfig::MouseParallax& mouseParallax) {
    mouseParallax.inputSource = "default-center";
    mouseParallax.hasRequestedPosition = false;
    mouseParallax.requestedPosition = { 0.5f, 0.5f };
    ClearDebugMouseTimelineManifest(mouseParallax);
}

void ApplyDebugMousePositionManifest(wallpaper::debug::EffectCaptureConfig::MouseParallax& mouseParallax,
                                     std::array<float, 2> position) {
    mouseParallax.inputSource = "synthetic";
    mouseParallax.hasRequestedPosition = true;
    mouseParallax.requestedPosition = position;
    ClearDebugMouseTimelineManifest(mouseParallax);
}

void ApplyDebugMouseTimelineManifest(wallpaper::debug::EffectCaptureConfig::MouseParallax& mouseParallax,
                                     const std::vector<DebugMouseTimelinePoint>& timeline) {
    mouseParallax.inputSource = timeline.empty() ? "default-center" : "synthetic-timeline";
    mouseParallax.hasRequestedPosition = false;
    ClearDebugMouseTimelineManifest(mouseParallax);
    mouseParallax.timeline.reserve(timeline.size());
    for (const auto& point : timeline) {
        mouseParallax.timeline.push_back({
            .timeMs = point.timeMs,
            .position = { point.x, point.y },
        });
    }
}

void ApplyInteractiveMouseManifest(wallpaper::debug::EffectCaptureConfig::MouseParallax& mouseParallax) {
    mouseParallax.inputSource = "interactive";
    mouseParallax.hasRequestedPosition = false;
    mouseParallax.requestedPosition = { 0.5f, 0.5f };
    ClearDebugMouseTimelineManifest(mouseParallax);
}

template<typename T>
void AddMsgCmd(looper::Message& msg, T cmd) {
    msg.setInt32("cmd", (int32_t)cmd);
}
template<typename T>
std::shared_ptr<looper::Message> CreateMsgWithCmd(const std::shared_ptr<looper::Handler>& handler,
                                                  T                                       cmd) {
    auto msg = looper::Message::create(0, handler);
    AddMsgCmd(*msg, cmd);
    return msg;
}
} // namespace

namespace wallpaper
{
class RenderHandler;

class MainHandler : public looper::Handler {
public:
    enum class CMD
    {
        CMD_LOAD_SCENE,
        CMD_SET_PROPERTY,
        CMD_STOP,
        CMD_FIRST_FRAME,
        CMD_NO
    };

public:
    MainHandler();
    virtual ~MainHandler() {};

    bool init();
    auto renderHandler() const { return m_render_handler; }
    bool inited() const { return m_inited; }

public:
    void onMessageReceived(const std::shared_ptr<looper::Message>& msg) override {
        int32_t cmd_int = (int32_t)CMD::CMD_NO;
        if (msg->findInt32("cmd", &cmd_int)) {
            CMD cmd = static_cast<CMD>(cmd_int);
            switch (cmd) {
                CASE_CMD(SET_PROPERTY);
                CASE_CMD(LOAD_SCENE);
                CASE_CMD(STOP);
                CASE_CMD(FIRST_FRAME);
            default: break;
            }
        }
    }

    void sendCmdLoadScene();
    void sendFirstFrameOk();
    bool isGenGraphviz() const { return m_gen_graphviz; }

private:
    void loadScene();
    void postDefaultMouseInputToRender();
    void postDebugMousePositionToRender(std::array<float, 2> position);
    void postDebugMouseTimelineToRender(const std::vector<DebugMouseTimelinePoint>& timeline);
    void postInteractiveMouseInputToRender();
    void postCurrentDebugMouseInputToRender();

    MHANDLER_CMD(LOAD_SCENE);
    MHANDLER_CMD(SET_PROPERTY);
    MHANDLER_CMD(STOP);
    MHANDLER_CMD(FIRST_FRAME);

private:
    bool m_inited { false };

    std::string m_assets;
    std::string m_source;
    std::string m_cache_path;
    std::string m_scene_properties_json;
    std::string m_media_state_json;
    std::string m_debug_effect_captures;
    std::string m_debug_effect_capture_command;
    int32_t     m_debug_effect_capture_delay_ms { 0 };
    std::string m_debug_effect_capture_layers;
    std::string m_debug_effect_probe_layers;
    std::string m_debug_effect_probe_high_risk_layers;
    std::string m_debug_effect_probe_channelmap_slots;
    std::string m_debug_effect_probe_max_effects;
    std::string m_debug_puppet_effect_final_mesh;
    bool        m_debug_puppet_effect_route_only { false };
    std::string m_debug_puppet_animation_layer_overrides;
    std::string m_debug_layer_visibility_overrides;
    std::string m_debug_mouse_position;
    std::string m_debug_mouse_timeline;
    std::string m_debug_media_state_timeline;
    bool        m_debug_mouse_position_active { false };
    bool        m_debug_mouse_timeline_active { false };
    bool        m_debug_interactive_mouse_active { false };
    bool        m_gen_graphviz { false };

    WPSceneParser                        m_scene_parser;
    std::unique_ptr<audio::SoundManager> m_sound_manager;
    FirstFrameCallback                   m_first_frame_callback;

private:
    std::shared_ptr<looper::Looper> m_main_loop;
    std::shared_ptr<looper::Looper> m_render_loop;
    std::shared_ptr<RenderHandler>  m_render_handler;
};
// for macro
using impl_MainHandler = MainHandler;

class RenderHandler : public looper::Handler {
public:
    enum class CMD
    {
        CMD_INIT_VULKAN,
        CMD_SET_SCENE,
        CMD_SET_FILLMODE,
        CMD_SET_SPEED,
        CMD_SET_MOUSE_DEFAULT,
        CMD_SET_MOUSE_POSITION,
        CMD_SET_MOUSE_TIMELINE,
        CMD_SET_MOUSE_INTERACTIVE,
        CMD_SET_MEDIA_STATE,
        CMD_STOP,
        CMD_DRAW,
        CMD_NO
    };
    MainHandler& main_handler;
    RenderHandler(MainHandler& m)
        : main_handler(m), m_render(std::make_unique<vulkan::VulkanRender>()) {}
    virtual ~RenderHandler() {
        frame_timer.Stop();
        m_render->destroy();
        LOG_INFO("render handler deleted");
    }

    void onMessageReceived(const std::shared_ptr<looper::Message>& msg) override {
        int32_t cmd_int = (int32_t)CMD::CMD_NO;
        if (msg->findInt32("cmd", &cmd_int)) {
            CMD cmd = static_cast<CMD>(cmd_int);
            switch (cmd) {
                CASE_CMD(DRAW);
                CASE_CMD(STOP);
                CASE_CMD(SET_FILLMODE);
                CASE_CMD(SET_SCENE);
                CASE_CMD(SET_SPEED);
                CASE_CMD(SET_MOUSE_DEFAULT);
                CASE_CMD(SET_MOUSE_POSITION);
                CASE_CMD(SET_MOUSE_TIMELINE);
                CASE_CMD(SET_MOUSE_INTERACTIVE);
                CASE_CMD(SET_MEDIA_STATE);
                CASE_CMD(INIT_VULKAN);
            default: break;
            }
        }
    }

    ExSwapchain* exSwapchain() const { return m_render->exSwapchain(); }

    bool renderInited() const { return m_render->inited(); }

    void setMousePos(double x, double y) { m_mouse_pos.store(std::array { (float)x, (float)y }); }

private:
    MHANDLER_CMD(STOP) {
        bool stop { false };
        if (msg->findBool("value", &stop)) {
            if (stop)
                frame_timer.Stop();
            else
                frame_timer.Run();
        }
    }
    MHANDLER_CMD(DRAW) {
        frame_timer.FrameBegin();
        if (m_rg) {
            if (ShouldLogHighFrequency(s_render_draw_begin_log_counter)) {
                LOG_INFO("render draw: begin speed=%.3f fillMode=%d", m_speed, static_cast<int>(m_fillmode));
            }
            // LOG_INFO("frame info, fps: %.1f, frametime: %.1f", 1.0f, 1000.0f*m_scene->frameTime);
            const double frameSeconds = frame_timer.IdeaTime() * m_speed;
            if (!m_mouse_timeline.empty()) {
                // Intentionally match the debug capture gate, which observes scene.elapsingTime
                // plus the previous scene.frameTime during drawFrame().
                const double frameTimelineElapsedMs =
                    std::max(0.0, m_scene->elapsingTime + std::max(0.0, m_scene->frameTime)) *
                    1000.0;
                const auto pos = EvaluateDebugMouseTimeline(m_mouse_timeline, frameTimelineElapsedMs);
                m_mouse_pos.store(std::array { pos[0], pos[1] });
                m_scene->debugEffectCaptures.mouseParallax.timelineElapsedMsAtCapture =
                    frameTimelineElapsedMs;
            }
            m_scene->shaderValueUpdater->FrameBegin();
            if (m_media_state && !m_scene->mediaTimelineScaleBindings.empty()) {
                const auto mediaState = InterpolatedSceneMediaState(
                    *m_media_state,
                    m_scene->elapsingTime - m_media_state_scene_time);
                ApplySceneMediaTimelineState(*m_scene, mediaState);
            }
            {
                auto pos = m_mouse_pos.load();
                m_scene->shaderValueUpdater->MouseInput(pos[0], pos[1]);
            }
            m_scene->paritileSys->Emitt();

            m_render->drawFrame(*m_scene);
            if (ShouldLogHighFrequency(s_render_draw_finish_log_counter)) {
                LOG_INFO("render draw: drawFrame finished");
            }

            m_scene->PassFrameTime(frameSeconds);
            if (!m_mouse_timeline.empty()) {
                m_mouse_timeline_elapsed_ms =
                    m_scene->debugEffectCaptures.mouseParallax.timelineElapsedMsAtCapture.value_or(0.0);
            }

            m_scene->shaderValueUpdater->FrameEnd();
            // fps_counter.RegisterFrame();

            if (! m_scene->first_frame_ok) {
                m_scene->first_frame_ok = true;
                LOG_INFO("render draw: first frame ok");
                main_handler.sendFirstFrameOk();
            }
        } else {
            LOG_INFO("render draw: skipped because render graph is not ready");
        }
        frame_timer.FrameEnd();
    }
    MHANDLER_CMD(SET_FILLMODE) {
        int32_t value;
        if (msg->findInt32("value", &value)) {
            m_fillmode = (FillMode)value;
            if (m_scene && renderInited()) {
                m_render->UpdateCameraFillMode(*m_scene, m_fillmode);
            }
        }
    }
    MHANDLER_CMD(SET_SCENE) {
        if (msg->findObject("scene", &m_scene)) {
            LOG_INFO("render set_scene: received scene");
            m_mouse_timeline_elapsed_ms = 0.0;
            if (!m_mouse_timeline.empty()) {
                const auto pos = EvaluateDebugMouseTimeline(m_mouse_timeline, m_mouse_timeline_elapsed_ms);
                m_mouse_pos.store(std::array { pos[0], pos[1] });
                m_scene->debugEffectCaptures.mouseParallax.timelineElapsedMsAtCapture =
                    m_mouse_timeline_elapsed_ms;
            }
            {
                auto pos = m_mouse_pos.load();
                m_scene->shaderValueUpdater->MouseInput(pos[0], pos[1]);
            }
            if (m_rg) m_render->clearLastRenderGraph();
            if (m_media_state) {
                m_media_state_scene_time = m_scene->elapsingTime;
                ApplySceneMediaTimelineState(*m_scene, *m_media_state);
            }
            LOG_INFO("render set_scene: building render graph");
            m_rg = sceneToRenderGraph(*m_scene);

            if (main_handler.isGenGraphviz()) m_rg->ToGraphviz("graph.dot");
            LOG_INFO("render set_scene: compiling render graph");
            m_render->compileRenderGraph(*m_scene, *m_rg);
            LOG_INFO("render set_scene: updating camera fill mode=%d", static_cast<int>(m_fillmode));
            m_render->UpdateCameraFillMode(*m_scene, m_fillmode);
            LOG_INFO("render set_scene: ready");
        }
    }
    MHANDLER_CMD(SET_SPEED) { msg->findFloat("value", &m_speed); }
    MHANDLER_CMD(SET_MOUSE_DEFAULT) {
        m_mouse_timeline.clear();
        m_mouse_timeline_elapsed_ms = 0.0;
        m_mouse_pos.store(std::array { 0.5f, 0.5f });
        if (m_scene) {
            ApplyDefaultCenterMouseManifest(m_scene->debugEffectCaptures.mouseParallax);
        }
    }
    MHANDLER_CMD(SET_MOUSE_POSITION) {
        float x = 0.5f;
        float y = 0.5f;
        if (msg->findFloat("x", &x) && msg->findFloat("y", &y)) {
            m_mouse_timeline.clear();
            m_mouse_timeline_elapsed_ms = 0.0;
            m_mouse_pos.store(std::array { x, y });
            if (m_scene) {
                ApplyDebugMousePositionManifest(
                    m_scene->debugEffectCaptures.mouseParallax,
                    std::array<float, 2> { x, y });
            }
        }
    }
    MHANDLER_CMD(SET_MOUSE_TIMELINE) {
        std::shared_ptr<std::vector<DebugMouseTimelinePoint>> timeline;
        if (msg->findObject("value", &timeline)) {
            m_mouse_timeline = *timeline;
            m_mouse_timeline_elapsed_ms = 0.0;
            if (m_scene) {
                auto& mouseParallax = m_scene->debugEffectCaptures.mouseParallax;
                if (mouseParallax.inputSource == "synthetic-timeline" || !m_mouse_timeline.empty()) {
                    ApplyDebugMouseTimelineManifest(mouseParallax, m_mouse_timeline);
                }
            }
            if (!m_mouse_timeline.empty()) {
                const auto pos = EvaluateDebugMouseTimeline(m_mouse_timeline, m_mouse_timeline_elapsed_ms);
                m_mouse_pos.store(std::array { pos[0], pos[1] });
            } else {
                m_mouse_pos.store(std::array { 0.5f, 0.5f });
            }
        }
    }
    MHANDLER_CMD(SET_MOUSE_INTERACTIVE) {
        m_mouse_timeline.clear();
        m_mouse_timeline_elapsed_ms = 0.0;
        if (m_scene) {
            ApplyInteractiveMouseManifest(m_scene->debugEffectCaptures.mouseParallax);
        }
    }
    MHANDLER_CMD(SET_MEDIA_STATE) {
        std::shared_ptr<SceneScriptMediaState> mediaState;
        if (msg->findObject("value", &mediaState) && mediaState) {
            m_media_state = *mediaState;
            m_media_state_scene_time = m_scene ? m_scene->elapsingTime : 0.0;
            if (m_scene) {
                ApplySceneMediaTimelineState(*m_scene, *m_media_state);
            }
        }
    }
    MHANDLER_CMD(INIT_VULKAN) {
        std::shared_ptr<RenderInitInfo> info;
        if (msg->findObject("info", &info)) {
            m_render->init(*info);

            // inited, callback to laod scene
            main_handler.sendCmdLoadScene();
        }
    }

public:
    FrameTimer frame_timer;
    FpsCounter fps_counter;

private:
    std::shared_ptr<Scene> m_scene { nullptr };
    float                  m_speed { 1.0f };

    std::unique_ptr<vulkan::VulkanRender> m_render;
    std::unique_ptr<rg::RenderGraph>      m_rg { nullptr };

    FillMode m_fillmode { FillMode::ASPECTCROP };

    std::atomic<std::array<float, 2>> m_mouse_pos { std::array { 0.5f, 0.5f } };
    std::vector<DebugMouseTimelinePoint> m_mouse_timeline;
    double m_mouse_timeline_elapsed_ms = 0.0;
    std::optional<SceneScriptMediaState> m_media_state;
    double m_media_state_scene_time = 0.0;
};
} // namespace wallpaper

SceneWallpaper::SceneWallpaper(): m_main_handler(std::make_shared<MainHandler>()) {}

SceneWallpaper::~SceneWallpaper() {
    /*
    if(m_offscreen) {
        // no wait
        auto msg = looper::Message::create(0, m_main_handler);
        msg->setObject("self_clean", m_main_handler);
        msg->setCleanAfterDeliver(true);
        m_main_handler = nullptr;
        msg->post();
    }
    */
}

bool SceneWallpaper::inited() const { return m_main_handler->inited(); }

bool SceneWallpaper::init() { return m_main_handler->init(); }

void SceneWallpaper::initVulkan(const RenderInitInfo& info) {
    m_offscreen                             = info.offscreen;
    std::shared_ptr<RenderInitInfo> sp_info = std::make_shared<RenderInitInfo>(info);
    auto                            msg =
        CreateMsgWithCmd(m_main_handler->renderHandler(), RenderHandler::CMD::CMD_INIT_VULKAN);
    msg->setObject("info", sp_info);
    msg->post();
}

void SceneWallpaper::play() {
    auto msg = CreateMsgWithCmd(m_main_handler, MainHandler::CMD::CMD_STOP);
    msg->setBool("value", false);
    msg->post();
}
void SceneWallpaper::pause() {
    auto msg = CreateMsgWithCmd(m_main_handler, MainHandler::CMD::CMD_STOP);
    msg->setBool("value", true);
    msg->post();
}

void SceneWallpaper::mouseInput(double x, double y) {
    m_main_handler->renderHandler()->setMousePos(x, y);
}

#define BASIC_TYPE(NAME, TYPENAME)                                                       \
    void SceneWallpaper::setProperty##NAME(std::string_view name, TYPENAME value) {      \
        auto msg = CreateMsgWithCmd(m_main_handler, MainHandler::CMD::CMD_SET_PROPERTY); \
        msg->setString("property", std::string(name));                                   \
        msg->set##NAME("value", value);                                                  \
        msg->post();                                                                     \
    }

BASIC_TYPE(Bool, bool);
BASIC_TYPE(Int32, int32_t);
BASIC_TYPE(Float, float);
BASIC_TYPE(String, std::string);
BASIC_TYPE(Object, std::shared_ptr<void>);

ExSwapchain* SceneWallpaper::exSwapchain() const {
    return m_main_handler->renderHandler()->exSwapchain();
}

MHANDLER_CMD_IMPL(MainHandler, LOAD_SCENE) {
    if (m_render_handler->renderInited()) {
        loadScene();
    }
}

MHANDLER_CMD_IMPL(MainHandler, SET_PROPERTY) {
    std::string property;
    if (msg->findString("property", &property)) {
        if (property == PROPERTY_SOURCE) {
            msg->findString("value", &m_source);
            LOG_INFO("source: %s", m_source.c_str());
            CALL_MHANDLER_CMD(LOAD_SCENE, msg);
        } else if (property == PROPERTY_ASSETS) {
            msg->findString("value", &m_assets);
            CALL_MHANDLER_CMD(LOAD_SCENE, msg);
        } else if (property == PROPERTY_FPS) {
            int32_t fps { 15 };
            msg->findInt32("value", &fps);
            if (fps >= 5) {
                m_render_handler->frame_timer.SetRequiredFps((uint8_t)fps);
            }
        } else if (property == PROPERTY_FILLMODE) {
            int32_t value;
            if (msg->findInt32("value", &value)) {
                auto nmsg =
                    CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_SET_FILLMODE);
                nmsg->setInt32("value", value);
                nmsg->post();
            }
        } else if (property == PROPERTY_GRAPHIVZ) {
            msg->findBool("value", &m_gen_graphviz);
        } else if (property == PROPERTY_MUTED) {
            bool muted { false };
            msg->findBool("value", &muted);
            m_sound_manager->SetMuted(muted);
        } else if (property == PROPERTY_VOLUME) {
            float volume { 1.0f };
            msg->findFloat("value", &volume);
            m_sound_manager->SetVolume(volume);
        } else if (property == PROPERTY_CACHE_PATH) {
            std::string path;
            msg->findString("value", &path);
            m_cache_path = path;
        } else if (property == PROPERTY_SCENE_PROPERTIES_JSON) {
            msg->findString("value", &m_scene_properties_json);
        } else if (property == PROPERTY_MEDIA_STATE_JSON) {
            msg->findString("value", &m_media_state_json);
            auto mediaState = std::make_shared<SceneScriptMediaState>(
                SceneScriptMediaStateFromJsonString(m_media_state_json));
            auto nmsg = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_SET_MEDIA_STATE);
            nmsg->setObject("value", mediaState);
            nmsg->post();
        } else if (property == PROPERTY_DEBUG_EFFECT_CAPTURES) {
            msg->findString("value", &m_debug_effect_captures);
        } else if (property == PROPERTY_DEBUG_EFFECT_CAPTURE_COMMAND) {
            msg->findString("value", &m_debug_effect_capture_command);
        } else if (property == PROPERTY_DEBUG_EFFECT_CAPTURE_DELAY_MS) {
            msg->findInt32("value", &m_debug_effect_capture_delay_ms);
        } else if (property == PROPERTY_DEBUG_EFFECT_CAPTURE_LAYERS) {
            msg->findString("value", &m_debug_effect_capture_layers);
        } else if (property == PROPERTY_DEBUG_EFFECT_PROBE_LAYERS) {
            msg->findString("value", &m_debug_effect_probe_layers);
        } else if (property == PROPERTY_DEBUG_EFFECT_PROBE_HIGH_RISK_LAYERS) {
            msg->findString("value", &m_debug_effect_probe_high_risk_layers);
        } else if (property == PROPERTY_DEBUG_EFFECT_PROBE_CHANNELMAP_SLOTS) {
            msg->findString("value", &m_debug_effect_probe_channelmap_slots);
        } else if (property == PROPERTY_DEBUG_EFFECT_PROBE_MAX_EFFECTS) {
            msg->findString("value", &m_debug_effect_probe_max_effects);
        } else if (property == PROPERTY_DEBUG_PUPPET_EFFECT_FINAL_MESH) {
            msg->findString("value", &m_debug_puppet_effect_final_mesh);
        } else if (property == PROPERTY_DEBUG_PUPPET_EFFECT_ROUTE_ONLY) {
            msg->findBool("value", &m_debug_puppet_effect_route_only);
        } else if (property == PROPERTY_DEBUG_PUPPET_ANIMATION_LAYER_OVERRIDES) {
            msg->findString("value", &m_debug_puppet_animation_layer_overrides);
        } else if (property == PROPERTY_DEBUG_LAYER_VISIBILITY_OVERRIDES) {
            msg->findString("value", &m_debug_layer_visibility_overrides);
        } else if (property == PROPERTY_DEBUG_MOUSE_POSITION) {
            msg->findString("value", &m_debug_mouse_position);
            if (const auto pos = ParseDebugMousePosition(m_debug_mouse_position)) {
                m_debug_mouse_timeline.clear();
                m_debug_mouse_timeline_active = false;
                m_debug_interactive_mouse_active = false;
                m_debug_mouse_position_active = true;
                postDebugMousePositionToRender(*pos);
            } else if (TrimAsciiWhitespace(m_debug_mouse_position).empty()) {
                m_debug_mouse_position_active = false;
                if (!m_debug_mouse_timeline_active && !m_debug_interactive_mouse_active) {
                    postDefaultMouseInputToRender();
                }
            } else if (!TrimAsciiWhitespace(m_debug_mouse_position).empty()) {
                LOG_ERROR("invalid debug mouse position: %s", m_debug_mouse_position.c_str());
            }
        } else if (property == PROPERTY_DEBUG_MOUSE_TIMELINE) {
            msg->findString("value", &m_debug_mouse_timeline);
            if (const auto timeline = ParseDebugMouseTimeline(m_debug_mouse_timeline)) {
                if (!timeline->empty()) {
                    m_debug_mouse_position.clear();
                    m_debug_mouse_position_active = false;
                    m_debug_interactive_mouse_active = false;
                    m_debug_mouse_timeline_active = true;
                    postDebugMouseTimelineToRender(*timeline);
                } else if (m_debug_mouse_timeline_active) {
                    m_debug_mouse_timeline_active = false;
                    if (!m_debug_interactive_mouse_active) {
                        postDefaultMouseInputToRender();
                    }
                } else {
                    m_debug_mouse_timeline_active = false;
                }
            } else if (!TrimAsciiWhitespace(m_debug_mouse_timeline).empty()) {
                LOG_ERROR("invalid debug mouse timeline: %s", m_debug_mouse_timeline.c_str());
            }
        } else if (property == PROPERTY_DEBUG_MEDIA_STATE_TIMELINE) {
            msg->findString("value", &m_debug_media_state_timeline);
        } else if (property == PROPERTY_DEBUG_INTERACTIVE_MOUSE) {
            bool value = false;
            if (msg->findBool("value", &value)) {
                m_debug_interactive_mouse_active = value;
                if (value) {
                    m_debug_mouse_position.clear();
                    m_debug_mouse_timeline.clear();
                    m_debug_mouse_position_active = false;
                    m_debug_mouse_timeline_active = false;
                    postInteractiveMouseInputToRender();
                } else if (!m_debug_mouse_position_active && !m_debug_mouse_timeline_active) {
                    postDefaultMouseInputToRender();
                }
            }
        } else if (property == PROPERTY_FIRST_FRAME_CALLBACK) {
            std::shared_ptr<FirstFrameCallback> cb;
            msg->findObject("value", &cb);
            m_first_frame_callback = *cb;
        } else if (property == PROPERTY_SPEED) {
            float speed { 1.0f };
            if (msg->findFloat("value", &speed)) {
                auto nmsg = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_SET_SPEED);
                nmsg->setFloat("value", speed);
                nmsg->post();
            }
        }
    }
}

MHANDLER_CMD_IMPL(MainHandler, STOP) {
    bool stop { false };
    if (msg->findBool("value", &stop)) {
        if (stop) {
            m_sound_manager->Pause();
        } else {
            m_sound_manager->Play();
        }

        auto msg_r = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_STOP);
        msg_r->setBool("value", stop);
        msg_r->post();
    }
}

MHANDLER_CMD_IMPL(MainHandler, FIRST_FRAME) {
    if (m_first_frame_callback) m_first_frame_callback();
}

void MainHandler::postDefaultMouseInputToRender() {
    auto msg = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_SET_MOUSE_DEFAULT);
    msg->post();
}

void MainHandler::postDebugMousePositionToRender(std::array<float, 2> position) {
    auto msg = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_SET_MOUSE_POSITION);
    msg->setFloat("x", position[0]);
    msg->setFloat("y", position[1]);
    msg->post();
}

void MainHandler::postDebugMouseTimelineToRender(const std::vector<DebugMouseTimelinePoint>& timeline) {
    auto msg = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_SET_MOUSE_TIMELINE);
    msg->setObject("value", std::make_shared<std::vector<DebugMouseTimelinePoint>>(timeline));
    msg->post();
}

void MainHandler::postInteractiveMouseInputToRender() {
    auto msg = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_SET_MOUSE_INTERACTIVE);
    msg->post();
}

void MainHandler::postCurrentDebugMouseInputToRender() {
    if (m_debug_mouse_timeline_active) {
        if (const auto timeline = ParseDebugMouseTimeline(m_debug_mouse_timeline);
            timeline && !timeline->empty()) {
            postDebugMouseTimelineToRender(*timeline);
            return;
        }
        m_debug_mouse_timeline_active = false;
    }
    if (m_debug_mouse_position_active) {
        if (const auto pos = ParseDebugMousePosition(m_debug_mouse_position)) {
            postDebugMousePositionToRender(*pos);
            return;
        }
        m_debug_mouse_position_active = false;
    }
    if (m_debug_interactive_mouse_active) {
        postInteractiveMouseInputToRender();
    }
}

void MainHandler::loadScene() {
    if (m_source.empty() || m_assets.empty()) return;

    LOG_INFO("loading scene: %s", m_source.c_str());

    if (! m_sound_manager->IsInited()) {
        m_sound_manager->Init();
        m_sound_manager->Play();
    } else {
        m_sound_manager->UnMountAll();
    }

    std::shared_ptr<Scene> scene { nullptr };

    // mount assets dir
    std::unique_ptr<fs::VFS> pVfs = std::make_unique<fs::VFS>();
    auto&                    vfs  = *pVfs;
    if (! vfs.IsMounted("assets")) {
        bool sus = vfs.Mount("/assets", fs::CreatePhysicalFs(m_assets), "assets");
        if (! sus) {
            LOG_ERROR("Mount assets dir failed");
            return;
        }
    }
    std::filesystem::path pkgPath_fs { m_source };
    pkgPath_fs.replace_extension("pkg");
    std::string pkgPath  = pkgPath_fs.native();
    std::string pkgEntry = pkgPath_fs.filename().replace_extension("json").native();
    std::string pkgDir   = pkgPath_fs.parent_path().native();
    std::string scene_id = pkgPath_fs.parent_path().filename().native();

    // load pkgfile
    if (! vfs.Mount("/assets", fs::WPPkgFs::CreatePkgFs(pkgPath))) {
        LOG_INFO("load pkg file %s failed, fallback to use dir", pkgPath.c_str());
        // load pkg dir
        if (! vfs.Mount("/assets", fs::CreatePhysicalFs(pkgDir))) {
            LOG_ERROR("can't load pkg directory: %s", pkgDir.c_str());
            return;
        }
    }
    if (! m_cache_path.empty()) {
        if (! vfs.Mount("/cache", fs::CreatePhysicalFs(m_cache_path, true), "cache")) {
            LOG_ERROR("can't load cache folder: %s", m_cache_path.c_str());
        } else {
            LOG_INFO("cache folder: %s", m_cache_path.c_str());
        }
    }

    {
        std::string       scene_src;
        const std::string base { "/assets/" };
        {
            std::string scenePath = base + pkgEntry;
            if (vfs.Contains(scenePath)) {
                auto f = vfs.Open(scenePath);
                if (f) scene_src = f->ReadAllStr();
            }
        }
        if (scene_src.empty()) {
            LOG_ERROR("Not supported scene type");
            return;
        }
        auto puppetAnimationLayerOverrides =
            wallpaper::debug::parsePuppetAnimationLayerOverrideList(
                m_debug_puppet_animation_layer_overrides);
        if (! puppetAnimationLayerOverrides) {
            LOG_ERROR("invalid debug puppet animation layer overrides: %s",
                      m_debug_puppet_animation_layer_overrides.c_str());
            puppetAnimationLayerOverrides = std::vector<wallpaper::debug::PuppetAnimationLayerOverride> {};
        }
        wallpaper::debug::EffectCaptureConfig debugEffectConfig {
            .outputDir = m_debug_effect_captures,
            .commandLine = m_debug_effect_capture_command,
            .captureLayerIds = wallpaper::debug::parseCaptureLayerIdList(m_debug_effect_capture_layers),
            .probeLayerIds = wallpaper::debug::parseProbeLayerIdList(m_debug_effect_probe_layers),
            .captureDelayMs = std::max<int32_t>(0, m_debug_effect_capture_delay_ms),
            .highRiskProbeLayerIds = wallpaper::debug::parseProbeLayerIdList(m_debug_effect_probe_high_risk_layers),
            .probeChannelMapSlots = wallpaper::debug::parseProbeChannelMapSlotList(m_debug_effect_probe_channelmap_slots),
            .puppetAnimationLayerOverrides = *puppetAnimationLayerOverrides,
            .probeMaxEffects = wallpaper::debug::parseProbeMaxEffects(m_debug_effect_probe_max_effects),
            .puppetFinalMeshOverride = m_debug_puppet_effect_final_mesh,
            .puppetEffectRouteOnly = m_debug_puppet_effect_route_only,
            .layerVisibilityOverrides = wallpaper::debug::parseLayerVisibilityOverrideList(m_debug_layer_visibility_overrides),
            .mediaStateTimelineJson = m_debug_media_state_timeline,
        };
        const auto debugMousePosition = ParseDebugMousePosition(m_debug_mouse_position);
        const auto debugMouseTimeline = ParseDebugMouseTimeline(m_debug_mouse_timeline);
        if (m_debug_mouse_position_active && !m_debug_mouse_timeline_active && debugMousePosition) {
            ApplyDebugMousePositionManifest(debugEffectConfig.mouseParallax, *debugMousePosition);
        }
        if (m_debug_mouse_timeline_active && debugMouseTimeline && !debugMouseTimeline->empty()) {
            ApplyDebugMouseTimelineManifest(debugEffectConfig.mouseParallax, *debugMouseTimeline);
        } else if (!m_debug_mouse_position_active && m_debug_interactive_mouse_active) {
            ApplyInteractiveMouseManifest(debugEffectConfig.mouseParallax);
        }
        m_scene_parser.SetDebugEffectCaptureConfig(std::move(debugEffectConfig));
        m_scene_parser.SetScenePropertiesJson(
            ResolveScenePropertiesJson(m_scene_properties_json, pkgDir));
        try {
            scene = m_scene_parser.Parse(scene_id, scene_src, vfs, *m_sound_manager);
        } catch (const nlohmann::json::exception& e) {
            LOG_ERROR("scene parse failed (JSON): %s", e.what());
            return;
        } catch (const std::exception& e) {
            LOG_ERROR("scene parse failed: %s", e.what());
            return;
        } catch (...) {
            LOG_ERROR("scene parse failed: unknown exception");
            return;
        }
        if (!scene) {
            LOG_ERROR("scene parse returned null for %s", scene_id.c_str());
            return;
        }
        if (!m_media_state_json.empty()) {
            ApplySceneMediaTimelineState(
                *scene,
                SceneScriptMediaStateFromJsonString(m_media_state_json));
        }
        LOG_INFO("main loadScene: parse finished for %s", scene_id.c_str());
        scene->vfs.swap(pVfs);
    }

    {
        postCurrentDebugMouseInputToRender();
        LOG_INFO("main loadScene: posting scene to render thread");
        auto msg = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_SET_SCENE);
        msg->setObject("scene", scene);
        msg->post();
    }

    // draw first frame
    {
        LOG_INFO("main loadScene: posting initial draw");
        auto msg = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_DRAW);
        msg->post();
    }
}
void MainHandler::sendCmdLoadScene() {
    LOG_INFO("main sendCmdLoadScene");
    auto msg = CreateMsgWithCmd(shared_from_this(), MainHandler::CMD::CMD_LOAD_SCENE);
    msg->post();
}
void MainHandler::sendFirstFrameOk() {
    LOG_INFO("main sendFirstFrameOk");
    auto msg = CreateMsgWithCmd(shared_from_this(), MainHandler::CMD::CMD_FIRST_FRAME);
    msg->post();
}

bool MainHandler::init() {
    if (m_inited) return true;
    m_main_loop->setName("main");
    m_render_loop->setName("render");

    m_main_loop->start();
    m_render_loop->start();

    m_main_loop->registerHandler(shared_from_this());
    m_render_loop->registerHandler(m_render_handler);

    {
        auto  msg        = CreateMsgWithCmd(m_render_handler, RenderHandler::CMD::CMD_DRAW);
        auto& frameTimer = m_render_handler->frame_timer;
        frameTimer.SetCallback([msg]() {
            msg->post();
        });
        frameTimer.SetRequiredFps(15);
        frameTimer.Run();
    }

    m_inited = true;
    return true;
}
MainHandler::MainHandler()
    : m_sound_manager(std::make_unique<audio::SoundManager>()),
      m_main_loop(std::make_shared<looper::Looper>()),
      m_render_loop(std::make_shared<looper::Looper>()),
      m_render_handler(std::make_shared<RenderHandler>(*this)) {}
