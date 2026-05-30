# Renderer Effect Candidate Backlog

This backlog records stripped effect families found by the Phase 3 debug
manifests. It exists so future renderer slices start from concrete scene
evidence instead of broad `EffectPolicy` changes.

Latest source artifacts used for this revision:

- `3476236738`: `/tmp/yakkai-debug/effect-captures-3476236738/manifest.json`
- `3228578419`: `/tmp/yakkai-debug/effect-captures-3228578419/manifest.json`
- `3476236738` Phase 3.8 probe:
  `/tmp/yakkai-phase3-8-probe-3476236738/effect-captures/manifest.json`
- Phase 3.10 high-risk probes can be regenerated with
  `--debug-effect-probe-high-risk-layers`; the probe artifacts remain
  generated diagnostics and are not committed baselines.
- Phase 3.12 blur/LUT probe artifacts:
  `/tmp/yakkai-phase3-12-probe-347-blur-137/effect-captures/manifest.json`,
  `/tmp/yakkai-phase3-12-probe-347-colorgrade-365/effect-captures/manifest.json`,
  `/tmp/yakkai-phase3-12-probe-arona-blur/effect-captures/manifest.json`, and
  `/tmp/yakkai-phase3-12-probe-arona-lut-wall-15s/effect-captures/manifest.json`.

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
negative evidence for broad puppet mixed-chain preservation. Phase 3.9 is
diagnostic-only and adds canonical blur/LUT/color-grade family flags plus
carrier-aware chain shapes so parity work can identify exact blocked layers
without re-enabling them.
Phase 3.10 adds a separate harness-only `--debug-effect-probe-high-risk-layers`
path for explicit stripped blur/LUT/color-grading layer IDs. This path records
`highRiskProbeLayerIds` and per-layer `debugProbe` reasons in the manifest, but
it is still diagnostic-only and does not alter normal Plasma rendering policy.
The `3476236738` Phase 3.10 probe on layers `137` and `365` completed without
capture failures and confirmed those layers can be isolated, but visual review
still shows the scene's large mid/background area as flat gray. Treat that as
negative evidence for a simple policy re-enable and as input for a future
background parity renderer slice.
Phase 3.10a adds a hard-fail validator sentinel for this scene. The sentinel
checks background regions for low-detail pixels close to the tint-adjusted clear
color.
Phase 3.11 fixes the `3476236738` background parity gap. The root cause was
parent transforms leaking into regular layer-local offscreen effect sources:
layer `124` (`窗户`) rendered only a cropped corner of its window/sky texture
into the effect input, leaving the final background close to clear color.
`SceneImageEffectLayer` now detaches the effect source node from its scene
parent for the local offscreen render while preserving the captured parent for
the final published output. `tools/validate-scene.sh 3476236738 10000` now
passes the sentinel, which remains a hard-fail regression guard.
Phase 3.12 is a diagnostic blur/LUT parity probe. It does not change normal
policy. For `3476236738`, probing both high-risk layers `137` and `365`
changed the frame materially (`RMSE 0.0851`). Splitting the run showed that
`137` (`桌左`, `blur-only`) is a small localized shift (`RMSE 0.0216`, mean and
variance essentially unchanged), while `365` (`可调整组合层`,
`blur-color-grade-composelayer`) accounts for the broad frame change
(`RMSE 0.0862`, mean drops from `0.642` to `0.620`). For `3228578419`, probing
fullscreen blur utility layers `53` and `155` was a small final-frame delta
(`RMSE 0.0057`), while a 15s single-layer LUT probe for `82` (`WALL`) produced a
valid structural render (`RMSE 0.0183`) and composed the sky/window plate behind
the character without obvious mask corruption. Earlier 8s Arona LUT captures hit
the harness loader screen, which led to first-frame-gated harness capture
timing: capture delays now start after the backend reports a real first frame.
Treat this as positive evidence for future narrow `blur-only` and background
`lut-only` investigations, but negative evidence for broad
color-grade/composelayer or utility-carrier re-enables.

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
| `waterflow` / `waterripple` | Partially enabled for `simple-water` | `3476236738` layer `124` (`窗户`) uses `effects/waterflow`; `3228578419` layer `239` is a utility composelayer with `effects/waterripple` and `effects/waterflow` | Phase 3.11 fixed parented layer-local effect source cropping for `3476236738`; utility composelayer evidence remains blocked, and generic puppet/carrier paths are not part of the allow path | Keep the simple-water allow path narrow and validate `3476236738` plus `3228578419` before any broader effect work |
| `waterwaves` | Partially enabled for isolated `simple-water` | `3476236738` layers `322`, `446`, `438`, `442`, `133`, `145` are simple `effects/waterwaves`; layers `168` and `22` are `puppet-mixed` chains mixing waterwaves with opacity/shine/iris; `3228578419` layer `405` is a `protected-puppet-mixed` chain on `ARONA_CROP_SHEET` with waterwaves plus LUT, pulse, and shake | Phase 3.8 probe rendered layers `168` and `22` only for debug and produced obvious rectangular/gray occlusion; mixed puppet chains and protected crop-sheet paths remain blocked | Do not broaden the waterwaves allow path. Any future puppet mixed work needs a renderer fix for puppet effect publish/alpha behavior, not a policy allowlist |
| `opacity` | Blocked | `3476236738` layers `168`, `22`, `219`, and `277` include `effects/opacity`; Phase 3.7 classifies `168` and `22` as `puppet-mixed`, while `219` and `277` are audio/utility-style `unknown-mixed` chains; Phase 3.8 probe captured `168` and `22` and confirmed unsafe final composition | Opacity appears only in mixed chains in the current evidence, including audio utility carriers. The only probed puppet opacity chains are unsafe with the current renderer path | Keep blocked. Split puppet opacity from audio/utility opacity only after a renderer-level puppet effect-chain fix exists |
| `shine` / `iris` | Blocked | `3476236738` layer `22` includes `effects/shine_*` and `effects/iris` mixed with waterwaves and opacity; Phase 3.8 probe captured this layer and produced unsafe final composition | Single mixed puppet layer, no isolated fixture, and current puppet mixed-chain route is visually unsafe | Find or install an isolated shine/iris fixture before any renderer or policy change |
| `blur` | High-risk blocked | `3476236738` layer `137` uses `effects/blur_precise_gaussian`; `3476236738` layer `365` mixes blur with color grading on a utility composelayer; `3228578419` layers `53` and `155` are fullscreen blur layers; Phase 3.12 found `137` has only a small final-frame delta while `365` carries the broad color/brightness change | Blur is a known Arona failure class and appears on fullscreen/utility carriers; isolated `blur-only` evidence is more promising than composelayer or fullscreen utility blur, but it still lacks Wallpaper Engine frame parity proof | Keep stripped globally. A future slice may target a narrow non-carrier `blur-only` predicate, with `3476236738` layer `137` as the candidate and Arona utility blur as the regression guard |
| `audio` | Deferred | `3476236738` layers `219` and `277` use `workshop/3082978660/effects/Simple_Audio_Bars` on `models/util/solidlayer.json`; Phase 3.7 reports audio mix-family evidence separately from puppet chains | Requires audio buffer semantics and utility carrier handling; not just an effect-chain issue | Defer to a future audio-reactive support slice |
| `color grading` / `LUT` | High-risk blocked | `3228578419` layers `174`, `314`, `82`, `267`, `1032`, `1013`, `2143`, and `405` use `workshop/3165346237/effects/lut_loader`; `3476236738` layer `365` uses `workshop/2795521260/effects/color_grading`; Phase 3.12 found Arona layer `82` (`WALL`) structurally plausible in isolation, but `3476236738` layer `365` still changes the whole frame materially | Known source of washed-out Arona backgrounds; LUT appears on protected background, character, and crop-sheet paths; color grading mixed with composelayer blur remains unsafe for broad policy | Keep stripped globally. A future slice may target a narrow background `lut-only` predicate starting with Arona layer `82`, but protected crop-sheet and composelayer color-grade paths stay blocked until frame-by-frame Wallpaper Engine comparison exists |
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
