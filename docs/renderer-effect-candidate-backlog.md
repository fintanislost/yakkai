# Renderer Effect Candidate Backlog

This backlog records stripped effect families found by the Phase 3.3 debug
manifests. It exists so future renderer slices start from concrete scene
evidence instead of broad `EffectPolicy` changes.

Source artifacts:

- `3476236738`: `/tmp/yakkai-phase3-3-diagnostics-3476236738/manifest.json`
- `3228578419`: `/tmp/yakkai-phase3-3-diagnostics-3228578419/manifest.json`

Those artifacts are generated diagnostics and are not committed baselines. If
they are missing, regenerate them with `--debug-effect-captures` before changing
policy.

## Rules For Candidate Slices

- Do not remove the puppet-scene strip rule wholesale.
- Do not re-enable fullscreen, composelayer utility, solidlayer,
  projectlayer, or fullscreenlayer carrier effects as part of a broad family
  change.
- Do not re-enable Arona background LUT/blur paths without a candidate-specific
  plan and visual proof.
- Treat mixed layers as blocked until the slice can isolate the exact effect
  family, shader list, and layer class.
- Clear the shader cache before every render validation:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

## Candidate Family Matrix

| Family | Current status | Evidence | Blocker | Required next slice |
| --- | --- | --- | --- | --- |
| `waterflow` / `waterripple` | Candidate, blocked | `3476236738` layer `124` (`窗户`) uses `effects/waterflow`; `3228578419` layer `239` is a utility composelayer with `effects/waterripple` and `effects/waterflow` | The Arona evidence is a dropped utility composelayer, so a broad family allowlist would cross an explicitly blocked class | Build a water-effects investigation slice that isolates non-utility, non-puppet layers and proves Arona utility carriers remain stripped |
| `waterwaves` | Candidate, blocked | `3476236738` layers `322`, `446`, `438`, `442`, `133`, `145` are simple `effects/waterwaves`; layers `168` and `22` mix `waterwaves` with opacity/shine/iris; `3228578419` layer `405` mixes waterwaves on `ARONA_CROP_SHEET` with LUT, pulse, and shake | The family appears both in safe-looking simple layers and in dangerous Arona puppet/character paths | Split simple non-puppet waterwaves from mixed puppet/character waterwaves and prove the predicate does not affect `ARONA_CROP_SHEET` |
| `opacity` | Blocked | `3476236738` layers `168`, `22`, `219`, and `277` include `effects/opacity` mixed with waterwaves, shine/iris, or audio bars | Opacity appears only in mixed chains in the current evidence, including audio utility carriers | Add a focused mixed-chain diagnostic before considering any policy allowlist |
| `shine` / `iris` | Blocked | `3476236738` layer `22` includes `effects/shine_*` and `effects/iris` mixed with waterwaves and opacity | Single mixed layer, no isolated fixture, and no preserved render-target capture for comparison | Find or install an isolated shine/iris fixture before any renderer or policy change |
| `blur` | High-risk blocked | `3476236738` layer `137` uses `effects/blur_precise_gaussian`; `3476236738` layer `365` mixes blur with color grading on a utility composelayer; `3228578419` layers `53` and `155` are fullscreen blur layers | Blur is a known Arona failure class and appears on fullscreen/utility carriers | Keep stripped until a blur-specific render-target slice can prove alpha/load/blend behavior against Arona |
| `audio` | Deferred | `3476236738` layers `219` and `277` use `workshop/3082978660/effects/Simple_Audio_Bars` on `models/util/solidlayer.json` | Requires audio buffer semantics and utility carrier handling; not just an effect-chain issue | Defer to a future audio-reactive support slice |
| `color grading` / `LUT` | High-risk blocked | `3228578419` layers `174`, `314`, `82`, `267`, `1032`, `1013`, `2143`, and `405` use `workshop/3165346237/effects/lut_loader`; `3476236738` layer `365` uses `workshop/2795521260/effects/color_grading` | Known source of washed-out Arona backgrounds; appears on protected background, character, and utility paths | Keep stripped until a LUT/color-management slice can compare Wallpaper Engine output and Yakkai output frame-by-frame |
| `pulse` / `shake` | High-risk blocked | `3228578419` layer `405` (`ARONA_CROP_SHEET`) mixes `effects/pulse`, `effects/shake`, waterwaves, and LUT | Directly affects the protected puppet crop sheet and is mixed with other dangerous effects | Defer until puppet-through-effect-chain rendering has a dedicated plan |

## Current Protected Paths

These are not candidates for broad re-enable work:

- Sleeping Arona (`3228578419`) background LUT and blur layers.
- Sleeping Arona `ARONA_CROP_SHEET` puppet effect chain.
- Fullscreen, composelayer, solidlayer, projectlayer, and utility carriers.
- Audio visualizer layers.

These paths should remain stripped unless a future design explicitly proves a
narrow predicate and passes visual review.

## Recommended Follow-Up Order

1. Water-effects investigation: diagnostic-only first, focused on
   `waterflow`, `waterripple`, and simple non-puppet `waterwaves` layers.
2. Mixed-chain diagnostics: understand opacity/shine/iris chains before
   considering any partial chain preservation.
3. Blur/LUT renderer parity: only after the harness can compare enough frames to
   detect washed-out output and alpha/load regressions.
4. Audio-reactive utility layers: separate from effect-chain re-enable work.

Each follow-up should include:

- exact policy predicate
- scene IDs and layer IDs
- effect names and material shader names
- native policy tests for the predicate
- debug manifests before and after the change
- `tools/validate-scene.sh 3228578419 8000`
- `tools/validate-scene.sh 3476236738 10000`
- strict smoke run and manual visual review
