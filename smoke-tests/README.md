# Smoke Tests

The smoke-test harness is the local visual regression gate for Wallpaper Engine scene rendering.

`quick` and `release` keep stable required coverage. `deep` can include additional visual variants for renderer-risk states. Arona is split into deterministic Day/Sunset/Night variants so LUT changes are tested without depending on the local clock. Spider-Verse `1591277437` is a deep-only still fixture for godrays, shake, pulse, and stale final-presentation artifact replay. Elaina `3326873240` is split into deep-only Morning/Day/Dusk/Night/Day Night Gradient candidates for SceneScript and time-mode review, with a wider review threshold for expected animated sky/video phase drift in still captures. Girl and Fluorescent Beach `2788691565` is a deep-only sequence fixture for overlay video texture, water-effect motion, and particle coverage. Cyber City Parkour `1576514332` is a deep-only still fixture for static model, material, lighting, composelayer, and particle sprite coverage.

## Commands

```bash
./smoke-tests/run.sh --suite quick
./smoke-tests/run.sh --suite deep
./smoke-tests/run.sh --suite release --strict --require-assets
./smoke-tests/run.sh --suite deep --write-candidates
./smoke-tests/run.sh --promote /tmp/yakkai-smoke/<run-id>
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

`scenePropertyOverrides` values are raw Wallpaper Engine user-property values. The runner loads the scene's `project.json` defaults, merges the overrides over those defaults, and passes compact JSON to the harness with `--scene-properties-json`.

`baselinePrefix` moves inherited capture and sequence baseline paths into a variant-specific directory, so related cases can share schedules without sharing PNG baselines.

## Baselines

PNG frames under `smoke-tests/baselines/` are the versioned comparison source of truth. Generated MP4/WebM clips, diffs, logs, and summaries are artifacts and stay out of git.
Smoke-test captures hide the harness info overlay so baselines contain scene content instead of local machine paths.
Capture timestamps are measured from the harness backend's first rendered frame, not from process startup or QML window creation.
Animated and video-heavy sequences can opt into a small `temporalToleranceFrames` window in `scenes.json`; the runner still compares PNGs, but it uses the closest nearby baseline frame to absorb animation or decoder timing jitter. Video-texture fixtures should prefer sequence baselines over standalone still captures when the decoder phase is not stable across harness invocations.

Baseline updates are two-step:

```bash
./smoke-tests/run.sh --suite deep --write-candidates
./smoke-tests/run.sh --promote /tmp/yakkai-smoke/<run-id>
```

Review the artifact bundle before promotion. Check camera angle, visible elements, character position, color balance, composition, effect intensity, and motion.

## Coverage Matrix

`smoke-tests/coverage-matrix.json` tracks active fixtures, candidate fixtures, and harness gaps by renderer limitation. Run `./smoke-tests/run.sh --coverage` before starting a renderer phase to confirm the phase has active or candidate coverage. Candidate coverage does not change quick/deep/release behavior until a scene is reviewed and promoted into `smoke-tests/scenes.json`.

## Assets

See `smoke-tests/ASSETS.md` for Workshop links and scene purpose.
