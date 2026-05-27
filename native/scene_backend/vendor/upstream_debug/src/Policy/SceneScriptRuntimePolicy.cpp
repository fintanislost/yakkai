#include "Policy/SceneScriptRuntimePolicy.hpp"

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

std::string sceneScriptRuntimeStubSource()
{
    return R"(
            var __scriptPropertyOverrides = __scriptPropertyOverrides || {};
            function createScriptProperties() {
                var props = {};
                var builder = {
                    addSlider: function(opt) {
                        props[opt.name] = (opt.name in __scriptPropertyOverrides)
                            ? __scriptPropertyOverrides[opt.name] : opt.value;
                        return builder;
                    },
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

            // WEMath library stubs
            var WEMath = {
                smoothStep: function(edge0, edge1, x) {
                    var t = Math.max(0, Math.min(1, (x - edge0) / (edge1 - edge0)));
                    return t * t * (3 - 2 * t);
                },
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
