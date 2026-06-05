#include "Policy/SceneScriptRuntimePolicy.hpp"

#include <cctype>

namespace wallpaper::policy {
namespace {

void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string asciiLower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool containsInsensitive(std::string_view value, std::string_view token)
{
    return asciiLower(value).find(asciiLower(token)) != std::string::npos;
}

std::string quotedIdentifier(std::string_view message)
{
    const auto start = message.find('\'');
    if (start == std::string_view::npos) {
        return {};
    }
    const auto end = message.find('\'', start + 1);
    if (end == std::string_view::npos || end <= start + 1) {
        return {};
    }
    return std::string(message.substr(start + 1, end - start - 1));
}

std::string inferredApi(std::string_view message)
{
    if (containsInsensitive(message, "is not defined")) {
        if (const std::string identifier = quotedIdentifier(message); !identifier.empty()) {
            return identifier;
        }
    }
    if (containsInsensitive(message, "cannot read property")) {
        if (const std::string property = quotedIdentifier(message); !property.empty()) {
            return "undefined." + property;
        }
        return "undefined-property";
    }
    return "unknown";
}

} // namespace

std::string sanitizeSceneScriptModule(std::string_view script)
{
    std::string src(script);

    replaceAll(src, "'use strict';", "");
    replaceAll(src, "\"use strict\";", "");
    replaceAll(src, "\xc2\xa0", " ");
    replaceAll(src, "export var ", "var ");
    replaceAll(src, "export function ", "function ");
    replaceAll(src, "export let ", "var ");
    replaceAll(src, "export default ", "");
    replaceAll(src, "export ", "");
    {
        size_t pos = 0;
        while ((pos = src.find("import ", pos)) != std::string::npos) {
            size_t end = src.find('\n', pos);
            if (end == std::string::npos) end = src.size();
            src.replace(pos, end - pos, "/* import stripped */");
            pos += 20;
        }
    }

    return src;
}

std::string sceneScriptRuntimeGapKindText(SceneScriptRuntimeGapKind kind)
{
    switch (kind) {
    case SceneScriptRuntimeGapKind::Visible:
        return "visible";
    case SceneScriptRuntimeGapKind::Harmless:
        return "harmless";
    case SceneScriptRuntimeGapKind::MediaRuntimeOnly:
        return "media-runtime-only";
    case SceneScriptRuntimeGapKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

SceneScriptRuntimeGap classifySceneScriptRuntimeGap(std::string_view message,
                                                    std::string_view stack)
{
    SceneScriptRuntimeGap gap;
    gap.api = inferredApi(message);

    const std::string loweredApi = asciiLower(gap.api);
    if (loweredApi == "console" ||
        loweredApi == "print" ||
        loweredApi == "alert") {
        gap.kind = SceneScriptRuntimeGapKind::Harmless;
        gap.reason = "diagnostic-only";
        return gap;
    }

    if (containsInsensitive(gap.api, "MediaPlaybackEvent") ||
        containsInsensitive(gap.api, "media") ||
        containsInsensitive(gap.api, "audio")) {
        gap.kind = SceneScriptRuntimeGapKind::MediaRuntimeOnly;
        gap.reason = "media-runtime-only";
        return gap;
    }

    if (loweredApi == "scene" ||
        loweredApi == "thisscene" ||
        loweredApi == "thislayer" ||
        loweredApi == "parent" ||
        loweredApi == "camera") {
        gap.kind = SceneScriptRuntimeGapKind::Visible;
        gap.reason = "missing-visible-runtime-api";
        return gap;
    }

    if (loweredApi.rfind("undefined.", 0) == 0 ||
        loweredApi == "undefined-property") {
        gap.kind = SceneScriptRuntimeGapKind::Visible;
        gap.reason = "missing-layer-object-property";
        return gap;
    }

    if (containsInsensitive(stack, "thisLayer") ||
        containsInsensitive(stack, "thisScene")) {
        gap.kind = SceneScriptRuntimeGapKind::Visible;
        gap.reason = "layer-runtime-stack";
        return gap;
    }

    return gap;
}

std::string sceneScriptRuntimeStubSource()
{
    return R"(
            var __scriptPropertyOverrides = __scriptPropertyOverrides || {};
            function __sceneScriptPropertyValue(opt) {
                if (!opt || !opt.name) return undefined;
                if (opt.name in __scriptPropertyOverrides) {
                    return __scriptPropertyOverrides[opt.name];
                }
                if ('value' in opt) {
                    return opt.value;
                }
                if (opt.options && opt.options.length > 0 && 'value' in opt.options[0]) {
                    return opt.options[0].value;
                }
                return undefined;
            }
            function createScriptProperties() {
                var props = {};
                var builder = {
                    addSlider: function(opt) {
                        props[opt.name] = __sceneScriptPropertyValue(opt);
                        return builder;
                    },
                    addColor: function(opt) { props[opt.name] = __sceneScriptPropertyValue(opt); return builder; },
                    addCheckbox: function(opt) { props[opt.name] = __sceneScriptPropertyValue(opt); return builder; },
                    addCombo: function(opt) { props[opt.name] = __sceneScriptPropertyValue(opt); return builder; },
                    addText: function(opt) { props[opt.name] = __sceneScriptPropertyValue(opt); return builder; },
                    finish: function() { return props; }
                };
                return builder;
            }

            function Vec3(x, y, z) {
                if (y === undefined && z === undefined) {
                    y = x;
                    z = x;
                }
                this.x = x !== undefined ? x : 0;
                this.y = y !== undefined ? y : 0;
                this.z = z !== undefined ? z : 0;
            }

            function __sceneScriptVec3(value) {
                if (value && typeof value === 'object' &&
                    'x' in value && 'y' in value && 'z' in value) {
                    return value;
                }
                return new Vec3(value);
            }

            Vec3.prototype.add = function(other) {
                other = __sceneScriptVec3(other);
                return new Vec3(this.x + other.x, this.y + other.y, this.z + other.z);
            };
            Vec3.prototype.subtract = function(other) {
                other = __sceneScriptVec3(other);
                return new Vec3(this.x - other.x, this.y - other.y, this.z - other.z);
            };
            Vec3.prototype.multiply = function(other) {
                other = __sceneScriptVec3(other);
                return new Vec3(this.x * other.x, this.y * other.y, this.z * other.z);
            };
            Vec3.prototype.mix = function(other, amount) {
                other = __sceneScriptVec3(other);
                amount = __sceneScriptVec3(amount);
                return new Vec3(
                    this.x + (other.x - this.x) * amount.x,
                    this.y + (other.y - this.y) * amount.y,
                    this.z + (other.z - this.z) * amount.z
                );
            };

            function __sceneScriptTextureAnimation() {
                return {
                    getFrame: function() { return 0; },
                    frameCount: 1,
                    duration: 1,
                    fps: 30,
                    play: function() {},
                    stop: function() {},
                    pause: function() {}
                };
            }

            function __sceneScriptVideoTexture() {
                return {
                    rate: 1,
                    play: function() {},
                    pause: function() {},
                    stop: function() {}
                };
            }

            function __sceneScriptTransformMatrix(origin) {
                origin = __sceneScriptVec3(origin);
                return {
                    m: [
                        1, 0, 0, 0,
                        0, 1, 0, 0,
                        0, 0, 1, 0,
                        origin.x, origin.y, origin.z, 1
                    ]
                };
            }

            function __makeSceneScriptLayer(name, origin, scale, color, alpha) {
                var layer = {
                    name: name || '',
                    visible: true,
                    origin: __sceneScriptVec3(origin),
                    scale: scale !== undefined ? __sceneScriptVec3(scale) : new Vec3(1),
                    color: color !== undefined ? __sceneScriptVec3(color) : new Vec3(1),
                    alpha: alpha !== undefined ? alpha : 1,
                    parent: null,
                    getParent: function() { return this.parent || __sceneScriptRootLayer; },
                    getTransformMatrix: function() { return __sceneScriptTransformMatrix(this.origin); },
                    getTextureAnimation: __sceneScriptTextureAnimation,
                    getVideoTexture: __sceneScriptVideoTexture
                };
                return layer;
            }

            var __sceneScriptRootLayer = __makeSceneScriptLayer(
                '__root',
                new Vec3(0, 0, 0),
                new Vec3(1, 1, 1),
                new Vec3(1, 1, 1),
                1
            );
            __sceneScriptRootLayer.parent = __sceneScriptRootLayer;

            var __sceneScriptLayerMap = {};
            function __sceneScriptGetLayer(name) {
                var key = String(name || '');
                if (!__sceneScriptLayerMap[key]) {
                    __sceneScriptLayerMap[key] = __makeSceneScriptLayer(
                        key,
                        new Vec3(0, 0, 0),
                        new Vec3(1, 1, 1),
                        new Vec3(1, 1, 1),
                        1
                    );
                }
                return __sceneScriptLayerMap[key];
            }

            function __makeSceneScriptScene() {
                return {
                    clearColor: new Vec3(0, 0, 0),
                    timeVarying: false,
                    getLayer: __sceneScriptGetLayer,
                    on: function() {}
                };
            }

            var scene = __makeSceneScriptScene();
            var thisScene = scene;

            var localStorage = {
                __values: {},
                get: function(key) { return this.__values[key]; },
                set: function(key, value) { this.__values[key] = value; },
                remove: function(key) { delete this.__values[key]; }
            };

            // WEMath library stubs
            var WEMath = {
                smoothStep: function(edge0, edge1, x) {
                    var t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
                    return t * t * (3 - 2 * t);
                },
                mix: function(a, b, t) { return a + (b - a) * t; },
                lerp: function(a, b, t) { return a + (b - a) * t; },
                clamp: function(x, lo, hi) { return Math.max(lo, Math.min(hi, x)); }
            };

            // engine API stubs
            if (typeof engine !== 'undefined') {
                engine.setTimeout = function(fn, ms) { return 0; };
                engine.runtime = 0;
                engine.frametime = 0.016;
                engine.registerAnimation = function() {};
                engine.AUDIO_RESOLUTION_16 = 16;
                engine.AUDIO_RESOLUTION_32 = 32;
                engine.AUDIO_RESOLUTION_64 = 64;
                engine.registerAudioBuffers = function(resolution) {
                    var zeros = [];
                    for (var i = 0; i < (resolution || 16); i++) zeros.push(0);
                    return { average: zeros, left: zeros, right: zeros };
                };
            }

            // input stub
            var input = {
                cursorPosition: new Vec3(0, 0, 0),
                cursorWorldPosition: new Vec3(0, 0, 0)
            };

            // shared object stub — inter-layer communication
            var shared = {};
        )";
}

} // namespace wallpaper::policy
