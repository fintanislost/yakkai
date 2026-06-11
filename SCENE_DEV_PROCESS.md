# Scene Development Process

## Overview

This document describes the iterative process for improving Wallpaper Engine scene rendering. Each change follows a validate → modify → validate cycle using the automated scene validator.

## Automated Preflight vs Human Visual Gate

Automated checks are preflight gates. They must catch crashes, shader failures,
missing captures, invalid generic routes, clear-color leakage sentinels, and
other structural regressions before a high-risk render change reaches review.
They do not decide visual parity for authored motion, blur, color grading,
fullscreen utility layers, composelayers, or puppet/crop-sheet movement.

When a high-risk probe has a generic route and passes structural validation,
metrics such as RMSE and region deltas route the artifact to human review. A
human reviewer approves or rejects the generated capture/contact sheet, and the
decision log records that result before any production predicate is widened.
Hard blockers are reserved for structural impossibility, missing fixtures after
reasonable attempts, scene-specific predicate requirements, or repeated human
visual rejection.

## Tools

### `tools/validate-scene.sh <scene_id> [capture_delay_ms]`

Automated validator that checks both structural rendering state and pixel output quality. Produces PASS/WARN/FAIL results without requiring visual screenshot comparison.

The validator clears `~/.cache/wescene-renderer/*/spvs01/` before each harness
render so stale SPIR-V does not mask regressions.

When comparing delayed visual frames to effect-chain TGAs, pass
`--debug-effect-capture-delay-ms <ms>` to the harness with the same timestamp as
the PNG capture. The debug manifest records this as `captureDelayMs`, and the
renderer waits until the current scene frame reaches that time before dumping
the registered render targets. For shader-output oracles, use the manifest's
`shaderTimeSeconds` field as the `g_Time` value; `captureDelayMs` is the
requested threshold, while `effectiveCaptureTimeSeconds` records the readiness
check including the current frame time.

When `--debug-effect-captures` is enabled, the harness records generic
`material-output` captures for preserved effect material passes. Use these
captures to inspect multi-pass effects stage by stage before changing shader
preprocessing, blend modes, render target sizing, or final-publish routing.
The manifest records layer id, effect index, pass index, material name, output
target, and texture bindings for each material-output capture.

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
- Scene-specific visual sentinels for known broad visual failures (`3476236738` hard-fails when multiple background regions look like tint-adjusted clear-color leakage or when the left character's extended-hand foreground region disappears)
- Generic puppet effect viewport guard: hard-fails when a puppet effect input mesh meaningfully exceeds its recorded effect-input viewport, which catches offscreen route clipping before a human visual review while tolerating tiny mesh bleed that should not resize authored effect-space.
- Optional explicit stripped-layer probe replay with `--probe-layers <ids>`,
  which runs a second harness pass with `--debug-effect-probe-layers`,
  summarizes probe-only inventory rows, and reports baseline-vs-probe RMSE.
- Optional high-risk probe replay with `--probe-high-risk-layers <ids>`, which
  runs a second harness pass with `--debug-effect-probe-high-risk-layers`,
  summarizes probe-only high-risk rows, reports baseline-vs-probe RMSE, and
  compares sentinel-region mean/stddev/unique-color deltas when a sentinel is
  configured.
- Optional probe prefix slicing with `--probe-max-effects N`, which forwards
  `--debug-effect-probe-max-effects N` during the second pass and records
  original/kept visible effect counts in the manifest. Use this when a mixed
  chain probe regresses and the first failing effect needs to be isolated.
- Optional puppet route-only probing with `--probe-puppet-route-only`, which
  forwards `--debug-puppet-effect-route-only` during an explicit
  `--probe-layers` pass. It keeps the puppet offscreen/final-display route
  active with zero visible effects so route/composition bugs can be separated
  from shader behavior.
- Optional puppet final-display mesh probing with
  `--probe-puppet-final-mesh layer-card|image-space`, which forwards the
  harness mesh override during an explicit puppet probe. The normal
  non-channelmap puppet effect route is atlas-first/deferred-puppet-final;
  use these overrides only to compare against the legacy standalone final-display
  paths during diagnostics, then validate production policy with a normal pass.
- Explicit probe limiting applies to requested production-allowed layers as
  well as stripped candidates. This lets a layer remain sliceable after it is
  promoted into normal rendering policy.

**Pixel analysis:**
- Capture file size (blank detection)
- Color variance / standard deviation (detail level)
- Average color (hue verification)
- Color diversity (unique colors in 100x100 downscale)
- Quadrant breakdown (spatial color distribution)

### `tools/scene_visual_sentinels.py`

Scene-specific visual sentinel helper used by `tools/validate-scene.sh`. It
uses ImageMagick to sample configured regions from a capture and fails when
known scene regions match a broad visual failure signature. The current
sentinel covers `3476236738` by detecting multiple background regions that are
both low-detail and close to the scene's tint-adjusted clear color, and by
checking color-presence in the left character's extended-hand region so puppet
effect viewport clipping is caught automatically.
When called with `--baseline`, it also reports region deltas between the
baseline and probe captures.

### `tools/effect_route_guards.py`

Structural manifest guard used by `tools/validate-scene.sh`. It checks each
puppet effect route once and compares `publish.effectInputMeshBounds` against
`publish.effectInputViewportSize`. A route fails when generated puppet geometry
falls meaningfully outside the current layer-local effect viewport, because that
means the offscreen effect input can clip visible puppet parts before any effect
shader runs. Tiny mesh bleed is tolerated to preserve authored waterwave/shake
mask coordinates. Routes that expanded to mesh bounds pass when the expanded
viewport contains the mesh and report the expansion as evidence.

### `tools/scene-script-log-summary.py`

SceneScript runtime diagnostic helper used by `tools/validate-scene.sh`. It
summarizes QuickJS binding layers, unsupported media-integration layers, and
non-fatal SceneScript runtime gaps by class, API name, and layer id. Visible
runtime gaps are validator warnings; harmless and media/runtime-only gaps are
kept as triage evidence. If a non-fatal script gap belongs to a layer already
suppressed as unsupported media integration, the helper reclassifies that gap as
media-runtime-only so deferred media widgets do not mask the remaining visible
SceneScript work. The summary preserves the top-level unique layer binding
count and also prints per-property binding counts. Generated text bindings are
counted separately as `text` when validator logs contain
`QuickJS binding: ... text=...`, distinct from transform, color, and alpha
bindings.

### `smoke-tests/run.sh`

Regression tests for known-good scenes. Clears shader cache before each run. Used to verify existing scenes aren't broken by changes.

### Policy Tests

Phase 2 adds `yakkai_scene_policy_tests` for behavior-preserving renderer policy boundaries. Run it before and after touching `EffectPolicy`, `VideoTexturePolicy`, `ModelFallbackPolicy`, or `SceneScriptRuntimePolicy`:

```bash
cmake --build build --target yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
```

### `tools/arona_protected_puppet_lab.py`

Protected Arona crop-sheet diagnostic report. Run it after an Arona comparator
run that includes `--debug-effect-captures`; add `--debug-effect-probe-layers 405`
when forced or prefix-sliced probe evidence is needed.
For time-dependent ribbon or LUT work, also pass
`--debug-effect-capture-delay-ms <ms>` through the harness/comparator path so
the effect TGAs describe the inspected delayed frame instead of the first
rendered frame.
It summarizes layer `405` normal or probe captures, protected metadata, effect order,
active puppet animation-layer contribution slots, alpha evidence, capture-level alpha/color statistics, stage-boundary
comparisons, generic route diagnostics, final-publish composition diagnostics,
active-slot final coverage diagnostics, and comparator metrics, then writes a contact sheet for human review. When
`final-display-before/after` captures exist, the lab uses their before/after
delta as isolated final-display contribution evidence before falling back to
post-frame `final-publish`. It then compares that final-display delta against
`effect-output` alpha geometry and reports alignment by visible ratio, bounds
IoU, and centroid drift. The lab also sweeps the final-display before/after
delta at RGB thresholds `1`, `4`, `8`, and `16` out of 255 and reports
`finalDisplayThresholdSensitivity` so weak halo/noise can be separated from
persistent shape drift. Because `effect-output` is layer-local while
`final-display-before/after` are screen-space captures, the lab also reports
`finalDisplayScreenSpaceProjection`, which projects the output alpha centroid
from output bounds into final-display delta bounds and classifies whether the
shape is affine-consistent after that coordinate-space remap. The lab also
reports `secondaryOnlySlots` from `puppetCutoutSlotCoverage` alongside active
animation slots; these are weighted mesh slots with no primary-owned vertices,
which helps identify child cutouts that must move with a parent bone before
enabling more puppet animation behavior. Pass
`--normal-summary <summary.json>` from a
normal non-probe Arona comparator run to add probe-final-to-normal drift and
screen-space centroid drift. The forced probe path is not a normal renderer policy
change. If the comparator times out after writing a per-variant manifest, the
lab can recover `variant/effect-captures/manifest.json` from the run output
directory even when the summary marks that variant failed.

`final-publish` is a post-frame `_rt_default` dump, not an isolated final-display
node capture. Prefer `final-display-before/after` when present. Treat
`final-screen-drift` or `local-to-frame-composition` from `final-publish` alone
as a signal to add or inspect stronger final-display evidence before changing
production route policy.

When a forced probe shifts the puppet or exposes utility crop-sheet output,
rerun the comparator with `--debug-effect-probe-max-effects N`, or rerun the
validator with `--probe-layers <ids> --probe-max-effects N`, to slice the probe
to the first `N` visible effects. Compare `N=0`, `N=1`, and subsequent prefixes
against the normal non-probe run before changing renderer policy. The manifest
records `probeMaxEffects` plus per-layer original/kept counts, so slice
artifacts remain traceable.

Do not use the old `--debug-effect-probe-channelmap-slots` route for puppet
portion isolation. It is quarantined because forcing derived sidecar channelmaps
onto non-channelmap puppet layers produced glitchy crop-sheet fragments before
effects ran. Use normal-material puppet animation-layer diagnostics instead:
`--debug-effect-captures` records `puppetAnimationLayerInventory`, and
`--debug-puppet-animation-layer-overrides` can isolate authored animation layers
without introducing wallpaper-specific renderer logic.

For runtime secondary-motion experiments, run the harness or validator with
`--puppet-simulation runtime`. The validator also accepts
`YAKKAI_PUPPET_SIMULATION=runtime` and translates it into the harness option,
but prefer the explicit flag for repeatable local validation. This debug gate
enables the generic post-animation puppet simulation pass for secondary bones
whose metadata includes active physics controls while leaving default Plasma behavior off. Use it only for
comparison artifacts until the focused scene and regression gates have passed
and a human visual review has approved default-on behavior.

### `tools/arona_layer405_fresh_publish_compare.py`

Fresh Windows final-publish evidence comparator for Arona layer `405`. It
validates the ignored local archive
`yakkai_arona/layer405_final_publish_composite_fresh.zip`, checks that the
Day/Sunset/Night entries are fresh capture-session-labeled RDC exports, parses
Windows final-publish blend/SRV state and pixel-history rows, and optionally
correlates that evidence with Yakkai debug-effect manifests.

Run it against the Windows package alone when checking the package shape and
layer-local-to-default-target registration:

```bash
python3 tools/arona_layer405_fresh_publish_compare.py \
  --windows yakkai_arona/layer405_final_publish_composite_fresh.zip \
  --output /tmp/yakkai-layer405-fresh-publish-compare
```

Run it with a fresh Arona comparator output root when checking Yakkai's
final-publish pass state:

```bash
python3 tools/arona_layer405_fresh_publish_compare.py \
  --windows yakkai_arona/layer405_final_publish_composite_fresh.zip \
  --yakkai-root /tmp/yakkai-layer405-fresh-yakkai-rerun \
  --output /tmp/yakkai-layer405-default-delta-locator
```

Current fresh Windows evidence publishes a `4160x2923` layer target into the
`2560x1440` default target with final-publish `writeMask=7`, which is RGB-only.
After rebuilding the backend plugin and harness, Yakkai debug manifests classify
the matching `_rt_default` final-publish pass as
`yakkai-final-publish-rgb-mask`; the earlier `unexpected-mask` result came from
a stale backend manifest without `debugEffectPassStates` or `colorMaskBits`.
When changing this area, rebuild the backend module directly:

```bash
cmake --build build/native/scene_backend --target yakkai_scene_backend yakkai_scene_backendplugin
cmake --build build/native/scene_harness --target yakkai_scene_harness
```

The current image registration is intentionally conservative and reports
`weak-or-unregistered-match` for all three fresh Windows variants. Treat that as
evidence that the next diagnostic target is stronger pixel-history/default-delta
registration keyed to final-publish sample points, not another color-mask patch.
When `--yakkai-root` is supplied, the report writes
`yakkaiDefaultDeltaOracle` and `yakkaiDefaultDeltaLocator` to `summary.json` and
sample-level Markdown tables. The oracle samples Yakkai layer `405`
`default-before-effect` and `default-after-effect` captures at the Windows
final-publish pixel-history coordinates and compares Yakkai's RGB delta against
Windows `pre_mod_rgba` to `post_mod_rgba` for `lowerRibbon` and
`transparentEdge`. The locator searches the Yakkai default-target delta map for
sample, nearest, and peak RGB changes and writes before/after/delta crop sheets
under `locator-crops/`.

Interpret locator results this way:

| Locator result | Meaning | Next target |
| --- | --- | --- |
| `delta-at-windows-sample` | Exact coordinate has Yakkai delta | shader/blend/value parity at the point |
| `delta-nearby` | Yakkai contribution exists near the Windows coordinate | projection, viewport, transform, or sample-coordinate mapping |
| `delta-elsewhere` | Contribution exists far from the Windows point | layer/default-target registration or wrong source region |
| `missing-default-delta` plus `boundaryAfterToFinalPublish=delta-*` | `default-after-effect` capture is too early, or final-publish capture is later | debug capture timing/final-publish boundary |
| `missing-default-delta` with no boundary delta | Yakkai is not publishing the expected contribution | render graph/final-publish route |

The current real run at `/tmp/yakkai-layer405-default-delta-locator` classifies
every variant/sample as `missing-default-delta` from `default-before-effect` to
`default-after-effect`, while `boundaryAfterToFinalPublish` is
`delta-at-windows-sample` for Day/Sunset/Night. That routes the next
investigation to the debug capture timing/final-publish boundary.
The tool extracts local diagnostic archives and is intended for trusted local
RenderDoc exports; it is not a general-purpose untrusted zip scanner. Full
registration over the real package can take a few minutes.

### `tools/arona_ribbon_tip_boundary.py`

Frame-sequence motion boundary helper for subtle Arona ribbon/bow defects. Pass
Windows WE frames, normal Yakkai frames, and optional prefix-sliced Yakkai frame
directories such as `N0`, `N2`, `N7`, `N10`, and `N11`. The region config uses a
`baseSize` and `tip`/`main` ROIs; the tool scales those ROIs to each sequence's
actual frame size before measuring vertical motion, then writes
`boundary-report.json`, `boundary-report.md`, per-region crops, and
`boundary-contact-sheet.png`.

Use this as a preflight and attribution tool only. A `moving`/`static`
classification can route the next investigation toward puppet composition,
waterwaves, shake, final composition, or stronger Windows evidence, but subtle
puppet/crop-sheet motion still needs human review of the contact sheet before a
renderer change is considered.

### `tools/arona_ribbon_flutter_parity.py`

Temporal edge-trace helper for lower-ribbon defects where both Windows WE and
Yakkai appear to move but the wave shape looks different. Pass Windows and
normal Yakkai frame directories plus a region JSON with `baseSize` and
`regions.lower_ribbon_edge`. The tool subtracts rigid per-frame vertical motion,
measures residual edge flutter, writes Windows/Yakkai kymographs, and classifies
the comparison as missing flutter, phase mismatch, amplitude mismatch, or
ambiguous reference evidence.

Use this after a normal Windows/Yakkai crop has been corrected by human review.
Do not capture prefix-sliced layer evidence or change renderer behavior until
the normal context sheet and kymograph sheet are approved as the visible defect
target.

### `tools/arona_shake_output_oracle.py`

CPU oracle for the final Arona layer `405` `effects/shake` pass. Capture the
same scene twice with `--debug-effect-probe-layers 405`, once with
`--debug-effect-probe-max-effects 10` and once with `11`, then pass both
manifests plus local `yakkai_arona/scene.pkg` to the oracle. It decodes the
authored `RG8` flow mask, uses manifest `shaderTimeSeconds`, models WE's
`effects/shake` shader with `M_PI_2 == 2*pi`, applies Yakkai source-alpha
preservation, and writes CPU/GPU PNGs, a diff, a focused ribbon contact sheet,
and JSON/Markdown metrics. Treat a close CPU-patched-vs-GPU result as evidence
that the Yakkai shader boundary is coherent; remaining parity work then needs
a Windows per-effect oracle or broader final-frame evidence before another
renderer change.

### `tools/effect-capture-summary.py`

Effect-capture manifest summary helper. In addition to stripped-candidate,
LUT/color-grade, and high-risk probe reports, it prints
`protected-puppet-cutout-inventory` rows when a manifest contains protected
puppet captures or metadata. Those rows include active animation ids, active
bone slots, and `puppetCutoutSlotCoverage` bone/parent names, primary coverage,
weighted coverage, secondary-only markers, parsed simulation target/mass fields,
active-physics markers, and simulated-inactive markers. The raw manifest also
records `layerLocalBounds` and `layerLocalCentroid` per slot so crop-sheet
motion investigations can map a visible region back to weighted puppet slots.
`simulatedInactive`
only means active physics metadata exists on an inactive slot; target point/mass
metadata alone is reported but treated as static tip metadata, not proof that a
runtime simulation step should move that slot.

### `tools/puppet_effect_lab.py`

Generic puppet effect-boundary diagnostic helper. Pass an effect capture
manifest and optional `--layers <ids>` to summarize route metadata,
effect-input/output boundaries, final-display before/after contribution, and
final-display geometry projection for non-protected puppet layers. Use it with
route-only and prefix-sliced probes to confirm whether a regression is in the
offscreen/final-display route or in an individual effect shader.

### `tools/effect-candidate-inventory.py`

Generic effect candidate inventory helper. Pass one or more debug manifest
paths to normalize allowed, stripped, protected, and probe-only candidates into
class-level records:

```bash
tools/effect-candidate-inventory.py \
  /tmp/yakkai-debug/effect-captures-3228578419/manifest.json \
  /tmp/yakkai-debug/effect-captures-3476236738/manifest.json
```

Use this before writing a new renderer allow predicate. The output groups
candidates by effect class, chain shape, disposition, route-audit
classification, and visual-gate-audit classification, and includes route and
puppet-slot evidence when the manifest provides it. The current route audit
covers `regular-lut-only` records and checks for layer-local effect input
reset, original-parent final publish, card/card meshes, empty route risk, a
final published material, and both screen-sized and layer-local material-output
capture stages. The current visual-gate audit covers
`composelayer-color-grade`, `utility-blur`, and `composelayer-water-only`
records and separates production
allowed captures, missing probe-route evidence, and structurally valid
high-risk routes that require human visual approval. `--json` emits the same
normalized records for local analysis. Inventory evidence is not approval to
enable a class; it only selects the next predicate, microscope, or human review
slice.

## Iteration Cycle

### 1. Baseline

Run the validator on the target scene before making changes:

```bash
./tools/validate-scene.sh 3476236738 10000 | tee /tmp/yakkai-debug/baseline.txt
```

Record key metrics: render nodes, effect passes, color variance, unique colors, average color.
For diagnostic-only high-risk effect replay, keep the default validation pass
and add a probe pass:

```bash
./tools/validate-scene.sh 3476236738 10000 --probe-high-risk-layers 137
```

For diagnostic-only explicit stripped-layer replay, use the generic probe path:

```bash
./tools/validate-scene.sh 3228578419 8000 --probe-layers 239
```

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
- Which SceneScript runtime gaps are visible, harmless, or media/runtime-only

### 3. Modify

Make targeted changes to the rendering pipeline. Common change points:

| File | What it controls |
|------|-----------------|
| `WPSceneParser.cpp` | Scene parsing, effect bypass, camera setup, tint system |
| `WPShaderParser.cpp` | Shader preprocessing, HLSL compatibility shims such as `clip(x)`, `#require` resolution, varying fixes |
| `WPMdlParser.cpp` | Puppet MDL format parsing |
| `WPJson.cpp` | Scene property resolution |
| `WPSceneScript.cpp` | QuickJS script evaluation and SceneScript runtime diagnostics |
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
- Arona regression check passes with no failures
- No new shader compilation failures

## Scene 3476236738 — Current State

**Scene structure:** 2 puppets (20+64 bones), 15+ image layers with parent container at (2294, 1078), composelayer with color grading + blur, 3 particle systems, waterflow/waterwaves/opacity/shine/iris effects.

**Current baseline (2026-06-01):**
- Render nodes: 98
- Effect passes: 23 (blur_precise_gaussian, iris, opacity, shine, waterflow, waterwaves)
- Color variance: 0.135551
- Unique colors: 10000
- Avg color: R=94.6096 G=123.847 B=162.869
- Shader fails: 0
- Material fails: 0
- Validator result: 19 pass, 0 fail, 2 warnings (SceneScript runtime gaps and no mixed-chain stripped candidates)

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
| 2026-05-29 | Local offscreen effect sources keep scene parent transforms out of effect inputs | 44 | 0 | 0.137 | 10000 |
| 2026-06-01 | Non-protected puppet water chains preserve input blend mode and render `water+opacity(+shine/iris)` | 98 | 0 | 0.136 | 10000 |
| 2026-06-01 | Non-channelmap puppet effect viewports expand to mesh bounds to avoid clipping animated geometry outside the object rectangle | 98 | 0 | 0.136 | 10000 |

**Known limitations:**
- Textures have their own blue-purple colors baked in (BC3/DXT5 with full RGB). The composite tint was REMOVED — textures render with natural colors. Clear color uses a 50/50 blend of the original gray and the scene's atmosphere property.
- QuickJS is embedded and ready but scripts are compiled, not inline text
- High-risk color-grading outside the strict `composelayer-color-grade` class, fullscreen blur outside the strict `utility-blur` class, mixed blur, utility/fullscreen water-only carrier classes, audio/lightshaft utility effects, and protected blur effect paths remain stripped or harness-probe-only. Regular image-layer `lut-only` chains, regular non-carrier `blur-only` chains, fullscreen utility blur carriers classified as `utility-blur`, composelayer water-only carriers classified as `composelayer-water-only`, composelayer blur/color-grade carriers classified as `composelayer-color-grade`, non-protected puppet water chains whose mix families are limited to `opacity`, `shine`, and `iris`, and protected puppet crop-sheet chains limited to recognized water/LUT/pulse/shake families can render normally. Protected puppet probe reports include route metadata and screen-space drift against a normal non-probe comparator run; forced probes remain diagnostic even when a safe-family chain is allowed. Non-channelmap protected puppet final displays publish as original-parent sibling layer cards with premultiplied final-display blending because the effect output is already a rendered layer-local image, and non-channelmap puppet effect targets expand to mesh bounds when the generated puppet geometry exceeds the nominal object size.
- Puppet chains with blur, LUT/color-grade, audio/lightshaft, utility/fullscreen carrier behavior, or unknown families remain deferred. Protected blur/color-grade crop-sheet chains and protected crop-sheet chains with unknown families also remain deferred.
