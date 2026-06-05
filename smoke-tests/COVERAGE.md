# Render Coverage Matrix

This file summarizes which Wallpaper Engine renderer limitations have active baselines, candidate fixtures, or deferred harness requirements. It is a human snapshot of `smoke-tests/coverage-matrix.json`; regenerate the table with:

```bash
./smoke-tests/run.sh --coverage
```

Detailed stripped-effect family blockers from Phase 3.3 are tracked in
`docs/renderer-effect-candidate-backlog.md`; keep this file focused on fixture
coverage status.

Coverage buckets track source Workshop ids. Smoke variants use case-specific ids such as `3228578419-day`, but report `sourceSceneId` so coverage remains tied to the underlying Workshop asset.

| Bucket | Name | Required | Best | Satisfied | Scenes |
| --- | --- | --- | --- | --- | --- |
| puppet-effects-alpha | Puppet Effects And Alpha | active | active | yes | 3228578419 |
| main-video-texture | Main Embedded Video Texture | active | active | yes | 3327063360 |
| effect-chain-regression | Effect Chain Regression Replay | candidate | candidate | yes | 1591277437 |
| scene-script-bindings | SceneScript Bindings And Runtime | candidate | candidate | yes | 3301291394, 3326873240 |
| overlay-video-texture | Overlay Or Small Embedded Video Texture | candidate | candidate | yes | 2788691565 |
| static-model-material-lighting | Static Model, Material, And Lighting | candidate | candidate | yes | 1576514332 |
| we-web-wallpaper | Wallpaper Engine Web Project | requiresHarness | requiresHarness | yes | 1509243786 |
| we-video-wallpaper | Wallpaper Engine Video Project | requiresHarness | requiresHarness | yes | 2478419118 |

## Active Baselines

- `3228578419` Sleeping Arona: puppet, flare/lens, particles, alpha-sensitive composition.
- `3327063360` Shiroko Night Video: main embedded video texture, particles, effect chain.

## Candidate Fixtures

- `1591277437` Spider-Verse: effect-chain regression replay for godrays, shake, and pulse.
- `3301291394` Alya Clock and Date: SceneScript clock/date candidate and user properties; current validator coverage is stronger for effect-chain behavior than script binding behavior.
- `3326873240` Elaina Day Night Gradient: non-puppet SceneScript and tint/property behavior; first script-runtime review fixture with deep-only Morning/Day/Dusk/Night/Day Night Gradient variants.
- `2788691565` Girl and Fluorescent Beach: small or overlay embedded video texture policy.
- `1576514332` Cyber City Parkour: static models, material fallback, camera framing, composelayers, and lights.

## Phase 1 Probe Notes

- `1591277437`: local structural validation passed, but visual review found a bottom-right-to-center diagonal godray/artifact that needs Wallpaper Engine comparison before promotion.
- `3301291394`: local structural validation passed, but the validator reported no QuickJS bindings despite the candidate's script purpose; visual review is required before promotion.
- `3326873240`: local structural validation passed and reported 23 QuickJS binding events, 20 binding layers, 20 unsupported media-integration layers, and 44 SceneScript/runtime gaps. Time-mode smoke review artifacts for Morning/Day/Dusk/Night/Day Night Gradient rendered successfully with no hard failures; they remain candidate review cases, not active baselines.
- `2788691565`: local structural validation passed; use later frame-sequence review to decide whether embedded video regions should be static or animated.
- `1576514332`: local structural validation passed with material-loading warnings; review model/material fidelity before promotion.

## Harness Gaps

- `1509243786` CWAV Engine: Wallpaper Engine web project; requires a web or Plasma-live capture harness.
- `2478419118` Blue Archive Shiroko Live2D: Wallpaper Engine video project; requires a video wallpaper or Plasma-live capture harness.

## Promotion Rule

Candidate scenes must stay out of active suites until they have reviewed candidate artifacts and promoted PNG baselines. Promote into `deep` first, then into `release` only after repeated strict runs are stable.
