# Scene Development Process

## Overview

This document describes the iterative process for improving Wallpaper Engine scene rendering. Each change follows a validate → modify → validate cycle using the automated scene validator.

## Tools

### `tools/validate-scene.sh <scene_id> [capture_delay_ms]`

Automated validator that checks both structural rendering state and pixel output quality. Produces PASS/WARN/FAIL results without requiring visual screenshot comparison.

**Structural checks:**
- Scene type detection (Puppet/Video/Standard)
- Shader compilation success/failure count
- Puppet MDL parsing (bone count, animation count)
- Effect chain loading (which effect types are in the render graph)
- Composelayer presence
- Render graph node count
- Scene property tint resolution
- QuickJS script evaluation
- Material loading failures
- Effect capture manifest gates for known renderer-risk fixtures (`3476236738` must allow at least one simple-water candidate; `3228578419` must allow none)

**Pixel analysis:**
- Capture file size (blank detection)
- Color variance / standard deviation (detail level)
- Average color (hue verification)
- Color diversity (unique colors in 100x100 downscale)
- Quadrant breakdown (spatial color distribution)

### `smoke-tests/run.sh`

Regression tests for known-good scenes. Clears shader cache before each run. Used to verify existing scenes aren't broken by changes.

### Policy Tests

Phase 2 adds `yakkai_scene_policy_tests` for behavior-preserving renderer policy boundaries. Run it before and after touching `EffectPolicy`, `VideoTexturePolicy`, `ModelFallbackPolicy`, or `SceneScriptRuntimePolicy`:

```bash
cmake --build build --target yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
```

## Iteration Cycle

### 1. Baseline

Run the validator on the target scene before making changes:

```bash
./tools/validate-scene.sh 3476236738 10000 | tee /tmp/yakkai-debug/baseline.txt
```

Record key metrics: render nodes, effect passes, color variance, unique colors, average color.

### 2. Investigate

Dump the scene JSON for analysis:

```bash
# The scene parser dumps the decrypted JSON on first load (temporary debug code).
# Or use the harness log to trace specific rendering paths.
```

Use the validator log at `/tmp/yakkai-debug/validate-<id>.log` to trace:
- Which effects are being stripped vs kept
- Which shaders fail to compile
- Which materials fail to load
- What scene properties are resolved

### 3. Modify

Make targeted changes to the rendering pipeline. Common change points:

| File | What it controls |
|------|-----------------|
| `WPSceneParser.cpp` | Scene parsing, effect bypass, camera setup, tint system |
| `WPShaderParser.cpp` | Shader preprocessing, `#require` resolution, varying fixes |
| `WPMdlParser.cpp` | Puppet MDL format parsing |
| `WPJson.cpp` | Scene property resolution, QuickJS script evaluation |
| `WPImageObject.cpp` | Layer parsing, suppression logic |
| `SceneImageEffectLayer.cpp` | Effect chain resolution, final output |
| `SceneToRenderGraph.cpp` | Render graph construction, texture routing |

### 4. Build and validate

```bash
# Clear shader cache (stale SPIR-V masks regressions)
rm -rf ~/.cache/wescene-renderer/*/spvs01/

# Build
cmake --build build/native/scene_harness

# Validate target scene
./tools/validate-scene.sh 3476236738 10000

# Regression check on Arona
./tools/validate-scene.sh 3228578419 8000
```

### 5. Compare metrics

Compare validator output against baseline:

| Metric | Direction | Meaning |
|--------|-----------|---------|
| Render nodes | ↑ higher | More effects processing |
| Color variance | ↑ higher | More visual detail |
| Unique colors | ↑ higher | Richer color output |
| Shader fails | ↓ lower | Better shader compatibility |
| Material fails | ↓ lower | Better material loading |
| Avg color hue | → matches scene | Correct atmospheric tinting |

### 6. Commit when stable

Only commit when:
- Target scene metrics improved or maintained
- Arona regression check passes (14 pass, 0 fail)
- No new shader compilation failures

## Scene 3476236738 — Current State

**Scene structure:** 2 puppets (20+64 bones), 15+ image layers with parent container at (2294, 1078), composelayer with color grading + blur, 3 particle systems, waterflow/waterwaves/opacity/shine/iris effects.

**Current baseline (2026-04-01):**
- Render nodes: 52
- Effect passes: 23 (blur, iris, opacity, shine, waterflow, waterwaves)
- Color variance: 0.087
- Unique colors: 5905
- Avg color: R=109 G=116 B=135 (blue-shifted, correct)
- Shader fails: 0
- Material fails: 0
- Validator result: 14 pass, 0 fail, 0 warnings

**Progress history:**
| Date | Change | Nodes | Shader fails | Variance | Colors |
|------|--------|-------|-------------|----------|--------|
| Initial | Effects disabled | 25 | 0 | 0.082 | 5552 |
| +effects | Selective bypass | 42 | 10 | 0.082 | 5698 |
| +inverse | Strip polyfill | 52 | 0 | 0.079 | 5904 |
| +tint | Direct color tint | 52 | 0 | 0.087 | 5905 |
| +mask | ENABLEMASK→MASK | 52 | 0 | 0.087 | 6035 |
| +alpha | Effect alpha write | 52 | 0 | **0.127** | 5935 |
| +mask | ENABLEMASK→MASK | 52 | 0 | 0.127 | 6035 |
| **natural** | **Remove tint, use texture colors** | 52 | 0 | **0.125** | 5955 |

**Known limitations:**
- Textures have their own blue-purple colors baked in (BC3/DXT5 with full RGB). The composite tint was REMOVED — textures render with natural colors. Clear color uses a 50/50 blend of the original gray and the scene's atmosphere property.
- QuickJS is embedded and ready but scripts are compiled, not inline text
- Scene segfaults on capture exit (render completes, capture saved, crash during cleanup)
