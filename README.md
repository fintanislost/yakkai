# Yakkai

A KDE Plasma 6 wallpaper plugin with native Wallpaper Engine scene rendering support.

## Features

- **Gradient backgrounds** — configurable two-color gradients with animation, vignette, and time-of-day palette blending
- **Video wallpapers** — local video playback via QtMultimedia
- **Wallpaper Engine support** — scan Steam libraries for WE workshops:
  - **Video** projects via QtMultimedia
  - **Web** projects via QtWebEngine with WE property, viewport, audio-listener, and media-listener compatibility shims
  - **Scene** projects via a native Vulkan C++ renderer with puppet animation, particle systems, QuickJS script evaluation, and day-night cycle support
- **Scene property editor** — in-settings controls for WE scene toggles (rain, snow, effects, etc.)
- **Playlist mode** — sequential, random, or time-scheduled wallpaper cycling for scenes, with shared playlist libraries and per-monitor active selection
- **All Wallpapers picker** — unified browser across scene, video, and web types, restoring the selected type and project from persisted Plasma config
- **Playlist (All)** — playlists that mix scene, video, and web wallpapers, also shared across monitors
- **Thumbnail wallpaper picker** — visual grid with previews, search, and a large scrollbar for long libraries

## Install From GitHub Release

For the easiest path, download a release asset from GitHub:

- `yakkai-plasma6-archlinux-x86_64-<version>.tar.gz` — prebuilt Plasma package with the native scene module already staged
- `yakkai-source-<version>.tar.gz` — source fallback for systems whose native libraries do not match the prebuilt package

Prebuilt release package:

```bash
tar -xf yakkai-plasma6-archlinux-x86_64-<version>.tar.gz
cd yakkai-plasma6-archlinux-x86_64-<version>
./install.sh
```

If the prebuilt package does not load because Qt, KDE Frameworks, FFmpeg, or other native library versions differ on your system, use the source package instead:

```bash
tar -xf yakkai-source-<version>.tar.gz
cd yakkai-source-<version>
./scripts/install-local.sh
```

After installation, open the Plasma wallpaper picker and choose **Yakkai**.

## Install From Source

From a source checkout:

```bash
./scripts/install-local.sh
```

The installer configures CMake in `${XDG_CACHE_HOME:-$HOME/.cache}/yakkai/build`, builds the native scene backend, stages the generated QML import module into `wallpapers/io.team7.yakkai/contents/imports/io/team7/scene/`, validates the package, and installs or updates the wallpaper for the current user.

Use `--clean` if CMake should start from a fresh build directory:

```bash
./scripts/install-local.sh --clean
```

Use `--no-install` to build, stage, and validate the package without touching the installed Plasma package:

```bash
./scripts/install-local.sh --no-install
```

## Developer Build

Use the dev flag when working on the renderer or smoke-test harness:

```bash
./scripts/install-local.sh --dev
```

`--dev` uses the repo-local `./build` directory and also builds the standalone scene harness. This keeps existing tooling such as `tools/validate-scene.sh`, `smoke-tests/run.sh`, and the harness path stable.

```bash
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source /path/to/scene.pkg \
  --assets /path/to/wallpaper_engine/assets \
  --fill crop
```

Use `--scene-properties-json '<json-object>'` to pass a complete Wallpaper Engine scene property object to the harness. Smoke tests generate this object from `scenePropertyOverrides` merged over the scene's `project.json` defaults; direct harness users can omit the flag to use the project defaults. Native SceneScript media-widget debugging can include a reserved `__yakkaiMedia` object in that JSON; this exposes synthetic harness media metadata/progress through SceneScript stubs such as `shared.mi`, `engine.media`, `MediaPlaybackEvent`, and `mediaPropertiesChanged(event)` without reading a live Linux media player:

```bash
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source /path/to/scene.pkg \
  --assets /path/to/wallpaper_engine/assets \
  --fill crop \
  --hide-info-overlay \
  --scene-properties-json '{"timeofday":{"value":"1"},"__yakkaiMedia":{"available":true,"playing":true,"title":"Constant Moderato","artist":"Mitsukiyo","album":"Blue Archive","duration":240,"position":42}}'
```

For media-widget color scripts, `__yakkaiMedia` can also include `textColor`, `primaryColor`, `secondaryColor`, and `tertiaryColor` as WE-style space-separated RGB strings or three-number arrays. The paper backend exposes those through a synthetic `mediaThumbnailChanged(event)` color event and registers `$mediaThumbnail` / `$mediaPreviousThumbnail` textures for native media-widget debugging. If `__yakkaiMedia.albumArtPath` points to a local image, that image is used as the synthetic thumbnail; when explicit thumbnail colors are omitted, the backend derives deterministic thumbnail colors from the album art before SceneScript evaluation, using a muted primary base that excludes only the brightest highlights, a subdued highlight accent for `tertiaryColor` rather than raw brightest pixels, and a blended contrast `textColor` rather than snapping text to pure white or black. If no album art is available, the backend generates a deterministic placeholder from the synthetic media colors. Tests can add `settleSeconds` to advance time-based SceneScript media fades before the final binding sample.
SceneScript `color` property scripts are evaluated from the authored color value, and vector values returned from `update()` are preserved as explicit color bindings, so media thumbnail colors such as black still override non-black wallpaper defaults.

`__yakkaiMedia` does not override wallpaper-authored feature toggles. If a scene gates its media widget behind ordinary user properties, pass those properties alongside the synthetic media payload. For example, Arona's media widget needs its authored `mediaintegration` property enabled in the same `--scene-properties-json` object. Playback callbacks are dispatched for both available and unavailable synthetic media: `available=true, playing=false` maps to `MediaPlaybackEvent.PLAYBACK_PAUSED`, while unavailable media maps to `PLAYBACK_STOPPED`. Metadata, timeline, and thumbnail callbacks remain gated by `available`, so authored widgets can show paused clock fallbacks without inventing metadata for a missing player. Structured scripted fields keep their authored fallback `value` when the native SceneScript evaluator cannot fully replay a runtime-only script, so movable media-widget containers still retain authored transforms such as origin/scale. Timeline-driven solid-layer progress bars keep their script-resolved horizontal origin instead of reapplying authored left/right alignment, then compensate their horizontal origin as playback scale changes so the screen-leading edge stays anchored in mirrored and non-mirrored media-widget subtrees.

In Plasma, native scene mode can also read the active Linux media player through MPRIS over the session DBus. The runtime maps title, artist, album, playback status, track duration, a position snapshot, and local `mpris:artUrl` album art into the same `__yakkaiMedia` object used by the harness. Album art accepts `file://` URLs and absolute local filesystem paths; remote and non-file art URLs are ignored. This path is read-only: Yakkai does not send play/pause/next commands and does not capture real audio-reactive data.

For a live-player smoke check, start an MPRIS-capable player separately and run
the qdbus-backed probe:

```bash
cvlc --extraintf dbus --no-video --quiet native/scene_harness/tests/fixtures/media/instalock.mp3

tools/mpris_live_smoke.py \
  --service org.mpris.MediaPlayer2.vlc \
  --expect-service org.mpris.MediaPlayer2.vlc \
  --expect-status Playing \
  --expect-title Instalock \
  --expect-artist WYLTK \
  --expect-album Instalock \
  --require-local-art \
  --pretty
```

`tools/mpris_live_smoke.py` requires `qdbus6`, reads the current session bus,
prints the normalized live `__yakkaiMedia` JSON, and exits nonzero if no
readable MPRIS player is available or any `--expect-*` assertion fails. It does
not launch, stop, or control media players. By default it mirrors the native
runtime selection policy: prefer a playing provider with metadata, then any
metadata-bearing provider, then fall back to blank providers only for
diagnostics. Pass `--service org.mpris.MediaPlayer2.<name>` when other session
providers such as image viewers are registered and you want to inspect one exact
player.

To inventory the current desktop's provider behavior, run the read-only matrix
wrapper:

```bash
tools/mpris_compat_matrix.py \
  --output-dir tmp/mpris-compat-matrix
```

The matrix always records a Yakkai-like default selection probe using that
metadata-first policy, then exact service probes for all currently registered
MPRIS services unless `--service` is used to limit the list. It writes
`summary.json` and `summary.md` with provider status, metadata availability,
playback state, album-art availability, and classification issues such as
`missing-metadata`, `missing-art`, or `not-playing`. The wrapper is diagnostic
by default; add `--fail-on-issues` only for a deliberately strict local gate.

To avoid restarting the native scene renderer every progress tick, stable MPRIS
metadata still flows through scene properties while live player state flows
through a separate runtime `mediaStateJson` path. Stable metadata changes, such
as a new title, artist, album, album art, playback availability, or playback
state, reload the parsed native scene so generated text and other parse-time
media widgets refresh from the new `__yakkaiMedia` snapshot. Runtime media
updates replay safe authored SceneScript media callbacks for existing image/solid
layers and can update origin, scale, color, alpha, and visibility inside the
already-running scene. Timeline-driven solid progress bars still use their
specialized mirror-aware leading-edge compensation. Position-only updates do not
rewrite `SceneGuard.scenePropertiesJson` or reload the wallpaper. Click controls,
real audio-reactive widgets, texture-animation media widgets, true in-place
generated text texture replacement without scene reload, and the full Wallpaper
Engine media event loop remain deferred.

For repeatable media-widget fixture checks, use the owned MP3 fixture at
`native/scene_harness/tests/fixtures/media/instalock.mp3`. It contains real
title, artist, album, duration, and embedded album-art metadata. Generate
harness-ready JSON with:

```bash
tools/media_fixture_payload.py \
  native/scene_harness/tests/fixtures/media/instalock.mp3 \
  --output-dir tmp/arona-media-instalock \
  --property timeofday=1 \
  --property mediaintegration=1 \
  --position-ratio 0.5 \
  --settle-seconds 1.25
```

The helper requires local `ffprobe` and `ffmpeg`, extracts album art into the
repo-local output directory, and prints the complete scene-property JSON that
can be passed to `--scene-properties-json`. Use `--position-ratio 0.0..1.0` to
match a reference playback fraction across media files with different
durations, or `--position seconds` for an exact synthetic timeline value. Use
`--paused --clock-time HH:MM` for paused clock-widget references, or
`--stopped` for unavailable-player references. It is still synthetic harness
input; it does not read the current Linux media player.

For harness-only live progress checks, keep the stable metadata in
`--scene-properties-json` and pass runtime position keyframes with
`--media-state-timeline-json`. Keyframes are a JSON array with increasing
`timeMs` values. Direct media fields are merged over the stable
`__yakkaiMedia` payload before each runtime `mediaStateJson` update:

```bash
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source /path/to/scene.pkg \
  --assets /path/to/wallpaper_engine/assets \
  --scene-properties-json '{"__yakkaiMedia":{"available":true,"playing":true,"duration":240,"position":0}}' \
  --media-state-timeline-json '[{"timeMs":0,"position":0},{"timeMs":1000,"position":120}]'
```

When combined with `--debug-effect-captures`, the debug manifest records the
normalized timeline under top-level `mediaStateTimeline`.

For broader media-widget coverage, build a local candidate inventory and run the
fixture-backed matrix:

```bash
tools/media_widget_candidate_inventory.py /path/to/Steam/steamapps/workshop/content/431960 --json \
  > tmp/media-widget-inventory.json

xvfb-run -a -s '-screen 0 1600x900x24' tools/media_widget_matrix.py \
  --inventory tmp/media-widget-inventory.json \
  --assets /path/to/wallpaper_engine/assets \
  --scene-id 3228578419 \
  --output-dir tmp/media-widget-matrix/visual \
  --window-size 1600x900 \
  --capture-delay-ms 10000
```

`tools/media_widget_candidate_inventory.py` classifies candidates as
`metadata-widget`, `property-only-media`, or `audio-reactive` from project
properties and any unpacked scene script signatures it can see. Packaged
`scene.pkg` content often classifies as `property-only-media` because the script
body is not visible during inventory, so treat the output as a candidate list,
not proof that a wallpaper has a supported metadata widget. The matrix runner
adds the `Instalock.mp3` synthetic media payload, applies conservative authored
media toggles from the inventory, writes per-scene captures/logs/results under
the requested repo-local output directory, and records `summary.json` plus
`summary.md`. Add `--debug` for generated-text diagnostics when a candidate
needs media text crops; debug-effect capture failures can make that run fail
even when the visual capture itself is usable.

For Arona fixture-backed media parity, region fixtures may include nested
`sampleRects` for localized album-frame, progress-line, and background-tint
checks. The comparator writes those sample metrics to both `summary.json` and
`summary.md` so visual review can start from the concrete drift rows instead of
only the top-level crop score.

For authored-script debugging of a specific media widget, use
`tools/media_widget_script_audit.py` against an unpacked `scene.json`. It
extracts script-bearing layer fields, callback usage, runtime API references,
and side effects so fixes can start from Wallpaper Engine's authored behavior
instead of from final-frame guesswork:

```bash
tools/media_widget_script_audit.py /path/to/scene/scene.json \
  --json tmp/media-widget-script-audit/audit.json \
  --markdown tmp/media-widget-script-audit/audit.md
```

For visual parity work against Windows Wallpaper Engine media-widget captures,
use `tools/media_widget_parity_compare.py` after generating or receiving a
same-sized Windows/Yakkai crop pair. The comparator writes a contact sheet,
whole-crop metrics, per-region metrics, and generated-text diagnostics summary
under repo-local `tmp/`. Region JSON entries can opt into bright feature-bound
detection to quantify visible text or highlight footprint drift in addition to
RMSE/mean-color metrics. Add `--dominant-mismatch` and `--review-note` after a
human visual pass to record the current classification in the generated report.
When producing pixel-sensitive Yakkai reference captures, pass `--frameless` to
the scene harness so native window decorations do not reduce the rendered and
captured content surface. For monitor-sized captures on Wayland/KWin, use
`--fullscreen`; otherwise the compositor can constrain the harness to the
available work area and silently change scene output size.
If the two crops appear to be the same scale but offset by a few pixels, add
`--align-search-radius <pixels>` to report the best integer Yakkai translation
and write `yakkai-candidate-aligned.png`; treat that as overlap evidence only,
not a substitute for matching the original render resolution and crop window.
This is a debugging/reporting tool; it does not imply that the synthetic
`__yakkaiMedia` harness path reads a live desktop player.

For Arona's collected Windows media-widget references, use the exact-size
fixture wrapper instead of hand-maintaining crop windows:

```bash
tools/arona_media_parity_fixture.py \
  --output-dir tmp/arona-media-tightening/exact-fixture \
  --candidate-frame tmp/arona-media-tightening/fullscreen-1440/capture.png \
  --compare \
  --reference duringPlayback
```

The fixture lives at
`native/scene_harness/tests/fixtures/media/arona_media_parity.json` and records
true `2560x1440` crop windows for `duringPlayback`, `almostEndPlayback`, and
`noPlaybackClock`. Repeat `--reference` to compare only the references that
match the synthetic playback state in the candidate frame; for example,
compare `noPlaybackClock` only against an available-but-not-playing capture.
When matching a Windows clock reference, synthetic media payloads may include
`"clockTime": "HH:MM"` or `clockEpochMs` under `__yakkaiMedia`; this freezes
SceneScript `new Date()` for clock widgets while leaving timer/progress
settling based on the harness time. The clock override is a fixture/debug aid,
not live desktop-player behavior.

The harness also has `video` and `web` backends for plain Wallpaper Engine video projects and web projects. Web candidate manifests can pass harness-only flags through `harnessArgs`, including `--debug-synthetic-audio` for synthetic audio-reactive motion coverage and `--capture-exit-mode immediate` for QtWebEngine capture sequences that finish writing frames but crash during browser teardown. The synthetic audio path exercises web visualizers but does not capture real Linux audio, and audio visualizer baselines may need wider RMSE thresholds because browser animation timing can legitimately change the exact bar shape between runs. Web candidate promotion requires clean smoke-runner exit evidence and human-reviewed clips because web projects can depend on browser timing, media codecs, and input APIs.

Add `--capture smoke-tests/artifacts/tmp/output.png --capture-delay-ms 10000` for automated testing.
Capture timers start only after the scene backend reports its first rendered
frame, so the delay is measured from the first real scene frame rather than
from QML window creation. This avoids saving the harness loader screen when a
scene needs extra time to compile shaders or build its render graph.

### Native Renderer Source Layout

The native Wallpaper Engine scene renderer lives under `native/scene_backend/vendor/upstream_debug/src/`.
Recent renderer-risk code is split by responsibility:

- `Shader/ShaderCompatPatches.*` owns named shader preprocessing patches, including puppet displaced-effect source-alpha preservation for direct shake passes. Windows pass-boundary evidence showed `waterwaves` displaces alpha normally, so waterwaves does not receive that compatibility combo.
- `Scene/PuppetEffectRoutePlan.*` computes pure puppet effect route decisions without allocating scene nodes or materials.
- `Scene/PuppetFinalDisplayBuilder.*` builds standalone puppet final-display nodes, materials, meshes, and debug final-display boundary metadata.
- `Puppet/PuppetSimulation.*` owns generic runtime puppet simulation eligibility and per-frame mechanics. The pass remains off by default and is only enabled through harness/validator debug options.
- `Policy/MediaIntegrationPolicy.*` classifies native SceneScript media-integration utility layers. Metadata/progress widgets can render through synthetic `__yakkaiMedia`, including common metadata callbacks such as `mediaPropertiesChanged(event)` and `mediaThumbnailChanged(event)`. The SceneScript runtime seeds common Workshop media-widget shared defaults for settings state, text/card placement, clock placement, and media text background colors so cross-layer media widgets do not collapse before their settings scripts run; color property scripts are seeded from authored color values before thumbnail events are applied, returned `Vec3` values from `update()` are preserved as color bindings, album art derives thumbnail event colors when explicit media colors are omitted, and non-finite `Vec3.mix` amounts are coerced to finite endpoints to avoid NaN render state from zero-width scripted transitions. Synthetic playback maps available-but-not-playing media to `PLAYBACK_PAUSED` and unavailable media to `PLAYBACK_STOPPED`. Supported media-widget subtrees keep authored/script-active container transforms so generated text keeps its authored placement, while hidden template variants remain hidden unless authored properties or scripts activate them. Supported media-widget utility/effect carriers, including workshop model album-art carriers that bind `$mediaThumbnail`, keep their authored tint/opacity/blendgradient chains instead of inheriting the puppet-scene alpha-strip fallback. Runtime media updates can replay safe authored callbacks on existing image/solid nodes to update origin, scale, color, alpha, and visibility without reparsing the scene; timeline-driven solid-layer progress bars preserve their script-resolved horizontal origin while ordinary aligned solid layers still apply authored horizontal alignment, and progress bars apply mirror-aware leading-edge compensation so timeline scale changes do not shrink the bar from its center. Generated text renders through Qt's font rasterizer when available, including authored font files, alignment, WE-style point-size scaling from the scene canvas height after 100-DPI point-to-pixel conversion, color, alpha, horizontal/vertical text-card edge anchoring, mirror-aware text placement based on the final effective mirror state, and resolved `maxwidth` raster surfaces that can be wider than the authored card so long media titles are not clipped, with the CPU bitmap renderer kept as a fallback. SceneScript layer lookups keep authored text-card sizes instead of replacing `thisScene.getLayer(...).size` with generated glyph alpha bounds. Synthetic `$mediaThumbnail` textures can be supplied through `albumArtPath` or generated from synthetic media colors; derived thumbnail `textColor` blends the album average toward the contrast color instead of hard white/black. Synthetic media can also pin clock widgets with `clockTime` or `clockEpochMs` for Windows-reference fixture comparisons; `new Date()` uses that fixed clock value while `Date.now()` continues to include harness elapsed time for settling scripts. Read-only Linux MPRIS metadata integration is available for native SceneScript media widgets.
  Click-driven player controls, native audio-reactive bars, texture-animation media widgets, exact rich text layout parity, generated text reraster from changing metadata, and unsupported object APIs remain deferred diagnostics. Generated-text debug manifests include authored font, resolved rasterizer, font-load status, resolved font family, point size, effective pixel size, authored card size, resolved texture size, and measured alpha bounds fields for parity triage.

When validating renderer-linkage issues, use a scratch build outside `./build`, point a smoke manifest at that scratch harness, and disable the QML disk cache. This proves the run is using the freshly built native renderer instead of repo-local build artifacts.

For effect-chain debugging, the paper backend can write intermediate render-target captures and a manifest:

```bash
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source /path/to/scene.pkg \
  --assets /path/to/wallpaper_engine/assets \
  --hide-info-overlay \
  --capture smoke-tests/artifacts/tmp/yakkai-effect-debug/final.png \
  --capture-delay-ms 8000 \
  --debug-effect-capture-delay-ms 8000 \
  --debug-effect-captures smoke-tests/artifacts/tmp/yakkai-effect-debug \
  --debug-effect-capture-layers 405
```

Use `smoke-tests/artifacts/tmp/` for large local debug captures and comparator
outputs. That directory is ignored and avoids filling the system `/tmp` with
hundreds of megabytes of TGA/PNG evidence.

`--debug-effect-captures` is harness-only and off by default. `--debug-effect-capture-delay-ms` can delay the TGA dumps until a requested scene time so effect captures line up with a delayed PNG or sequence frame; the manifest records this as `captureDelayMs`. `--debug-effect-capture-layers 405,239` filters normal capture records to those layer IDs and records the filter as top-level `captureLayerIds`; it reduces heavy TGA output but does not force, probe, strip, or otherwise change render policy. The manifest also records `shaderTimeSeconds`, `frameTimeSeconds`, and `effectiveCaptureTimeSeconds` so CPU shader oracles can use the exact `g_Time` value from the dumped frame instead of assuming the requested delay is the shader time. It writes `manifest.json` plus `effect-input`, `effect-output`, and `final-publish` TGA captures for preserved effect-chain layers. Each capture entry includes `captureIndex`, a zero-based manifest order marker for diagnostics that need stable capture progression. Rendered effect entries include diagnostic classification fields such as `candidateFamilies`, `candidateMixFamilies`, `candidateRisk`, `candidateChainShape`, `candidateEffectClass`, `candidateBlockedReason`, and `candidateChecks` when the policy can classify the layer. `candidateEffectClass` gives high-risk reports a stable grouping key, including `regular-blur-only`, `utility-blur`, `regular-lut-only`, `protected-puppet-lut`, `mixed-puppet-lut`, `composelayer-color-grade`, and water-carrier diagnostics such as `composelayer-water-only` and `utility-water-only`. `candidateChecks` also flags high-risk stripped families with `hasBlurFamily`, `hasLutFamily`, and `hasColorGradingFamily`. Preserved effect layers also include `effectMaterials` diagnostics with material shader names, authored/resolved texture slots, authored/resolved combos, material values, resolved shader constant values, generated defines, final-material routing, and a `publish` block with target, blend, parent, transform, mesh route, final display route, route risk, and capture-timing metadata. The `publish` block also records final-publish composition evidence: effect-input and standalone-display local transforms, standalone final source texture, standalone final material blend, display node ordinal, input/final mesh bounds, effect-input viewport size and whether it expanded to mesh bounds, and `puppetCutoutSlotCoverage` for puppet layers. Non-fullscreen composelayers keep their authored layer transform and publish through `effect-layer-node-final-publish`; fullscreen utility layers still use fullscreen final publish. Each puppet slot records bone/parent names, primary and weighted vertex/triangle coverage, secondary-only weighted slots, weighted layer-local bounds/centroid, raw simulation metadata, parsed simulation target point/mass fields, whether the metadata contains active physics controls, and a `simulatedInactive` marker for active-physics bones that are currently inactive. Target point/mass metadata alone is reported as tip metadata and does not mark a slot as simulated inactive. Standalone final-display nodes also register `final-display-before` and `final-display-after` captures by inserting debug-only render-graph copies around the tagged node; effect-layer node final-publish passes now register the same boundary around the actual final material node, with timing `render-graph-copy-around-effect-layer-final-publish-node`. Matching publish fields record `finalDisplayBoundaryCaptureTiming`, `finalDisplayBeforeRenderTarget`, and `finalDisplayAfterRenderTarget`. Puppet layers add `puppetAnimationLayers` entries with animation id/name, blend, rate, visibility, paused state, additive state, current time, animation match state, active weighting state, and active bone slot ids; the same state is summarized in top-level `puppetAnimationLayerInventory`. For preserved effect materials with non-empty shader names, the debug path registers `material-output-<effectIndex>-<materialIndex>` captures around the material stage. When a LUT material is also the final published pass, it additionally registers `material-output-local-<effectIndex>-<materialIndex>` from a duplicate layer-local diagnostic pass before final publish, while `material-output-<effectIndex>-<materialIndex>` remains the screen-sized final-publish material capture. The debug path also registers `default-before-effect` and `default-after-effect` captures around preserved effect layers by copying `_rt_default` into non-reused diagnostic render targets. `final-publish` is a post-frame render-target dump of `_rt_default`, not a per-layer final-node capture; prefer `final-display-before/after` when present for isolated final-display contribution evidence. Mismatched debug-copy extents are clamped to the overlapping region instead of aborting the renderer. These fields are diagnostic metadata only and do not imply that an effect chain is safe to render. The manifest also includes a top-level `strippedCandidates` array for effect chains that policy removed before render graph construction; these entries are metadata only and do not represent failed dumps. Protected puppet crop-sheet chains that remain stripped are additionally copied into `protectedPuppetDiagnostics` with `captureMode=metadata-only`, alpha evidence, authored effect order, authored material constants, pass order, final-publish routing, and puppet animation layer state so they can be inspected without creating normal capture records or modifying `_rt_default`. Simple isolated water candidates (`waterflow`, `waterripple`, and isolated `waterwaves`), composelayer water-only carriers classified as `composelayer-water-only`, regular image-layer `lut-only` chains, regular non-carrier `blur-only` chains, fullscreen utility blur carriers classified as `utility-blur`, composelayer color-grade carriers classified as `composelayer-color-grade`, and protected puppet crop-sheet chains made only from recognized water/LUT/pulse/shake families can be preserved. Water-only utility/fullscreen carrier classes remain diagnostic/probe-only unless a later slice adds a narrow production predicate. Mixed blur/color-grading chains outside the strict composelayer predicate, unknown families, non-blur utility carriers, fullscreen layers outside the strict `utility-blur` class, protected blur paths, and generic puppet paths remain stripped or probe-only. These captures are run artifacts for investigation; PNG smoke baselines remain the committed source of truth.

For generated SceneScript/media text debugging, debug manifests also include
top-level `sceneOrtho` and `generatedTextDiagnostics`. Each text diagnostic
records resolved text, generated texture name, rasterizer, authored font path,
font-load status, resolved font family, alignment fields, authored point size,
effective canvas-scaled pixel size, parent chain, text-card size, local/world
bounds, glyph alpha bounds, and a visibility classification. The
crop helper maps those bounds as Wallpaper Engine scene coordinates with X from
the left edge and Y from the bottom edge. To crop the final PNG around those
regions and create a review contact sheet:

```bash
tools/media_text_diagnostics.py \
  --manifest smoke-tests/artifacts/tmp/yakkai-effect-debug/manifest.json \
  --capture smoke-tests/artifacts/tmp/yakkai-effect-debug/final.png \
  --output-dir smoke-tests/artifacts/tmp/yakkai-effect-debug/media-text
```

Narrow non-protected puppet water chains can also be preserved as `puppet-water-effect` when the layer has a water family and every non-water mix family is one of `opacity`, `shine`, or `iris`. This route keeps the puppet render in a layer-local offscreen target, expands that target to the generated puppet mesh bounds when animated geometry extends outside the nominal object rectangle, preserves the layer's blend mode while composing overlapping puppet cutouts into that target, and publishes the result through the standalone puppet final-display path. Offscreen puppet effect targets are composited back into the scene with a premultiplied final-display blend so semi-transparent puppet edges are not multiplied by alpha twice. Generic puppet chains outside this predicate, plus audio/lightshaft, blur, LUT, color-grade, fullscreen/utility, protected blur, or unknown-family mixes, remain stripped or diagnostic-only.

Puppet channelmap and filtered-overlay routes expand active animation bone slots to include secondary-only weighted descendant slots. This keeps child cutouts that have no primary-owned geometry, such as weighted ribbon or hand pieces, in the same active mask as their animated parent bone without adding wallpaper-specific logic.

Drawable scene nodes with no authored `parallaxDepth`, or an authored zero vector, inherit the nearest parent transform/container `parallaxDepth` during scene parsing. This makes transform-only parallax anchors affect their rendered child images and particles through the existing shader model-matrix path, matching Wallpaper Engine scenes that group visible layers under parallax transform anchors.

Deferred non-channelmap puppet effect routes apply inherited parallax when rendering the effect input into the layer-local target, then publish the final puppet-skinned material back to `_rt_default` with zero additional parallax. This keeps protected crop-sheet puppets aligned with sibling desk/chair layers under the same parent transform instead of moving twice as far during mouse/camera parallax.

For investigation only, combine `--debug-effect-captures` with `--debug-effect-probe-layers 168,22` to render specific eligible stripped layers and dump their usual effect captures. Eligible explicit probes currently include puppet/protected-puppet mixed chains and water-only carrier diagnostics such as `composelayer-water-only`; use `--debug-effect-probe-high-risk-layers 53,155` for explicit stripped blur/LUT/color-grading candidates. Add `--debug-effect-probe-max-effects N` to prefix-slice a forced probe layer to its first `N` visible effects while isolating which effect introduces drift. Probe flags are rejected without `--debug-effect-captures`, the prefix limit requires a probe layer list, and all of these options are available only through the harness. They do not change normal Plasma wallpaper behavior. `--debug-layer-visibility-overrides '306:true,240:false'` is also harness-only and requires `--debug-effect-captures`; it changes scene-layer `visible` flags before render-graph construction so disabled or user-gated layers can be tested one at a time without changing production policy. `--debug-puppet-animation-layer-overrides 'layerId:animationId:key=value[,key=value];...'` is also harness-only and requires `--debug-effect-captures`; supported keys are `visible`, `paused`, `additive`, `blend`, `rate`, and `curTime`. The manifest records the configured `probeLayerIds`, `highRiskProbeLayerIds`, optional `probeMaxEffects`, `layerVisibilityOverrides`, `puppetAnimationLayerOverrides`, and per-layer `debugProbe`/`debugLayerVisibilityOverride` metadata, including original and kept visible effect counts for sliced probes and original visibility for visibility probes, so probe artifacts are visibly distinct from default policy output. The old `--debug-effect-probe-channelmap-slots` route is quarantined: it forced derived sidecar channelmaps into non-channelmap puppet layers and produced glitchy crop-sheet fragments before effects ran. Probe final frames are diagnostic artifacts; a successful probe capture is not evidence that the layer is safe to re-enable.

Add `--debug-puppet-effect-route-only` with an explicit probe layer list to keep the puppet offscreen/final-display route active while dropping all visible effects; this separates route/composition bugs from shader bugs. Prefix and route-only probe limiting work for explicitly requested production-allowed layers as well as stripped candidates, so a layer can still be sliced after it graduates into normal policy. The manifest records top-level `puppetEffectRouteOnly` and per-layer `debugProbe.routeOnly`.

For investigation-only puppet secondary-motion work, pass `--puppet-simulation runtime` to the scene harness or `tools/validate-scene.sh`. The validator also accepts `YAKKAI_PUPPET_SIMULATION=runtime` and translates it into the harness option, but the explicit flag is preferred for repeatable local validation. This enables a generic post-animation simulation pass for secondary bones whose parsed metadata includes active physics controls after authored animation evaluation and before skinning. The default is off, Plasma behavior is unchanged unless the environment variable or harness option is set, and default-on behavior requires separate visual/regression approval.


Summarize a capture manifest before comparing images:

```bash
tools/effect-capture-summary.py smoke-tests/artifacts/tmp/yakkai-effect-debug/manifest.json
```

`tools/effect-capture-summary.py` reports stripped-candidate risk, chain-shape, family, and mix-family counts so mixed water chains can be triaged before enabling any new effect behavior. It also prints `stripped-high-risk-*` sections for blur, LUT, and color-grading candidates so Arona/background parity work can start from exact layer and shader evidence. For LUT/color-grade entries it prints `lut-color-class-counts`, `lut-color-disposition-counts`, and per-layer class/disposition rows, separating allowed, stripped, probe-only, and protected paths. For protected puppet rows it prints `protected-puppet-cutout-inventory` with active animation ids, active bone slots, and per-slot bone/parent names, primary coverage, weighted coverage, secondary-only markers, parsed simulation target/mass values, active-physics markers, and simulated-inactive markers. For broader high-risk probe runs it also prints `high-risk-disposition-counts`, `high-risk-shape-counts`, and de-duplicated `high-risk-probe-layers` rows so a layer rendered only by `--debug-effect-probe-high-risk-layers` is reported once as `probe-only`.

To compare effect candidates across multiple manifests before choosing the next generic allow predicate:

```bash
tools/effect-candidate-inventory.py \
  smoke-tests/artifacts/tmp/yakkai-debug/effect-captures-3228578419/manifest.json \
  smoke-tests/artifacts/tmp/yakkai-debug/effect-captures-3476236738/manifest.json
```

`tools/effect-candidate-inventory.py` normalizes allowed, stripped, protected, and probe-only records into class-level evidence. Use it to group candidates by `candidateEffectClass`, chain shape, disposition, carrier flags, active puppet slots, and final-publish route before writing a production policy predicate. It also infers stable water-only carrier classes such as `composelayer-water-only` and `utility-water-only` from older manifests that predate explicit `candidateEffectClass` fields. It reports `route-audits` for classes with explicit route evidence; currently `regular-lut-only` records are classified as `route-complete`, `route-incomplete`, or `missing-local-material-output-capture` based on final-publish route metadata and local/material output capture coverage. It also reports `visual-gate-audits` for known high-risk classes; currently `composelayer-color-grade`, `utility-blur`, and `composelayer-water-only` records are classified as `production-allowed`, `needs-high-risk-probe-route`, `incomplete-visual-gate-evidence`, or `human-visual-review-required` so production captures, stripped baselines, and structurally valid probe routes are separated before a human visual gate. Add `--json` when a machine-readable inventory is needed for local analysis.

`tools/validate-scene.sh` clears `~/.cache/wescene-renderer/*/spvs01/` before each harness render and writes a debug effect manifest during validation. Validator artifacts are written to `smoke-tests/artifacts/tmp/yakkai-debug` by default; set `YAKKAI_VALIDATE_OUTDIR=/path/to/output` to override. If the Qt harness exits 134 before writing a log, the validator now records a display-access diagnostic in the log; in sandboxed agent runs, rerun with unsandboxed GUI/display access before treating that as a render failure. It also runs `tools/effect_route_guards.py`, a generic structural guard that fails when a puppet effect input mesh exceeds the recorded layer-local effect viewport by a meaningful amount. Tiny mesh bleed is tolerated so authored waterwave/shake mask coordinates stay in the original layer effect-space, while large overflow still requires an expanded viewport. For the renderer-risk fixtures, it fails if `3476236738` has no allowed simple-water candidates, if any simple-water candidate remains stripped there, if the `3476236738` visual sentinel detects clear-color leakage or a missing extended-hand foreground region, or if `3228578419` gains any allowed simple-water candidates. That sentinel is a regression guard for the layer-local effect source fix that keeps large parented background effect inputs from being cropped to clear color and the puppet effect viewport fix that keeps animated puppet geometry from being clipped outside the object rectangle. Add `--probe-layers 239` to run a second harness pass that forces explicitly eligible stripped layers through `--debug-effect-probe-layers`; add `--probe-high-risk-layers 137` for the blur/LUT/color-grade-only probe path. Add `--probe-max-effects N` with either probe mode to limit each forced probe layer to the first `N` visible effects, which is useful for finding the first effect in a mixed chain that causes drift. The validator reports baseline-vs-probe RMSE and, for scenes with configured sentinels, per-region mean/stddev/unique-color deltas.

For harness-only mouse/parallax diagnostics, run:

```bash
tools/arona_mouse_parallax_probe.py \
  --scene-id 3228578419 \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --harness build/native/scene_harness/yakkai_scene_harness \
  --capture-delay-ms 10000 \
  --scene-properties-json '{"timeofday":{"value":"1"}}'
```

The probe writes left/center/right captures, effect manifests, `contact-sheet.png`, `report.json`, and `report.md` under `smoke-tests/artifacts/tmp/arona-mouse-parallax-probe`. It uses the harness-only `--debug-mouse-position x,y` flag, which requires `--debug-effect-captures` and records top-level `mouseParallax` manifest diagnostics without changing Plasma wallpaper runtime mouse behavior. Pin Arona `timeofday` while testing parallax so time-of-day color changes do not pollute visible-motion registration. Motion-region registration uses a bounded downsampled search for large captures, then scores the winning offset and zero-shift baseline at full resolution; each row records `zeroShiftRmse`, `searchScale`, and `searchedMaxShift` so sweep timing and resolution effects are inspectable. For in-process motion probes, the harness-only `--debug-mouse-timeline timeMs:x,y;timeMs:x,y` flag drives an interpolated synthetic mouse path for the `paper` backend, requires `--debug-effect-captures`, cannot be combined with `--debug-mouse-position`, and records the requested timeline plus capture-time elapsed timeline milliseconds in `mouseParallax`.

For manual live pointer review in the harness, run:

```bash
build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --scene-properties-json '{"timeofday":{"value":"1"}}' \
  --interactive-mouse
```

`--interactive-mouse` enables live hover/pointer input without requiring debug captures and cannot be combined with `--debug-mouse-position` or `--debug-mouse-timeline`. When used with `--debug-effect-captures`, the paper backend records `mouseParallax.inputSource: "interactive"` and the renderer-sampled effective mouse position in the manifest. Use this for quick human checks of parallax feel after synthetic still/timeline probes pass.

Mouse parallax smoothing treats unchanged pointer coordinates as the same input event, and changed pointer coordinates update the target without rewinding the current delay progress. Holding the pointer still and swiping across the screen both let the authored `cameraparallaxdelay` settle instead of restarting the delay every frame.

For strict local preflight, add `--gate`. The gate exits with status `3` unless every requested size classifies as `movement-detected`, center expected offsets stay at or below `0.01`, at least one layer has opposite-sign left/right expected offsets, Arona reports at least 17 parallax layers, anchor propagation is explicitly reported as either `anchor-propagation-evidence-present` or `anchor-child-map-missing`, every expected motion-region row is present with a known classification, no motion region reports true `same-direction-motion`, and large expected transform-anchor offsets also produce visible opposite-sign translation of at least 10% of the expected anchor offset. One-sided detail-region motion is reported separately and allowed when the core visible-translation gate passes. Combine `--gate` with `--window-sizes 1600x900,1920x1080,2560x1440` to gate a resolution/aspect sweep. This is a structural and still-motion preflight only; it does not replace human review of `tools/arona_mouse_parallax_motion_probe.py` MP4/contact-sheet artifacts.

For Plasma-live coordinate checks, Yakkai has a hidden wallpaper config key named `WESceneMouseDiagnosticsEnabled`. It defaults to `false`, has no settings-panel toggle, and only logs when the normal scene mouse input option is also enabled. To enable it for one wallpaper containment:

```bash
kwriteconfig6 --file plasma-org.kde.plasma.desktop-appletsrc \
  --group Containments --group <containment-id> \
  --group Wallpaper --group io.team7.yakkai --group General \
  --key WESceneMouseDiagnosticsEnabled true
```

Disable it with the same command and `false`. After reloading the wallpaper, read the diagnostics with:

```bash
journalctl --user -f | rg "Yakkai.*mouse-diagnostic"
```

Expected lines include QML-side item-local coordinates such as `mouse-diagnostic screen=<name> local=512.000,384.000 normalized=0.500,0.500 itemSize=1024x768` and backend snapshots such as `mouse-diagnostic backend-normalized=0.502,0.491 source=hover`. The logger is diagnostic-only and throttled to avoid per-frame output.

For timeline motion review, run:

```bash
tools/arona_mouse_parallax_motion_probe.py \
  --scene-id 3228578419 \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --harness build/native/scene_harness/yakkai_scene_harness \
  --scene-properties-json '{"timeofday":{"value":"1"}}'
```

The motion probe writes deterministic PNG timeline frames, effect-capture diagnostics, a live `review-live.mp4`, `review-contact-sheet.png`, `motion-report.json`, and `motion-report.md` under `smoke-tests/artifacts/tmp/arona-mouse-parallax-motion`. Use `--skip-record` when only the PNG/effect-capture sequence is needed.

For puppet effect-route isolation, add `--probe-puppet-route-only` with `--probe-layers`; this forwards the harness route-only probe and keeps the puppet route active with zero visible effects. Non-channelmap puppet effects normally use the atlas-first deferred puppet-final route, so `--probe-puppet-final-mesh layer-card|image-space` is only for forcing legacy final-display diagnostics. Probe limiting now applies to explicitly requested allowed layers as well as stripped candidates, so scene `3476236738` layers `168` and `22` can be route-only or prefix-sliced even though their normal policy reason is `puppet-water-effect`.

For high-risk visual classes, automated validator and inventory output are
preflight gates, not final parity approval. Crashes, shader failures, missing
captures, invalid generic routes, and scene sentinels can block a change.
Broad RMSE or frame-delta changes in authored blur, color grading, composelayer,
fullscreen utility, or puppet/crop-sheet effects instead mark the artifact for
human visual review. Record the approval or rejection in
`docs/renderer-effect-predicate-decisions.md` before widening a production
predicate.

### Manual Build

The normal build target for installable packages is `yakkai_stage_wallyakkai_scene_import`:

```bash
cmake -S . -B build
cmake --build build --target yakkai_stage_wallyakkai_scene_import --parallel
./scripts/check-package.sh
```

Then install or update the staged package:

```bash
# First install
kpackagetool6 -t Plasma/Wallpaper -i wallpapers/io.team7.yakkai

# Subsequent updates
kpackagetool6 -t Plasma/Wallpaper -u wallpapers/io.team7.yakkai
```

## Dependencies

Yakkai requires KDE Plasma 6, Qt 6.6 or newer with Core/Gui/Qml/Quick/DBus/WebEngineQuick, CMake 3.24 or newer, a C++20 compiler, Vulkan development files, and liblz4. Qt Multimedia and Qt WebEngine runtime packages are needed for video and web wallpaper modes. FFmpeg development packages enable Wallpaper Engine video textures in native scene rendering when available.

On distro packages, look for the Qt Quick/QML development packages, Qt DBus, Qt Multimedia, Qt WebEngine Quick, `extra-cmake-modules`, `vulkan-headers`/`vulkan-loader`, `lz4`, and FFmpeg libraries (`libavformat`, `libavcodec`, `libswscale`, `libavutil`). The Python scanner used by the wallpaper package needs Python 3.

## Package Validation

```bash
./scripts/check-package.sh
```

The package check verifies required Plasma files, verifies the generated native QML import files are staged, runs `qmllint` when available, and performs a throwaway `kpackagetool6` install. Use `--skip-kpackage` when only the file and QML checks are needed.

### Render Regression Checks

Use the local smoke-test harness before renderer-risk changes and before releases:

```bash
./smoke-tests/run.sh --coverage
./smoke-tests/run.sh --suite quick
./smoke-tests/run.sh --suite deep
./smoke-tests/run.sh --suite release --strict --require-assets
```

The quick suite is for normal development loops. The deep suite is required for changes affecting pixels, scene loading, animation timing, video textures, shader preprocessing, effect policy, blend/composition, model/material/light behavior, SceneScript, QML render plumbing, harness capture behavior, or validator behavior.
The release suite contains the currently baselined required scenes. Add optional deep candidates only after reviewing the local asset and promoting baselines.

Use the coverage command before renderer phase work to confirm the limitation has an active baseline or candidate fixture. It performs no rendering and does not promote candidates into active smoke suites.

`tools/validate-scene.sh <scene_id>` also summarizes SceneScript/runtime gaps from the harness log. Non-zero SceneScript gap totals are reported as validator warnings; normal visible property bindings are included in the detail string but do not warn by themselves. Script gaps on layers suppressed as deferred media integration stay grouped as media-runtime-only; metadata/progress media-widget layers that can run through `__yakkaiMedia` are allowed to render instead of being hidden solely for mentioning media APIs. SceneScript summaries preserve the unique layer binding count and also count binding properties separately; generated text bindings appear under `text` when `QuickJS binding: ... text=...` is present, separate from transform, color, and alpha bindings. Generated text objects are represented structurally in the scene graph with their script-resolved origin/scale/color and authored text-card alignment. For per-layer API detail after a validation run:

```bash
tools/scene-script-log-summary.py smoke-tests/artifacts/tmp/yakkai-debug/validate-3326873240.log
```

The harness compares deterministic PNG captures against versioned baselines and writes review artifacts to `smoke-tests/artifacts/tmp/yakkai-smoke` by default. Capture schedules are relative to the backend readiness signal, not process startup, and the smoke runner budgets for that readiness wait so cold shader-cache renders can complete before capture timers fire. Smoke-test captures hide the local harness info overlay; generated review videos are artifacts for human animation review, either encoded from PNG sequences at the configured frame interval or recorded live by piping repeated harness window grabs to `ffmpeg`. PNG frames remain the source of truth.

Scenes in `smoke-tests/scenes.json` can define variants that inherit shared capture schedules, thresholds, features, expectations, and source paths from a base template. Each variant uses `baselinePrefix` to keep inherited PNG baselines under a variant-specific directory. `scenePropertyOverrides` contains raw Wallpaper Engine user-property values; the smoke runner merges them over `project.json` defaults for scene and web projects and passes compact `scenePropertiesJson` to the harness. Candidate manifests can use `harnessArgs` for narrow harness debug flags such as synthetic web audio. Arona is split into deterministic Day, Sunset, and Night variants so LUT-related rendering changes are tested without depending on the local clock; the Night variant carries a slightly wider still review threshold for observed animated-particle timing drift while its motion sequence still uses temporal matching. Spider-Verse `1591277437` is a deep-only still fixture for godrays, shake, pulse, and stale final-presentation artifact replay. The Elaina `3326873240` candidate is split into deep-only Morning, Day, Dusk, Night, and Day Night Gradient variants for SceneScript/time-mode review, with a slightly wider review threshold for expected animated sky/video phase drift between otherwise valid still captures. Girl and Fluorescent Beach `2788691565` is a deep-only sequence fixture for overlay video texture, water-effect motion, and particle coverage. Cyber City Parkour `1576514332` is a deep-only still fixture for static model, material, lighting, composelayer, and particle sprite coverage. Rain Drops `779812076`, Audio Visualizer `893418273`, and TIMDRIFT II `874499201` are deep-only sequence fixtures for WE web-video/canvas animation, WE Web synthetic-audio visualization, and plain WE Video motion coverage.

Local Windows Wallpaper Engine reference capture dumps can live at the repo root under `yakkai_arona/` for detailed Arona time-of-day/effect evidence and `yakkai-reference/` or `yakkai-references/` for broader library references. These directories are ignored and should stay local; committed smoke baselines remain under `smoke-tests/baselines/`.

For Arona LUT/effect debugging against Windows Wallpaper Engine still captures, place local ignored references under `yakkai_arona/day/still.png`, `yakkai_arona/sunset/still.png`, and `yakkai_arona/night/still.png`, then run `tools/compare-arona-reference.sh`. Use `tools/compare-arona-reference.sh --debug-effect-captures` when investigating LUT/color-grade parity; this writes each variant's harness `effect-captures/manifest.json` beside its render artifacts. Add `--debug-effect-capture-delay-ms <ms>` when the TGA effect captures need to line up with a delayed screenshot or a known Windows `g_Time`; if the debug delay is later than `--capture-delay-ms`, raise `--capture-delay-ms` too so the harness does not exit before the TGA dump. The comparator also writes a registered diagnostic by default: it scans a bounded Yakkai scale/offset grid against the normalized Windows reference, selects the transform with the lowest downsampled grayscale/contrast structure RMSE, and then records both `registeredRmse` for the final RGB comparison and `registrationStructureRmse` for the selection score. It also records `registrationMetric`, `registrationScale`, `registrationOffsetX`, `registrationOffsetY`, and `registrationRmseImprovement`. Use `--skip-registration` when only the raw same-framing comparison is needed. Heavy debug-effect probes can raise the per-variant timeout with `--harness-timeout-extra-seconds <seconds>`; the default extra budget is `30` seconds beyond `--capture-delay-ms`. Summarize those manifests with:

```bash
RUN_DIR="$(ls -td /tmp/yakkai-arona-reference/* | head -1)"
python3 tools/arona_lut_parity_report.py "$RUN_DIR/summary.json"
```

The report ranks the highest-drift time-of-day variant and lists preserved or stripped LUT/color-grade layers from the debug manifests. Each row includes a class/disposition pair so regular `lut-only`, protected puppet LUT, mixed puppet LUT, and composelayer color-grade paths can be counted separately without changing render policy. Preserved LUT rows include the layer image, effect names, material shader paths, capture-stage render-target sizes, texture bindings, material values, resolved shader constants, and generated defines when a fresh debug manifest exposes `effectMaterials`; `missingEvidence` is shown only for records that still lack those diagnostics. It is diagnostic-only and does not change normal Plasma rendering policy. The comparator clears `~/.cache/wescene-renderer/*/spvs01/` before rendering to avoid stale shader output. Missing references skip cleanly before artifact generation; when all required references are present, comparison artifacts are written to `/tmp/yakkai-arona-reference/<timestamp>/`: `summary.json`, `contact-sheet.png`, `contact-sheet-registered.png`, and per-variant Yakkai capture, normalized reference, raw diff, registered Yakkai capture, registered RGB diff, structure reference, registered structure capture, registered structure diff, log, and optional effect-manifest files. The raw sheet catches camera/framing drift; the registered sheet helps isolate color/effect drift after rough alignment and must not be treated as proof that camera or viewport parity is correct. Successful comparisons exit with status `review`/0 because before/after renderer changes still require human visual review.

To inspect the protected `ARONA_CROP_SHEET` puppet chain, including forced or prefix-sliced probe variants:

```bash
tools/compare-arona-reference.sh
tools/compare-arona-reference.sh --debug-effect-captures --debug-effect-probe-layers 405
tools/compare-arona-reference.sh --debug-effect-captures --debug-effect-capture-delay-ms 8000 --debug-effect-probe-layers 405 --debug-effect-probe-max-effects 2
tools/compare-arona-reference.sh --debug-effect-captures \
  --debug-layer-visibility-overrides '306:true'
tools/compare-arona-reference.sh --debug-effect-captures \
  --debug-puppet-animation-layer-overrides '405:781:paused=false'
tools/arona_protected_puppet_lab.py /tmp/yakkai-arona-reference/<probe-timestamp>/summary.json \
  --normal-summary /tmp/yakkai-arona-reference/<normal-timestamp>/summary.json
```

The comparator forwards `--debug-layer-visibility-overrides` and `--debug-puppet-animation-layer-overrides` to the harness when `--debug-effect-captures` is enabled, which lets Arona reference runs isolate authored scene-layer and puppet animation-layer state without launching the harness manually. The lab writes `protected-puppet-lab.json`, `protected-puppet-lab.md`, and `protected-puppet-contact-sheet.png`. It summarizes the protected layer's normal or metadata-only diagnostic, probe captures, policy reason, effect order, alpha evidence, generic route diagnostics, final-publish composition diagnostics, final coverage diagnostics for active puppet slots, capture-level alpha/color statistics, boundary comparisons such as `effect-input->effect-output`, and optional probe-final-to-normal-frame drift when `--normal-summary` is supplied. When `final-display-before/after` captures are present, the lab prefers their before/after delta as the final coverage source and reports `final-display-boundary-present` plus delta bounds/centroid evidence. It also compares the final-display delta geometry against `effect-output` alpha geometry and classifies it as `final-display-aligned`, `final-display-shape-drift`, or `final-display-coverage-loss` using visible ratio, normalized bounds IoU, and centroid drift. The same final-display delta is swept at RGB delta thresholds `1`, `4`, `8`, and `16` out of 255; `finalDisplayThresholdSensitivity` reports whether drift is threshold-sensitive, persistent, weak-signal, or stable before a renderer route change is considered. Because `effect-output` is layer-local while `final-display-before/after` are screen-space captures, `finalDisplayScreenSpaceProjection` additionally projects the output alpha centroid from output bounds into final-display delta bounds and reports whether the shape is affine-consistent after that coordinate-space remap. The normal-summary comparison also reports screen-space centroid drift so a puppet can be flagged when it shifts on screen even if layer-local alpha looks stable. If a comparator variant fails after writing `variant/effect-captures/manifest.json`, the lab recovers that manifest from the run output directory so partial timeout artifacts can still be inspected. Probe runs remain diagnostic artifacts; current safe `ARONA_CROP_SHEET` water/LUT/pulse/shake chains can also appear as normal preserved captures. `final-publish` remains a post-frame `_rt_default` dump, so final coverage diagnostics from `final-publish` are routing evidence, not proof that a production route is safe by themselves.

For fresh Layer 405 final-publish evidence exported from Windows RenderDoc, keep
the local ignored archive at
`yakkai_arona/layer405_final_publish_composite_fresh.zip`. To validate the
package and run layer-local-to-default-target registration:

```bash
python3 tools/arona_layer405_fresh_publish_compare.py \
  --windows yakkai_arona/layer405_final_publish_composite_fresh.zip \
  --output smoke-tests/artifacts/tmp/yakkai-layer405-fresh-publish-compare
```

To correlate the Windows evidence with a fresh Yakkai debug-effect run:

```bash
python3 tools/arona_layer405_fresh_publish_compare.py \
  --windows yakkai_arona/layer405_final_publish_composite_fresh.zip \
  --yakkai-root smoke-tests/artifacts/tmp/yakkai-layer405-fresh-yakkai-rerun \
  --output smoke-tests/artifacts/tmp/yakkai-layer405-default-delta-locator
```

This diagnostic validates the Day/Sunset/Night source labels, confirms Windows
final-publish state (`4160x2923` layer target into `2560x1440`, RGB-only
`writeMask=7`), parses final-event pixel histories, and correlates Yakkai
`debugEffectPassStates` when a comparator output root is supplied. With
`--yakkai-root`, the report also writes `yakkaiDefaultDeltaOracle`, which samples
Yakkai layer `405` `default-before-effect` and `default-after-effect` captures at
the Windows final-publish pixel-history coordinates (`lowerRibbon` and
`transparentEdge`) and compares the RGB delta against Windows `pre_mod_rgba` to
`post_mod_rgba`. The same run writes `yakkaiDefaultDeltaLocator`, which searches
the Yakkai default-target delta map for sample, nearest, and peak RGB changes
and emits before/after/delta crop sheets under `locator-crops/`.

The current real locator run at
`smoke-tests/artifacts/tmp/yakkai-layer405-final-publish-boundary-compare`
classifies all Day/Sunset/Night `default-before-effect` to
`default-after-effect` samples as `missing-default-delta`, but
`layerFinalPublishBoundary` uses the isolated
`final-display-before` to `final-display-after` captures and classifies all
variants as `delta-at-windows-sample`. Treat that as evidence that the earlier
missing default delta was a debug capture timing issue, not a missing layer
publish, color-mask parity, projection, or Windows capture issue. It is intended
for trusted local RenderDoc evidence archives and can take a few minutes when
the full image-registration pass runs.

The report also writes `yakkaiIsolatedPublishParity` when isolated
`final-display-before/after` captures are present. This compares the Windows
final-publish pixel-history RGB delta against Yakkai's isolated layer
final-publish RGB delta at the same scaled default-target coordinates and writes
sample crops under `isolated-publish-crops/`. Use this after
`layerFinalPublishBoundary=delta-at-windows-sample` to decide whether the
remaining drift is inside the layer output itself or downstream of the publish
boundary.

The same `--yakkai-root` run writes `yakkaiContentStageAttribution` and
`content-stage-crops/`. This compares Windows layer-local anchors
(`effect-input`, `prefix-3`, `prefix-7`, and `final-publish-input`) against
Yakkai layer `405` `effect-input` plus same-sized `material-output-*` captures.
Screen-sized synthetic puppet-final captures are skipped for this layer-local
comparison. Use the worst-anchor ranking to choose the next shader/effect-stage
target before changing final-publish routing.

The current real content-stage run at
`smoke-tests/artifacts/tmp/yakkai-layer405-content-stage-attribution` shows raw
`effect-input`/`prefix-3` remain close, Day and Night now classify as
`content-stage-close`, and Sunset remains `content-stage-drift` at `prefix-7`
with best Yakkai stage `material-output-12-0`. Treat that as evidence for a
focused Sunset middle-effect progression target, not a layer-loading, Windows
capture-label, color-mask, or final-publish route target.

The report also writes `yakkaiContentTransitionAttribution` and
`content-transition-crops/`. This compares signed deltas between Windows
layer-local anchors (`effect-input -> prefix-3`, `prefix-3 -> prefix-7`, and
`prefix-7 -> final-publish-input`) against adjacent same-sized Yakkai
`effect-input`/`material-output-*` transitions. Use this to identify which
Windows checkpoint block first diverges before drilling into individual effect
passes.

The current real transition run at
`smoke-tests/artifacts/tmp/yakkai-layer405-content-transition-attribution`
classifies all Day/Sunset/Night variants as `content-transition-mismatch`. The
worst transition is `prefix-3-to-prefix-7` for every variant; the later
`prefix-7-to-final-publish-input` delta is near zero. Treat that as evidence that
the next renderer target is the middle visible-effect block before final publish.

The report also writes `yakkaiContentRangeAttribution` and
`content-range-crops/`. This compares each Windows checkpoint delta against
every same-sized contiguous Yakkai `effect-input`/`material-output-*` range, so
it can catch a cumulative Yakkai block even when no adjacent transition matches.
The current real range run at
`smoke-tests/artifacts/tmp/yakkai-layer405-content-range-attribution` still
classifies Day and Night as `content-range-close`, while Sunset remains
`content-range-drift`. The best Day range is `material-output-2-0 ->
material-output-11-0`; the best Sunset and Night range is `material-output-2-0
-> material-output-12-0`. Treat that as evidence that the broad middle
layer-local visible-effect block is mostly aligned, with Sunset still needing
focused pass-internal or shader/effect work inside that block.

The report also writes `yakkaiMiddleBlockMicroscope` and
`middle-block-crops/`. This treats Windows `prefix-3` and `prefix-7` as fixed
endpoints, reports each Yakkai layer-local stage's distance to both endpoints,
and ranks adjacent Yakkai steps by whether they move toward or away from
Windows `prefix-7`. The current real microscope run at
`smoke-tests/artifacts/tmp/yakkai-layer405-middle-block-microscope` classifies
Day and Night as `middle-block-close`, and Sunset as `middle-block-drift`.
Selected targets are Day `material-output-10-0 -> material-output-11-0`
(`shake -> shake`), Sunset `material-output-11-0 -> material-output-12-0`
(`shake -> shake`), and Night `material-output-2-0 -> material-output-3-0`
(`pulse -> lut_loader`). Treat that as evidence for focused pass-internal work
on the selected middle-effect steps, not another final-publish or
range-selection patch.

The same run now writes `yakkaiSelectedStepMetadata`,
`selected-step-crops/`, and `middle-block-windows-request.md`. The metadata
report preserves the selected Yakkai step's shader name, effect/material
indices, authored and resolved combos, defines, material constants, texture
bindings, render-target routing, and input/output image stats. It includes the
microscope-selected step plus any distinct strongest toward/away step, then
generates a Windows handoff asking for every internal pass between Windows
`prefix-3` and `prefix-7`.

The current real selected-step metadata run at
`smoke-tests/artifacts/tmp/yakkai-layer405-selected-step-metadata` records Day
`material-output-10-0 -> material-output-11-0` (`shake -> shake`), Sunset
`material-output-11-0 -> material-output-12-0` (`shake -> shake`), and Night
`material-output-2-0 -> material-output-3-0` (`pulse -> lut_loader`) as the
current selected middle-block steps. Treat
`middle-block-windows-request.md` from that directory as the current precise
Windows-side ask for finer pass-boundary evidence.

For full internal Layer 405 pass-boundary evidence exported from fresh Windows
RenderDoc captures, keep the local ignored archive at
`yakkai_arona/layer405_full_pass_export.zip`. Compare it against a Yakkai
debug-effect capture root with:

```bash
python3 tools/arona_layer405_full_pass_align.py \
  --windows yakkai_arona/layer405_full_pass_export.zip \
  --yakkai-root smoke-tests/artifacts/tmp/arona-layer405 \
  --output smoke-tests/artifacts/tmp/yakkai-layer405-full-pass-alignment
```

`tools/arona_layer405_full_pass_align.py` maps Windows pass order `1` to
Yakkai `effect-input` and each later pass to
`material-output-<passOrder-1>-0`, then writes `alignment.json` and
`alignment.md` with RMSE, alpha-weighted RMSE, alpha RMSE, and adjacent-pass
delta cosine. It accepts per-variant directories such as `day`,
`sunset-buffered`, and `night-buffered` under the supplied Yakkai root. The
current real full-pass run at
`smoke-tests/artifacts/tmp/arona-layer405-full-pass-alignment` shows the extra
Sunset/Night internal pass after `prefix-3` is present in Yakkai and matches
Windows closely. Final layer-local outputs classify as close for Day, Sunset,
and Night, so remaining Layer 405 drift should be treated as small animated
waterwaves/shake timing or visual-review noise unless a later artifact shows a
specific pass-local mismatch.

If final-publish state looks missing or malformed, rebuild the backend and
harness explicitly with
`cmake --build build/native/scene_backend --target yakkai_scene_backend yakkai_scene_backendplugin`
and `cmake --build build/native/scene_harness --target yakkai_scene_harness`
before rerunning; stale backend manifests can omit `debugEffectPassStates` and
`colorMaskBits`.

For focused Arona mask/effect parity checks, `tools/arona_mask_effect_parity.py` reads a layer `405` effect-capture manifest, slot-boundary JSON, and local `scene.pkg` to extract authored pulse/waterwaves/shake masks. The report maps mask coverage onto the ribbon slots, records mask texture formats such as `R8` and `RG8`, and estimates per-slot displacement from WE shader constants before any renderer patch is considered.

When Windows Wallpaper Engine prefix captures are available under the ignored
`yakkai_arona/_targets/` package, use
`tools/arona_prefix_boundary_bisect.py` to compare those official WE effect
tiers against matching Yakkai scene variants. It validates the target package,
copies parallax-disabled tier scenes, and compares white-margin and bow/ribbon
motion metrics before any renderer predicate is widened:

```bash
python3 tools/arona_prefix_boundary_bisect.py validate-targets \
  --targets yakkai_arona/_targets \
  --scene-dir yakkai_arona/scene \
  --output-dir md-fixes/arona-we-prefix-boundary-bisect/reports
python3 tools/arona_prefix_boundary_bisect.py prepare-scenes \
  --scene-dir yakkai_arona/scene \
  --output-dir md-fixes/arona-we-prefix-boundary-bisect/yakkai-scenes \
  --report-dir md-fixes/arona-we-prefix-boundary-bisect/reports
python3 tools/arona_prefix_boundary_bisect.py compare \
  --targets yakkai_arona/_targets \
  --yakkai-root md-fixes/arona-we-prefix-boundary-bisect/yakkai-captures \
  --output-dir md-fixes/arona-we-prefix-boundary-bisect/reports
```

The comparison writes `prefix-boundary-report.json`, `final-summary.md`, and
per-tier contact sheets under `md-fixes/arona-we-prefix-boundary-bisect/`. The
report classifies the first bad boundary as base puppet/skinning, pulse,
waterwaves, shake, final-display composition, reference mismatch, or no
reproduced regression. Treat this as diagnostic evidence only; a renderer fix
still needs a focused follow-up plan and human visual review.

For focused ribbon/bow motion checks across Windows WE and Yakkai frame sequences, use `tools/arona_ribbon_tip_boundary.py`. The tool reads a region JSON with `baseSize`, scales ROIs to each sequence's actual frame size, tracks vertical motion in `tip` and `main` regions, and writes a boundary report plus contact sheet for human review. Use it with prefix-sliced layer `405` frame directories when checking whether motion is lost before effects, after waterwaves, after shake, or only at final composition:

```bash
python3 tools/arona_ribbon_tip_boundary.py boundary-report \
  --regions md-fixes/arona-ribbon-tip-motion-boundary/reports/day-ribbon-regions.json \
  --windows-dir md-fixes/arona-ribbon-tip-motion-boundary/windows/day/frames \
  --normal-yakkai-dir md-fixes/arona-ribbon-tip-motion-boundary/yakkai/day/frames-normal \
  --prefix N0=md-fixes/arona-ribbon-tip-motion-boundary/prefix/day/N0 \
  --prefix N2=md-fixes/arona-ribbon-tip-motion-boundary/prefix/day/N2 \
  --prefix N7=md-fixes/arona-ribbon-tip-motion-boundary/prefix/day/N7 \
  --prefix N10=md-fixes/arona-ribbon-tip-motion-boundary/prefix/day/N10 \
  --prefix N11=md-fixes/arona-ribbon-tip-motion-boundary/prefix/day/N11 \
  --output-dir md-fixes/arona-ribbon-tip-motion-boundary/reports
```

The motion report is a preflight aid, not visual approval. Always inspect `boundary-contact-sheet.png` when the defect is a subtle puppet/crop-sheet motion mismatch.

For lower-ribbon flutter checks where rigid motion is not enough, use
`tools/arona_ribbon_flutter_parity.py`. It reads Windows WE and Yakkai frame
sequences, scales the configured lower-ribbon ROI, subtracts rigid vertical
motion, writes residual edge-flutter kymographs, and classifies whether the
remaining mismatch looks like missing flutter, phase drift, amplitude drift, or
ambiguous reference alignment:

```bash
python3 tools/arona_ribbon_flutter_parity.py flutter-report \
  --regions md-fixes/arona-lower-ribbon-flutter-parity/regions/lower-ribbon-flutter-regions.json \
  --windows-dir md-fixes/arona-ribbon-tip-composition-boundary/windows/day/frames \
  --normal-yakkai-dir md-fixes/arona-ribbon-tip-composition-boundary/yakkai/day/frames-normal \
  --output-dir md-fixes/arona-lower-ribbon-flutter-parity/reports/normal
```

Treat this as a human-review gate before renderer changes. Add
`--prefix LABEL=/path/to/frames` entries only after the normal Windows/Yakkai
crop is approved as the visible defect target.

For black-box runtime fitting against a Windows WE video/reference sequence, use
`tools/arona_we_behavior_fitter.py`. Extract Windows frames to the same base
resolution as the ROI config before running it; the current lower-ribbon fitter
regions use a `1600x900` base. The tool inventories the Windows/Yakkai/prefix
frame inputs, rejects malformed ROI configs, writes per-region temporal feature
JSON for normal and prefix captures, writes candidate ranking, contact-sheet,
kymograph, and `final-summary.md` review artifacts, and records missing prefix
slices without failing required Windows/Yakkai validation:

```bash
python3 tools/arona_we_behavior_fitter.py \
  --windows-frames md-fixes/arona-we-behavior-fitter/windows/day-video-1600x900 \
  --yakkai-frames md-fixes/arona-we-behavior-fitter/yakkai/day-normal \
  --prefix-root md-fixes/arona-we-behavior-fitter/prefix \
  --regions md-fixes/arona-we-behavior-fitter/regions/lower-ribbon-fit-regions.json \
  --output-dir md-fixes/arona-we-behavior-fitter/reports
```

This fitter is diagnostic-only. A high-scoring hypothesis still needs visual
review before any shader, sampler, effect-order, or composition behavior changes.
When alignment is below the report gate, ranked candidates are marked
`diagnostic-only` and the final summary should be treated as a request for
stronger Windows evidence instead of a renderer-fix target.

For the final Arona shake pass, capture layer `405` twice with `--debug-effect-probe-max-effects 10` and `11`, then run:

```bash
python3 tools/arona_shake_output_oracle.py \
  --prefix-manifest md-fixes/arona-shake-output-oracle/captures/prefix10/effect-captures/manifest.json \
  --full-manifest md-fixes/arona-shake-output-oracle/captures/full11/effect-captures/manifest.json \
  --scene-pkg yakkai_arona/scene.pkg \
  --layer-id 405 \
  --effect-index 11 \
  --output-dir md-fixes/arona-shake-output-oracle/reports
```

The oracle reproduces WE `effects/shake` on CPU using the captured shader time, decoded `RG8` flow mask, and Yakkai's shake-only source-alpha preservation behavior. It writes CPU/GPU PNGs, a diff, a ribbon contact sheet, and JSON/Markdown metrics; use it to decide whether a remaining ribbon issue is shader math/source-alpha evidence or requires a stronger Windows per-effect oracle.

For focused Arona waterwaves boundary checks, capture layer `405` around the `pulse_waterwaves` prefix tier, then run:

```bash
python3 tools/arona_waterwaves_output_oracle.py \
  --pulse-manifest md-fixes/arona-waterwaves-boundary-oracle/captures/prefix2/effect-captures/manifest.json \
  --first-waterwaves-manifest md-fixes/arona-waterwaves-boundary-oracle/captures/prefix3/effect-captures/manifest.json \
  --full-waterwaves-manifest md-fixes/arona-waterwaves-boundary-oracle/captures/prefix7/effect-captures/manifest.json \
  --scene-root yakkai_arona/scene \
  --layer-id 405 \
  --output-dir md-fixes/arona-waterwaves-boundary-oracle/reports
```

The waterwaves oracle models captured `effects/waterwaves` passes on CPU using the dumped shader time, material constants, masks, and effect input/output captures. It writes CPU candidate images, contact sheets, and JSON/Markdown metrics for source-alpha, displaced-alpha, sampler, UV, timing, and DUALWAVES hypotheses. Prefer `yakkai_arona/captures/waterwaves_frame90_shader_debug/` over the older full texture export when checking official WE source-vs-displaced alpha behavior; the shader-debug export showed official WE waterwaves samples displaced alpha normally, so Yakkai does not apply the source-alpha preservation combo to waterwaves. Treat DUALWAVES or strength-scaling probes as diagnostic scene copies unless Windows per-pass evidence proves the same generic behavior.

Use the LUT sampling lab to rank plausible `lut_loader` sampling variants for one captured layer:

```bash
python3 tools/arona_lut_sampling_lab.py "$RUN_DIR/summary.json" \
  --variant night \
  --layer-id 82 \
  --output-dir /tmp/yakkai-arona-lut-lab/night-82
```

The lab reads the layer's bound LUT texture from `scene.pkg`, applies candidate `QUAD_SIZE`, `LUT_FLIP_Y`, filter, and color-space modes to the captured effect input, and writes `lut-sampling-report.json`, `lut-sampling-report.md`, and top-candidate preview PNGs. It also reports decoded WE texture sampler metadata and applies the manifest material context for `CLAMP`, `BLENDMODE`, `multiply1`, and `tc`. When the manifest contains `material-output-local-*`, the lab prefers that same-size layer-local material output; otherwise it falls back to `material-output-*` and then `effect-output`. Per-layer reports include a `publishDrift` block that compares layer-local material output, screen-sized material output, optional `final-publish`, and the variant's registered Windows-reference screen drift. `publishDrift.registeredLocalToFinal` runs a bounded crop/scale registration between layer-local material output and the screen-sized publish target so ordinary transform/crop differences can be separated from remaining color or final-stage drift. `publishDrift.publishDiagnostics` mirrors the manifest `publish` block and final-material routing fields; classifications treat `final-publish` differences as post-frame composite deltas when the manifest marks `finalPublishCaptureTiming=post-frame-render-target-dump`. Treat the ranking as directional when the selected output stage and `effect-input` dimensions differ.

To run the same lab against every preserved LUT layer for a time-of-day variant:

```bash
python3 tools/arona_lut_sampling_lab.py "$RUN_DIR/summary.json" \
  --variant night \
  --all-lut-layers \
  --output-dir /tmp/yakkai-arona-lut-lab/night-all-material
```

This writes `lut-sampling-summary.json` and `lut-sampling-summary.md`, plus one layer report directory per captured LUT layer. Reports include plain, alpha-weighted, and opaque-pixel RMSE fields plus visible/opaque pixel fractions so transparent or partial overlay layers can be separated from meaningful background evidence. Aggregate reports also separate visible captures from comparison-trusted captures: `trustedBestCandidateCounts` includes only layers with enough visible/opaque pixels and matching input/output dimensions, so screen-sized final-publish captures are not treated as layer-local shader-output proof. Summary reports also include `publishClassificationCounts` to separate likely publish transform/final-stage drift from missing local captures and downstream or missing-effect drift.

To inspect how screen-sized LUT material outputs change the default frame over manifest order:

```bash
python3 tools/arona_lut_sampling_lab.py "$RUN_DIR/summary.json" \
  --variant night \
  --default-frame-progression \
  --output-dir /tmp/yakkai-arona-lut-lab/night-progression
```

This writes `default-frame-progression.json` and `default-frame-progression.md`. The progression report compares each screen-sized `material-output-*` LUT capture against the normalized Windows reference and the final Yakkai frame, then ranks the largest consecutive frame deltas, reference improvements, and reference regressions. It uses `captureIndex` when available and falls back to manifest order for older artifacts. The report is diagnostic-only; it does not prove a per-layer fix without visual review of the comparator sheets and captures.

To attribute drift between the last preserved screen-sized LUT output and the final frame:

```bash
python3 tools/arona_lut_sampling_lab.py "$RUN_DIR/summary.json" \
  --variant night \
  --post-lut-drift \
  --output-dir /tmp/yakkai-arona-lut-lab/night-post-lut-drift
```

This writes `post-lut-drift.json` and `post-lut-drift.md`. The report compares the last screen-sized LUT material-output capture against the final Yakkai frame and normalized Windows reference, classifies whether the final frame improved or regressed after the LUT progression, lists later capture records in `captureIndex` order, and ranks coarse top/middle/bottom/left/center/right regions by added reference drift. It also includes a full-frame attribution section that ranks active post-LUT output captures, skips non-output stages, and separates disabled/no-op zero-alpha captures when both layer alpha and resolved `g_UserAlpha` are zero. The default-RT boundary attribution section uses `default-before-effect` and `default-after-effect` captures to identify whether `_rt_default` changed between the last LUT snapshot and the next preserved effect layer. When the manifest contains protected puppet metadata, the report adds a Protected Puppet Diagnostics section with crop-sheet chain shape, alpha, effect order, material shader list, and final-publish target. This is an attribution aid for choosing the next renderer target, not a pixel parity gate.

To zoom into the post-LUT flare/default-frame sequence:

```bash
python3 tools/arona_lut_sampling_lab.py "$RUN_DIR/summary.json" \
  --variant night \
  --post-lut-flare-drift \
  --output-dir /tmp/yakkai-arona-lut-lab/night-post-lut-flare-drift
```

This writes `post-lut-flare-drift.json` and `post-lut-flare-drift.md`. The report filters full-frame post-LUT effect captures, tags named flare/lens layers separately from unnamed post-LUT effect layers, separates disabled zero-alpha captures from active captures when both layer alpha and resolved `g_UserAlpha` are zero, compares active captures against the previous active/default-frame capture, and ranks the largest reference and region deltas. Use it to decide whether a specific active flare layer, shared flare composition path, or another default-frame effect is the next renderer target.

Native renderer policy decisions for effect preservation, video texture playback, static model fallback, and SceneScript stubs live in focused C++ modules under `native/scene_backend/vendor/upstream_debug/src/Policy/`. Run `yakkai_scene_policy_tests` alongside smoke tests when changing these boundaries:

```bash
cmake --build build --target yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
```

## Release Packaging

Release assets are built from tags by `.github/workflows/release.yml`. To create a release, push a tag such as `v0.1.0`; the workflow builds an Arch Linux x86_64 prebuilt package, builds a source fallback tarball, writes `SHA256SUMS`, and creates or updates the matching GitHub release.

The same packaging can be reproduced locally:

```bash
scripts/package-release.sh --output-dir dist --version v0.1.0 --target archlinux-x86_64
```

### Settings persistence test

```bash
QT_QPA_PLATFORM=offscreen /usr/lib/qt6/bin/qmltestrunner \
  -input tools/tst_config_persistence.qml \
  -o -,txt -v2 -maxwarnings 0 -nocrashhandler
```

## Repo layout

```
.
├── CMakeLists.txt
├── native/
│   ├── scene_backend/       # Native Vulkan scene renderer (C++)
│   │   ├── src/             # YakkaiSceneViewer QML integration
│   │   ├── tests/           # Native no-framework renderer policy tests
│   │   └── vendor/          # Vendored upstream scene sources + QuickJS + glslang
│   │       └── upstream_debug/src/Policy/ # Behavior-preserving renderer policy boundaries
│   └── scene_harness/       # Standalone debugging app
├── wallpapers/
│   └── io.team7.yakkai/     # Plasma wallpaper package
│       ├── metadata.json
│       └── contents/
│           ├── config/main.xml
│           ├── imports/     # (generated) native QML module
│           ├── tools/       # Python scan helper
│           └── ui/          # QML UI components
├── tools/                   # Development utilities
│   ├── arona_lut_parity_report.py # Summarize Arona comparator effect manifests
│   ├── arona_mask_effect_parity.py # Map Arona ribbon masks/constants to slot displacement evidence
│   ├── arona_media_parity_fixture.py # Emit exact Arona media-widget crops/regions from fixture data
│   ├── arona_ribbon_flutter_parity.py # Compare lower-ribbon residual flutter kymographs
│   ├── arona_ribbon_tip_boundary.py # Compare ribbon-tip motion across Windows/Yakkai frame sequences
│   ├── arona_we_behavior_fitter.py # Fit Windows WE reference motion against Yakkai/prefix captures
│   ├── arona_shake_output_oracle.py # CPU oracle for Arona layer 405 final shake pass
│   ├── arona_waterwaves_output_oracle.py # CPU oracle for Arona layer 405 waterwaves passes
│   ├── arona_protected_puppet_lab.py # Summarize protected ARONA_CROP_SHEET probe captures
│   ├── arona_lut_sampling_lab.py # Rank LUT sampling candidates against effect captures
│   ├── color-lab/           # Interactive color debugging web tool
│   ├── effect-capture-summary.py # Summarize harness effect-capture manifests
│   ├── effect_route_guards.py # Structural guards for effect-route manifests
│   ├── media_text_diagnostics.py # Crop generated SceneScript/media text diagnostics
│   ├── scene-script-log-summary.py # Summarize SceneScript bindings and runtime gaps
│   ├── tst_config_persistence.qml # QML regression test for settings persistence
│   └── validate-scene.sh    # Automated render validator
├── scripts/                 # Local install and package validation helpers
├── .github/workflows/       # GitHub release packaging workflow
├── smoke-tests/             # Regression test runner
└── references/              # Third-party reference materials
```

## Content modes

| Mode | Description |
|------|-------------|
| Gradient | Two-color gradient with animation and time-of-day support |
| Video | Local video file via QtMultimedia |
| WE Video | Wallpaper Engine video projects from Steam library |
| WE Web | Wallpaper Engine web projects via QtWebEngine with WE property, viewport, audio-listener, and media-listener compatibility shims |
| WE Scene (diagnostics) | Safe scan/selection with placeholder diagnostics |
| WE Scene (native) | Native Vulkan renderer for WE scene projects |
| Playlist | Sequential, random, or scheduled cycling of scene wallpapers |
| All Wallpapers | Unified picker across scene, video, and web types |
| Playlist (All) | Playlists mixing scene, video, and web wallpapers |

## Native scene renderer

The native backend supports:
- **Qt Quick startup safety**: image-backed placeholder texture before the first external Vulkan frame
- **Plasma backend guard**: the native scene runtime only starts when Plasma reports an OpenGL Qt Quick scenegraph; software-rendered Plasma sessions stay on the diagnostic placeholder instead of invoking the Vulkan/OpenGL interop path
- **Vulkan synchronization**: explicit render-target write-to-sample barriers for custom shader passes and final presentation, so diagnostic copy passes are not required to avoid stale `_rt_default` artifacts
- **Textures**: TEXB v1-v4 with LZ4 decompression, TEXB v4 sprite-sheet header parsing for particle material preflight, BC1/BC2/BC3/BC7 formats, embedded PNG/JPEG detection, video textures via FFmpeg
- **Puppets**: MDL bone animation with additive layer blending, UTF-8 names, and mapped-area UV scaling for padded WE texture storage
- **Shaders**: GLSL 150 preprocessing, authored combo preservation, HLSL `clip(x)` fragment-discard translation, `inverse()` polyfill stripping, varying type mismatch fix, `#require LightingV1` injection, ENABLEMASK/MASK combo mapping
- **Effects**: Composelayer support, selective effect stripping for alpha compositing, no-op skip for stripped fullscreen/composelayer effect carriers, colorkey preservation, flare/lens detection via colorBlendMode, script-resolved zero-alpha flares preserved as hidden, supported media-widget utility/effect carriers preserved for synthetic metadata widgets
- **Scripts**: QuickJS-based SceneScript evaluation with WE API stubs (thisLayer, thisScene/scene layer lookup, layer transform helpers, Vec3/WEMath helpers, createScriptProperties override/default resolution, generated text return capture with Qt font rasterization and bitmap fallback, localStorage, engine.registerAudioBuffers, engine.timeOfDay, `shared.mi`, `engine.media`, `MediaPlaybackEvent`, etc.)
- **Properties**: Conditional user property resolution, animation curve evaluation (alpha/color/origin), time-of-day mapping for day-night cycles, container visibility inheritance
- **Particles**: Conditional visibility, parent container hiding

### Known limitations
- Regular per-layer offscreen effect chains in puppet scenes can break alpha compositing. Yakkai selectively strips regular/heavy effects in puppet scenes while preserving composelayers, colorkey, flare/lens, and other essential effect paths.
- Essential flare/lens effect paths keep their authored alpha. Yakkai does not globally force zero-alpha flares visible; wallpapers that hide flares with time-of-day or property scripts should stay hidden when those bindings resolve to zero.
- High-risk color-grading outside the strict `composelayer-color-grade` class, fullscreen blur outside the strict `utility-blur` class, mixed blur, utility/fullscreen water-only carrier classes, and protected blur effect paths remain stripped or probe-only. Regular non-carrier `blur-only` chains can render normally under the narrow `regular-blur-only-effect` predicate, fullscreen utility blur carriers can render normally under the narrow `utility-blur-effect` predicate, composelayer water-only carriers can render normally under the narrow `composelayer-water-effect` predicate, and composelayer blur/color-grade carriers can render normally under the narrow `composelayer-color-grade-effect` predicate after human visual approval. Non-fullscreen composelayers publish preserved effect output through their authored local layer card instead of fullscreen-blitting the effect texture; this keeps small local water/reflection carriers from overwriting the whole frame. Protected puppet crop-sheet chains can render normally only when their visible effects are limited to recognized water/LUT/pulse/shake families; protected blur/color-grade, unknown mixed families, and generic puppet paths remain stripped or probe-only. Non-channelmap puppet final routes avoid double-applying puppet skinning and parallax: standalone final displays publish the rendered effect texture as an original-parent sibling layer card with premultiplied blending, while deferred effect-layer final-publish materials consume zero additional parallax because the offscreen effect input has already been parallaxed. For non-channelmap puppet final-display direct displaced `shake` passes, the shader preserves the original source alpha while displacing color so transparent crop-sheet silhouettes are not warped by effect UV offsets; restored-alpha edge pixels blend back toward source RGB when the displaced sample is more transparent. `waterwaves` uses the official WE displaced-alpha behavior after Windows shader-debug evidence showed source-alpha preservation was wrong for that shader. Source puppet meshes sample only the mapped image area of padded WE textures, while render-target final displays keep full render-target UVs. The layer-local effect viewport expands to puppet mesh bounds when authored geometry extends outside the nominal object size. The former `3476236738` flat gray background and missing extended-hand regressions are guarded by the scene visual sentinel.
- The current stripped-effect family backlog, including candidate scenes and blocked follow-up slices, is tracked in `docs/renderer-effect-candidate-backlog.md`.
- Small embedded video textures are decoded as static first frames to keep CPU use bounded. Continuous decode is enabled only for large/main videos when FFmpeg is available at build time.
- WE Web audio listener APIs are compatibility shims. Real Linux audio capture is not implemented yet; current web-audio regression coverage uses harness-only synthetic data, motion thresholds, and tolerant PNG comparison for browser-timed visualizer output. It still requires human review before promotion.
- WE Web user-property editing/import is not implemented in the Plasma settings yet. Yakkai loads authored `project.json` defaults for web wallpapers, but per-user Wallpaper Engine adjustments such as moving CWAV's date position are a far-future settings feature.
- Static model scenes use an experimental fallback for basis correction, camera framing, and material selection.
- Material/lighting fidelity is partial: generic materials and point lights are supported, but full Wallpaper Engine PBR, shadow, and reflection parity is not.
- SceneScript support is partial: Yakkai evaluates simple layer bindings (origin/scale/color/alpha/visible and generated text returns) with generic layer/scene stubs, not the full Wallpaper Engine runtime. Text objects are represented structurally at their script-resolved transform, logged for validation, and rendered to generated RGBA textures with Qt font rasterization when available; authored font files, horizontal/vertical text-card edge anchoring, scene-canvas-scaled 100-DPI point-size conversion, color, alpha, and mirror-aware text placement based on the final effective mirror state are honored, with a simple bitmap renderer retained as fallback. SceneScript layer lookups preserve authored text-card sizes for `thisScene.getLayer(...).size`; generated glyph alpha bounds are recorded for diagnostics but do not resize the layer object exposed to scripts. Debug manifests and media text crop reports record whether Qt or the fallback renderer was used and whether an authored font loaded successfully. Full Wallpaper Engine wrapping, ellipsis, rich typography, and exact text layout parity are not implemented yet. Native SceneScript media metadata/progress widgets can read synthetic `__yakkaiMedia` state through `shared.mi`, `engine.media`, `MediaPlaybackEvent`, `mediaPropertiesChanged(event)`, and `mediaThumbnailChanged(event)`; read-only Linux MPRIS metadata integration can feed the same object in Plasma native scene mode.
  The runtime seeds common media-widget shared defaults, preserves returned `Vec3` color bindings, and guards non-finite `Vec3.mix` amounts so property-only widgets can open without NaN transforms/colors. Supported widgets keep authored/script-active container transforms and structured transform fallback values. Safe media callback bindings for existing image/solid nodes can receive live runtime `mediaStateJson` updates for origin, scale, color, alpha, and visibility; supported timeline solid progress layers keep their mirror-aware leading-edge compensation. Hidden template variants, click-driven player controls, native audio-reactive bars, texture-animation media widgets, exact rich text layout parity, generated text reraster from changing metadata, and unsupported object APIs remain deferred diagnostics. Safe inert stubs exist for compatibility-only calls such as `thisObject.getAnimation().play()` and `thisLayer.play()` / `thisLayer.pause()` so unsupported control calls do not abort unrelated binding side effects. Synthetic media thumbnail textures are available for harness/backend debugging through `albumArtPath` or deterministic color placeholders; when album art is provided without explicit thumbnail colors, Yakkai derives the thumbnail event colors from that image, including a blended contrast `textColor` rather than pure white/black. Validator logs classify missing runtime APIs as visible, harmless, or media/runtime-only diagnostics so candidate fixtures can be triaged before adding new API stubs.

## Remove

```bash
kpackagetool6 -t Plasma/Wallpaper -r io.team7.yakkai
```

## License

MIT
