# Renderer Fixture Candidate Slices

This is an untracked working document for expanding render regression coverage
without making the active smoke suites noisy. The active manifest should only
contain scenes with reviewed baselines. Candidate fixtures should move through
these slices before they are added to `smoke-tests/scenes.json`.

## Current Baselined Coverage

| Scene ID | Title | Gates | Coverage |
| --- | --- | --- | --- |
| 3228578419 | Sleeping Arona | quick, deep, release | Puppet, flare/lens, particles, alpha-sensitive composition, effect stripping constraints |
| 3327063360 | Shiroko Night Video | quick, deep, release | Main embedded video texture, particles, effect chain, decoder timing tolerance |

## Candidate Intake Rules

Do not add a candidate directly to `release`.

1. Confirm the Workshop item is locally installed and still relevant.
2. Identify the coverage bucket it adds beyond the current baselines.
3. Run `tools/validate-scene.sh <scene_id>` for scene packages when supported.
4. Generate `--write-candidates` artifacts in a temporary run.
5. Do a visual review of stills and review clips.
6. Promote baselines only when the candidate run has no failed scenes or failed frames.
7. Add the candidate to `deep` first, optional unless it is promoted to required coverage.
8. Move to `release` only after repeated strict runs are stable.

## Phase 0 Candidate Slices

### Slice A: Spider-Verse Godrays Regression Candidate

- Scene ID: `1591277437`
- Title: `[4K] Spider-Man: Into the Spider-Verse ~ Animated Wallpaper with Music`
- Local type: `scene`
- Why it matters: known prior visual issue; package strings show `godrays`, `shake`, and `pulse` effect paths.
- Coverage bucket: effect-chain fidelity and prior visual regression replay.
- Proposed Phase 0 work:
  - Run `tools/validate-scene.sh 1591277437`.
  - Add one still around 8000-10000ms and one short motion sequence if particles/effects move.
  - Review godray placement, brightness, visible character/background composition, and effect intensity.
- Future phase fit: Phase 3, per-layer effect alpha and effect-family re-enable work.
- Add to gate: optional `deep` after baseline promotion.
- Phase 1 status: locally present on 2026-05-27; structural validation passed with one composelayer warning; visual review found a bottom-right-to-center diagonal godray/artifact that must be compared against Wallpaper Engine before baseline promotion; coverage tracked in `smoke-tests/coverage-matrix.json`.

### Slice B: Alya Clock And Date SceneScript Candidate

- Scene ID: `3301291394`
- Title: `Alya ED 5 - Alya Sometimes Hides Her Feelings in Russian | Clock & Date`
- Local type: `scene`
- Why it matters: explicit clock/date SceneScript bindings and user property toggles.
- Coverage bucket: SceneScript runtime, user properties, generated text/solid layers.
- Proposed Phase 0 work:
  - Run `tools/validate-scene.sh 3301291394`.
  - Capture a still after scripts initialize.
  - Consider adding a longer second timestamp only if clock/date output visibly changes.
  - Review whether clock/date elements render, text placement is stable, and scripts do not produce fatal errors.
- Future phase fit: Phase 6, persistent SceneScript runtime and update lifecycle.
- Add to gate: optional `deep` after baseline promotion.
- Phase 1 status: locally present on 2026-05-27; structural validation passed with one composelayer warning; validator reported no QuickJS bindings, so script coverage needs visual review before baseline promotion; coverage tracked in `smoke-tests/coverage-matrix.json`.

### Slice C: Elaina Day-Night Script Candidate

- Scene ID: `3326873240`
- Title: `Elaina Day Night Gradient`
- Local type: `scene`
- Why it matters: string scan shows high SceneScript/user-property density without puppet MDL assets.
- Coverage bucket: non-puppet SceneScript, day-night/tint behavior, scene property bindings.
- Proposed Phase 0 work:
  - Run `tools/validate-scene.sh 3326873240`.
  - Capture at least two timestamps if the scene changes by time or scripted state.
  - Review tint/color balance, layer visibility, and whether script-driven changes are visible.
- Future phase fit: Phase 6, SceneScript runtime; possibly Phase 2 script boundary refactor.
- Add to gate: optional `deep` after baseline promotion.
- Phase 1 status: locally present on 2026-05-27; structural validation passed with effect/composelayer warnings and 23 QuickJS bindings resolved; coverage tracked in `smoke-tests/coverage-matrix.json`.

### Slice D: Embedded Overlay Video Texture Candidate

- Scene ID: `2788691565`
- Title: `Girl and Fluorescent Beach`
- Local type: `scene`
- Why it matters: package string scan found embedded `.mp4` references inside a scene package.
- Coverage bucket: small or overlay video texture policy, static-first-frame versus continuous playback.
- Proposed Phase 0 work:
  - Run `tools/validate-scene.sh 2788691565`.
  - Capture a still and a frame sequence.
  - Review whether embedded video areas are present, whether expected motion is static or animated, and whether the chosen policy is documented in the baseline.
  - Record whether this is main-video-like or overlay-video-like before promoting.
- Future phase fit: Phase 4, video texture fidelity and decoder budget policy.
- Add to gate: optional `deep` after baseline promotion.
- Phase 1 status: locally present on 2026-05-27; structural validation passed with one composelayer warning; coverage tracked in `smoke-tests/coverage-matrix.json`.

### Slice E: Static Model And Lighting Candidate

- Scene ID: `1576514332`
- Title: `Cyber City Parkour`
- Local type: `scene`
- Why it matters: non-puppet scene with many model references, light references, and composelayers.
- Coverage bucket: static model fallback, camera framing, material selection, point-light uniform fidelity.
- Proposed Phase 0 work:
  - Run `tools/validate-scene.sh 1576514332`.
  - Capture a still after startup and inspect framing before any baseline promotion.
  - Review camera angle, model orientation, visible foreground/background elements, light balance, and material fidelity.
- Future phase fit: Phase 5, static model, material, and lighting fidelity.
- Add to gate: optional `deep` after baseline promotion.
- Phase 1 status: locally present on 2026-05-27; structural validation passed with material-loading warnings that need review before baseline promotion; coverage tracked in `smoke-tests/coverage-matrix.json`.

### Slice F: WE Web Harness Candidate

- Scene ID: `1509243786`
- Title: `CWAV Engine`
- Local type: `web`
- Why it matters: represents WE Web wallpaper behavior and audio/media integration, not native scene rendering.
- Coverage bucket: web wallpaper picker/render path, future web harness.
- Proposed Phase 0 work:
  - Do not add to the native scene smoke manifest.
  - Define a separate web capture harness or Plasma-live capture path first.
  - Once a harness exists, capture a deterministic default preset state.
- Future phase fit: Stretch Phase, Plasma-live capture; possible separate web regression harness.
- Add to gate: not until a web-specific harness exists.
- Phase 1 status: locally present on 2026-05-27; not active in `smoke-tests/scenes.json`; requires a web or Plasma-live harness before baseline promotion; coverage tracked in `smoke-tests/coverage-matrix.json`.

### Slice G: WE Video Harness Candidate

- Scene ID: `2478419118`
- Title: `Blue Archive - Shiroko Live2D`
- Local type: `video`
- Why it matters: represents plain WE Video project handling, which is separate from embedded video textures inside scene packages.
- Coverage bucket: WE Video picker/render path and QtMultimedia playback.
- Proposed Phase 0 work:
  - Do not add to the native scene smoke manifest.
  - Define a video wallpaper capture harness first.
  - Once a harness exists, capture a short deterministic playback sequence with temporal tolerance.
- Future phase fit: Stretch Phase, Plasma-live capture; possibly separate video regression harness.
- Add to gate: not until a video-specific harness exists.
- Phase 1 status: locally present on 2026-05-27; not active in `smoke-tests/scenes.json`; requires a video wallpaper or Plasma-live harness before baseline promotion; coverage tracked in `smoke-tests/coverage-matrix.json`.

## Future Fix Backlog

- Add manifest-level fixture status such as `candidate`, `deep`, `release`, and `requiresHarness`.
- Add artifact metadata recording Qt backend, GPU/driver, ImageMagick version, FFmpeg version, capture size, shader-cache state, and source Workshop path.
- Add baseline metadata files next to PNG baselines so temporal tolerances and manual review decisions are auditable.
- Add a candidate discovery tool that scans local Workshop items and reports probable buckets: script-heavy, model-heavy, effect-heavy, embedded video, web, and video.
- Add separate capture support for WE Web and WE Video projects before putting those projects in strict gates.
- Add a small fixture review checklist to `smoke-tests/ASSETS.md` once the candidate process stabilizes.
