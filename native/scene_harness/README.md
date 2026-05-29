# Scene Harness

This app is the standalone debugging surface for `Wallpaper Engine Scene Native`.

Why it exists:
- native scene bugs should be reproducible outside `plasmashell`
- scene backend launches should be testable without risking a Plasma crash
- renderer state should be inspectable before the backend is wired back into the wallpaper package

Current backends:
- `system`: uses the currently installed `com.github.catsout.wallpaperEngineKde` `SceneViewer`
- `paper`: uses the repo-owned `io.team7.scene` native backend module

Example usage after building:

```bash
./yakkai_scene_harness \
  --backend system \
  --source /path/to/scene.json \
  --assets /path/to/wallpaper_engine/assets \
  --fill crop
```

Useful flags:
- `--backend system|paper`
- `--source /absolute/path/to/scene.json`
- `--assets /absolute/path/to/wallpaper_engine/assets`
- `--fill crop|fit|stretch`
- `--window-size WIDTHxHEIGHT` (defaults to `1600x900`; smoke tests set this from `scenes.json`)
- `--hide-info-overlay` hides the local debug overlay so captured PNGs contain only scene pixels.
- `--mouse`
- `--unmuted`

Capture flags:
- `--capture path --capture-delay-ms ms` keeps the original single-capture behavior.
- `--capture-dir path --capture-times-ms 1000,3000,8000` captures fixed timestamps in one harness process and writes `frame-00001000ms.png`, `frame-00003000ms.png`, and `frame-00008000ms.png`.
- `--capture-dir path --capture-sequence 5000:60:33` captures a sequence starting at 5000ms with 60 frames spaced 33ms apart and writes `frame-0000.png`, `frame-0001.png`, and so on.

Debug flags:
- `--debug-effect-captures path` writes effect-chain diagnostic captures and `manifest.json` for the repo-owned `paper` backend. The flag is rejected for `--backend system`.
- `--debug-effect-probe-layers ids` renders specific stripped puppet mixed-chain layer IDs only for debug capture. It requires `--debug-effect-captures`; comma-separated IDs such as `168,22` are accepted.

The debug manifest records scene id, layer id/name/type, `EffectPolicy` preserve/strip reason, effect names, material shaders, render targets, render-target dimensions/format, pass load operations, blend state, color masks, output paths, and capture status. It also records manifest-only `strippedCandidates` for effect chains removed by policy before render graph construction. Probe runs add top-level `probeLayerIds` plus per-layer `debugProbe` metadata so investigation captures are distinguishable from default policy output. A debug-enabled run exits non-zero if the manifest is missing or reports failed render-target captures; an empty `strippedCandidates` array is not a failure by itself. Debug captures are investigation artifacts and should not be committed as smoke baselines.

Use multi-capture with `--hide-info-overlay` for render regression tests so timing and scene setup stay inside one process and baselines are not tied to local absolute paths. Capture sequences are limited to 3600 frames.
Invalid multi-capture schedules are rejected, including empty fixed-time elements, duplicate fixed timestamps, negative values, overflow, and sequences over 3600 frames.
After the final capture, the harness asks the loaded backend to pause and waits briefly before exiting. This gives the render thread time to drain before Qt tears down the window and graphics context.

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
