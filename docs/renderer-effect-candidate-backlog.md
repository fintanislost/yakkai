# Renderer Effect Candidate Backlog

This backlog records stripped effect families found by the Phase 3 debug
manifests. It exists so future renderer slices start from concrete scene
evidence instead of broad `EffectPolicy` changes.

Latest source artifacts used for this revision:

- `3476236738`: `/tmp/yakkai-debug/effect-captures-3476236738/manifest.json`
- `3228578419`: `/tmp/yakkai-debug/effect-captures-3228578419/manifest.json`
- `3476236738` Phase 3.8 probe:
  `/tmp/yakkai-phase3-8-probe-3476236738/effect-captures/manifest.json`

Those artifacts are generated diagnostics and are not committed baselines. If
they are missing, regenerate them with `--debug-effect-captures` before changing
policy.

Phase 3.5 added diagnostic classification for stripped candidates. Phase 3.6
uses that classification to preserve only `simple-water` candidates while
leaving mixed chains, puppet layers, crop-sheet paths, and carrier layers
blocked. Phase 3.7 is diagnostic-only and adds `candidateChainShape` plus
`candidateMixFamilies` so mixed chains can be planned from observed chain shape
instead of inferred family names. Phase 3.8 added a harness-only
`--debug-effect-probe-layers` path and used it on `3476236738` layers `168`
and `22`. That probe captured input/output/final-publish targets and confirmed
that forcing those puppet mixed chains through the current authored effect path
causes large rectangular/gray occlusion in the final frame. Treat that as
negative evidence for broad puppet mixed-chain preservation.

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
| `waterflow` / `waterripple` | Partially enabled for `simple-water` | `3476236738` layer `124` (`窗户`) uses `effects/waterflow`; `3228578419` layer `239` is a utility composelayer with `effects/waterripple` and `effects/waterflow` | Utility composelayer evidence remains blocked; generic puppet/carrier paths are not part of the allow path | Keep the simple-water allow path narrow and validate `3476236738` plus `3228578419` before any broader effect work |
| `waterwaves` | Partially enabled for isolated `simple-water` | `3476236738` layers `322`, `446`, `438`, `442`, `133`, `145` are simple `effects/waterwaves`; layers `168` and `22` are `puppet-mixed` chains mixing waterwaves with opacity/shine/iris; `3228578419` layer `405` is a `protected-puppet-mixed` chain on `ARONA_CROP_SHEET` with waterwaves plus LUT, pulse, and shake | Phase 3.8 probe rendered layers `168` and `22` only for debug and produced obvious rectangular/gray occlusion; mixed puppet chains and protected crop-sheet paths remain blocked | Do not broaden the waterwaves allow path. Any future puppet mixed work needs a renderer fix for puppet effect publish/alpha behavior, not a policy allowlist |
| `opacity` | Blocked | `3476236738` layers `168`, `22`, `219`, and `277` include `effects/opacity`; Phase 3.7 classifies `168` and `22` as `puppet-mixed`, while `219` and `277` are audio/utility-style `unknown-mixed` chains; Phase 3.8 probe captured `168` and `22` and confirmed unsafe final composition | Opacity appears only in mixed chains in the current evidence, including audio utility carriers. The only probed puppet opacity chains are unsafe with the current renderer path | Keep blocked. Split puppet opacity from audio/utility opacity only after a renderer-level puppet effect-chain fix exists |
| `shine` / `iris` | Blocked | `3476236738` layer `22` includes `effects/shine_*` and `effects/iris` mixed with waterwaves and opacity; Phase 3.8 probe captured this layer and produced unsafe final composition | Single mixed puppet layer, no isolated fixture, and current puppet mixed-chain route is visually unsafe | Find or install an isolated shine/iris fixture before any renderer or policy change |
| `blur` | High-risk blocked | `3476236738` layer `137` uses `effects/blur_precise_gaussian`; `3476236738` layer `365` mixes blur with color grading on a utility composelayer; `3228578419` layers `53` and `155` are fullscreen blur layers | Blur is a known Arona failure class and appears on fullscreen/utility carriers; Phase 3.7 classifies observed blur as `unknown-mixed`, not isolated puppet-safe evidence | Keep stripped until a blur-specific render-target slice can prove alpha/load/blend behavior against Arona |
| `audio` | Deferred | `3476236738` layers `219` and `277` use `workshop/3082978660/effects/Simple_Audio_Bars` on `models/util/solidlayer.json`; Phase 3.7 reports audio mix-family evidence separately from puppet chains | Requires audio buffer semantics and utility carrier handling; not just an effect-chain issue | Defer to a future audio-reactive support slice |
| `color grading` / `LUT` | High-risk blocked | `3228578419` layers `174`, `314`, `82`, `267`, `1032`, `1013`, `2143`, and `405` use `workshop/3165346237/effects/lut_loader`; `3476236738` layer `365` uses `workshop/2795521260/effects/color_grading` | Known source of washed-out Arona backgrounds; appears on protected background, character, and utility paths; Phase 3.7 shows most LUT evidence as `unknown-mixed` plus one protected puppet chain | Keep stripped until a LUT/color-management slice can compare Wallpaper Engine output and Yakkai output frame-by-frame |
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

1. Blur/LUT renderer parity: only after the harness can compare enough frames to
   detect washed-out output and alpha/load regressions.
2. Puppet mixed-chain renderer repair: use the Phase 3.8 probe captures as
   negative evidence, and only revisit policy after the offscreen puppet
   effect publish path no longer occludes the final frame.
3. Audio-reactive utility layers: separate from effect-chain re-enable work.

Each follow-up should include:

- exact policy predicate
- scene IDs and layer IDs
- effect names and material shader names
- native policy tests for the predicate
- debug manifests before and after the change
- `tools/validate-scene.sh 3228578419 8000`
- `tools/validate-scene.sh 3476236738 10000`
- strict smoke run and manual visual review
