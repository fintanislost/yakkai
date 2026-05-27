# Arona Scene Rendering — Situation Report

## Scene: Sleeping Arona (Workshop 3228578419)

### What it is
A Blue Archive puppet scene with a 14-bone skinned crop-sheet character, background layers (desk, wall, chair), lens flare effects, sparkle particles, and Z sleep letters.

### Scene type: Puppet
Detected by `has_puppet_objects` flag (image objects with non-empty `.puppet` field).

### What works
- Character renders via direct puppet mesh with SKINNING (no effect chain)
- Background layers render with raw textures (effects stripped)
- Flare/lens layers render through their full effect chain (HLSL UV fix active)
- Hex-hash named layers (auto-generated WE flare components) keep their effects
- Sparkle particles and Z letters render normally

### What doesn't work (known limitations)
- Background LUT color grading effects — produce washed-out blue output through our pipeline. Stripped as workaround.
- Background blur effects — same issue. Stripped.
- Puppet mesh through effect chain — FinalMesh swap after SceneImageEffectLayer resolution produces wrong colors. Direct rendering used instead.
- Script execution — WE scripts (alpha animation, property conditions) not implemented. Layers with script-controlled visibility may be hidden.

### The puppet effect bypass (WPSceneParser.cpp ~line 2345)
```
if (context.has_puppet_objects && hasEffect) {
    // flare/lens/hash detection...
    if (! isFlareOrLens && ! isHashElement) {
        strip effects
    }
}
```

**Rules:**
1. This block ONLY runs for Puppet scene type
2. Flare layers (name contains "flare", "lense", "lens") KEEP their effects
3. Hex-hash layers (16+ hex chars) KEEP their effects
4. ALL other layers get effects stripped
5. The puppet layer itself also gets effects stripped (separate check: `if (puppet && hasEffect)`)
6. Flare/lens/hash layers with `alpha == 0.0` get alpha forced to `1.0` (script-controlled visibility workaround)

### Regression history
This scene has broken repeatedly during development. Common failure modes:

1. **Washed-out blue background** — caused by LUT/blur effects running through the pipeline. Fix: strip effects for non-flare layers.
2. **Black diamond overlay** — `c7884e...` hex-hash layer rendering with opaque black background. Fix: keep its effect chain (it's a lens flare component).
3. **White rectangle** — flare layer with wrong blend mode. Fix: keep effect chain for proper alpha processing.
4. **Invisible flares** — flare textures have alpha=0 (script-controlled visibility). Fix: keep flare effect chains AND force `alpha=1.0` for flare/lens/hash layers in the puppet bypass. The shader cache can mask this bug — always verify with a fresh cache (`rm -rf ~/.cache/wescene-renderer/3228578419/spvs01/`).
5. **Puppet mesh tile seams** — background pulse effects create brightness variations visible through puppet mesh gaps. Fix: strip background effects.
6. **Missing desk/background** — global effect changes (like HLSL define in fragment shaders) affecting lighting normals. Fix: HLSL only in common header, let each shader handle its own fragment HLSL blocks.

### How to verify
```bash
smoke-tests/run.sh
```
Compare against `smoke-tests/3228578419-sleeping-arona.png`. Check for:
- Brown desk visible
- Character sleeping on desk (not floating in sky)
- Rainbow flare arc at bottom
- Sparkle particles
- No black diamonds or white rectangles
- No washed-out blue tint on backgrounds

### Files involved
- `WPSceneParser.cpp` — puppet bypass, flare detection, scene type
- `WPShaderParser.cpp` — HLSL define, shader preprocessing
- `VulkanRender/PassCommon.hpp` — blend mode factors
- `Type.hpp` — BlendMode enum
