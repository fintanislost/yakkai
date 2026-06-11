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
- `--hide-info-overlay` hides the local debug overlay so captured PNGs contain only scene pixels.
- `--mouse`
- `--unmuted`

Capture flags:
- `--capture path --capture-delay-ms ms` waits for the backend readiness signal, then saves one capture after the requested delay.
- `--capture-dir path --capture-times-ms 1000,3000,8000` waits for the backend readiness signal, then captures fixed timestamps in one harness process and writes `frame-00001000ms.png`, `frame-00003000ms.png`, and `frame-00008000ms.png`.
- `--capture-dir path --capture-sequence 5000:60:33` waits for the backend readiness signal, then captures a sequence starting at 5000ms with 60 frames spaced 33ms apart and writes `frame-0000.png`, `frame-0001.png`, and so on.
- `--capture-exit-mode graceful|immediate` controls harness-only shutdown after capture. `graceful` is the default and runs the backend shutdown hook before normal Qt exit. `immediate` exits the process after the final capture save returns and is intended only for QtWebEngine smoke/debug captures where teardown is known to trip after frames are already written. Do not use it with `--debug-effect-captures`.
- `--record path --record-duration-ms ms --record-fps fps` waits for backend readiness, repeatedly grabs the harness window, and pipes raw RGBA frames to `ffmpeg` for a live MP4 review artifact. `--record-start-delay-ms ms` waits after readiness before the first recorded frame. Recording cannot be combined with PNG capture flags; PNG captures remain the regression baseline source of truth.

Debug flags:
- `--debug-synthetic-audio` emits harness-only synthetic Wallpaper Engine web audio data into `window.wpeQml.sigAudio`. Use it only with `--backend web` when validating audio-reactive web wallpaper behavior; it does not capture real Linux audio and does not change Plasma wallpaper behavior.
- `--debug-synthetic-audio-bins count` and `--debug-synthetic-audio-interval-ms ms` tune the synthetic web audio payload shape and timer interval.
- `--debug-effect-captures path` writes effect-chain diagnostic captures and `manifest.json` for the repo-owned `paper` backend. The flag is rejected for `--backend system`.
- `--debug-effect-capture-delay-ms ms` waits for the requested scene time before dumping debug effect captures. Use the same value as `--capture-delay-ms` when a PNG and TGA evidence need to describe the same delayed frame.
- `--debug-effect-probe-layers ids` renders specific stripped puppet mixed-chain layer IDs only for debug capture. It requires `--debug-effect-captures`; comma-separated IDs such as `168,22` are accepted.
- `--debug-effect-probe-high-risk-layers ids` renders specific stripped blur/LUT/color-grading layer IDs only for debug capture. It requires `--debug-effect-captures`; comma-separated IDs such as `53,155` are accepted.
- `--debug-effect-probe-channelmap-slots slots` is quarantined. The derived sidecar channelmap render path produced glitchy puppet fragments before effects ran, so the harness rejects the flag while the negative evidence remains documented.
- `--debug-effect-probe-max-effects count` limits forced probe layers to their first `count` visible effects. It requires `--debug-effect-captures` plus one of the probe layer lists, and is intended for prefix-slicing a stripped chain during investigation.
- `--debug-puppet-effect-route-only` keeps the puppet offscreen/final-display route active while dropping all visible effects on explicitly requested puppet probe layers. It requires `--debug-effect-captures` and `--debug-effect-probe-layers`, and is intended to separate route/composition bugs from shader bugs.
- `--debug-puppet-effect-final-mesh layer-card|image-space|deferred-puppet-final` overrides the puppet effect final-display route for explicit puppet effect probes. The normal non-channelmap puppet effect route is now `deferred-puppet-final` by default: authored effects run on the flat crop-sheet source, then a puppet-skinned final mesh publishes the effect result. Use `layer-card` or `image-space` only to force legacy diagnostics.
- `--debug-puppet-animation-layer-overrides rules` applies harness-only puppet animation layer overrides and requires `--debug-effect-captures`. Rules are semicolon-separated `layerId:animationId:key=value[,key=value]` entries; supported keys are `visible`, `paused`, `additive`, `blend`, `rate`, and `curTime`.
- `--puppet-simulation off|diagnostic|runtime` sets the investigation-only puppet secondary-motion mode for this harness process. It is off by default and should not be used as a production policy decision without normal render validation and visual review.

The debug manifest records scene id, layer id/name/type, `EffectPolicy` preserve/strip reason, effect names, material shaders, render targets, render-target dimensions/format, pass load operations, blend state, color masks, output paths, capture delay, actual shader time (`shaderTimeSeconds`), frame time (`frameTimeSeconds`), effective capture readiness time (`effectiveCaptureTimeSeconds`), and capture status. Effect publish metadata includes parent/route fields plus final-publish composition evidence such as local transforms, final source texture, standalone final blend, display node ordinal, mesh bounds, effect-input viewport size/expansion state, and whether the effect-input material preserved the layer blend mode. Puppet publish coverage records bone and parent names, primary and weighted vertex/triangle coverage, secondary-only weighted slots, weighted layer-local bounds/centroid, and simulation metadata presence for each cutout slot. It also records manifest-only `strippedCandidates` for effect chains removed by policy before render graph construction. Candidate diagnostics include high-risk blur/LUT/color-grading flags and carrier-aware chain shapes such as `blur-fullscreen`, `lut-only`, and `blur-color-grade-composelayer`; these fields do not change render policy. Puppet layers include `puppetAnimationLayers`, top-level `puppetAnimationLayerInventory`, and configured `puppetAnimationLayerOverrides` when the lab override flag is used. Probe runs add top-level `probeLayerIds`, `highRiskProbeLayerIds`, optional `probeMaxEffects`, `puppetEffectRouteOnly`, and per-layer `debugProbe` metadata so investigation captures are distinguishable from default policy output. A sliced probe records the original and kept visible effect counts in `debugProbe`; a route-only probe records `debugProbe.routeOnly=true` with zero kept visible effects. Probe limiting applies to explicitly requested production-allowed layers as well as stripped candidates. A debug-enabled run exits non-zero if the manifest is missing or reports failed render-target captures; an empty `strippedCandidates` array is not a failure by itself. Debug captures are investigation artifacts and should not be committed as smoke baselines.

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
