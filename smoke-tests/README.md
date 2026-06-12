# Smoke Tests

The smoke-test harness is the local visual regression gate for Wallpaper Engine scene rendering.

`quick` and `release` keep stable required coverage. `deep` can include additional visual variants for renderer-risk states. Arona is split into deterministic Day/Sunset/Night variants so LUT changes are tested without depending on the local clock; Night keeps a slightly wider still review threshold for observed animated-particle timing drift while the motion sequence remains temporally matched. Spider-Verse `1591277437` is a deep-only still fixture for godrays, shake, pulse, and stale final-presentation artifact replay. Elaina `3326873240` is split into deep-only Morning/Day/Dusk/Night/Day Night Gradient candidates for SceneScript and time-mode review, with a wider review threshold for expected animated sky/video phase drift in still captures. Alya `3301291394` is a deep-only still fixture for Clock/Date SceneScript text binding discovery and generated text-layer representation. Girl and Fluorescent Beach `2788691565` is a deep-only sequence fixture for overlay video texture, water-effect motion, and particle coverage. Cyber City Parkour `1576514332` is a deep-only still fixture for static model, material, lighting, composelayer, and particle sprite coverage. Rain Drops `779812076`, Audio Visualizer `893418273`, and TIMDRIFT II `874499201` are active deep-only sequence fixtures for WE web-video, WE Web/audio with synthetic audio, and plain WE Video motion coverage. CWAV `1509243786` and Shiroko Live2D `2478419118` remain harness-backed candidates, and Silk `823274093` is deferred as an interactive-web candidate because it needs synthesized input before it produces useful motion.

## Commands

```bash
./smoke-tests/run.sh --suite quick
./smoke-tests/run.sh --suite deep
./smoke-tests/run.sh --suite release --strict --require-assets
./smoke-tests/run.sh --suite deep --write-candidates
./smoke-tests/run.sh --promote smoke-tests/artifacts/tmp/yakkai-smoke/<run-id>
./smoke-tests/run.sh --coverage
./smoke-tests/run.sh --list
```

## Result States

- `pass`: structural checks and visual metrics are within thresholds.
- `review`: visual drift exceeded the warning threshold but not the hard-fail threshold. The command exits zero unless `--strict` is set.
- `fail`: blank frames, missing expected motion, structural errors, shader/material errors, required dependency failures, missing expected frames, capture size mismatches, or large visual drift.
- `skip`: local Workshop assets are missing. Skips exit zero unless `--require-assets` is set for required scenes.

## Scene Variants

Scene entries in `scenes.json` can define `variants`. Variants inherit base fields such as source, required status, captures, sequences, thresholds, features, and expectations, while supplying their own ids, names, gates, baseline prefixes, and property overrides.

`scenePropertyOverrides` values are raw Wallpaper Engine user-property values. The runner loads the project `project.json` defaults for scene and web entries, merges the overrides over those defaults, and passes compact JSON to the harness with `--scene-properties-json`.

Candidate manifests can use `projectType: "scene" | "video" | "web"`. Use `harnessArgs` only for narrow harness flags needed by a candidate, such as `--debug-synthetic-audio` for WE Web audio visualizer motion coverage or `--capture-exit-mode immediate` for QtWebEngine sequence captures whose frames are complete before browser teardown. Audio visualizer fixtures should keep motion thresholds strict but may need wider RMSE thresholds than scene/video fixtures because browser animation timing and audio smoothing can change the exact bar shape between otherwise valid runs. A candidate can also define `reviewVideo` with `name`, `durationMs`, `fps`, and optional `startDelayMs` to ask the harness for a live MP4 recording artifact in addition to PNG regression captures.

`baselinePrefix` moves inherited capture and sequence baseline paths into a variant-specific directory, so related cases can share schedules without sharing PNG baselines.

## Baselines

PNG frames under `smoke-tests/baselines/` are the versioned comparison source of truth. Generated MP4/WebM clips are artifacts for human animation review. Sequence clips are encoded from captured PNG sequences at each sequence's configured `intervalMs`; `reviewVideo` clips are live harness recordings produced by piping repeated `grabWindow()` frames to `ffmpeg`. Clips, diffs, logs, and summaries stay out of git.
Smoke-test captures hide the harness info overlay so baselines contain scene content instead of local machine paths.
Capture timestamps are measured from the harness backend's first rendered frame, not from process startup or QML window creation.
Animated and video-heavy sequences can opt into a small `temporalToleranceFrames` window in `scenes.json`; the runner still compares PNGs, but it uses the closest nearby baseline frame to absorb animation or decoder timing jitter. Video-texture fixtures should prefer sequence baselines over standalone still captures when the decoder phase is not stable across harness invocations.

Baseline updates are two-step:

```bash
./smoke-tests/run.sh --suite deep --write-candidates
./smoke-tests/run.sh --promote smoke-tests/artifacts/tmp/yakkai-smoke/<run-id>
```

Review the artifact bundle before promotion. Check camera angle, visible elements, character position, color balance, composition, effect intensity, and motion.

## Coverage Matrix

`smoke-tests/coverage-matrix.json` tracks active fixtures, candidate fixtures, and harness gaps by renderer limitation. Run `./smoke-tests/run.sh --coverage` before starting a renderer phase to confirm the phase has active or candidate coverage. Candidate coverage does not change quick/deep/release behavior until a scene is reviewed and promoted into `smoke-tests/scenes.json`.

## Assets

See `smoke-tests/ASSETS.md` for Workshop links and scene purpose.
