# Renderer Limitations Phase Plan

This is a durable working outline for reducing the known Wallpaper Engine scene renderer limitations without taking blind regressions. It is intentionally untracked until explicitly added.

## Current README Limitations

- Regular per-layer offscreen effect chains in puppet scenes can break alpha compositing. Yakkai selectively strips regular/heavy effects in puppet scenes while preserving composelayers, colorkey, flare/lens, and other essential effect paths.
- Small embedded video textures are decoded as static first frames to keep CPU use bounded. Continuous decode is enabled only for large/main videos when FFmpeg is available at build time.
- Static model scenes use an experimental fallback for basis correction, camera framing, and material selection.
- Material/lighting fidelity is partial: generic materials and point lights are supported, but full Wallpaper Engine PBR, shadow, and reflection parity is not.
- SceneScript support is partial: Yakkai evaluates simple layer bindings (origin/color/alpha/visible) with API stubs, not the full Wallpaper Engine runtime.

## Strategy

Do not start by changing renderer behavior. Start by upgrading regression detection so each renderer slice has before/after evidence. Then refactor risky decision logic into focused boundaries, and only then fix one rendering limitation at a time.

## Phase 0: Regression Harness Upgrade

Goal: make renderer regressions visible before changing renderer logic.

Slices:
- Convert smoke tests from hardcoded shell calls into a manifest-driven suite.
- Keep a fast quick gate for normal edits and add a stricter deep render gate for visual-risk changes.
- Compare captured PNG images against baselines with real visual metrics, not only file size.
- Add fixed-timestamp still captures plus short deterministic PNG frame sequences for video and animated effects.
- Generate MP4/WebM review clips from captured frame sequences, but do not use encoded video as the primary baseline format.
- Record structured artifacts per scene: captures, frame sequences, generated review clips, logs, metrics, and visual diffs.
- Add strict baseline update flow so drift is intentional and reviewable.
- Keep existing `tools/validate-scene.sh` structural checks, but make smoke tests the visual regression gate.

Quick gate:
- Run quickly enough for normal development loops.
- Cover a small set of representative scenes.
- Fail on missing captures, blank/low-variance captures, major baseline drift, and structural render failures.

Deep render gate:
- Required for changes that can affect pixels, scene loading, animation timing, video textures, shader preprocessing, effect policy, blend/composition, model/material/light behavior, SceneScript, QML render plumbing, harness capture behavior, or validator behavior.
- Capture multiple timestamps per scene and short frame sequences where motion matters.
- Compare each sampled frame against PNG baselines and compute temporal metrics for expected or unexpected motion.
- Fail hard on structural problems, blank frames, missing expected motion, unexpected large drift, and shader/material errors.
- Produce review artifacts for smaller visual deltas so manual comparison can judge intentional renderer changes.

Result states:
- `pass`: visual metrics and structural checks are within thresholds.
- `review`: visual drift exceeds the warning threshold but stays below the hard-fail threshold. The run writes artifacts and exits zero unless `--strict` is set.
- `fail`: blank frames, missing expected motion, structural errors, shader/material errors, missing required dependencies, or very large visual drift.
- `skip`: local Workshop assets for a scene are unavailable. Skips are acceptable in normal local runs, but a strict release run can require no skipped required scenes.

Baseline policy:
- Phase 0 is behavior-preserving. Renderer output changes during this phase are regressions unless the change is explained by capture/comparison mechanics.
- PNG still frames and PNG frame sequences are the versioned source of truth.
- Generated MP4/WebM clips, visual diffs, logs, run summaries, and candidate updates stay in artifact directories and out of git.
- Baseline updates use two explicit steps: generate candidate artifacts first, then promote approved PNG frames and metadata with a separate command.
- The manifest supports default thresholds plus per-scene overrides so stable, flare-heavy, video, and motion-sensitive scenes can have appropriate drift tolerances.

Runner design:
- Keep `smoke-tests/run.sh` as the stable entry point.
- Move orchestration into a Python stdlib runner so manifests, thresholds, artifacts, JSON summaries, skip/review/fail states, and baseline promotion stay maintainable.
- Clear `~/.cache/wescene-renderer/*/spvs01/` by default before render validation. Allow an explicit debugging option such as `--keep-shader-cache`.
- Require image comparison tooling for visual gates and fail with install guidance if unavailable.
- Treat video generation as optional: if FFmpeg is unavailable, still compare PNG frames and warn that review clips were skipped.

Harness changes:
- Preserve the existing single-capture `--capture` behavior.
- Add native multi-capture support instead of launching one process per frame.
- Proposed options: `--capture-dir`, `--capture-times-ms 1000,3000,8000`, and `--capture-sequence 5000:60:33`.
- Capture environment metadata with each run: capture size, device pixel ratio, Qt/QtQuick backend, GPU/driver renderer string when available, FFmpeg/ImageMagick versions, shader-cache-cleared state, harness commit, and wallpaper asset path.
- Warn when current environment metadata differs from baseline metadata. Strict/release mode may require matching metadata.

Scene manifest and catalog:
- Start with a small curated scene manifest before trying to inventory every known scene.
- Cover known risky classes first: Arona/flare, puppet alpha/effects, main video, overlay video when available, static model fallback, and one SceneScript-heavy scene when available.
- Add a human-readable asset catalog with Workshop links, scene purpose, expected visual features, and whether each scene participates in quick, deep, or release gates.
- Keep unbaselined local candidates out of active suites. Track intake slices in `RENDERER_FIXTURE_CANDIDATE_SLICES.md` until each candidate has reviewed artifacts and promoted baselines.

Manual review:
- Artifact output should point reviewers at side-by-side frames, diffs, metrics, logs, and generated review clips.
- Review instructions should explicitly check camera angle, visible elements, character position, color balance, composition, effect intensity, and motion.

Phase 0 explicitly uses the standalone scene harness only. Plasma-live capture is intentionally out of scope until the local harness gate is stable.

## Phase 1: Scene Coverage Matrix

Goal: ensure every known limitation has at least one representative scene before fixes.

Scene buckets:
- Puppet scenes with regular effects, composelayers, colorkey, flare/lens, and alpha-sensitive layers.
- Video texture scenes with main video textures and small overlay videos.
- Static model scenes with basis/framing/material fallback behavior.
- Material and lighting scenes covering generic materials, point lights, lightmaps, normal maps, and reflection paths.
- SceneScript scenes covering origin, color, alpha, visible, shared state, timers, input, audio buffers, and animation/video APIs where available.

## Phase 2: Behavior-Preserving Refactor

Goal: isolate risky policy decisions currently embedded in large renderer files before changing behavior.

Slices:
- Extract effect strip/preserve decisions into an `EffectPolicy` boundary.
- Extract video texture playback/static decisions into a `VideoTexturePolicy` boundary.
- Extract static model fallback decisions into a model fallback boundary.
- Tighten SceneScript wrapper/stub ownership into a clear runtime boundary.
- Run upgraded smoke tests after each refactor slice.

## Phase 3: Per-Layer Effect Alpha

Goal: preserve more authored effects without washing out or occluding puppet layers.

Slices:
- Add debug captures around effect input, effect output, and final publish nodes.
- Fix alpha preservation through offscreen render targets.
- Re-enable effect families one at a time.
- Keep composelayer, colorkey, flare/lens, and existing puppet baselines locked.
- Remove strip rules only when visual metrics and manual review agree.

## Phase 4: Video Texture Fidelity

Goal: replace width-based static/continuous video decisions with deliberate playback policy.

Slices:
- Add frame-sequence tests for main videos and overlay videos.
- Detect video role from material/layer usage where possible.
- Add decoder budget controls for maximum active videos and resolution.
- Validate loop timing and frame progression.
- Allow animated overlay videos when budget permits.

## Phase 5: Static Model, Material, And Lighting Fidelity

Goal: improve static model scenes without destabilizing puppet scenes.

Slices:
- Lock model fallback scenes with image baselines and structural expectations.
- Improve basis correction and camera framing heuristics.
- Expand generic material support for lightmap, normal map, and reflection cases.
- Improve point-light uniform fidelity.
- Defer full PBR/shadow/reflection parity until lower-level material paths are stable.

## Phase 6: SceneScript Runtime

Goal: evolve the current layer-binding evaluator into a controlled mini-runtime.

Slices:
- Keep persistent per-layer script contexts.
- Implement frame lifecycle for `init()` and `update()`.
- Expand `shared`, timer, animation, video texture, input, and audio buffer APIs.
- Make script failures non-fatal but measurable.
- Add script-specific regression scenes before broadening API behavior.

## Phase 7: Release Gate

Goal: no renderer release without renderer evidence.

Local gate:

```bash
./scripts/install-local.sh --dev --no-install
./smoke-tests/run.sh
tools/validate-scene.sh <target-scene>
```

Future gate:
- Run all available smoke scenes.
- Save artifacts for failures and baseline updates.
- Include release notes listing renderer-risk areas touched.

## Stretch Phase: Plasma-Live Capture

Goal: validate that Plasma displays the same rendered output the standalone harness considers good.

This remains a later integration check because it depends on compositor state, monitor layout, wallpaper containment state, KWin timing, and desktop capture behavior. It should not block Phase 0.
