#include "WPSceneScript.hpp"
#include "Policy/SceneScriptRuntimePolicy.hpp"
#include "Utils/Logging.h"

#include <nlohmann/json.hpp>
#include <cmath>
#include <sstream>
#include <vector>

extern "C" {
#include "quickjs.h"
}

using namespace wallpaper;

struct SceneScriptContext::Impl {
    JSRuntime* rt { nullptr };
    JSContext*  ctx { nullptr };
    SceneScriptMediaState mediaState;
    std::vector<SceneScriptLayerSnapshot> layerSnapshots;

    Impl() {
        rt = JS_NewRuntime();
        ctx = JS_NewContext(rt);
    }

    ~Impl() {
        if (ctx) JS_FreeContext(ctx);
        if (rt) JS_FreeRuntime(rt);
    }

    void applyMediaState() {
        if (!ctx) {
            return;
        }

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue media = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, media, "available", JS_NewBool(ctx, mediaState.available));
        JS_SetPropertyStr(ctx, media, "playing", JS_NewBool(ctx, mediaState.playing));
        JS_SetPropertyStr(ctx, media, "title", JS_NewString(ctx, mediaState.title.c_str()));
        JS_SetPropertyStr(ctx, media, "artist", JS_NewString(ctx, mediaState.artist.c_str()));
        JS_SetPropertyStr(ctx, media, "album", JS_NewString(ctx, mediaState.album.c_str()));
        JS_SetPropertyStr(ctx, media, "albumArtPath", JS_NewString(ctx, mediaState.albumArtPath.c_str()));
        JS_SetPropertyStr(ctx, media, "duration", JS_NewFloat64(ctx, mediaState.duration));
        JS_SetPropertyStr(ctx, media, "position", JS_NewFloat64(ctx, mediaState.position));
        JS_SetPropertyStr(ctx, media, "settleSeconds", JS_NewFloat64(ctx, mediaState.settleSeconds));
        JS_SetPropertyStr(ctx, media, "hasFixedClock", JS_NewBool(ctx, mediaState.hasFixedClock));
        JS_SetPropertyStr(ctx, media, "fixedClockEpochMs", JS_NewFloat64(ctx, mediaState.fixedClockEpochMs));
        JS_SetPropertyStr(ctx, media, "hasThumbnailColors", JS_NewBool(ctx, mediaState.hasThumbnailColors));
        const auto makeColorObject = [this](const std::array<float, 3>& color) {
            JSValue value = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, value, "x", JS_NewFloat64(ctx, color[0]));
            JS_SetPropertyStr(ctx, value, "y", JS_NewFloat64(ctx, color[1]));
            JS_SetPropertyStr(ctx, value, "z", JS_NewFloat64(ctx, color[2]));
            return value;
        };
        JS_SetPropertyStr(ctx, media, "textColor", makeColorObject(mediaState.textColor));
        JS_SetPropertyStr(ctx, media, "primaryColor", makeColorObject(mediaState.primaryColor));
        JS_SetPropertyStr(ctx, media, "secondaryColor", makeColorObject(mediaState.secondaryColor));
        JS_SetPropertyStr(ctx, media, "tertiaryColor", makeColorObject(mediaState.tertiaryColor));
        JS_SetPropertyStr(ctx, global, "__yakkaiMedia", media);
        JS_SetPropertyStr(ctx, global, "__sceneScriptFixedDateMs",
                          mediaState.hasFixedClock
                              ? JS_NewFloat64(ctx, mediaState.fixedClockEpochMs)
                              : JS_UNDEFINED);
        JS_FreeValue(ctx, global);
    }

    void applyLayerSnapshot(const SceneScriptLayerSnapshot& layer) {
        if (!ctx) {
            return;
        }

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue seedFn = JS_GetPropertyStr(ctx, global, "__sceneScriptSeedLayer");
        if (!JS_IsFunction(ctx, seedFn)) {
            JS_FreeValue(ctx, seedFn);
            JS_FreeValue(ctx, global);
            return;
        }

        JSValue payload = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, payload, "id", JS_NewInt32(ctx, layer.id));
        JS_SetPropertyStr(ctx, payload, "parentId", JS_NewInt32(ctx, layer.parentId));
        JS_SetPropertyStr(ctx, payload, "name", JS_NewString(ctx, layer.name.c_str()));
        JS_SetPropertyStr(ctx, payload, "originX", JS_NewFloat64(ctx, layer.origin[0]));
        JS_SetPropertyStr(ctx, payload, "originY", JS_NewFloat64(ctx, layer.origin[1]));
        JS_SetPropertyStr(ctx, payload, "originZ", JS_NewFloat64(ctx, layer.origin[2]));
        JS_SetPropertyStr(ctx, payload, "scaleX", JS_NewFloat64(ctx, layer.scale[0]));
        JS_SetPropertyStr(ctx, payload, "scaleY", JS_NewFloat64(ctx, layer.scale[1]));
        JS_SetPropertyStr(ctx, payload, "scaleZ", JS_NewFloat64(ctx, layer.scale[2]));
        JS_SetPropertyStr(ctx, payload, "sizeX", JS_NewFloat64(ctx, layer.size[0]));
        JS_SetPropertyStr(ctx, payload, "sizeY", JS_NewFloat64(ctx, layer.size[1]));
        JS_SetPropertyStr(ctx, payload, "visible", JS_NewBool(ctx, layer.visible));

        JSValue args[] = {payload};
        JSValue result = JS_Call(ctx, seedFn, global, 1, args);
        if (JS_IsException(result)) {
            JSValue exc = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, exc);
            LOG_INFO("QuickJS layer seed error (non-fatal): %s", msg ? msg : "unknown");
            if (msg) JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, payload);
        JS_FreeValue(ctx, seedFn);
        JS_FreeValue(ctx, global);
    }

    void setupEngineObject(const nlohmann::json& properties, int canvasW, int canvasH) {
        JSValue global = JS_GetGlobalObject(ctx);

        // engine object
        JSValue engine = JS_NewObject(ctx);

        // engine.userProperties
        JSValue userProps = JS_NewObject(ctx);
        if (properties.is_object()) {
            for (const auto& [key, prop] : properties.items()) {
                if (! prop.is_object() || ! prop.contains("value")) continue;
                const auto& val = prop.at("value");
                if (val.is_number()) {
                    JS_SetPropertyStr(ctx, userProps, key.c_str(),
                                      JS_NewFloat64(ctx, val.get<double>()));
                } else if (val.is_boolean()) {
                    JS_SetPropertyStr(ctx, userProps, key.c_str(),
                                      JS_NewBool(ctx, val.get<bool>()));
                } else if (val.is_string()) {
                    const auto& sv = val.get_ref<const std::string&>();
                    // Try parsing as space-separated numbers (WE color format)
                    JS_SetPropertyStr(ctx, userProps, key.c_str(),
                                      JS_NewString(ctx, sv.c_str()));
                }
            }
        }
        JS_SetPropertyStr(ctx, engine, "userProperties", userProps);

        // engine.canvasSize
        JSValue canvasSize = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, canvasSize, "x", JS_NewFloat64(ctx, canvasW));
        JS_SetPropertyStr(ctx, canvasSize, "y", JS_NewFloat64(ctx, canvasH));
        JS_SetPropertyStr(ctx, engine, "canvasSize", canvasSize);

        // engine.timeOfDay — default to current time, can be overridden later
        JS_SetPropertyStr(ctx, engine, "timeOfDay", JS_NewFloat64(ctx, 0.5));

        JS_SetPropertyStr(ctx, global, "engine", engine);

        const std::string stubCode = wallpaper::policy::sceneScriptRuntimeStubSource();
        JSValue result = JS_Eval(ctx, stubCode.c_str(), stubCode.size(), "<stubs>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            JSValue exc = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, exc);
            LOG_ERROR("QuickJS stub init error: %s", msg ? msg : "unknown");
            if (msg) JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, result);

        for (const auto& layer : layerSnapshots) {
            applyLayerSnapshot(layer);
        }

        applyMediaState();

        JS_FreeValue(ctx, global);
    }
};

SceneScriptContext::SceneScriptContext() : m_impl(std::make_unique<Impl>()) {}
SceneScriptContext::~SceneScriptContext() = default;

void SceneScriptContext::setUserProperties(const nlohmann::json& properties) {
    m_impl->setupEngineObject(properties, 1920, 1080);
}

void SceneScriptContext::setTimeOfDay(double fraction) {
    JSValue global = JS_GetGlobalObject(m_impl->ctx);
    JSValue engine = JS_GetPropertyStr(m_impl->ctx, global, "engine");
    if (! JS_IsUndefined(engine)) {
        JS_SetPropertyStr(m_impl->ctx, engine, "timeOfDay", JS_NewFloat64(m_impl->ctx, fraction));
    }
    JS_FreeValue(m_impl->ctx, engine);
    JS_FreeValue(m_impl->ctx, global);
}

void SceneScriptContext::setMediaState(const SceneScriptMediaState& state) {
    m_impl->mediaState = state;
    m_impl->applyMediaState();
}

void SceneScriptContext::setScriptProperty(const std::string& name, double value) {
    // Store override in a global __scriptPropertyOverrides object.
    // The createScriptProperties stub reads from this to override slider defaults.
    JSValue global = JS_GetGlobalObject(m_impl->ctx);
    JSValue overrides = JS_GetPropertyStr(m_impl->ctx, global, "__scriptPropertyOverrides");
    if (JS_IsUndefined(overrides)) {
        overrides = JS_NewObject(m_impl->ctx);
        JS_SetPropertyStr(m_impl->ctx, global, "__scriptPropertyOverrides", JS_DupValue(m_impl->ctx, overrides));
    }
    JS_SetPropertyStr(m_impl->ctx, overrides, name.c_str(), JS_NewFloat64(m_impl->ctx, value));
    JS_FreeValue(m_impl->ctx, overrides);
    JS_FreeValue(m_impl->ctx, global);
}

void SceneScriptContext::registerLayerSnapshot(const SceneScriptLayerSnapshot& layer)
{
    m_impl->layerSnapshots.push_back(layer);
    m_impl->applyLayerSnapshot(layer);
}

void SceneScriptContext::setCanvasSize(int width, int height) {
    JSValue global = JS_GetGlobalObject(m_impl->ctx);
    JSValue engine = JS_GetPropertyStr(m_impl->ctx, global, "engine");
    if (! JS_IsUndefined(engine)) {
        JSValue canvasSize = JS_NewObject(m_impl->ctx);
        JS_SetPropertyStr(m_impl->ctx, canvasSize, "x", JS_NewFloat64(m_impl->ctx, width));
        JS_SetPropertyStr(m_impl->ctx, canvasSize, "y", JS_NewFloat64(m_impl->ctx, height));
        JS_SetPropertyStr(m_impl->ctx, engine, "canvasSize", canvasSize);
    }
    JS_FreeValue(m_impl->ctx, engine);
    JS_FreeValue(m_impl->ctx, global);
}

SceneScriptResult SceneScriptContext::evaluateLayerScript(
    std::string_view script,
    const std::array<float, 3>& currentOrigin,
    const std::array<float, 3>& currentColor,
    float currentAlpha,
    int32_t layerId,
    bool currentVisible)
{
    SceneScriptResult result;
    auto* ctx = m_impl->ctx;

    std::string src = wallpaper::policy::sanitizeSceneScriptModule(script);

    // Wrap in IIFE to isolate variable declarations between scripts.
    // Provide thisLayer/thisScene stubs that WE scripts expect.
    std::ostringstream wrapper;
    wrapper << "(function() {\n";
    wrapper << "var __val = new Vec3("
            << currentOrigin[0] << "," << currentOrigin[1] << "," << currentOrigin[2]
            << ");\n";
    wrapper << "var thisLayer = __sceneScriptLayerForEvaluation(" << layerId << ", 'layer_" << layerId
            << "', __val, new Vec3("
            << currentColor[0] << "," << currentColor[1] << "," << currentColor[2]
            << "), " << currentAlpha << ", " << (currentVisible ? "true" : "false")
            << ");\n";
    wrapper << "var thisScene = __makeSceneScriptScene();\n";
    wrapper << "var MediaPlaybackEvent = globalThis.MediaPlaybackEvent || {"
            << "PLAYBACK_STOPPED: 0,"
            << "PLAYBACK_PLAYING: 1,"
            << "PLAYBACK_PAUSED: 2"
            << "};\n";
    wrapper << src << "\n";
    wrapper << "if (typeof init === 'function') { try {"
            << "  var __initRet = init(__sceneScriptCloneVec3(__val));"
            << "  if (__initRet !== undefined) {"
            << "    __val = typeof __initRet === 'object' ? __sceneScriptCloneVec3(__initRet) : __initRet;"
            << "    if (typeof __initRet === 'object') thisLayer.origin = __sceneScriptCloneVec3(__initRet);"
            << "  }"
            << "} catch(e) {} }\n";
    wrapper << "if (typeof __sceneScriptMediaState === 'function') { try {"
            << "  var __media = __sceneScriptMediaState();"
            << "  if (__media && __media.available) {"
            << "    if (typeof mediaPropertiesChanged === 'function') {"
            << "      mediaPropertiesChanged({"
            << "        title: __media.title,"
            << "        artist: __media.artist,"
            << "        album: __media.album,"
            << "        albumArtPath: __media.albumArtPath,"
            << "        duration: __media.duration,"
            << "        position: __media.position,"
            << "        playing: !!__media.playing"
            << "      });"
            << "    }"
            << "    if (typeof mediaTimelineChanged === 'function') {"
            << "      mediaTimelineChanged({ duration: __media.duration, position: __media.position });"
            << "    }"
            << "    if (__media.hasThumbnailColors && typeof mediaThumbnailChanged === 'function') {"
            << "      mediaThumbnailChanged({"
            << "        textColor: new Vec3(__media.textColor.x, __media.textColor.y, __media.textColor.z),"
            << "        primaryColor: new Vec3(__media.primaryColor.x, __media.primaryColor.y, __media.primaryColor.z),"
            << "        secondaryColor: new Vec3(__media.secondaryColor.x, __media.secondaryColor.y, __media.secondaryColor.z),"
            << "        tertiaryColor: new Vec3(__media.tertiaryColor.x, __media.tertiaryColor.y, __media.tertiaryColor.z),"
            << "        albumArtPath: __media.albumArtPath"
            << "      });"
            << "    }"
            << "  }"
            << "  if (__media && typeof mediaPlaybackChanged === 'function') {"
            << "    mediaPlaybackChanged({"
            << "      state: __media.playing ? MediaPlaybackEvent.PLAYBACK_PLAYING : (__media.available ? MediaPlaybackEvent.PLAYBACK_PAUSED : MediaPlaybackEvent.PLAYBACK_STOPPED),"
            << "      playing: !!__media.playing,"
            << "      title: __media.title,"
            << "      artist: __media.artist,"
            << "      album: __media.album"
            << "    });"
            << "  }"
            << "} catch(e) {} }\n";
    wrapper << "globalThis.__sceneScriptReturn = undefined;\n";
    wrapper << "if (typeof update === 'function') {\n"
            << "  var __settleFrames = 0;\n"
            << "  if (typeof __sceneScriptMediaState === 'function') { try {\n"
            << "    var __settleMedia = __sceneScriptMediaState();\n"
            << "    if (__settleMedia && __settleMedia.settleSeconds > 0) {\n"
            << "      __settleFrames = Math.ceil(__settleMedia.settleSeconds / Math.max(0.001, engine.frametime || 0.016));\n"
            << "    }\n"
            << "  } catch(e) {} }\n"
            << "  for (var __settleIndex = 0; __settleIndex < __settleFrames; __settleIndex++) {\n"
            << "    if (engine && typeof engine.__sceneScriptAdvanceTimers === 'function') engine.__sceneScriptAdvanceTimers((engine.frametime || 0.016) * 1000.0);\n"
            << "    var __settleRet = update(__sceneScriptCloneVec3(__val));\n"
            << "    if (__settleRet && typeof __settleRet === 'object') {\n"
            << "      __val = __sceneScriptCloneVec3(__settleRet);\n"
            << "      thisLayer.origin = __sceneScriptCloneVec3(__settleRet);\n"
            << "    }\n"
            << "  }\n"
            << "  if (engine && typeof engine.__sceneScriptAdvanceTimers === 'function') engine.__sceneScriptAdvanceTimers((engine.frametime || 0.016) * 1000.0);\n"
            << "  var __ret = update(__sceneScriptCloneVec3(__val));\n"
            << "  globalThis.__sceneScriptReturn = __ret;\n"
            << "  if (__ret && typeof __ret === 'object') { thisLayer.origin = __sceneScriptCloneVec3(__ret); }\n"
            << "}\n"
            << "if (typeof scriptProperties !== 'undefined' && typeof scriptProperties === 'object') {\n"
            << "  if ('color' in scriptProperties) {\n"
            << "    var __c = scriptProperties.color;\n"
            << "    if (typeof __c === 'string') {\n"
            << "      var __parts = __c.split(' ');\n"
            << "      if (__parts.length >= 3) thisLayer.color = new Vec3(parseFloat(__parts[0]), parseFloat(__parts[1]), parseFloat(__parts[2]));\n"
            << "    }\n"
            << "  }\n"
            << "  if ('alpha' in scriptProperties) thisLayer.alpha = scriptProperties.alpha;\n"
            << "  if ('visible' in scriptProperties) thisLayer.visible = scriptProperties.visible;\n"
            << "}\n"
            << "globalThis.__thisLayer = thisLayer;\n"
            << "})();\n";

    std::string fullSrc = wrapper.str();

    JSValue val = JS_Eval(ctx, fullSrc.c_str(), fullSrc.size(), "<scenescript>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exc);
        // Get stack trace for more context
        JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
        const char* stackStr = JS_ToCString(ctx, stack);
        const std::string message = msg ? msg : "unknown";
        const std::string stackMessage = stackStr ? stackStr : "n/a";
        const auto gap = wallpaper::policy::classifySceneScriptRuntimeGap(message, stackMessage);
        const std::string gapKind = wallpaper::policy::sceneScriptRuntimeGapKindText(gap.kind);
        LOG_INFO("SceneScript eval (non-fatal): %s | stack: %s",
                 message.c_str(), stackMessage.c_str());
        LOG_INFO("SceneScript gap: layer=%d class=%s api=%s reason=%s message=%s",
                 layerId,
                 gapKind.c_str(),
                 gap.api.c_str(),
                 gap.reason.c_str(),
                 message.c_str());
        if (stackStr) JS_FreeCString(ctx, stackStr);
        JS_FreeValue(ctx, stack);
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, val);
        return result;
    }
    JS_FreeValue(ctx, val);

    // Read __thisLayer results
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue thisLayer = JS_GetPropertyStr(ctx, global, "__thisLayer");
    if (! JS_IsUndefined(thisLayer)) {
        // color
        JSValue jcolor = JS_GetPropertyStr(ctx, thisLayer, "color");
        if (! JS_IsUndefined(jcolor) && JS_IsObject(jcolor)) {
            double r, g, b;
            JSValue jx = JS_GetPropertyStr(ctx, jcolor, "x");
            JSValue jy = JS_GetPropertyStr(ctx, jcolor, "y");
            JSValue jz = JS_GetPropertyStr(ctx, jcolor, "z");
            if (JS_ToFloat64(ctx, &r, jx) == 0 &&
                JS_ToFloat64(ctx, &g, jy) == 0 &&
                JS_ToFloat64(ctx, &b, jz) == 0) {
                if (r != currentColor[0] || g != currentColor[1] || b != currentColor[2]) {
                    result.color = { (float)r, (float)g, (float)b };
                }
            }
            JS_FreeValue(ctx, jx);
            JS_FreeValue(ctx, jy);
            JS_FreeValue(ctx, jz);
        }
        JS_FreeValue(ctx, jcolor);

        // alpha
        JSValue jalpha = JS_GetPropertyStr(ctx, thisLayer, "alpha");
        double alpha;
        if (JS_ToFloat64(ctx, &alpha, jalpha) == 0 && (float)alpha != currentAlpha) {
            result.alpha = (float)alpha;
        }
        JS_FreeValue(ctx, jalpha);

        // visible
        JSValue jvis = JS_GetPropertyStr(ctx, thisLayer, "visible");
        if (JS_IsBool(jvis)) {
            bool vis = JS_ToBool(ctx, jvis);
            if (vis != currentVisible) {
                result.visible = vis;
            }
        }
        JS_FreeValue(ctx, jvis);

        // origin
        JSValue jorigin = JS_GetPropertyStr(ctx, thisLayer, "origin");
        if (! JS_IsUndefined(jorigin) && JS_IsObject(jorigin)) {
            double x, y, z;
            JSValue jx = JS_GetPropertyStr(ctx, jorigin, "x");
            JSValue jy = JS_GetPropertyStr(ctx, jorigin, "y");
            JSValue jz = JS_GetPropertyStr(ctx, jorigin, "z");
            if (JS_ToFloat64(ctx, &x, jx) == 0 &&
                JS_ToFloat64(ctx, &y, jy) == 0 &&
                JS_ToFloat64(ctx, &z, jz) == 0) {
                if (x != currentOrigin[0] || y != currentOrigin[1] || z != currentOrigin[2]) {
                    result.origin = { (float)x, (float)y, (float)z };
                }
            }
            JS_FreeValue(ctx, jx);
            JS_FreeValue(ctx, jy);
            JS_FreeValue(ctx, jz);
        }
        JS_FreeValue(ctx, jorigin);

        const auto readStringProperty = [&](const char* name) -> std::optional<std::string> {
            JSValue value = JS_GetPropertyStr(ctx, thisLayer, name);
            if (!JS_IsString(value)) {
                JS_FreeValue(ctx, value);
                return std::nullopt;
            }
            size_t len = 0;
            const char* raw = JS_ToCStringLen(ctx, &len, value);
            std::optional<std::string> out;
            if (raw) {
                out = std::string(raw, len);
                JS_FreeCString(ctx, raw);
            }
            JS_FreeValue(ctx, value);
            return out;
        };
        result.horizontalAlign = readStringProperty("horizontalalign");
        result.verticalAlign = readStringProperty("verticalalign");
    }
    JS_FreeValue(ctx, thisLayer);

    JSValue jreturn = JS_GetPropertyStr(ctx, global, "__sceneScriptReturn");
    if (JS_IsString(jreturn)) {
        size_t textLen = 0;
        const char* text = JS_ToCStringLen(ctx, &textLen, jreturn);
        if (text) {
            result.text = std::string(text, textLen);
            JS_FreeCString(ctx, text);
        }
    } else if (JS_IsNumber(jreturn)) {
        double scalar = 0.0;
        if (JS_ToFloat64(ctx, &scalar, jreturn) == 0 && std::isfinite(scalar)) {
            result.scalar = static_cast<float>(scalar);
        }
    } else if (JS_IsObject(jreturn)) {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        JSValue jx = JS_GetPropertyStr(ctx, jreturn, "x");
        JSValue jy = JS_GetPropertyStr(ctx, jreturn, "y");
        JSValue jz = JS_GetPropertyStr(ctx, jreturn, "z");
        if (JS_ToFloat64(ctx, &x, jx) == 0 &&
            JS_ToFloat64(ctx, &y, jy) == 0 &&
            JS_ToFloat64(ctx, &z, jz) == 0 &&
            std::isfinite(x) &&
            std::isfinite(y) &&
            std::isfinite(z)) {
            result.returnVector = {
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(z),
            };
        }
        JS_FreeValue(ctx, jx);
        JS_FreeValue(ctx, jy);
        JS_FreeValue(ctx, jz);
    }
    JS_FreeValue(ctx, jreturn);
    JS_FreeValue(ctx, global);

    return result;
}
