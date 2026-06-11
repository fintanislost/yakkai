# Arona Windows Evidence Request

Status date: 2026-06-05

This is an evidence request for the Windows-side agent working with official Wallpaper Engine captures for Arona. It is not a request to patch Yakkai.

## Current State

The target wallpaper is:

- Wallpaper: `Blue Archive | Sleeping Arona [4K]`
- Workshop id: `3228578419`
- Scene type: Wallpaper Engine scene / puppet scene
- Primary Yakkai layer under investigation: object id `405`, name `ARONA_CROP_SHEET`

The current Yakkai renderer on `release/renderer-limitations` is not equivalent to the simple Windows-side patch in `yakkai_arona/patches/yakkai-arona-puppet-effects.diff`.

Yakkai already has a much deeper implementation:

- protected puppet effect policy
- standalone puppet final-display route
- source-alpha preservation narrowed to puppet `shake`; `waterwaves` follows the
  official WE displaced-alpha behavior after shader-debug evidence
- premultiplied final-display blending
- route, effect, and puppet diagnostics
- harness property overrides via `--scene-properties-json`
- harness frame sequences via `--capture-sequence`

Do not apply the old `yakkai-arona-puppet-effects.diff` as-is. It was useful historically, but it is stale for the current renderer state.

## Current Symptoms

Current Yakkai output still appears regressed compared with official Wallpaper Engine:

- A visible white or rectangular-ish area appears around the puppet/ribbon region.
- The lower-ribbon and bow motion still does not visually match official WE.
- The current issue may look like a bone/animation problem, but the strongest evidence points to the layer `405` effect/final-display path.

Important distinction:

- The MDL/bone/skinning path still matters because it defines the crop-sheet mesh, active slots, and base pose.
- The visible Windows motion target is mostly produced by layer `405` effects over that puppet crop sheet: `pulse`, `waterwaves`, and `shake`.

## Evidence Already Present

The copied `yakkai_arona` bundle is useful and should be preserved:

- `yakkai_arona/_motion/ribbon/_raw/`
  - 675 official WE PNG frames
  - `1280x720`
  - timestamped by `timestamps_ms.txt`
  - span: about `52.768s`
  - mean sample rate: about `12.773 Hz`

- `yakkai_arona/_motion/analysis_summary.json`
  - validates measured Windows motion frequencies against scene constants

- `yakkai_arona/_motion/signal.csv`
  - reusable motion traces

- `yakkai_arona/_motion/fft.png`
  - frequency plot with predicted effect rates

- `yakkai_arona/analysis/effect-passes.md`
  - layer `405` effect inventory
  - pass order and shader constants

- `yakkai_arona/scene/`
  - unpacked scene
  - includes masks and scene-specific shaders

- `yakkai_arona/scene.pkg`
  - packed source scene

- `yakkai_arona/_we_assets/`
  - copied WE base assets for offline Yakkai rendering

The Windows motion summary is credible for broad bow/arm motion:

- `0.159 Hz`: whole-body / breathe sway from `Arm Shake` and `Breathe Shake`
- `0.318 Hz`: ribbon/hair components from `Ribbon Waves 2`, `Hair waves`, `Mouth Shake`
- `0.398 Hz`: ribbon top/bottom from `Ribbon Wave Top` and `Ribbon Wave Bot`

## Completed Since The Original Request

The Windows-side `waterwaves_frame90_textures` export was received and is useful:

- Path: `yakkai_arona/captures/waterwaves_frame90_textures/`
- Contents: full-resolution `4160x2923` `SRV0 before draw` and `RTV after draw` PNGs for waterwaves draws `936`, `959`, `982`, `1005`, and `1028`.
- The pass chain is coherent: each RTV-after image is byte-identical to the next draw's SRV-before image.

Linux/Yakkai-side analysis artifacts:

- `md-fixes/arona-renderdoc/waterwaves_frame90_pass_oracle.py`
- `md-fixes/arona-renderdoc/waterwaves-frame90-pass-oracle/waterwaves-frame90-pass-oracle.md`
- `md-fixes/arona-renderdoc/reports/converted-waterwaves-evidence.md`

Current finding:

- The initial full-texture exports cleared the old blocker of lacking Windows
  pass-boundary textures, but later pixel-level shader-debug evidence superseded
  the early full-texture uncertainty.
- RenderDoc shader-debug evidence under
  `yakkai_arona/waterwaves_frame90_shader_debug/` matches the official WE
  waterwaves model with displaced color and displaced alpha on stable pixels.
- The older Yakkai source-alpha preservation behavior for `effects/waterwaves`
  is no longer the current model. Production now preserves source alpha only for
  `effects/shake` pending equivalent shake pass-boundary evidence.

## Windows Evidence Status

### 2026-06-10 Fresh Evidence Complete

Windows supplied the requested fresh labeled package:
`yakkai_arona/layer405_final_publish_composite_fresh.zip`

Linux validation accepted it as complete fresh Day/Sunset/Night Layer 405
final-publish/composite evidence:

- status: `complete_fresh_live_rdc_labeled_variants`
- Day: `timeofday=1`, final publish event `621`, SRV0 resource `374`
- Sunset: `timeofday=2`, final publish event `824`, SRV0 resource `1736`
- Night: `timeofday=3`, final publish event `1199`, SRV0 resource `1769`
- all variants publish to swapchain resource `65` (`2560x1440`) from layer
  targets `4160x2923`, `R8G8B8A8_UNORM`
- all variants report final-publish blend
  `color=(SrcAlpha Add InvSrcAlpha)`, `alpha=(SrcAlpha Add InvSrcAlpha)`,
  `writeMask=7`

No further Windows action is required for the current Linux debugging step. The
active next step is Yakkai-side final-publish color-mask diagnostics and
layer-local-to-swapchain registration/comparison against the fresh package.

The older `capture_one` / `capture_two` packages remain historical/day-like
corroborating evidence only.

### 2026-06-10 Addendum: Layer 405 Final-Publish/Composite Boundary

Yakkai now has evidence that layer `405` local prefix-3 LUT output matches the
local model, while remaining drift appears downstream. Please capture Windows
WE evidence for the same Arona scene/time variants at the layer-405 boundary:

- Preferred Windows source package:
  `C:\Users\peter\Documents\Codex\2026-06-05\so-i-ve-had-a-project\outputs\layer405_final_publish_composite_original_rdc\README.md`
- Use the original-RDC package above for follow-up exports. Do not base the
  follow-up on the older rehydrated package unless the original-RDC package is
  unavailable and the README explains that fallback.
- Scene: `3228578419`
- Layer/material target: `ARONA_CROP_SHEET`, layer `405`
- Time variants: Day, Sunset, Night matching the current
  `tools/compare-arona-reference.sh` delays
- Variant labels are capture-session labels, not values the Windows agent must
  discover from local Yakkai metadata. No local `yakkai_arona` checkout is
  required. If the capture operator intentionally records the Day run, put it
  in `day/` and set `"variant": "day"`; do the same for `sunset/` and
  `night/`.
- Required captures:
  - Effect input before layer-405 visible effects
  - Output after the first `LUT Loader` pair, equivalent to Yakkai
    `--debug-effect-probe-max-effects 3`
  - Output after prefix `7`, if possible
  - The texture or render target fed into the final puppet publish/composite
  - The frame/default render target immediately before and after the layer-405
    final publish/composite
- Required metadata:
  - Render target dimensions and formats
  - SRV bindings for the LUT pair and final publish material
  - Blend state for final publish/composite
  - Draw id / pixel history for one lower-ribbon sample and one
    transparent-edge sample
  - For each variant, include `"variant"`, `"variantLabelSource"`, and
    `"variantMappingStatus"`. For the requested rerun, use values like
    `"variant": "day"`, `"variantLabelSource": "capture-session-label"`, and
    `"variantMappingStatus": "labeled-by-capture-session"`.

The goal is to decide whether the remaining mismatch is a Yakkai
final-publish/composite bug or a Windows behavior we have not modeled yet.
Please avoid inferred screenshots only; pass-boundary textures and binding
metadata are the useful data.

If rerunning the three known variants is not possible and only existing
unmapped RenderDoc captures are available, do not fabricate Day/Sunset/Night
labels. Keep representative folder names, set
`"variantMappingStatus": "unmapped"`, and explain why the capture could not be
mapped. That evidence remains useful, but it does not close the open
Day/Sunset/Night-labeled capture request.

For existing `capture_one` / `capture_two` style folders, fully satisfying the
variant request requires a concrete mapping such as
`capture_one_frame90 -> sunset` with the reason for that mapping. Without that
mapping, keep the representative folder names and mark the package partial.

### 2026-06-10 Zip Review: Current Windows Zip Is Still Partial

Reviewed archive: `yakkai_arona/layer405_final_publish_composite.zip`

This archive is useful corroborating evidence, but it is not the requested
follow-up package:

- It contains `capture_one_frame90/` and `capture_two_frame104/`, not
  `day/`, `sunset/`, and `night/`.
- Its README says the exports came from
  `work/captures-frame90-rehydrated.rdc` and
  `work/captures-frame104-rehydrated.rdc`.
- `manifest.json` still marks Day/Sunset/Night as missing.
- The metadata keeps the representatives unmapped and does not provide an
  explicit capture-set/frame-to-variant mapping.

Treat this zip as already-ingested partial evidence. Do not continue refining
this exact package unless the Windows side can add a concrete variant mapping.
The next attempt should start from the original-RDC package listed above or
from a fresh Windows WE RenderDoc capture session with labels recorded at
capture time.

### 2026-06-10 Source Manifest Blocked As Written

Windows-side blocker report:
`C:\Users\peter\Documents\Codex\2026-06-05\so-i-ve-had-a-project\outputs\layer405_final_publish_composite_blocked\README.md`

Source manifest status:
`C:\Users\peter\Documents\Codex\2026-06-05\so-i-ve-had-a-project\outputs\layer405_final_publish_composite_blocked\source_manifest_blocked.json`

The source-manifest preflight correctly stopped. `E:\captures` contains only
`capture_one` and `capture_two` plus converted Linux exports, with no
capture-session labels or files identifying Day/Sunset/Night. Filling the
required source manifest from that state would require fabricating labels.

Valid next Windows-side starting points:

- Provide an explicit, justified mapping, for example
  `capture_one_frame90 -> day`, if that mapping is known from outside the local
  files.
- Or make new labeled RDC captures/folders, for example
  `E:\captures\day\...`, `E:\captures\sunset\...`, and
  `E:\captures\night\...`, then rerun the source-manifest preflight before
  exporting pass-boundary textures.

Do not produce another `capture_one` / `capture_two` package unless one of
those two conditions is met.

### 2026-06-10 Intake Note: Partial Unmapped Representatives Received

Received path: `yakkai_arona/layer405_final_publish_composite/`

This package is useful but not complete. It contains two unmapped representative
captures (`capture_one_frame90`, `capture_two_frame104`) with pass-boundary
textures, LUT-pair SRVs, final-publish blend/SRV metadata, and pixel histories.
It does not identify the requested Day/Sunset/Night variants. Please keep the
original request open for authoritative Day/Sunset/Night-labeled captures.

Useful signal from the partial package:

- final publish event `621`
- SRV0: resource `374`, `4160x2923`, `R8G8B8A8_UNORM`
- target: swapchain resource `65`, `2560x1440`, `R8G8B8A8_UNORM`
- blend state: `color=(SrcAlpha Add InvSrcAlpha)`,
  `alpha=(SrcAlpha Add InvSrcAlpha)`, `writeMask=7`
- pixel history shows RGB changes at the final publish while default alpha stays
  opaque

Still needed:

- Day/Sunset/Night-labeled captures in the requested directory shape. These can
  be labeled by the capture session itself; they do not need local Yakkai
  metadata. A clear mapping from the received representatives is also acceptable
  if the Windows side can prove it, but representative names alone are still
  considered unmapped.
- If using the existing `capture_one` / `capture_two` captures, provide the
  capture-set/frame-to-variant mapping explicitly. Otherwise, leave them as
  unmapped representatives and rerun labeled captures from the original-RDC
  package.
- For the next attempt, create a top-level source manifest before exporting
  pass-boundary textures. It should list each intended variant, the source RDC
  path, whether the source is `original-rdc` or `fresh-live-rdc`, and the label
  source. If the Windows side cannot fill that manifest, stop and report that
  blocker instead of producing another unmapped package.
- Each per-variant `metadata.json` should include `sourcePackageReadme`,
  `sourceRdcPath`, `sourceType`, `variant`, `variantLabelSource`, and
  `variantMappingStatus`.
- The current blocked report confirms `E:\captures` only has `capture_one` and
  `capture_two`; a fresh labeled capture session is the cleanest path unless an
  external capture-set/frame-to-variant mapping is known.
- If possible, any RenderDoc state/export that makes the final publish
  color-write mask and target format obvious without relying on text parsing.

## Historical Or Deferred Windows Requests

The sections below are retained for provenance from earlier Arona ribbon and
effect debugging. They are not the active ask for the current goal. The active
Windows request is the layer `405` final-publish/composite boundary request
above.

### Historical: Pixel-Level Shader Debug For Waterwaves

This request was completed by `yakkai_arona/waterwaves_frame90_shader_debug/`
and is retained here for provenance. It is no longer the highest-priority
Windows ask; the active request is the layer `405` final-publish/composite
boundary above.

Original request:

Please use RenderDoc on the frame `90` replay and debug the pixel shader for
these exact events/pixels:

| Draw/event | Pixel `(x,y)` | Why |
| ---: | --- | --- |
| `936` | `(2871, 2071)` | Stable alpha, active mask, CPU model samples a much darker texel than Windows output. |
| `959` | `(3024, 2355)` | Stable alpha, active mask, Windows output moves toward a dark sample while the CPU model remains close to source. |

For each debugged pixel, please record:

- interpolated `v_TexCoord.xy`
- interpolated `v_TexCoord.zw`
- interpolated `v_Direction`
- sampled `g_Texture1` mask value
- `g_Time`, `g_Speed`, `g_Scale`, `g_Exponent`, `g_Strength`
- computed `distance`
- `sin(distance)`
- squared `strength`
- final `texCoord` used to sample `g_Texture0`
- sampled `g_Texture0` RGBA at that final `texCoord`
- final pixel shader output RGBA

If RenderDoc can export a shader-debug text/CSV log, that is better than screenshots.

Optional but useful:

- export `SRV1` mask PNG at the same draw events from replay, not from the converted archive, to confirm the exact mask orientation/value RenderDoc sees during shader debugging.
- export a small crop around each debugged pixel for `SRV0 before`, `SRV1 mask`, and `RTV after`.

### Deferred: Confirm The Exact Visible Defect ROI

Please provide a small annotated target package that identifies the exact visual defect location.

Needed outputs:

- 3 to 5 official WE frames showing the correct appearance.
- Matching frame numbers from `_motion/ribbon/_raw/` if using the existing 675-frame set.
- Cropped closeups around:
  - lower white ribbon tip
  - wall/glow immediately behind or under the ribbon
  - white/rectangular margin area that should not appear around the puppet
- A simple ROI JSON file:

```json
{
  "baseSize": [1280, 720],
  "regions": {
    "lowerRibbonTip": [0, 0, 0, 0],
    "wallGlowBehindRibbon": [0, 0, 0, 0],
    "whiteMarginCheck": [0, 0, 0, 0],
    "bowMotion": [0, 0, 0, 0]
  }
}
```

Use pixel boxes as `[x0, y0, x1, y1]` in the `1280x720` frame coordinate system.

### Deferred: Preserve Timestamped Motion Frames

The current 675-frame set is useful. If recapturing, keep this shape:

- Resolution: `1280x720`
- Time of day: `Day` / `timeofday=1`
- Media integration: disabled / `mediaintegration=0`
- Duration: at least `50s`
- Frame rate: around `12.8 fps` is fine if timestamps are exact
- Include `timestamps_ms.txt`
- Use PNG frames, not a compressed-only MP4, for analysis

Non-uniform frame spacing is acceptable if every frame has a true timestamp.

### Deferred: Provide A Fresh Official WE Still For The White-Margin Comparison

Please provide one fresh official WE final-frame still matching the current Yakkai comparison setup:

- `1280x720`
- `timeofday=1`
- `mediaintegration=0`
- settled after at least `10000ms`
- no overlay, no cursor, no UI
- same crop/framing as the existing `_motion/ribbon/_raw` frames

Suggested path:

```text
yakkai_arona/_targets/day_margin_reference/frame_settled_10000ms.png
```

### Deferred: Optional High-Value Prefix Captures From Official WE

If the Windows-side agent can safely use the Wallpaper Engine editor or a copied project to toggle layer `405` effects, provide prefix captures. This is optional but very valuable.

Use effect names, not just pass counts:

- Base puppet only: layer `405` effects disabled
- Pulse only:
  - `Halo Pulse`
  - `Triangle Pulse`
- Pulse plus waterwaves:
  - `Halo Pulse`
  - `Triangle Pulse`
  - `Ribbon Wave Top`
  - `Ribbon Wave Bot`
  - `Ribbon Waves 2`
  - `Hair waves`
  - `Hair Waves 2`
- Full safe motion chain:
  - pulse plus waterwaves plus all `shake` effects
  - keep LUT thumbnail/test passes disabled
  - keep media integration disabled

For each prefix capture:

- capture at least `20s`, preferred `50s`
- provide PNG frames and timestamps
- record exactly which effects were enabled/disabled
- avoid changing layer transforms, camera, or scene properties other than the intended effect toggles

These captures would let us separate:

- base puppet/skinning errors
- waterwaves motion errors
- shake motion errors
- final composition/alpha errors

### Deferred: Optional Per-Effect/Editor Evidence

If available without invasive binary reversing, include:

- screenshots or exported JSON showing layer `405` effect order in the WE editor
- confirmation that `strength` values are squared in official effect shader behavior
- confirmation that `speed` is angular rate, using `frequency = speed / (2*pi)`
- screenshots of the lower ribbon with the editor effect stack visible

No decompilation or invasive exe work is requested.

## Historical Broad Motion Processing Plan

If a future goal explicitly reopens the deferred ROI/reference-frame package,
the Yakkai side can:

1. Capture current Yakkai frames at matching `1280x720` and matching timestamps.
2. Run the same motion FFT checks against the Windows trace.
3. Prefix-slice layer `405` in Yakkai:
   - base
   - pulse
   - pulse plus waterwaves
   - pulse plus waterwaves plus shake
4. Compare the white-margin ROI across the same boundaries.
5. Identify whether the first bad boundary is:
   - base puppet/MDL/skinning
   - waterwaves shader or sampling
   - shake shader or sampling
   - shake source-alpha handling
   - final-publish color-write mask or blend/composition
   - standalone final-display blend/composition
   - time-of-day LUT/glow interaction

## Historical Broad Motion Hypothesis

The current best hypothesis is not "missing bone animation" by itself.

More likely:

- base puppet skinning is close enough to run;
- authored layer `405` effects create most of the visible motion;
- current Yakkai regressed in the effect/final-display composition path, producing the white margin and making the motion look wrong.

The deferred broad motion request above was designed to prove or disprove that
boundary cleanly. The current active request is narrower: layer `405`
final-publish/composite pass-boundary evidence.

## Historical Broad Motion Deliverable Checklist

For the deferred broad motion package, return a folder with:

```text
yakkai_arona/_targets/
  README.md
  roi.json
  day_margin_reference/
    frame_settled_10000ms.png
    annotated_margin.png
  selected_frames/
    frame_XXXX.png
    frame_YYYY.png
    frame_ZZZZ.png
  optional_prefix/
    base/
      frames/
      timestamps_ms.txt
      enabled_effects.txt
    pulse/
      frames/
      timestamps_ms.txt
      enabled_effects.txt
    pulse_waterwaves/
      frames/
      timestamps_ms.txt
      enabled_effects.txt
    full_safe_motion/
      frames/
      timestamps_ms.txt
      enabled_effects.txt
```

The minimum acceptable deliverable is:

- `roi.json`
- one fresh official WE settled Day still at `1280x720`
- 3 annotated/cropped frames identifying the lower-ribbon/white-margin target

The best deliverable is the minimum set plus optional prefix captures.
