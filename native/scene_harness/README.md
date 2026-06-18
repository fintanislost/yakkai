# Scene Harness

This app is the standalone debugging surface for `Wallpaper Engine Scene Native`.

Why it exists:
- native scene bugs should be reproducible outside `plasmashell`
- scene backend launches should be testable without risking a Plasma crash
- renderer state should be inspectable before the backend is wired back into the wallpaper package

Current backends:
- `system`: uses the currently installed `com.github.catsout.wallpaperEngineKde` `SceneViewer`
- `paper`: uses the repo-owned `io.team7.scene` native backend module
- `video`: uses the package `VideoBackground.qml` adapter for plain video wallpaper sources
- `web`: uses the package `WebBackground.qml` adapter for Wallpaper Engine web wallpaper sources

Example usage after building:

```bash
./yakkai_scene_harness \
  --backend system \
  --source /path/to/scene.json \
  --assets /path/to/wallpaper_engine/assets \
  --fill crop
```

Minimal video backend example:

```bash
./yakkai_scene_harness \
  --backend video \
  --source /path/to/wallpaper.mp4 \
  --fill crop \
  --hide-info-overlay
```

Minimal web backend example:

```bash
./yakkai_scene_harness \
  --backend web \
  --source /path/to/index.html \
  --scene-properties-json '{}' \
  --hide-info-overlay
```

The web backend installs the Wallpaper Engine compatibility script before loading the page URL. The shim forwards WE user/general properties, exposes WE-style viewport globals (`window.width`/`window.height`), and provides registration-compatible audio/media listener APIs so web projects that initialize media modules can still reach `wallpaperPropertyListener`. Runtime diagnostics remain available through backend status/logs, but the on-wallpaper diagnostics overlay is hidden by default during capture runs so valid web pages without a property listener are not covered by Yakkai status text.

WE Web candidates are promotion-ready only when capture sequences exit cleanly and reviewed clips show full web content without Yakkai status overlays. If a smoke-runner launch reports a negative signal-style exit while the same command succeeds when run directly through the harness, treat that as a QtWebEngine harness launch/teardown blocker instead of promoting the candidate.

Useful flags:
- `--backend system|paper|video|web`
- `--source /absolute/path/to/scene.json`, `/absolute/path/to/scene.pkg`, a local video file, or a web wallpaper `index.html`
- `--assets /absolute/path/to/wallpaper_engine/assets`
- `--fill crop|fit|stretch`
- `--window-size WIDTHxHEIGHT` (defaults to `1600x900`; smoke tests set this from `scenes.json`)
- `--frameless` removes native window decorations so `--window-size` is less likely to be reduced by desktop frame extents. Use this for pixel-sensitive comparison runs that should not include a decorated client frame.
- `--fullscreen` requests a fullscreen harness window on the active screen. Use this for monitor-sized Windows parity captures when KWin/Wayland constrains normal or frameless windows to the available work area.
- `--scene-properties-json json` passes a complete Wallpaper Engine scene property object to the backend. The `paper` backend also accepts reserved `__yakkaiMedia` synthetic media state for native SceneScript metadata/progress widget debugging, including common metadata callbacks such as `mediaPropertiesChanged(event)`, `mediaThumbnailChanged(event)`, and synthetic `$mediaThumbnail` / `$mediaPreviousThumbnail` textures. Example: `{"timeofday":{"value":"1"},"__yakkaiMedia":{"available":true,"playing":true,"title":"Constant Moderato","artist":"Mitsukiyo","album":"Blue Archive","duration":240,"position":42,"textColor":"1 1 1","primaryColor":"0 0 0","secondaryColor":"0.5 0.5 1","tertiaryColor":"0.25 0.25 0.75","settleSeconds":1.25,"albumArtPath":"/path/to/cover.png"}}`. Explicit thumbnail colors override derived colors; if `albumArtPath` is provided and explicit colors are omitted, the backend derives deterministic `mediaThumbnailChanged(event)` colors from the album art with a muted primary base, subdued `tertiaryColor` accent, and blended contrast `textColor`. Available-but-not-playing media dispatches `PLAYBACK_PAUSED`; unavailable media dispatches `PLAYBACK_STOPPED`. `__yakkaiMedia` is only the synthetic player payload; include any wallpaper-authored user toggles separately, such as Arona's `{"mediaintegration":{"value":"1"}}`.
- `--media-state-timeline-json json` drives harness-only runtime media-state updates for the `paper` backend without changing `scenePropertiesJson`. The value is a JSON array of keyframes with strictly increasing `timeMs`; direct media fields are merged over any stable `__yakkaiMedia` payload from `--scene-properties-json` before each runtime update. Example: `[{"timeMs":0,"position":0},{"timeMs":1000,"position":120}]`. This is intended for progress-animation review of supported SceneScript media timeline solid layers. When `--debug-effect-captures` is enabled, the normalized keyframes are recorded as top-level `mediaStateTimeline` in `manifest.json`.
- `--hide-info-overlay` hides the local debug overlay so captured PNGs contain only scene pixels.
- `--mouse`
- `--interactive-mouse` enables live pointer/hover input for manual mouse/parallax checks. It is equivalent to live mouse input for rendering, but records `inputSource: interactive` in paper-backend debug manifests when used with `--debug-effect-captures`.
- `--unmuted`

Capture flags:
- `--capture path --capture-delay-ms ms` waits for the backend readiness signal, then saves one capture after the requested delay.
- `--capture-dir path --capture-times-ms 1000,3000,8000` waits for the backend readiness signal, then captures fixed timestamps in one harness process and writes `frame-00001000ms.png`, `frame-00003000ms.png`, and `frame-00008000ms.png`.
- `--capture-dir path --capture-sequence 5000:60:33` waits for the backend readiness signal, then captures a sequence starting at 5000ms with 60 frames spaced 33ms apart and writes `frame-0000.png`, `frame-0001.png`, and so on.
- `--capture-exit-mode graceful|immediate` controls harness-only shutdown after capture. `graceful` is the default and runs the backend shutdown hook before normal Qt exit. `immediate` exits the process after the final capture save returns and is intended only for QtWebEngine smoke/debug captures where teardown is known to trip after frames are already written. Do not use it with `--debug-effect-captures`.
- `--record path --record-duration-ms ms --record-fps fps` waits for backend readiness, repeatedly grabs the harness window, and pipes raw RGBA frames to `ffmpeg` for a live MP4 review artifact. `--record-start-delay-ms ms` waits after readiness before the first recorded frame. Recording cannot be combined with PNG capture flags; PNG captures remain the regression baseline source of truth.

Debug flags:
- `--debug-synthetic-audio` emits harness-only synthetic Wallpaper Engine web audio data into `window.wpeQml.sigAudio`. Use it only with `--backend web` when validating audio-reactive web wallpaper behavior; it does not capture real Linux audio and does not change Plasma wallpaper behavior. In the web harness, synthetic audio is armed at the same backend-ready boundary used for capture timers so sequence captures start from a stable synthetic-audio origin, but individual visualizer bar shapes can still vary with browser animation timing and smoothing.
- `--debug-synthetic-audio-bins count` and `--debug-synthetic-audio-interval-ms ms` tune the synthetic web audio payload shape and timer interval.
- `--debug-effect-captures path` writes effect-chain diagnostic captures and `manifest.json` for the repo-owned `paper` backend. The flag is rejected for `--backend system`.
- `--debug-effect-capture-delay-ms ms` waits for the requested scene time before dumping debug effect captures. Use the same value as `--capture-delay-ms` when a PNG and TGA evidence need to describe the same delayed frame.
- `--debug-effect-capture-layers ids` filters normal debug effect capture registration to the listed layer IDs. It requires `--debug-effect-captures`, accepts comma-separated IDs such as `405,239`, and only reduces diagnostic output volume; it does not force stripped layers, prefix-slice effects, or change render policy.
- `--debug-mouse-position x,y` sets a harness-only synthetic normalized mouse position for the `paper` backend. It requires `--debug-effect-captures`, records `mouseParallax` diagnostics in the manifest, and does not change normal Plasma mouse behavior.
- `--debug-mouse-timeline timeMs:x,y;timeMs:x,y` sets a harness-only interpolated synthetic mouse path for the `paper` backend. It requires `--debug-effect-captures`, cannot be combined with `--debug-mouse-position`, records `inputSource: synthetic-timeline`, the normalized timeline, and `timelineElapsedMsAtCapture` under `mouseParallax`, and does not change normal Plasma mouse behavior.
- `--interactive-mouse` can be used without debug captures for manual live-input review. It cannot be combined with `--debug-mouse-position` or `--debug-mouse-timeline`; with `--debug-effect-captures`, the manifest records `inputSource: interactive` and the live effective mouse position sampled by the renderer.
- `--debug-layer-visibility-overrides rules` applies harness-only scene-layer visibility overrides before the paper backend builds its render graph. It requires `--debug-effect-captures`, accepts comma-separated `layerId:true|false` entries such as `306:true,240:false`, and is intended for one-layer-at-a-time disabled/gated layer probes. A successful probe capture is not production evidence without visual review and normal regression validation.
- `--debug-effect-probe-layers ids` renders specific stripped puppet mixed-chain layer IDs only for debug capture. It requires `--debug-effect-captures`; comma-separated IDs such as `168,22` are accepted.
- `--debug-effect-probe-high-risk-layers ids` renders specific stripped blur/LUT/color-grading layer IDs only for debug capture. It requires `--debug-effect-captures`; comma-separated IDs such as `53,155` are accepted.
- `--debug-effect-probe-channelmap-slots slots` is quarantined. The derived sidecar channelmap render path produced glitchy puppet fragments before effects ran, so the harness rejects the flag while the negative evidence remains documented.
- `--debug-effect-probe-max-effects count` limits forced probe layers to their first `count` visible effects. It requires `--debug-effect-captures` plus one of the probe layer lists, and is intended for prefix-slicing a stripped chain during investigation.
- `--debug-puppet-effect-route-only` keeps the puppet offscreen/final-display route active while dropping all visible effects on explicitly requested puppet probe layers. It requires `--debug-effect-captures` and `--debug-effect-probe-layers`, and is intended to separate route/composition bugs from shader bugs.
- `--debug-puppet-effect-final-mesh layer-card|image-space|deferred-puppet-final` overrides the puppet effect final-display route for explicit puppet effect probes. The normal non-channelmap puppet effect route is now `deferred-puppet-final` by default: authored effects run on the flat crop-sheet source, then a puppet-skinned final mesh publishes the effect result. Use `layer-card` or `image-space` only to force legacy diagnostics.
- `--debug-puppet-animation-layer-overrides rules` applies harness-only puppet animation layer overrides and requires `--debug-effect-captures`. Rules are semicolon-separated `layerId:animationId:key=value[,key=value]` entries; supported keys are `visible`, `paused`, `additive`, `blend`, `rate`, and `curTime`.
- `--puppet-simulation off|diagnostic|runtime` sets the investigation-only puppet secondary-motion mode for this harness process. It is off by default and should not be used as a production policy decision without normal render validation and visual review.

The debug manifest records scene id, `sceneOrtho`, layer id/name/type,
`EffectPolicy` preserve/strip reason, effect names, material shaders, render
targets, render-target dimensions/format, pass load operations, blend state,
color masks, output paths, capture delay, actual shader time
(`shaderTimeSeconds`), frame time (`frameTimeSeconds`), effective capture
readiness time (`effectiveCaptureTimeSeconds`), and capture status. When a
normal capture filter is used, the manifest records it as top-level
`captureLayerIds`. Effect publish metadata includes parent/route fields plus
final-publish composition evidence such as local transforms, final source
texture, standalone final blend, display node ordinal, mesh bounds,
effect-input viewport size/expansion state, and whether the effect-input
material preserved the layer blend mode. Puppet publish coverage records bone
and parent names, primary and weighted vertex/triangle coverage, secondary-only
weighted slots, weighted layer-local bounds/centroid, and simulation metadata
presence for each cutout slot. It also records manifest-only
`strippedCandidates` for effect chains removed by policy before render graph
construction. Candidate diagnostics include high-risk blur/LUT/color-grading
flags and carrier-aware chain shapes such as `blur-fullscreen`, `lut-only`, and
`blur-color-grade-composelayer`; these fields do not change render policy.
Puppet layers include `puppetAnimationLayers`, top-level
`puppetAnimationLayerInventory`, and configured `puppetAnimationLayerOverrides`
when the lab override flag is used. Mouse diagnostics are recorded under
top-level `mouseParallax`; fixed synthetic input records `inputSource:
synthetic` and `requestedPosition`, timeline input records `inputSource:
synthetic-timeline`, `timeline`, and `timelineElapsedMsAtCapture`, and manual
live input records `inputSource: interactive`. Generated SceneScript/media text
diagnostics are recorded under top-level `generatedTextDiagnostics` with
resolved text, generated texture name, rasterizer, authored font path,
font-load status, resolved font family, alignment fields, authored point size,
effective 100-DPI point-to-pixel and canvas-scaled pixel size, parent chain,
text-card size, local/world bounds, glyph alpha bounds, and visibility
classification. Use
`tools/media_text_diagnostics.py --manifest <manifest.json> --capture
<final.png> --output-dir <dir>` to produce crops and a contact sheet for human
review. The helper maps text bounds as Wallpaper Engine scene coordinates,
with X from the left edge and Y from the bottom edge. Visibility probe runs add
top-level `layerVisibilityOverrides` and
per-layer `debugLayerVisibilityOverride` metadata with requested and original
visibility when a captured layer was touched by `--debug-layer-visibility-overrides`.
Probe runs add top-level `probeLayerIds`, `highRiskProbeLayerIds`, optional
`probeMaxEffects`, `puppetEffectRouteOnly`, and per-layer `debugProbe` metadata
so investigation captures are distinguishable from default policy output. A
sliced probe records the original and kept visible effect counts in
`debugProbe`; a route-only probe records `debugProbe.routeOnly=true` with zero
kept visible effects. Probe limiting applies to explicitly requested
production-allowed layers as well as stripped candidates. A debug-enabled run
exits non-zero if the manifest is missing or reports failed render-target
captures; an empty `strippedCandidates` array is not a failure by itself. Debug
captures are investigation artifacts and should not be committed as smoke
baselines.

Synthetic `__yakkaiMedia` is not a live player read. It is a harness/backend input for native SceneScript widgets that read metadata or timeline state directly or through metadata callbacks such as `mediaPropertiesChanged(event)`. It can also provide `textColor`, `primaryColor`, `secondaryColor`, and `tertiaryColor` to `mediaThumbnailChanged(event)` handlers; optional `albumArtPath` supplies the synthetic `$mediaThumbnail` / `$mediaPreviousThumbnail` image, and if explicit thumbnail colors are omitted those event colors are derived from the album art before SceneScript evaluation. Derived thumbnail colors use a muted primary base below the brightest highlights, a subdued tertiary accent, and blended contrast text instead of hard white/black. Omitted album art falls back to a deterministic color placeholder. Optional `settleSeconds` advances time-based media fades before the final static binding sample. Playback callbacks are always dispatched from the synthetic state: `available=true, playing=false` maps to paused, and unavailable media maps to stopped. It does not force wallpaper-specific feature toggles on, so tests for scenes with a separate media setting must pass that setting in the normal scene-property object. Supported media-widget subtrees keep authored/script-active container transforms so generated text remains attached to authored sliding/container transforms, and structured scripted transform fields fall back to authored `value` data when a runtime-only script cannot be replayed natively. SceneScript color scripts that return a `Vec3` from `update()` preserve that returned vector as an explicit color binding. Timeline-driven solid-layer progress bars preserve script-resolved horizontal origins while applying mirror-aware leading-edge compensation as timeline scale changes, and ordinary aligned solid layers still apply authored horizontal alignment. `--media-state-timeline-json` can reapply those supported progress scale scripts at runtime for synthetic position-review sequences without reparsing the scene. Hidden template variants remain hidden unless authored properties or scripts activate them. Metadata text can render through Qt font rasterization, including authored font files, horizontal/vertical text-card edge anchoring, 100-DPI WE point-size conversion, color, alpha, mirror-aware text placement based on the final effective mirror state, and resolved `maxwidth` raster surfaces that can be wider than the authored card so long media titles are not clipped, with the older generated bitmap text renderer kept as fallback. SceneScript layer lookups preserve authored text-card sizes for `thisScene.getLayer(...).size`; measured glyph alpha bounds stay diagnostic-only. Debug manifests and media text crop reports record renderer choice, authored font-load status, authored card size, and resolved generated texture size for parity triage. Plasma runtime MPRIS support now exists outside the harness for read-only native SceneScript media metadata and supported live timeline progress, but harness synthetic input remains separate. Click controls, audio-reactive media bars, rich text layout, texture-animation media widgets, and broader non-progress live media event-loop updates should stay visually reviewed before promotion.

For fixture-backed media checks, use
`native/scene_harness/tests/fixtures/media/instalock.mp3`. The fixture is owned
by this repository and includes embedded album art plus title, artist, album,
and duration tags. `tools/media_fixture_payload.py` converts it into the
existing synthetic media input and writes extracted album art under a repo-local
output directory:

```bash
tools/media_fixture_payload.py \
  native/scene_harness/tests/fixtures/media/instalock.mp3 \
  --output-dir tmp/arona-media-instalock \
  --property timeofday=1 \
  --property mediaintegration=1 \
  --position-ratio 0.5 \
  --settle-seconds 1.25
```

The helper prints one-line JSON by default so the output can be passed directly
to `--scene-properties-json`, or redirected to a temporary file for repeatable
harness runs. Use `--position-ratio 0.0..1.0` when matching a reference
playback fraction across media files with different durations; use
`--position seconds` when an exact synthetic timeline value matters. Use
`--paused --clock-time HH:MM` for paused clock-widget references, or
`--stopped` for unavailable-player references. It requires `ffprobe` and
`ffmpeg` for metadata and cover-art extraction.

For cross-wallpaper checks, first build a candidate inventory and then run the
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

The inventory is conservative: packaged `scene.pkg` candidates may only expose
media-related project properties, so they are marked `property-only-media` until
unpacked scene script signatures prove a metadata widget or audio-reactive
feature. The matrix writes per-scene captures, logs, scene-property JSON, and a
summary under the chosen repo-local output directory. Add `--debug` only when
generated-text crops are needed; debug-effect capture failures can block the
diagnostic run even if the visual-only capture succeeds.

Use `tools/media_widget_script_audit.py /path/to/scene/scene.json --markdown
tmp/media-widget-script-audit/audit.md` when a widget needs authored callback,
runtime API, and side-effect inventory before changing SceneScript behavior.

When a Windows reference crop exists for a media widget, compare the harness
crop with `tools/media_widget_parity_compare.py` before changing SceneScript or
text rendering behavior. The comparator helps distinguish layout drift, card
styling drift, album-art handling, and text rasterization differences. Region
JSON entries can opt into bright feature-bound detection for visible text or
highlight footprint checks. Use `--dominant-mismatch` and `--review-note` to
record the visual classification in the generated report after review. Add
`--align-search-radius <pixels>` only when same-scale crops appear to be shifted
by a small integer offset; it writes `yakkai-candidate-aligned.png` and reports
overlap RMSE, but matched render resolution and crop coordinates are still
required before using the metrics to justify renderer changes.

For Arona media-widget parity, use
`tools/arona_media_parity_fixture.py` with
`native/scene_harness/tests/fixtures/media/arona_media_parity.json`. The fixture
records exact `2560x1440` crop windows and region files for
`duringPlayback`, `almostEndPlayback`, and `noPlaybackClock` so stale
hand-written crop regions do not drive renderer changes. Pass repeated
`--reference` filters to compare only references that match the candidate
media state. Regions may include nested `sampleRects` for localized album-frame,
progress-line, or background-tint checks; the comparator writes those sample
metrics to both `summary.json` and `summary.md`:

```bash
tools/arona_media_parity_fixture.py \
  --output-dir tmp/arona-media-tightening/exact-fixture-playback-true1440 \
  --candidate-frame tmp/arona-media-tightening/fullscreen-1440/capture.png \
  --compare \
  --reference duringPlayback \
  --reference almostEndPlayback
```

For paused-clock references, add a fixed clock to the synthetic media payload,
for example `"__yakkaiMedia":{"available":true,"playing":false,"clockTime":"09:22"}`.
The harness SceneScript runtime freezes `new Date()` at that clock value so
clock widgets match captured Windows references, while `Date.now()` still
advances with the harness elapsed time for timers and transition settling.
This is only a synthetic fixture control.

Use multi-capture with `--hide-info-overlay` for render regression tests so timing and scene setup stay inside one process and baselines are not tied to local absolute paths. Capture sequences are limited to 3600 frames.
Invalid multi-capture schedules are rejected, including empty fixed-time elements, duplicate fixed timestamps, negative values, overflow, and sequences over 3600 frames.
Capture and recording modes exit with status `7` if no backend readiness signal arrives within 60 seconds. That usually means the scene failed before rendering, a video source did not decode a frame, a web source did not finish loading, the backend wrapper did not forward a readiness signal, or the selected backend does not expose one.
In the default graceful capture exit mode, the harness asks the loaded backend to pause and waits briefly before exiting. This gives the render thread time to drain before Qt tears down the window and graphics context. Immediate capture exit mode intentionally skips that shutdown hook and Qt teardown path after the PNG capture or MP4 recording has been finalized.

The harness is expected to evolve faster than the Plasma package. Keep backend experiments here first, then move stable behavior back into `Wallpaper Engine Scene Native`.

The repo-owned `paper` backend now shares the same fill-mode semantics as the wallpaper package:
- `crop` -> `AspectCrop`
- `fit` -> `AspectFit`
- `stretch` -> `Stretch`

Current status:
- `system` reaches external-texture import and reproduces the known `GL_INVALID_ENUM` path outside Plasma.
- `paper` now reaches:
  - scene parsing
  - render-graph compilation
  - finishing-pass shader compilation
  - first draw
  - external-texture import
- `paper` now also exercises explicit per-image external semaphore interop:
  - GL imports per-image `ready` and `release` semaphores
  - GL waits before sampling and signals release after rendering
  - Vulkan waits before reusing an image
- the current repo-owned blocker is no longer shader compilation
- the harness now shows imported textures being created successfully in the repo-owned backend
- the previous `GL_INVALID_ENUM` signal is stale GL state seen before the next sync point, not a confirmed `glImportMemoryFdEXT` failure
- if visual issues remain after this point, the next likely native issue is renderer/presentation behavior rather than missing interop plumbing
