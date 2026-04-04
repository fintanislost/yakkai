# Branch: fix-scene-3476236738-v2

Comprehensive fixes for WE puppet scene rendering, primarily targeting scene 3476236738 (Chinese classroom puppet scene with two anime characters). Many fixes are general-purpose and improve rendering across all scene types.

## TEXB v4 Texture Loading

**File:** `WPTexImageParser.cpp`

Scene textures using TEXB v4 format were completely failing to load (0 mipmaps). The TEXB v4 binary format uses a flat structure after the `count` field that differs from the v1-v3 per-mipmap loop:

```
TEXB v4: {unknown_ff(4), unknown_0(4), format(4), width(4), height(4),
           lz4_flag(4), decomp_size(4), comp_size(4), payload...}
```

The v1-v3 parser read the v4 `0xFFFFFFFF` unknown field as `mipmap_count`, which clamped to 0 via `std::max<i32>(-1, 0)`, silently producing empty texture slots. Added a `isTexbV4` code path that reads the flat v4 fields correctly. The TEXI header format ID is authoritative (v4 body uses a different numbering scheme).

## Additive Puppet Animation Layers

**Files:** `WPPuppet.hpp`, `WPPuppet.cpp`, `WPImageObject.cpp`

WE scene.json `animationlayers` entries can have `"additive": true`, meaning the layer's bone transforms should be added as deltas on top of the base animation rather than blended as competing alternatives. Previously the `additive` field was ignored — both layers competed for blend weight (each getting 0.5 when both had blend=1.0).

Now:
- `additive` field is parsed from scene.json
- Non-additive layers participate in blend normalization
- Additive layers use their blend weight directly and only contribute the delta (no base frame contribution)

## Effect Chain Alpha Fix

**File:** `WPSceneParser.cpp`

The offscreen effect rendering path (ping-pong targets) breaks alpha compositing. When a layer with effects is rendered through the offscreen path, transparent regions become opaque. This caused:
- Puppet sprite parts (eyes) to vanish
- Background layers (sky/window) to be hidden behind opaque foreground layers
- Any layer with effects to lose its transparency

Fix: strip all effects from regular layers in puppet scenes. Exempt composelayers and flare/lens layers (detected by `colorBlendMode != 0` or name matching). This is a workaround — the root cause is the offscreen render target alpha handling which needs a proper fix.

## Flare Detection by colorBlendMode

**File:** `WPSceneParser.cpp`

Flare/lens layers were previously detected only by name substring matching ("flare", "lens", "lense"). Hash-named flare layers (e.g. `c7884e6807cf62bb85f8d8b67942cec4`) were missed, causing them to have their effects stripped and render as opaque black quads.

Fix: also detect flares via `colorBlendMode != 0`. All flare layers in WE use non-zero colorBlendMode (typically 6 or 7), while normal layers use 0.

## QuickJS SceneScript Evaluator

**Files:** `WPSceneScript.cpp/hpp`, `WPJson.cpp/hpp`, `CMakeLists.txt`

WE scenes use JavaScript (SceneScript) for dynamic property evaluation. Embedded QuickJS to evaluate these scripts. Provides stubs for `engine.userProperties`, `engine.canvasSize`, `createScriptProperties()`, `Vec3()`. Used for resolving dynamic layer origins, alpha values, and other scene properties.

Build notes: QuickJS compiled with `-fvisibility=hidden` to prevent 195+ symbol conflicts with Qt's QML engine.

## Shader Preprocessing Fixes

**File:** `WPShaderParser.cpp`

- **`inverse()` polyfill stripping**: Custom `mat3 inverse(mat3)` definitions conflict with GLSL 150 built-in when `#define highp` strips precision qualifiers. Detect and comment out custom inverse function definitions.
- **Varying type mismatch fix**: Composelayer vertex outputs `vec4 v_TexCoord` but color grading fragment expects `vec2`. Fixed by demoting vertex output type to match fragment input, preserving varying name for cross-stage matching.
- **`#require LightingV1` resolution**: Inject `PerformLighting_V1` GLSL function with PBR light arrays.
- **ENABLEMASK to MASK combo mapping**: Scene JSON uses `ENABLEMASK=1` but shaders use `#if MASK`.
- **NBSP normalization**: Non-breaking spaces (U+00A0) in WE scripts replaced with regular spaces.

## MDL Parser UTF-8 Fix

**File:** `WPMdlParser.cpp`

`ReadSectionAsciiString` rejected bytes >= 0x80, breaking Chinese puppet animation names. Removed the upper bound check to accept valid UTF-8 sequences.

## Composelayer Support

**Files:** `SceneImageEffectLayer.cpp/h`, `WPSceneParser.cpp`

- Removed unconditional composelayer skip
- Added fullscreen blit path for composelayer effect output
- Effect camera positioned at (0,0,0) for composelayer
- Effectless composelayers skipped as no-ops

## Scene Property Detection

**File:** `WPSceneParser.cpp`

Scans `project.json` for colour/opacity property pairs and blends them into the scene clear color. Detects `colour0/opacity0` + `colour1/opacity1` patterns.

## Debug/Development Tools

- `tools/color-lab/` — Interactive HTML color debugging tool with texture viewer, g_Color4 sliders, blend mode simulation
- `tools/color-lab/export_textures.py` — Extract WE .tex files to PNG via LZ4 + DDS + ImageMagick
- `tools/validate-scene.sh` — Automated render validator with structural + pixel analysis
- `YAKKAI_NO_EFFECTS` env var — strips all effects for color debugging

## Tested Scenes

| Scene | Status |
|-------|--------|
| 3476236738 (Chinese classroom puppet) | Working — eyes, sky, characters correct |
| 3228578419 (Arona puppet) | Working — flares render correctly |
| 2920980901 (non-puppet) | No regression |
| 2376308754 (non-puppet) | No regression |
