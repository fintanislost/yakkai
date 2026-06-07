# Spider-Verse Godray Investigation

## Scene

- Scene ID: `1591277437`
- Title: `[4K] Spider-Man: Into the Spider-Verse ~ Animated Wallpaper with Music`
- Date: `2026-06-07`

## Current Status

Promoted. The generic renderer synchronization fix removed the Yakkai-only no-debug artifact, and human visual review approved the fixed contact sheet for active deep smoke coverage.

## Original Decision

Promotion was initially blocked. The bottom-right-to-center diagonal godray/artifact was present and materially stronger in Yakkai than in the Windows Wallpaper Engine reference.

This had to be fixed as a generic effect-chain/godray renderer issue. No scene-specific override for `1591277437` was added.

## Evidence

- Yakkai still: `/tmp/yakkai-spiderverse-reference-gate/yakkai/stills/frame-00010000ms.png`
- Windows still: `/tmp/yakkai-spiderverse-reference-gate/windows/stills/still-1280x720.png`
- Contact sheet: `/tmp/yakkai-spiderverse-reference-gate/comparison/still-contact-sheet.png`
- Diff: `/tmp/yakkai-spiderverse-reference-gate/comparison/still-diff.png`
- Windows sampled frames: `/tmp/yakkai-spiderverse-reference-gate/comparison/windows-sampled-frames.png`
- Yakkai sampled frames: `/tmp/yakkai-spiderverse-reference-gate/comparison/yakkai-sampled-frames.png`
- Yakkai motion clip: `/tmp/yakkai-spiderverse-reference-gate/yakkai/motion-10000.mp4`
- Validator still: `/tmp/yakkai-spiderverse-reference-gate/effect-captures/validate-1591277437.png`
- Validator log: `/tmp/yakkai-spiderverse-reference-gate/effect-captures/validate-1591277437.log`
- Effect manifest: `/tmp/yakkai-spiderverse-reference-gate/effect-captures/effects-validator/manifest.json`

The still comparison RMSE was `0.03699`, but the artifact is localized and visually obvious, so global RMSE understates the problem.

## Visual Finding

The Windows reference still and sampled Windows frames do not show a bright bottom-right-to-center beam. The Yakkai still and sampled Yakkai motion frames consistently show a bright diagonal wedge/beam from the character toward the lower-right screen edge.

The original artifact crossed empty background space and changed the composition enough that `1591277437` was not suitable as an active PNG baseline until the renderer behavior was understood and fixed.

## Renderer Finding

Structural validation passed with one non-fatal warning:

- `Composelayer`: not present

The validator reported:

- `17 passed, 0 failed, 1 warnings`
- `8 effect passes`
- Effect pass families: `godrays_cast`, `godrays_combine`, `godrays_downsample2`, `godrays_gaussian`, `pulse`, `shake`
- Render graph nodes: `13`
- Shader compilation: no failures
- Material loading: no failures
- SceneScript/runtime gaps: none

The scene has one visible effect-bearing image layer:

- Layer `12`: `spiderman-into-the-spider-verse-2018-movie-ey-3840x2400`
- Authored size: `3840x2400`
- Effects in order:
  - `effects/shake/effect.json`
  - `effects/pulse/effect.json`
  - `effects/pulse/effect.json` with red pulse constants
  - `effects/godrays/effect.json`

The godrays effect includes:

- Noise pass with `NOISE=1`, `ui_editor_properties_ray_threshold=0.5`, `ui_editor_properties_noise_amount=0.4`, `ui_editor_properties_noise_scale=3.0`, `ui_editor_properties_noise_speed=0.15`
- Cast pass with `CASTER=1`, `SAMPLES=0`, `ui_editor_properties_direction=0.0`, `ui_editor_properties_ray_intensity=0.25`, `ui_editor_properties_ray_length=0.5`, `ui_editor_properties_color="1 1 1"`
- Horizontal and vertical gaussian passes with `KERNEL=1`
- Combine pass with `BLENDMODE=9`

The copied validator manifest contains five debug captures: `effect-input`, `default-before-effect`, `effect-output`, `default-after-effect`, and `final-publish`. Its `passStates` show nine passes writing through ping-pong effect targets, half composition buffers, and finally `_rt_default`. The final pass writes RGB only to `_rt_default`.

The exact standalone `--debug-effect-capture-delay-ms 10000` harness command aborted silently in this environment. The diagnostic evidence above uses the successful `tools/validate-scene.sh 1591277437 10000` path, copied into the Spider-Verse investigation directory.

## Next Fix Slice

Write a follow-up renderer plan before changing shader preprocessing, effect-chain logic, blend modes, or publish routing.

The next slice should compare the Windows/Yakkai godrays chain stage by stage and focus on generic causes first:

- Godray caster direction and coordinate system
- Light-source/ray origin normalization for oversized image layers
- Mask/noise texture coordinate handling
- `BLENDMODE=9` semantics in the combine pass
- Final RGB-only publish into `_rt_default`
- Interaction between `shake`/`pulse` pre-passes and the godray source texture

Promotion criteria for this fixture: the diagonal beam must either match Windows WE placement/brightness or disappear from Yakkai output to match the current Windows reference captures.

## Follow-up Fix Result

- Date: `2026-06-07`
- Result: generic renderer synchronization fix implemented; fixture promoted to active deep smoke coverage after human visual approval.
- Root cause: the final presentation pass sampled `_rt_default` after the final effect write without an explicit render-target write-to-fragment-sample visibility barrier. Debug effect captures masked the issue because their diagnostic copy passes inserted transfer synchronization before presentation.
- Stage classification: presentation/FinPass sampling, after final-publish. The debug `final-publish` and material-output captures were clean, while the no-debug final still showed the beam.
- Generic fix: add an explicit FinPass result texture read barrier before sampling the final render target, and harden CustomShaderPass target-read synchronization for the same write-to-sample class of issue.
- No scene-specific, Spider-Verse-specific, or Arona-specific predicate was added.
- Evidence:
  - `/tmp/yakkai-spiderverse-godray-fix/direct-nodebug-finpass-sync/windows-old-finpass-debug-contact-sheet.png`
  - `/tmp/yakkai-spiderverse-godray-fix/runner-finpass-sync-nodebug/windows-old-smokefix-debug-contact-sheet.png`
