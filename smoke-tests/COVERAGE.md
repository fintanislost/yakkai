# Render Coverage Matrix

This file summarizes which Wallpaper Engine renderer limitations have active baselines, candidate fixtures, or deferred harness requirements. It is a human snapshot of `smoke-tests/coverage-matrix.json`; regenerate the table with:

```bash
./smoke-tests/run.sh --coverage
```

Detailed stripped-effect family blockers from Phase 3.3 are tracked in
`docs/renderer-effect-candidate-backlog.md`; keep this file focused on fixture
coverage status.

Media-widget runtime follow-ups, including read-only MPRIS coverage and
deferred controls/audio/texture-animation work, are tracked in
`docs/media-feature-backlog.md`.

Coverage buckets track source Workshop ids. Smoke variants use case-specific ids such as `3228578419-day`, but report `sourceSceneId` so coverage remains tied to the underlying Workshop asset.

| Bucket | Name | Required | Best | Satisfied | Scenes |
| --- | --- | --- | --- | --- | --- |
| puppet-effects-alpha | Puppet Effects And Alpha | active | active | yes | 3228578419 |
| main-video-texture | Main Embedded Video Texture | active | active | yes | 3327063360 |
| effect-chain-regression | Effect Chain Regression Replay | candidate | active | yes | 1591277437 |
| scene-script-bindings | SceneScript Bindings And Runtime | candidate | active | yes | 3301291394, 3326873240 |
| overlay-video-texture | Overlay Or Small Embedded Video Texture | candidate | active | yes | 2788691565 |
| static-model-material-lighting | Static Model, Material, And Lighting | candidate | active | yes | 1576514332 |
| we-web-wallpaper | Wallpaper Engine Web Project And Audio Shim | candidate | active | yes | 1509243786, 893418273 |
| we-web-video-wallpaper | Wallpaper Engine Web Project With Video Element | candidate | active | yes | 779812076 |
| we-video-wallpaper | Wallpaper Engine Video Project | candidate | active | yes | 2478419118, 874499201 |

## Active Baselines

- `3228578419` Sleeping Arona: puppet, flare/lens, particles, alpha-sensitive composition.
- `3327063360` Shiroko Night Video: main embedded video texture, particles, effect chain.
- `1591277437` Spider-Verse: deep-only godrays, shake, pulse, and stale final-presentation artifact replay.
- `3326873240` Elaina Day Night Gradient: non-puppet SceneScript, day-night tint/property behavior, and deep-only Morning/Day/Dusk/Night/Day Night Gradient variants.
- `3301291394` Alya Clock and Date: deep-only Clock/Date text SceneScript bindings, generated text-layer representation, and user property gating.
- `2788691565` Girl and Fluorescent Beach: deep-only overlay video/water-effect motion sequence.
- `1576514332` Cyber City Parkour: deep-only static model/material/light/composelayer still coverage.
- `779812076` Rain Drops: deep-only WE web-video/canvas rain animation sequence coverage.
- `893418273` Audio Visualizer: deep-only WE Web/audio visualizer sequence coverage using harness-only synthetic audio. This fixture is motion/compatibility coverage, not a frame-exact oracle: browser animation timing and visualizer smoothing can shift the exact bar shape, so its RMSE threshold is intentionally wider while `minMotionRmse` still requires visible motion.
- `874499201` TIMDRIFT II Mountains/Clouds Timelapse: deep-only plain WE Video motion sequence coverage.

## Candidate Fixtures

- `1509243786` CWAV Engine: WE web project candidate using the web harness, WE viewport/property compatibility, and harness-only synthetic audio for visible visualizer motion. Real Linux audio capture remains deferred. Local review on 2026-06-09 showed the authored defaults can place the clock and date tightly enough to overlap at the 1280x720 harness size; keep tests on the authored defaults and do not add fixture-only placement overrides. A far-future Plasma settings feature should expose or import WE Web user properties so users can adjust values such as `DateY` themselves.
- `2478419118` Blue Archive Shiroko Live2D: WE video decode candidate using the video harness against `shiroko.mp4`; sampled windows showed motion, but the source is visually calm and is not the strong-motion fixture.

## Phase 1 Probe Notes

- `1591277437`: local structural validation passed. Windows WE comparison on 2026-06-07 confirmed the bottom-right-to-center diagonal godray/artifact was absent from Windows reference frames but present in Yakkai still and motion captures. After the generic render-target synchronization fix removed the no-debug artifact, human review approved promotion to active deep still coverage.
- `3228578419`: Arona Day/Sunset/Night variants provide deterministic time-of-day coverage. Night keeps a scoped `rmseReview` threshold of `0.04` because repeated targeted runs showed valid still RMSE spread below that level while the motion sequence passed with temporal matching.
- `3301291394`: local structural validation now resolves Clock/Date text SceneScript bindings with property counts for `origin` and `text`, generated text layers are represented in the scene graph/logs, and human review approved the character/shoulder composition for deep baseline promotion.
- `3326873240`: local structural validation passed and reported 23 QuickJS binding events, 20 binding layers, 20 unsupported media-integration layers, and 44 SceneScript/runtime gaps. Human review approved Morning/Day/Dusk/Night/Day Night Gradient stills, and deep smoke passed after promoting variant baselines with an Elaina-specific review threshold for expected animated sky/video phase drift.
- `2788691565`: local structural validation passed with one composelayer warning; still and motion artifacts under `/tmp/yakkai-phase1-candidates/2788691565` were human-reviewed and promoted as deep-only motion sequence coverage.
- `1576514332`: local structural validation now passes with no material-loading warnings after TEXB v4 sprite-header parsing and local composelayer publishing fixes. Human review approved the deep-only still capture on 2026-06-07 and the baseline was promoted.
- `779812076`, `893418273`, and `874499201`: candidate-only smoke run on 2026-06-11 generated reviewed sequence artifacts under `smoke-tests/artifacts/tmp/web-video-candidate-slice/runs/20260612T024828575962Z`, and promoted PNG baselines for deep-only WE web-video, synthetic-audio web visualizer, and plain WE video motion coverage. Optional live `reviewVideo` recording was intentionally left out of these active fixtures after QtWebEngine/video recording paths produced signal exits while PNG sequence capture completed cleanly. Follow-up fresh-vs-fresh checks for `893418273` measured valid audio visualizer RMSE up to roughly `0.15`, so that fixture uses a tolerant baseline band plus motion enforcement.

## Harness Gaps

No current harness-only blocker remains for the active PNG-sequence WE Web and WE Video fixtures. Live `reviewVideo` recording remains optional and is not part of the active gate for these fixtures. Interactive-web wallpapers such as `823274093` Silk remain deferred until the harness can synthesize input.

## Promotion Rule

Candidate scenes must stay out of active suites until they have reviewed candidate artifacts and promoted PNG baselines. Promote into `deep` first, then into `release` only after repeated strict runs are stable.
