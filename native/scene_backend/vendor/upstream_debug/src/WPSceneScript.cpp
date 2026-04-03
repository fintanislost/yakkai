#include "WPSceneScript.hpp"
#include "Utils/Logging.h"

#include <nlohmann/json.hpp>
#include <sstream>

extern "C" {
#include "quickjs.h"
}

using namespace wallpaper;

struct SceneScriptContext::Impl {
    JSRuntime* rt { nullptr };
    JSContext*  ctx { nullptr };

    Impl() {
        rt = JS_NewRuntime();
        ctx = JS_NewContext(rt);
    }

    ~Impl() {
        if (ctx) JS_FreeContext(ctx);
        if (rt) JS_FreeRuntime(rt);
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

        JS_SetPropertyStr(ctx, global, "engine", engine);

        // createScriptProperties() stub — returns a builder with .addSlider/.addColor/.finish
        const char* stubCode = R"(
            function createScriptProperties() {
                var props = {};
                var builder = {
                    addSlider: function(opt) { props[opt.name] = opt.value; return builder; },
                    addColor: function(opt) { props[opt.name] = opt.value; return builder; },
                    addCheckbox: function(opt) { props[opt.name] = opt.value; return builder; },
                    addCombo: function(opt) { props[opt.name] = opt.value; return builder; },
                    addText: function(opt) { props[opt.name] = opt.value; return builder; },
                    finish: function() { return props; }
                };
                return builder;
            }

            function Vec3(x, y, z) {
                this.x = x !== undefined ? x : 0;
                this.y = y !== undefined ? y : 0;
                this.z = z !== undefined ? z : 0;
            }
        )";
        JSValue result = JS_Eval(ctx, stubCode, strlen(stubCode), "<stubs>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            JSValue exc = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, exc);
            LOG_ERROR("QuickJS stub init error: %s", msg ? msg : "unknown");
            if (msg) JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, result);

        JS_FreeValue(ctx, global);
    }
};

SceneScriptContext::SceneScriptContext() : m_impl(std::make_unique<Impl>()) {}
SceneScriptContext::~SceneScriptContext() = default;

void SceneScriptContext::setUserProperties(const nlohmann::json& properties) {
    m_impl->setupEngineObject(properties, 1920, 1080);
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
    float currentAlpha)
{
    SceneScriptResult result;
    auto* ctx = m_impl->ctx;

    // WE SceneScript uses ES module syntax (export var/function).
    // Convert to a plain script that we can evaluate:
    // 1. Strip 'export' keywords
    // 2. Wrap in an IIFE that calls update() with the current layer state
    std::string src(script);

    // Replace 'export var' -> 'var', 'export function' -> 'function'
    auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replaceAll(src, "'use strict';", "");
    replaceAll(src, "\"use strict\";", "");
    replaceAll(src, "\xc2\xa0", " "); // normalize NBSP
    replaceAll(src, "export var ", "var ");
    replaceAll(src, "export function ", "function ");
    replaceAll(src, "export let ", "var ");
    replaceAll(src, "export default ", "");
    replaceAll(src, "export ", "");

    // Append: call update() with a value object, then store results on __result
    std::ostringstream wrapper;
    wrapper << src << "\n";
    wrapper << "var __thisLayer = { color: new Vec3("
            << currentColor[0] << "," << currentColor[1] << "," << currentColor[2]
            << "), alpha: " << currentAlpha
            << ", visible: true"
            << ", origin: new Vec3("
            << currentOrigin[0] << "," << currentOrigin[1] << "," << currentOrigin[2]
            << ") };\n";
    wrapper << "if (typeof update === 'function') {\n"
            << "  var __val = new Vec3(" << currentOrigin[0] << "," << currentOrigin[1] << "," << currentOrigin[2] << ");\n"
            << "  var __ret = update(__val);\n"
            << "  if (__ret) { __thisLayer.origin = __ret; }\n"
            << "}\n"
            << "if (typeof scriptProperties !== 'undefined' && typeof scriptProperties === 'object') {\n"
            << "  if ('color' in scriptProperties) {\n"
            << "    var __c = scriptProperties.color;\n"
            << "    if (typeof __c === 'string') {\n"
            << "      var __parts = __c.split(' ');\n"
            << "      if (__parts.length >= 3) __thisLayer.color = new Vec3(parseFloat(__parts[0]), parseFloat(__parts[1]), parseFloat(__parts[2]));\n"
            << "    }\n"
            << "  }\n"
            << "  if ('alpha' in scriptProperties) __thisLayer.alpha = scriptProperties.alpha;\n"
            << "  if ('visible' in scriptProperties) __thisLayer.visible = scriptProperties.visible;\n"
            << "}\n";

    std::string fullSrc = wrapper.str();

    JSValue val = JS_Eval(ctx, fullSrc.c_str(), fullSrc.size(), "<scenescript>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exc);
        LOG_INFO("SceneScript eval (non-fatal): %s", msg ? msg : "unknown");
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
            result.visible = vis;
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
    }
    JS_FreeValue(ctx, thisLayer);
    JS_FreeValue(ctx, global);

    return result;
}
