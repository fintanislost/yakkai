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

            function __sceneScriptCloneVec3(value) {
                value = __sceneScriptVec3(value);
                return new Vec3(value.x, value.y, value.z);
            }

            function __sceneScriptSafeMixAmount(value) {
                value = Number(value);
                if (isFinite(value)) return value;
                return value > 0 ? 1 : 0;
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
                    this.x + (other.x - this.x) * __sceneScriptSafeMixAmount(amount.x),
                    this.y + (other.y - this.y) * __sceneScriptSafeMixAmount(amount.y),
                    this.z + (other.z - this.z) * __sceneScriptSafeMixAmount(amount.z)
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

            var thisObject = globalThis.thisObject || {
                __animationPlayed: false,
                getAnimation: function() {
                    var owner = this;
                    return {
                        play: function() { owner.__animationPlayed = true; },
                        stop: function() { owner.__animationPlayed = false; },
                        pause: function() {},
                        reset: function() { owner.__animationPlayed = false; }
                    };
                }
            };
            globalThis.thisObject = thisObject;

            function __sceneScriptWorldScale(layer) {
                if (!layer || layer === __sceneScriptRootLayer || layer.parent === layer) {
                    return new Vec3(1, 1, 1);
                }
                var parent = layer.getParent ? layer.getParent() : __sceneScriptRootLayer;
                if (!parent || parent === layer) {
                    return __sceneScriptCloneVec3(layer.scale);
                }
                var parentScale = __sceneScriptWorldScale(parent);
                return new Vec3(
                    parentScale.x * layer.scale.x,
                    parentScale.y * layer.scale.y,
                    parentScale.z * layer.scale.z
                );
            }

            function __sceneScriptWorldOrigin(layer) {
                if (!layer || layer === __sceneScriptRootLayer || layer.parent === layer) {
                    return new Vec3(0, 0, 0);
                }
                var parent = layer.getParent ? layer.getParent() : __sceneScriptRootLayer;
                if (!parent || parent === layer) {
                    return __sceneScriptCloneVec3(layer.origin);
                }
                var parentOrigin = __sceneScriptWorldOrigin(parent);
                var parentScale = __sceneScriptWorldScale(parent);
                return new Vec3(
                    parentOrigin.x + layer.origin.x * parentScale.x,
                    parentOrigin.y + layer.origin.y * parentScale.y,
                    parentOrigin.z + layer.origin.z * parentScale.z
                );
            }

            function __sceneScriptTransformMatrixForLayer(layer) {
                var origin = __sceneScriptWorldOrigin(layer);
                var scale = __sceneScriptWorldScale(layer);
                return {
                    m: [
                        scale.x, 0, 0, 0,
                        0, scale.y, 0, 0,
                        0, 0, scale.z, 0,
                        origin.x, origin.y, origin.z, 1
                    ]
                };
            }

            function __makeSceneScriptLayer(name, origin, scale, color, alpha, visible, size, id, parentId) {
                var layer = {
                    id: id !== undefined ? id : -1,
                    parentId: parentId !== undefined ? parentId : 0,
                    name: name || '',
                    visible: visible !== undefined ? !!visible : true,
                    origin: __sceneScriptVec3(origin),
                    scale: scale !== undefined ? __sceneScriptVec3(scale) : new Vec3(1),
                    size: size !== undefined ? __sceneScriptVec3(size) : new Vec3(0, 0, 0),
                    color: color !== undefined ? __sceneScriptVec3(color) : new Vec3(1),
                    alpha: alpha !== undefined ? alpha : 1,
                    parent: null,
                    getParent: function() {
                        if (this.parent) return this.parent;
                        if (this.parentId && __sceneScriptLayerIdMap[this.parentId]) {
                            return __sceneScriptLayerIdMap[this.parentId];
                        }
                        return __sceneScriptRootLayer;
                    },
                    getTransformMatrix: function() { return __sceneScriptTransformMatrixForLayer(this); },
                    getTextureAnimation: __sceneScriptTextureAnimation,
                    getVideoTexture: __sceneScriptVideoTexture,
                    play: function() { return this; },
                    pause: function() { return this; }
                };
                return layer;
            }

            var __sceneScriptRootLayer = __makeSceneScriptLayer(
                '__root',
                new Vec3(0, 0, 0),
                new Vec3(1, 1, 1),
                new Vec3(1, 1, 1),
                1,
                new Vec3(0, 0, 0),
                0,
                0
            );
            __sceneScriptRootLayer.parent = __sceneScriptRootLayer;

            var __sceneScriptLayerMap = {};
            var __sceneScriptLayerIdMap = {};
            function __sceneScriptStoreLayer(layer) {
                if (layer.id !== undefined && layer.id >= 0) {
                    __sceneScriptLayerIdMap[layer.id] = layer;
                }
                if (layer.name) {
                    __sceneScriptLayerMap[String(layer.name)] = layer;
                }
                return layer;
            }

            function __sceneScriptSeedLayer(info) {
                if (!info) return null;
                var layer = __makeSceneScriptLayer(
                    info.name || '',
                    new Vec3(info.originX || 0, info.originY || 0, info.originZ || 0),
                    new Vec3(
                        info.scaleX !== undefined ? info.scaleX : 1,
                        info.scaleY !== undefined ? info.scaleY : 1,
                        info.scaleZ !== undefined ? info.scaleZ : 1
                    ),
                    new Vec3(1, 1, 1),
                    1,
                    info.visible !== undefined ? !!info.visible : true,
                    new Vec3(info.sizeX || 0, info.sizeY || 0, 0),
                    info.id !== undefined ? info.id : -1,
                    info.parentId || 0
                );
                return __sceneScriptStoreLayer(layer);
            }

            function __sceneScriptGetLayer(name) {
                var key = String(name || '');
                if (__sceneScriptLayerMap[key]) {
                    return __sceneScriptLayerMap[key];
                }
                if (typeof name === 'number' && __sceneScriptLayerIdMap[name]) {
                    return __sceneScriptLayerIdMap[name];
                }
                if (!__sceneScriptLayerMap[key]) {
                    __sceneScriptLayerMap[key] = __sceneScriptStoreLayer(__makeSceneScriptLayer(
                        key,
                        new Vec3(0, 0, 0),
                        new Vec3(1, 1, 1),
                        new Vec3(1, 1, 1),
                        1,
                        new Vec3(0, 0, 0),
                        -1,
                        0
                    ));
                }
                return __sceneScriptLayerMap[key];
            }

            function __sceneScriptLayerForEvaluation(id, fallbackName, currentValue, color, alpha, visible) {
                var registered = id !== undefined && __sceneScriptLayerIdMap[id]
                    ? __sceneScriptLayerIdMap[id]
                    : null;
                var layer = __makeSceneScriptLayer(
                    registered ? registered.name : fallbackName,
                    registered ? registered.origin : currentValue,
                    registered ? registered.scale : new Vec3(1, 1, 1),
                    color,
                    alpha,
                    visible,
                    registered ? registered.size : new Vec3(0, 0, 0),
                    id,
                    registered ? registered.parentId : 0
                );
                return layer;
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
                globalThis.__sceneScriptNowMs = Number(globalThis.__sceneScriptNowMs) || 0;
                if (!globalThis.__sceneScriptNativeDate) {
                    globalThis.__sceneScriptNativeDate = Date;
                    function __sceneScriptDateBaseMs() {
                        var fixed = Number(globalThis.__sceneScriptFixedDateMs);
                        return isFinite(fixed) ? fixed : undefined;
                    }
                    function __sceneScriptDate(y, m, d, h, min, s, ms) {
                        if (!(this instanceof __sceneScriptDate)) {
                            return (new __sceneScriptDate()).toString();
                        }
                        if (arguments.length === 0) {
                            var fixed = __sceneScriptDateBaseMs();
                            if (fixed !== undefined) {
                                return new globalThis.__sceneScriptNativeDate(fixed);
                            }
                            return new globalThis.__sceneScriptNativeDate();
                        }
                        if (arguments.length === 1) return new globalThis.__sceneScriptNativeDate(y);
                        if (arguments.length === 2) return new globalThis.__sceneScriptNativeDate(y, m);
                        if (arguments.length === 3) return new globalThis.__sceneScriptNativeDate(y, m, d);
                        if (arguments.length === 4) return new globalThis.__sceneScriptNativeDate(y, m, d, h);
                        if (arguments.length === 5) return new globalThis.__sceneScriptNativeDate(y, m, d, h, min);
                        if (arguments.length === 6) return new globalThis.__sceneScriptNativeDate(y, m, d, h, min, s);
                        return new globalThis.__sceneScriptNativeDate(y, m, d, h, min, s, ms);
                    }
                    __sceneScriptDate.prototype = globalThis.__sceneScriptNativeDate.prototype;
                    __sceneScriptDate.parse = globalThis.__sceneScriptNativeDate.parse;
                    __sceneScriptDate.UTC = globalThis.__sceneScriptNativeDate.UTC;
                    __sceneScriptDate.now = function() {
                        var fixed = __sceneScriptDateBaseMs();
                        return (fixed !== undefined ? fixed : 0) + globalThis.__sceneScriptNowMs;
                    };
                    Date = __sceneScriptDate;
                }
                try {
                    Date.now = function() {
                        var fixed = Number(globalThis.__sceneScriptFixedDateMs);
                        return (isFinite(fixed) ? fixed : 0) + globalThis.__sceneScriptNowMs;
                    };
                } catch (e) {}
                engine.__sceneScriptTimeouts = engine.__sceneScriptTimeouts || [];
                engine.setTimeout = function(fn, ms) {
                    if (typeof fn !== 'function') return function() {};
                    var timer = {
                        fn: fn,
                        delayMs: Math.max(0, Number(ms) || 0),
                        elapsedMs: 0,
                        active: true
                    };
                    engine.__sceneScriptTimeouts.push(timer);
                    return function() { timer.active = false; };
                };
                engine.__sceneScriptAdvanceTimers = function(deltaMs) {
                    var __deltaMs = Math.max(0, Number(deltaMs) || 0);
                    globalThis.__sceneScriptNowMs += __deltaMs;
                    engine.runtime = (Number(engine.runtime) || 0) + __deltaMs / 1000.0;
                    var timers = engine.__sceneScriptTimeouts || [];
                    for (var i = 0; i < timers.length; i++) {
                        var timer = timers[i];
                        if (!timer || !timer.active) continue;
                        timer.elapsedMs += __deltaMs;
                        if (timer.elapsedMs >= timer.delayMs) {
                            timer.active = false;
                            try { timer.fn(); } catch (e) {}
                        }
                    }
                    engine.__sceneScriptTimeouts = timers.filter(function(timer) {
                        return timer && timer.active;
                    });
                };
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
                engine.media = engine.media || {
                    getTitle: function() { return __sceneScriptMediaState().title; },
                    getArtist: function() { return __sceneScriptMediaState().artist; },
                    getAlbum: function() { return __sceneScriptMediaState().album; },
                    getDuration: function() { return __sceneScriptMediaState().duration; },
                    getPosition: function() { return __sceneScriptMediaState().position; },
                    isPlaying: function() { return !!__sceneScriptMediaState().playing; },
                    addEventListener: function() {},
                    removeEventListener: function() {}
                };
            }

            function __sceneScriptMediaState() {
                return globalThis.__yakkaiMedia || {
                    available: false,
                    playing: false,
                    title: '',
                    artist: '',
                    album: '',
                    albumArtPath: '',
                    duration: 0,
                    position: 0,
                    settleSeconds: 0,
                    hasFixedClock: false,
                    fixedClockEpochMs: 0,
                    hasThumbnailColors: false,
                    textColor: new Vec3(1, 1, 1),
                    primaryColor: new Vec3(0, 0, 0),
                    secondaryColor: new Vec3(0, 0, 0),
                    tertiaryColor: new Vec3(0, 0, 0)
                };
            }

            var MediaPlaybackEvent = {
                PLAYBACK_STOPPED: 0,
                PLAYBACK_PLAYING: 1,
                PLAYBACK_PAUSED: 2,
                PLAYBACK_CHANGED: 'mediaPlaybackChanged',
                TIMELINE_CHANGED: 'mediaTimelineChanged',
                THUMBNAIL_CHANGED: 'mediaThumbnailChanged',
                mediaPlaybackChanged: 'mediaPlaybackChanged',
                mediaTimelineChanged: 'mediaTimelineChanged',
                mediaThumbnailChanged: 'mediaThumbnailChanged'
            };

            // input stub
            var input = {
                cursorPosition: new Vec3(0, 0, 0),
                cursorWorldPosition: new Vec3(0, 0, 0)
            };

            // shared object stub — inter-layer communication
            var shared = globalThis.shared || {};
            function __sceneScriptIsFiniteVec3(value) {
                return value !== undefined && value !== null &&
                    isFinite(Number(value.x)) &&
                    isFinite(Number(value.y)) &&
                    isFinite(Number(value.z));
            }
            function __sceneScriptVec3Or(value, fallback) {
                if (__sceneScriptIsFiniteVec3(value)) {
                    return new Vec3(Number(value.x), Number(value.y), Number(value.z));
                }
                return fallback;
            }
            var __sceneScriptSharedMedia = __sceneScriptMediaState();
            if (shared.miSettingsOpen === undefined) shared.miSettingsOpen = false;
            if (shared.miSettingsOpenSpeed === undefined || !isFinite(Number(shared.miSettingsOpenSpeed))) {
                shared.miSettingsOpenSpeed = 0.2;
            }
            if (shared.miTextPos === undefined) shared.miTextPos = 2;
            if (shared.miTextVisible === undefined) shared.miTextVisible = true;
            if (shared.miCursorIn === undefined) shared.miCursorIn = false;
            if (shared.miShowClock === undefined) shared.miShowClock = 2;
            if (shared.miClockPos === undefined) shared.miClockPos = 2;
            if (!__sceneScriptIsFiniteVec3(shared.miTextContainerScale)) {
                shared.miTextContainerScale = new Vec3(1, 1, 1);
            }
            if (!__sceneScriptIsFiniteVec3(shared.miPrimaryColor) ||
                typeof shared.miPrimaryColor.mix !== 'function') {
                shared.miPrimaryColor = __sceneScriptVec3Or(
                    __sceneScriptSharedMedia.primaryColor,
                    new Vec3(0, 0, 0));
            }
            if (!__sceneScriptIsFiniteVec3(shared.miTextBgColor) ||
                typeof shared.miTextBgColor.mix !== 'function') {
                shared.miTextBgColor = new Vec3(0, 0, 0);
            }
            if (shared.miTextBgColorFadeSpeed === undefined ||
                !isFinite(Number(shared.miTextBgColorFadeSpeed))) {
                shared.miTextBgColorFadeSpeed = 1;
            }
            if (shared.miInitTextBgColorAlpha === undefined ||
                !isFinite(Number(shared.miInitTextBgColorAlpha))) {
                shared.miInitTextBgColorAlpha = 1;
            }
            Object.defineProperty(shared, 'mi', {
                configurable: true,
                enumerable: true,
                get: function() { return __sceneScriptMediaState(); }
            });
            globalThis.shared = shared;
        )";
}

} // namespace wallpaper::policy
