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
- Renderer-limitations composelayer color-grade probe:
  `/tmp/yakkai-debug/effect-captures-3476236738-probe-365/manifest.json`,
  `/tmp/yakkai-debug/validate-3476236738-probe-365.log`, and
  `/tmp/yakkai-debug/validate-3476236738-probe-365.png`.
- Renderer-limitations utility blur probe:
  `/tmp/yakkai-debug/effect-captures-3228578419-probe-53_155/manifest.json`,
  `/tmp/yakkai-debug/validate-3228578419-probe-53_155.log`, and
  `/tmp/yakkai-debug/validate-3228578419-probe-53_155.png`.
- Renderer-limitations water carrier probe:
  `/tmp/yakkai-debug/effect-captures-3228578419-probe-layer_239/manifest.json`,
  `/tmp/yakkai-debug/validate-3228578419-probe-layer_239.log`, and
  `/tmp/yakkai-visual-gate/3228578419-carrier-water-review.png`.
- Phase 3.12 blur/LUT probe artifacts:
  `/tmp/yakkai-phase3-12-probe-347-blur-137/effect-captures/manifest.json`,
  `/tmp/yakkai-phase3-12-probe-347-colorgrade-365/effect-captures/manifest.json`,
  `/tmp/yakkai-phase3-12-probe-arona-blur/effect-captures/manifest.json`, and
  `/tmp/yakkai-phase3-12-probe-arona-lut-wall-15s/effect-captures/manifest.json`.
- Arona material-output LUT diagnostics:
  `/tmp/yakkai-arona-reference/20260530T184333044866Z/summary.json`,
  `/tmp/yakkai-arona-lut-lab/sunset-82-material/lut-sampling-report.md`, and
  `/tmp/yakkai-arona-lut-lab/night-82-material/lut-sampling-report.md`.
- Arona all-layer LUT microscope diagnostics:
  `/tmp/yakkai-arona-lut-lab/sunset-all-material/lut-sampling-summary.md` and
  `/tmp/yakkai-arona-lut-lab/night-all-material/lut-sampling-summary.md`.
- Arona repaired material-output diagnostics:
  `/tmp/yakkai-arona-reference/20260530T221704403505Z/summary.json`,
  `/tmp/yakkai-arona-lut-lab/sunset-all-repaired-strict/lut-sampling-summary.md`,
  and
  `/tmp/yakkai-arona-lut-lab/night-all-repaired-strict/lut-sampling-summary.md`.
- Arona layer-local material-output diagnostics:
  `/tmp/yakkai-arona-reference/20260530T224418896720Z/summary.json`,
  `/tmp/yakkai-arona-lut-lab/sunset-all-local/lut-sampling-summary.md`, and
  `/tmp/yakkai-arona-lut-lab/night-all-local/lut-sampling-summary.md`.
- Arona final-publish drift microscope diagnostics:
  `/tmp/yakkai-arona-reference/20260530T231836903776Z/summary.json`,
  `/tmp/yakkai-arona-lut-lab/sunset-all-publish-drift-fresh/lut-sampling-summary.md`,
  and
  `/tmp/yakkai-arona-lut-lab/night-all-publish-drift-fresh/lut-sampling-summary.md`.
- Arona bounded publish-registration diagnostics:
  `/tmp/yakkai-arona-lut-lab/sunset-all-publish-registration/lut-sampling-summary.md`
  and
  `/tmp/yakkai-arona-lut-lab/night-all-publish-registration/lut-sampling-summary.md`.
- Arona publish/composition diagnostics:
  `/tmp/yakkai-arona-reference/20260531T005302548765Z/summary.json`,
  `/tmp/yakkai-arona-lut-lab/sunset-all-publish-diag/lut-sampling-summary.md`,
  and
  `/tmp/yakkai-arona-lut-lab/night-all-publish-diag/lut-sampling-summary.md`.
- Arona default-frame progression diagnostics:
  `/tmp/yakkai-arona-reference/20260531T014823297979Z/summary.json`,
  `/tmp/yakkai-arona-lut-lab/day-default-frame-progression/default-frame-progression.md`,
  `/tmp/yakkai-arona-lut-lab/sunset-default-frame-progression/default-frame-progression.md`,
  and
  `/tmp/yakkai-arona-lut-lab/night-default-frame-progression/default-frame-progression.md`.
- Arona post-LUT drift attribution diagnostics:
  `/tmp/yakkai-arona-lut-lab/day-post-lut-drift/post-lut-drift.md`,
  `/tmp/yakkai-arona-lut-lab/sunset-post-lut-drift/post-lut-drift.md`,
  and
  `/tmp/yakkai-arona-lut-lab/night-post-lut-drift/post-lut-drift.md`.
- Arona post-LUT flare/default-frame diagnostics:
  `/tmp/yakkai-arona-lut-lab/day-post-lut-flare-drift/post-lut-flare-drift.md`,
  `/tmp/yakkai-arona-lut-lab/sunset-post-lut-flare-drift/post-lut-flare-drift.md`,
  and
  `/tmp/yakkai-arona-lut-lab/night-post-lut-flare-drift/post-lut-flare-drift.md`.
- Arona 2026-06-09 regular-LUT stop diagnostics:
  `/tmp/yakkai-arona-lut-color-parity/baseline/summary.json`,
  `/tmp/yakkai-arona-lut-color-parity/baseline/lut-all/lut-sampling-summary.md`,
  `/tmp/yakkai-arona-lut-color-parity/baseline/default-frame-progression/default-frame-progression.md`,
  `/tmp/yakkai-arona-lut-color-parity/baseline/post-lut-drift/post-lut-drift.md`,
  and
  `/tmp/yakkai-arona-lut-color-parity/baseline/post-lut-flare-drift/post-lut-flare-drift.md`.
- Arona 2026-06-10 layer-405 prefix 2-7 effect-chain diagnostics:
  `/tmp/yakkai-arona-prefix-2-7-effect-chain/prefix-2/summary.json`,
  `/tmp/yakkai-arona-prefix-2-7-effect-chain/prefix-3/summary.json`,
  `/tmp/yakkai-arona-prefix-2-7-effect-chain/prefix-labs/`,
  `/tmp/yakkai-arona-prefix-2-7-effect-chain/effect-drilldown/prefix-3-all-lut-layers/lut-sampling-summary.md`,
  and
  `/tmp/yakkai-arona-prefix-2-7-effect-chain/effect-drilldown/prefix-3-all-lut-layers/layer-405-ARONA_CROP_SHEET/lut-sampling-report.md`.
- Arona 2026-06-10 layer-405 final-publish/composite boundary diagnostics:
  `/tmp/yakkai-arona-final-publish-composite-boundary/normal/summary.json`,
  `/tmp/yakkai-arona-final-publish-composite-boundary/prefix-2/summary.json`,
  `/tmp/yakkai-arona-final-publish-composite-boundary/prefix-3/summary.json`,
  `/tmp/yakkai-arona-final-publish-composite-boundary/prefix-7/summary.json`,
  `/tmp/yakkai-arona-final-publish-composite-boundary/labs/prefix-3/protected-puppet-lab.json`,
  `/tmp/yakkai-arona-final-publish-composite-boundary/lut-prefix-3/lut-sampling-summary.json`,
  `/tmp/yakkai-arona-final-publish-composite-boundary/evidence/layer405-lut-crosscheck.json`,
  and `ARONA_WINDOWS_AGENT_REQUEST.md`.
- Arona 2026-06-10 Windows final-publish/composite fresh intake:
  `yakkai_arona/layer405_final_publish_composite_fresh.zip`,
  `yakkai_arona/layer405_final_publish_composite/`, and
  `/tmp/yakkai-arona-windows-final-publish-composite-analysis/summary.md`.
- Arona 2026-06-10 fresh final-publish Linux diagnostics:
  `/tmp/yakkai-layer405-fresh-yakkai-rerun/summary.json`,
  `/tmp/yakkai-layer405-fresh-publish-compare-rerun/summary.json`, and
  `/tmp/yakkai-layer405-fresh-publish-compare/summary.json`.
- Arona 2026-06-10 layer-405 default-delta oracle:
  `/tmp/yakkai-layer405-default-delta-oracle/summary.json` and
  `/tmp/yakkai-layer405-default-delta-oracle/summary.md`.
- Arona 2026-06-10 layer-405 default-delta locator:
  `/tmp/yakkai-layer405-default-delta-locator/summary.json`,
  `/tmp/yakkai-layer405-default-delta-locator/summary.md`, and
  `/tmp/yakkai-layer405-default-delta-locator/locator-crops/`.
- Arona 2026-06-11 layer-405 isolated final-publish boundary diagnostics:
  `/tmp/yakkai-arona-boundary-day/effect-captures/3228578419/405_ARONA_CROP_SHEET/manifest.json`,
  `/tmp/yakkai-arona-final-publish-boundary-run3/summary.json`,
  `/tmp/yakkai-layer405-final-publish-boundary-compare/summary.json`, and
  `/tmp/yakkai-layer405-final-publish-boundary-compare/summary.md`.
- Arona 2026-06-11 layer-405 isolated final-publish output parity:
  `/tmp/yakkai-layer405-isolated-publish-parity/summary.json`,
  `/tmp/yakkai-layer405-isolated-publish-parity/summary.md`, and
  `/tmp/yakkai-layer405-isolated-publish-parity/isolated-publish-crops/`.
- Arona 2026-06-11 layer-405 content-stage attribution:
  `/tmp/yakkai-layer405-content-stage-attribution/summary.json`,
  `/tmp/yakkai-layer405-content-stage-attribution/summary.md`, and
  `/tmp/yakkai-layer405-content-stage-attribution/content-stage-crops/`.
- Arona 2026-06-11 layer-405 content-transition attribution:
  `/tmp/yakkai-layer405-content-transition-attribution/summary.json`,
  `/tmp/yakkai-layer405-content-transition-attribution/summary.md`, and
  `/tmp/yakkai-layer405-content-transition-attribution/content-transition-crops/`.
- Arona 2026-06-11 layer-405 content-range attribution:
  `/tmp/yakkai-layer405-content-range-attribution/summary.json`,
  `/tmp/yakkai-layer405-content-range-attribution/summary.md`, and
  `/tmp/yakkai-layer405-content-range-attribution/content-range-crops/`.
- Arona 2026-06-11 layer-405 middle-block microscope:
  `/tmp/yakkai-layer405-middle-block-microscope/summary.json`,
  `/tmp/yakkai-layer405-middle-block-microscope/summary.md`, and
  `/tmp/yakkai-layer405-middle-block-microscope/middle-block-crops/`.
- Arona 2026-06-11 layer-405 selected-step metadata and Windows ask:
  `/tmp/yakkai-layer405-selected-step-metadata/summary.json`,
  `/tmp/yakkai-layer405-selected-step-metadata/summary.md`,
  `/tmp/yakkai-layer405-selected-step-metadata/selected-step-crops/`, and
  `/tmp/yakkai-layer405-selected-step-metadata/middle-block-windows-request.md`.

Those artifacts are generated diagnostics and are not committed baselines. If
they are missing, regenerate them with `--debug-effect-captures` before changing
policy.

## Generic Inventory Snapshot

Snapshot date: 2026-06-01.

Command:

```bash
python3 tools/effect-candidate-inventory.py \
  /tmp/yakkai-debug/effect-captures-3228578419/manifest.json \
  /tmp/yakkai-debug/effect-captures-3476236738/manifest.json
```

Result: 30 candidate records.

Effect classes:

- `simple-water`: 7
- `regular-lut-only`: 7
- `essential-effect`: 6
- `audio-utility`: 2
- `puppet-mixed`: 2
- `utility-blur`: 2
- `composelayer-water-only`: 1
- `composelayer-color-grade`: 1
- `protected-puppet-lut`: 1
- `regular-blur-only`: 1

Dispositions:

- `allowed`: 25
- `protected`: 1
- `stripped`: 4

Route audits:

- `route-complete`: 7
- `not-audited`: 23

Visual-gate audits:

- `not-audited`: 26
- `production-allowed`: 4

Immediate class-level reading:

- `regular-lut-only` is already allowed for seven Arona layers and should be
  kept narrow. The route audit now classifies all seven as `route-complete`,
  which makes later LUT work a shader/color parity problem rather than a
  missing final-publish route evidence problem.
- `protected-puppet-lut` is represented by Arona layer `405` with active slot
  `2`. Fresh Layer 405 content-stage attribution now shows raw `effect-input`
  is nearly exact, `prefix-3` is close, and `prefix-7`/`final-publish-input`
  diverge before final publish. Next work should target the later layer-local
  visible-effect progression, not another layer enablement, layer-loading
  change, final-publish RGB color-mask patch, or Windows capture-label request.
  The follow-up transition attribution narrows the worst checkpoint block to
  `prefix-3 -> prefix-7` for Day/Sunset/Night; the later
  `prefix-7 -> final-publish-input` transition is near zero.
- `regular-blur-only` has one allowed non-carrier candidate
  (`3476236738` layer `137`) under the strict `regular-blur-only-effect`
  predicate.
- The `composelayer-color-grade` record is now allowed by the strict
  `composelayer-color-grade-effect` predicate after the layer `365` probe
  proved a full-frame composelayer route with color-grade plus blur materials
  and the generated review sheet was human-approved on 2026-06-01.
- The two `utility-blur` records are now allowed by the strict
  `utility-blur-effect` predicate after the Arona probe of layers `53` and
  `155` proved fullscreen utility blur publish routes and the generated review
  sheet was human-approved on 2026-06-01.
- `composelayer-water-only` is represented by Arona layer `239` and is now
  allowed by the strict `composelayer-water-effect` predicate after explicit
  probe review and human approval. It is no longer grouped under generic
  `carrier-mixed`.
- `puppet-mixed` and `audio-utility` remain blocked or diagnostic-only.

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
negative evidence for broad puppet mixed-chain preservation. The 2026-06-01
puppet-mixed prefix probe kept this class diagnostic-only and added
validator-level `--probe-max-effects` support. Normal validation for
`3476236738` still passes with the puppet mixed chains stripped. Forced `N=0`
probes for layers `168` and `22` are structurally close to normal output
(`RMSE 0.0140`), but forcing only the first visible effect already regresses:
layer `168` with `effects/waterwaves` reports `RMSE 0.0506`, layer `22` with
`effects/waterwaves` reports `RMSE 0.0936`, and both together report
`RMSE 0.1079`. The review artifact is
`/tmp/yakkai-visual-gate/3476236738-puppet-mixed-waterwaves-regression.png`.
Treat this as negative evidence for re-enabling puppet-mixed waterwaves until
puppet-layer effect source/final-publish routing has a generic fix. Phase 3.9 is
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
The renderer-limitations L.1 slice adds `candidateEffectClass` as a stable
LUT/color-grade grouping key. Reports now separate regular `lut-only`,
protected puppet LUT, mixed puppet LUT, and composelayer color-grade paths by
class and disposition (`allowed`, `stripped`, `probe-only`, or `protected`)
without changing the current allow/strip policy.
The renderer-limitations B.1 slice keeps blur stripped by default but adds a
stable `regular-blur-only`/`utility-blur` diagnostic class and validator probe
replay. `tools/validate-scene.sh --probe-high-risk-layers` now compares the
normal final frame against a probe-only frame, prints high-risk disposition and
shape rows, and reports configured sentinel-region deltas. Fresh probe evidence
keeps `3476236738` layer `137` (`blur-only`) small and localized while Arona
utility blur remains diagnostic-only.
The renderer-limitations B.2 slice promotes only the regular non-carrier
`blur-only` class. Fresh validation reports `3476236738` layer `137` as allowed
with reason `regular-blur-only-effect`; Arona `utility-blur` layers remain
stripped. `tools/validate-scene.sh 3476236738 10000` passed the background
sentinel and `tools/validate-scene.sh 3228578419 8000` passed the Arona guard.
Deep smoke artifacts for this slice are in
`/tmp/yakkai-smoke/20260601T041115565678Z`; all configured non-video fixtures
completed as `REVIEW`, and the video fixture completed as `PASS`.
The renderer-limitations B.3 slice promotes only the fullscreen utility blur
class after human visual approval. Fresh validation reports Arona layers `53`
and `155` as allowed with reason `utility-blur-effect`; the effect capture
manifest includes 10 utility-blur captures with that reason. The inventory now
reports two `production-allowed` visual-gate audit rows, one remaining
`needs-high-risk-probe-route` row for `composelayer-color-grade`, 23 allowed
records, and six stripped records. `tools/validate-scene.sh 3228578419 8000`
passed with `21` effect passes and `tools/validate-scene.sh 3476236738 10000`
still passed the background sentinel. Post-change shallow smoke wrote artifacts
to `/tmp/yakkai-smoke/20260601T055440202772Z`; Arona Day completed as `REVIEW`
and the video fixture completed as `PASS`.
The renderer-limitations C.2 slice promotes only the composelayer blur/color
grade class after human visual approval. Fresh validation reports `3476236738`
layer `365` as allowed with reason `composelayer-color-grade-effect`; the
effect capture manifest includes five composelayer-color-grade captures through
local `effect-layer-node-final-publish`. The inventory now reports three
`production-allowed` visual-gate audit rows, 24 allowed records, and five
stripped records. `tools/validate-scene.sh 3476236738 10000` passed the
background sentinel with 11 effect passes and `tools/validate-scene.sh
3228578419 8000` still passed the Arona guard. Post-change shallow smoke wrote
artifacts to `/tmp/yakkai-smoke/20260601T062019846493Z`; Arona Day completed as
`REVIEW` and the video fixture completed as `PASS`.
The renderer-limitations L.2 slice adds route-audit classification to the
generic inventory. Current manifests report seven `route-complete`
`regular-lut-only` records. Those records all use the repaired regular
effect-layer node final publish route and expose both screen-sized and
layer-local material-output captures, so the next regular-LUT work should focus
on shader/color parity against the Windows reference rather than widening the
predicate.
The renderer-limitations C.1 slice changes high-risk route evidence from a
metric blocker to a human visual gate. `tools/effect-candidate-inventory.py`
emits `visual-gate-audits` for high-risk visual classes, initially
`composelayer-color-grade` and `utility-blur` and now also
`composelayer-water-only`. Baseline manifests report
`needs-high-risk-probe-route`; the fresh `3476236738` layer `365` probe reports
`human-visual-review-required`,
with route evidence showing a composelayer, color-grade plus blur materials, a
fullscreen-card final mesh, and the then-current
`effect-layer-composite-final-publish` route. The
validator probe changed the frame materially (`baseline-vs-probe
RMSE=0.0840355`) and moved configured background sentinel regions by mean RGB
distances of `10.533` to `17.405` without triggering clear-color leakage. The
fresh `3228578419` layers `53,155` probe also reports
`human-visual-review-required` for fullscreen utility blur routes and changed
the frame by `baseline-vs-probe RMSE=0.0372153`. Treat those metrics as review
prompts, not automatic blockers: production policy still requires generic
route evidence, generated review artifacts, and recorded human visual approval.
Human review approved both generated sheets on 2026-06-01:
`/tmp/yakkai-visual-gate/3476236738-composelayer-color-grade-review.png` and
`/tmp/yakkai-visual-gate/3228578419-utility-blur-review.png`.
The renderer-limitations W.1 slice adds stable water-carrier diagnostics and a
generic validator probe wrapper. Arona layer `239` now reports
`candidateEffectClass=composelayer-water-only` and
`candidateChainShape=water-composelayer` while staying stripped under
`puppet-alpha-strip`. `tools/validate-scene.sh 3228578419 8000 --probe-layers
239` passed with 23 checks, 0 failures, and one SceneScript warning; the forced
probe reported `composelayer-water-only probe-only` and baseline-vs-probe
`RMSE=0.0487605`. The review sheet is
`/tmp/yakkai-visual-gate/3228578419-carrier-water-review.png`. This is positive
probe evidence only; no production predicate has been added. Post-change
shallow smoke exited 0 with artifacts in
`/tmp/yakkai-smoke/20260601T071256498198Z`; Arona Day and the Shiroko video
fixture both completed as `REVIEW`, with Shiroko review limited to 10 late
sequence frames and no smoke failures.
The renderer-limitations W.2 slice promotes only the composelayer water-only
class after human visual approval. Fresh validation reports Arona layer `239`
as allowed with reason `composelayer-water-effect`; the route is
local `effect-layer-node-final-publish`, publishes final output, has empty
route risk, and uses a card final mesh with `effects/waterripple` plus
`effects/waterflow` materials.
`tools/validate-scene.sh 3228578419 8000` passed with 23 effect passes, and
`tools/validate-scene.sh 3476236738 10000` still passed the background
sentinel. The combined inventory now reports 25 allowed records, one protected
record, four stripped records, and four `production-allowed` visual-gate rows.
Post-change shallow smoke exited 0 with artifacts in
`/tmp/yakkai-smoke/20260601T074157769790Z`; Arona Day completed as `REVIEW` and
the Shiroko video fixture completed as `PASS`.

## Rules For Candidate Slices

- Do not remove the puppet-scene strip rule wholesale.
- Do not re-enable fullscreen, composelayer utility, solidlayer,
  projectlayer, or fullscreenlayer carrier effects as part of a broad family
  change.
- Do not re-enable Arona background LUT/blur paths without a candidate-specific
  plan and visual proof.
- Treat RMSE and region deltas as review-routing evidence for authored blur,
  color-grade, composelayer, fullscreen utility, and puppet/crop-sheet changes;
  they are not automatic rejection when the route is structurally valid.
- Treat mixed layers as blocked until the slice can isolate the exact effect
  family, shader list, and layer class.
- Clear the shader cache before every render validation:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

## Candidate Family Matrix

| Family | Current status | Evidence | Gate / blocker | Required next slice |
| --- | --- | --- | --- | --- |
| `waterflow` / `waterripple` | Enabled for isolated `simple-water` and narrow `composelayer-water-only`; other carrier water remains probe-only | `3476236738` layer `124` (`窗户`) uses `effects/waterflow`; `3228578419` layer `239` is a utility composelayer with `effects/waterripple` and `effects/waterflow` and now renders with reason `composelayer-water-effect` | Phase 3.11 fixed parented layer-local effect source cropping for `3476236738`; W.1 probe of Arona layer `239` passed structurally with `RMSE=0.0487605`, and W.2 validation promoted only the composelayer water route. Utility/fullscreen water-only carriers remain unapproved | Keep both water predicates narrow. Do not widen into utility/fullscreen, puppet, protected puppet, or mixed water paths without separate evidence |
| `waterwaves` | Partially enabled for isolated `simple-water` | `3476236738` layers `322`, `446`, `438`, `442`, `133`, `145` are simple `effects/waterwaves`; layers `168` and `22` are `puppet-mixed` chains mixing waterwaves with opacity/shine/iris; `3228578419` layer `405` is a `protected-puppet-mixed` chain on `ARONA_CROP_SHEET` with waterwaves plus LUT, pulse, and shake | Phase 3.8 probe rendered layers `168` and `22` only for debug and produced obvious rectangular/gray occlusion; mixed puppet chains and protected crop-sheet paths remain blocked | Do not broaden the waterwaves allow path. Any future puppet mixed work needs a renderer fix for puppet effect publish/alpha behavior, not a policy allowlist |
| `opacity` | Blocked | `3476236738` layers `168`, `22`, `219`, and `277` include `effects/opacity`; Phase 3.7 classifies `168` and `22` as `puppet-mixed`, while `219` and `277` are audio/utility-style `unknown-mixed` chains; Phase 3.8 probe captured `168` and `22` and confirmed unsafe final composition | Opacity appears only in mixed chains in the current evidence, including audio utility carriers. The only probed puppet opacity chains are unsafe with the current renderer path | Keep blocked. Split puppet opacity from audio/utility opacity only after a renderer-level puppet effect-chain fix exists |
| `shine` / `iris` | Blocked | `3476236738` layer `22` includes `effects/shine_*` and `effects/iris` mixed with waterwaves and opacity; Phase 3.8 probe captured this layer and produced unsafe final composition | Single mixed puppet layer, no isolated fixture, and current puppet mixed-chain route is visually unsafe | Find or install an isolated shine/iris fixture before any renderer or policy change |
| `blur` | Narrowly enabled for regular non-carrier `blur-only`, fullscreen utility `utility-blur`, and composelayer `composelayer-color-grade`; other fullscreen/protected/mixed blur remains probe-only | `3476236738` layer `137` uses `effects/blur_precise_gaussian` and reports `regular-blur-only`; `3476236738` layer `365` mixes blur with color grading on a utility composelayer and now reports `composelayer-color-grade-effect`; `3228578419` layers `53` and `155` report `utility-blur`; B.2 validation keeps `137` allowed, B.3 keeps Arona utility blur allowed, and C.2 keeps the composelayer color-grade route allowed after human-approved probe review (`RMSE 0.0840355`) | Blur is a known Arona failure class and appears on fullscreen/utility carriers; only the strict regular, utility, and composelayer color-grade classes have enough route and visual evidence for production. Protected, puppet, audio utility, and other mixed blur still require separate evidence | Keep all three blur predicates narrow. Future blur work should target protected/generic puppet or audio utility routes only with separate evidence |
| `audio` | Deferred | `3476236738` layers `219` and `277` use `workshop/3082978660/effects/Simple_Audio_Bars` on `models/util/solidlayer.json`; Phase 3.7 reports audio mix-family evidence separately from puppet chains | Requires audio buffer semantics and utility carrier handling; not just an effect-chain issue | Defer to a future audio-reactive support slice |
| `color grading` / `LUT` | Regular image-layer `lut-only` enabled and route-audited; composelayer color-grade enabled narrowly; other LUT/color-grade classes remain high-risk split by class | `3228578419` layers `174`, `314`, `82`, `267`, `1032`, `1013`, and `2143` are `regular-lut-only` and now classify as `route-complete`; `3228578419` layer `405` (`ARONA_CROP_SHEET`) is `protected-puppet-lut`; `3476236738` layer `365` is `composelayer-color-grade` and now uses `composelayer-color-grade-effect` after the human-approved probe changed the frame materially (`RMSE 0.0840355`) | Known source of washed-out Arona backgrounds; regular image-layer LUT and the specific composelayer color-grade route have evidence, but protected puppet/mixed puppet LUT and unrelated color-grade classes do not | Keep protected/mixed/generic color-grade paths stripped or probe-only. Future color-grade work needs separate native tests, validator runs, and smoke review |
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

1. Arona layer `405` middle visible-effect block internals before final publish:
   fresh Windows intake under
   `yakkai_arona/layer405_final_publish_composite_fresh.zip` provides
   authoritative capture-session-labeled Day/Sunset/Night final-publish
   evidence. The color-mask, default-delta, and isolated boundary questions are
   closed for now: Yakkai classifies as `yakkai-final-publish-rgb-mask`,
   `/tmp/yakkai-layer405-final-publish-boundary-compare` shows the isolated
   `final-display-before -> final-display-after` boundary changes the expected
   Windows sample points, and
   `/tmp/yakkai-layer405-isolated-publish-parity` classifies Day/Night as
   `isolated-publish-mismatch` plus Sunset as `isolated-publish-mixed`. The
   content-stage, transition, and range attribution passes all point at the
   Windows `prefix-3 -> prefix-7` checkpoint block. The range pass rules out a
   simple adjacent-vs-cumulative Yakkai stage-selection mistake, and the
   middle-block microscope narrows the next useful target to finer evidence or
   shader/effect-internal work around the first post-pulse material step and
   later waterwaves segment.
2. Blur/LUT renderer parity for other scenes or non-regular classes: only after
   the harness can compare enough frames to detect washed-out output and
   alpha/load regressions.
3. Puppet mixed-chain renderer repair: use the Phase 3.8 probe captures as
   negative evidence, and only revisit policy after the offscreen puppet
   effect publish path no longer occludes the final frame.
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

## Arona WE Reference Parity Subplan

This subplan refines the blur/LUT follow-up now that local Windows Wallpaper
Engine reference captures exist for Sleeping Arona (`3228578419`). Use it as the
target order for future renderer slices inside the broader Phase 3 limitations
track.

Current evidence from `tools/compare-arona-reference.sh --debug-effect-captures`
after merging `project.json` scene-property defaults with explicit `timeofday`
overrides:

- latest comparator artifact:
  `/tmp/yakkai-arona-reference/20260531T014823297979Z`
- `day`: `RMSE 0.0752796`
- `sunset`: `RMSE 0.0929019`
- `night`: `RMSE 0.181338`
- harness logs confirm `scene property defaults active: count=31`, so the
  remaining drift is not caused by missing Arona user-property defaults.
- Sunset and Night debug manifests show seven captured `lut-only` preserved
  layers: `82` (`WALL`), `174` (`00000-3034166934-inpainted`), `267`
  (`DESK_2_redraw`), `314` (`BG_CHAIR_REDRAW`), `1013` (`ARONA_DESK`), `1032`
  (`ARONA CHAIR_REDRAW`), and `2143` (`Arm_fix`).
- Slice A.1 enriched the LUT report with captured layer image paths, effect
  names, material shader paths, and capture-stage render-target sizes. That
  confirms the manifest can explain which `lut_loader` passes rendered and at
  what source/output dimensions.
- Slice A.2 adds preserved-layer `effectMaterials` diagnostics to the manifest
  and report. For layer `82` (`WALL`) and the other preserved Sunset/Night
  `lut-only` layers, material `1:0` binds slot `0` to the layer-local effect
  input render target and slot `1` to the time-of-day LUT texture. Sunset uses
  `P3R_sunset` with `g_Texture1Resolution=[64,64,64,64]`; Night uses
  `moonlight-contrast-brightness-50` with
  `g_Texture1Resolution=[512,512,512,512]`. Both report `multiply1=[1.0]`,
  `tc=[0.0]`, `BLENDMODE=0`, `CLAMP=1`, `LUT_FLIP_Y=0`, and generated defines
  `g_Texture0`/`g_Texture1`. Sunset resolves `QUAD_SIZE=16`, while Night
  resolves `QUAD_SIZE=64`.
- Slice A.3 adds `tools/arona_lut_sampling_lab.py`, a tooling-only sampler
  that extracts the bound LUT texture from `scene.pkg`, applies plausible
  `lut_loader` candidates, and ranks them against the captured layer output.
  Real reports:
  `/tmp/yakkai-arona-lut-lab/sunset-82/lut-sampling-report.md` and
  `/tmp/yakkai-arona-lut-lab/night-82/lut-sampling-report.md`. For layer `82`,
  the manifest-resolved candidates rank poorly in the coarse comparison
  (`sunset`: rank `14`, RMSE `0.8175`; `night`: rank `16`, RMSE `0.5633`),
  while the best coarse candidates prefer `flipY=true` plus `input-linear`.
  Do not patch shader math from this alone: both reports warn that
  `effect-input` and `effect-output` dimensions differ, so the comparison is
  mixing layer-local input with a screen-sized output capture.
- Slice A.4 adds a diagnostics-only `material-output-<effectIndex>-<materialIndex>`
  capture for preserved `lut_loader` materials. For Arona layer `82`, Sunset and
  Night now expose `material-output-1-0` captures at `4160x2923`, matching
  `effect-input` and removing the old resize warning. Same-size lab reports:
  `/tmp/yakkai-arona-lut-lab/sunset-82-material/lut-sampling-report.md` and
  `/tmp/yakkai-arona-lut-lab/night-82-material/lut-sampling-report.md`.
  Slice A.6 later found these captures are not valid shader-output evidence:
  `material_output_1_0.tga` is fully black/zero-alpha for the inspected Arona
  layers. Root cause: the debug copy reads the ping-pong target, but
  `SceneImageEffectLayer::ResolveEffect()` repurposes the final material node to
  write `SpecTex_Default`, so the ping-pong target is not populated for that
  final pass.
- Slice A.5 expands the LUT sampling lab with decoded WE texture sampler flags,
  manifest material context (`CLAMP`, `BLENDMODE`, `multiply1`, `tc`), and an
  `--all-lut-layers` aggregate report. Sunset and Night each report seven
  preserved LUT layers with `material-output-1-0` captures. Every sampled LUT
  texture currently reports sampler `flags=2`, `filterMode=bilinear`,
  `wrapMode=clamp`, so the occasional `nearest` best-candidate result is not
  explained by the TEX no-interpolation flag. Aggregate results:
  `/tmp/yakkai-arona-lut-lab/sunset-all-material/lut-sampling-summary.md` and
  `/tmp/yakkai-arona-lut-lab/night-all-material/lut-sampling-summary.md`. The
  candidate rankings from those reports are now treated as invalid because the
  selected material-output capture was blank.
- Slice A.6 adds alpha/visibility-aware scoring to the LUT lab and generated
  `/tmp/yakkai-arona-lut-lab/sunset-all-visible/lut-sampling-summary.md` and
  `/tmp/yakkai-arona-lut-lab/night-all-visible/lut-sampling-summary.md`.
  Both variants reported seven low-visibility layers and zero trusted layers
  because every selected `material-output-1-0` capture had `visibleFraction=0`
  and `opaqueFraction=0`. Manual inspection confirmed `effect_input.tga` has
  real alpha for layers such as `82_WALL`, while `material_output_1_0.tga` is
  black/zero-alpha. This turns A.6 into a regression detector for the debug
  capture itself rather than shader math evidence.
- Slice A.7 repaired final published `material-output-*` capture routing and
  added stricter LUT lab trust gates. The native debug copy now tracks the
  published final output when the LUT material is the final node, and invalid
  Vulkan image-copy extents are clamped instead of causing device loss. The
  repaired comparator run completed Day/Sunset/Night with effect manifests and
  no `VK_ERROR_DEVICE_LOST`. Fresh aggregate reports:
  `/tmp/yakkai-arona-lut-lab/sunset-all-repaired-strict/lut-sampling-summary.md`
  and
  `/tmp/yakkai-arona-lut-lab/night-all-repaired-strict/lut-sampling-summary.md`.
  Both Sunset and Night now report seven visible/opaque `material-output-1-0`
  captures (`visibleFraction=1.0`, `opaqueFraction=1.0`) and zero low-visibility
  layers. However, both variants also report seven dimension-mismatched layers
  and zero comparison-trusted layers because the repaired material outputs are
  screen-sized final-publish captures while the effect inputs remain
  layer-local. Candidate counts still skew toward `flipY=true` plus
  `input-linear`, but those rankings are directional composition evidence, not
  trustworthy shader-output proof.
- Slice A.8 adds layer-local pre-final-publish LUT diagnostics. When a preserved
  `lut_loader` material is also the final published pass, the debug renderer now
  inserts a duplicate layer-local diagnostic pass and captures
  `material-output-local-<effectIndex>-<materialIndex>` before the original
  final node publishes to `_rt_default`. The regular
  `material-output-<effectIndex>-<materialIndex>` capture remains screen-sized
  final-publish evidence. The LUT lab now prefers local material-output captures
  before final material-output captures. Fresh aggregate reports:
  `/tmp/yakkai-arona-lut-lab/sunset-all-local/lut-sampling-summary.md` and
  `/tmp/yakkai-arona-lut-lab/night-all-local/lut-sampling-summary.md`.
  Sunset now has six comparison-trusted layers and one low-visibility layer; the
  manifest candidate is rank `1` for all seven layers with RMSE roughly
  `0.0007`-`0.0034`, and all trusted best candidates are
  `quad=16 flipY=false filter=bilinear color=shader`. Night now has six
  comparison-trusted layers and one low-visibility layer. The manifest candidate
  is rank `1` on four layers and rank `2` on three layers with RMSE roughly
  `0.0008`-`0.0044`; trusted best candidates split between
  `quad=64 flipY=false filter=bilinear color=shader` and
  `quad=64 flipY=false filter=bilinear color=lut-linear-output-srgb`. This
  strongly indicates the core LUT texture selection, QUAD_SIZE, flip-Y, and
  filtering are mostly correct; the remaining Windows WE drift is more likely in
  final publish composition, color-space handling around publish, missing
  post-process blur/color grading, protected crop-sheet effects, or script/time
  dependent layers than in basic LUT lookup math.
- Slice A.9 adds a tooling-only final-publish drift microscope to the LUT lab.
  Per-layer reports now include `publishDrift`, which pairs
  `material-output-local-*`, screen-sized `material-output-*`, optional
  `final-publish`, and the variant's registered Windows-reference screen drift.
  Aggregate reports also include `publishClassificationCounts`. Fresh reports
  from `/tmp/yakkai-arona-reference/20260530T231836903776Z/summary.json`:
  `/tmp/yakkai-arona-lut-lab/sunset-all-publish-drift-fresh/lut-sampling-summary.md`
  and
  `/tmp/yakkai-arona-lut-lab/night-all-publish-drift-fresh/lut-sampling-summary.md`.
  Sunset classifies one layer as `final-publish-stage-drift` and six layers as
  `publish-transform-and-final-stage-drift`; Night classifies all seven LUT
  layers as `publish-transform-and-final-stage-drift`. The naive local-to-final
  resize RMSE remains high on most layers (`0.40`-`0.82` for the affected
  Sunset layers and `0.09`-`0.56` for Night), and final material output to
  `final-publish` RMSE is also high (`0.18`-`0.40`). This is directional
  evidence only: it proves the remaining drift is not explained by the
  layer-local LUT shader output alone, but it does not yet distinguish crop/
  transform mapping from color-space or final-stage composition differences.
- Slice A.10 adds bounded crop/scale registration inside `publishDrift` as
  `registeredLocalToFinal`. This searches a small set of same-aspect crops from
  the layer-local material output and compares the best crop to the screen-sized
  publish material output. Fresh reports from the same comparator artifact:
  `/tmp/yakkai-arona-lut-lab/sunset-all-publish-registration/lut-sampling-summary.md`
  and
  `/tmp/yakkai-arona-lut-lab/night-all-publish-registration/lut-sampling-summary.md`.
  Registration improves every local-to-final comparison, but does not collapse
  the drift. Sunset registered RMSE ranges from `0.022` on layer `174` to
  `0.590` on `82` (`WALL`); Night ranges from `0.055` on layer `174` to
  `0.410` on `82`. Final material output to `final-publish` remains high
  (`0.18`-`0.40`). This makes a pure crop/scale explanation unlikely for the
  broad background mismatch and keeps color-space/final-stage composition,
  missing post-process layers, and protected crop-sheet effects as active
  suspects.
- Slice A.11 adds manifest-only publish/composition diagnostics. Preserved
  effect layers now include a `publish` block with effect targets, final blend
  mode, parent id, transform, and capture timing; LUT material diagnostics now
  include final-material routing fields such as `resolvedOutputRenderTarget`,
  `debugMaterialOutputCommandSource`, and `debugSourceFinalEffectOutput`. Fresh
  reports from `/tmp/yakkai-arona-reference/20260531T005302548765Z/summary.json`:
  `/tmp/yakkai-arona-lut-lab/sunset-all-publish-diag/lut-sampling-summary.md`
  and
  `/tmp/yakkai-arona-lut-lab/night-all-publish-diag/lut-sampling-summary.md`.
  The important result is semantic: `final-publish` is explicitly
  `post-frame-render-target-dump`, so comparing a layer's screen-sized
  `material-output-*` to `final-publish` compares that layer against the
  completed `_rt_default` frame after other layers have rendered. The lab now
  classifies those differences as `post-frame-composite-delta` rather than a
  final-stage bug. For these Arona LUT layers, the final LUT material writes to
  `_rt_default`, final blend mode is `1`, `finalPublishedMaterial=true`, and
  the parent ids are populated (`82`/`WALL` parent `195`; desk/character layers
  primarily parent `328`). Remaining high registered local-to-final RMSE points
  back at transform/parent mapping, layer ordering/composition, missing
  post-process layers, or Windows reference differences, not a missing final
  material publish copy.
- Slice A.12 adds a default-frame progression microscope. Debug manifests now
  include zero-based `captureIndex` values, and the LUT lab can write
  `default-frame-progression.json`/`.md` with
  `--default-frame-progression`. Fresh reports from
  `/tmp/yakkai-arona-reference/20260531T014823297979Z/summary.json`:
  `/tmp/yakkai-arona-lut-lab/day-default-frame-progression/default-frame-progression.md`,
  `/tmp/yakkai-arona-lut-lab/sunset-default-frame-progression/default-frame-progression.md`,
  and
  `/tmp/yakkai-arona-lut-lab/night-default-frame-progression/default-frame-progression.md`.
  Day has no screen-sized LUT snapshots. Sunset and Night each have seven,
  ordered by capture index as `174 -> 314 -> 82 -> 267 -> 1032 -> 1013 ->
  2143`. Among those snapshots, every consecutive step moves closer to the
  normalized Windows reference; the largest improvements are `1032 -> 1013`
  (`ARONA_DESK`) and `314 -> 82` (`WALL`). Sunset final-frame RMSE to the
  normalized reference (`0.102447` in the progression report) is lower than the
  last LUT snapshot (`0.157336`), so downstream composition helps Sunset.
  Night final-frame RMSE (`0.207129`) is worse than the last LUT snapshot
  (`0.140010`), so the next Night-specific target is downstream/default-frame
  drift after the preserved LUT progression, not core LUT shader sampling.
- Slice A.13 adds post-LUT drift attribution. The LUT lab can now write
  `post-lut-drift.json`/`.md` with `--post-lut-drift`, comparing the last
  screen-sized LUT material-output capture to the final Yakkai frame and the
  normalized Windows reference, then listing later captures and coarse region
  drift. Reports from the same comparator artifact:
  `/tmp/yakkai-arona-lut-lab/day-post-lut-drift/post-lut-drift.md`,
  `/tmp/yakkai-arona-lut-lab/sunset-post-lut-drift/post-lut-drift.md`, and
  `/tmp/yakkai-arona-lut-lab/night-post-lut-drift/post-lut-drift.md`. Day
  classifies as `missing-screen-lut-snapshots`. Sunset classifies as
  `final-frame-improved-after-lut` with downstream reference RMSE delta
  `-0.054889`. Night classifies as `final-frame-regressed-after-lut` with
  downstream reference RMSE delta `0.067119`; the largest added drift is in the
  `top` (`+0.129635`), `right` (`+0.124791`), and `center` (`+0.060752`)
  regions. The only captured post-LUT effect layers are lens/flare layers
  `469`, `472`, `475`, `479`, `482`, and `485`; their screen-sized captures are
  very close to the final Yakkai frame and far from the last LUT snapshot. The
  next target should investigate Night-specific flare/default-frame
  composition and tone contribution before touching LUT lookup math.
- Slice A.14 adds a post-LUT flare/default-frame microscope. The LUT lab can
  now write `post-lut-flare-drift.json`/`.md` with
  `--post-lut-flare-drift`, filtering full-frame post-LUT effect captures,
  tagging named flare/lens layers separately from unnamed post-LUT effect
  layers, and ranking previous-to-current deltas plus region deltas. Reports
  from `/tmp/yakkai-arona-reference/20260531T014823297979Z/summary.json`:
  `/tmp/yakkai-arona-lut-lab/day-post-lut-flare-drift/post-lut-flare-drift.md`,
  `/tmp/yakkai-arona-lut-lab/sunset-post-lut-flare-drift/post-lut-flare-drift.md`,
  and
  `/tmp/yakkai-arona-lut-lab/night-post-lut-flare-drift/post-lut-flare-drift.md`.
  Day has no post-LUT full-frame effect captures. Sunset and Night each include
  six post-LUT full-frame effect layers: `469` (`1st flare`), `472`
  (`2nd flare`), `475` (`3rd flare`), `479` (`4th flare`), `482`
  (`c7884e6807cf62bb85f8d8b67942cec4`, tagged `post-lut-effect` and using
  `effects/pulse`/`genericimage3`), and `485`
  (`60-604156_flare17-rainbow-lens-flare-png`). Night's dominant regression is
  the first post-LUT full-frame capture, layer `469` `effect-output`
  (`captureIndex=36`): previous-frame RMSE `0.210314`, reference RMSE delta
  `+0.064072`, with region deltas `top +0.127277`, `right +0.119651`, and
  `center +0.058200`. Subsequent post-LUT effect/output/final-publish captures
  are small oscillations around the same final frame (`previousRmse` roughly
  `0.018` or less). Sunset has the same dominant first post-LUT step but it
  improves the reference (`referenceDelta=-0.052636`). The next target should
  compare the authored properties and blend/tone handling for layer `469`
  between Sunset and Night before touching the general LUT path.
- Day reports only stripped `405` (`ARONA_CROP_SHEET`) as LUT/color evidence,
  which suggests the `timeofday`-dependent property state may disable the
  regular background LUT pass for Day.
- all variants still strip `405` (`ARONA_CROP_SHEET`) as
  `protected-puppet-mixed`, and logs still show stripped
  `Post-processing Layer==BLUR` chains.
- Slice P.2 adds a protected puppet probe lab. The Arona comparator can pass
  `--debug-effect-probe-layers 405` with debug effect captures, and
  `tools/arona_protected_puppet_lab.py` summarizes the resulting
  `ARONA_CROP_SHEET` probe captures. Fresh probe evidence from
  `/tmp/yakkai-arona-reference/20260531T114625490973Z` produced
  `effect-input`, `effect-output`, default-boundary, `final-publish`, and
  Sunset/Night `material-output-3-0` captures. The probe materially changed the
  final-frame RMSE for Day/Sunset, so it is lab evidence only and not a normal
  policy candidate.
- Slice P.3 extends the protected puppet lab with capture image statistics,
  boundary comparisons, and optional probe-final-to-normal comparison through
  `--normal-summary`. Fresh evidence from normal
  `/tmp/yakkai-arona-reference/20260531T122510965493Z` and probe
  `/tmp/yakkai-arona-reference/20260531T122728054918Z` produced
  `protected-puppet-lab/protected-puppet-lab.md`. Day
  `effect-input->effect-output` classified `stable` (`RMSE 0.022465`), while
  Sunset and Night classified `color-drift` (`RMSE 0.062581` and `0.155686`)
  with alpha bounds stable (`alphaIou 1.0`). `effect-output->final-publish`
  classifies as `local-to-frame-composition`, and probe-final-to-normal
  classifies as full-frame `color-drift` for all variants (`RMSE 0.22353`,
  `0.24217`, and `0.244704`). This points away from a pure alpha/bounds failure
  in the layer-local chain and toward time-variant color/LUT/effect
  contribution plus composition routing as the next protected-chain questions.
- Slice P.4 adds generic route metadata to effect-capture manifests and
  screen-space centroid drift to the protected puppet lab. Fresh evidence from
  normal `/tmp/yakkai-arona-reference/20260531T130326634018Z` and probe
  `/tmp/yakkai-arona-reference/20260531T130548924778Z` classified layer `405`
  as `flat-card-puppet-effect-route` with route risk
  `puppet-effect-output-displayed-as-flat-card`. Day and Sunset now report
  `screen-space-drift` (`52.68px` and `68.27px`), while Night remains
  `screen-space-stable` (`11.21px`). That points to a generic puppet effect
  publish routing problem, not an Arona-specific layer-id rule.
- Slice P.5 changes the generic non-channelmap puppet final route from a flat
  card to `puppet-image-space-skinned-mesh` and enables puppet skinning on the
  standalone final material. Fresh evidence from normal
  `/tmp/yakkai-arona-reference/20260531T133427196190Z` and probe
  `/tmp/yakkai-arona-reference/20260531T133657538359Z` now classifies layer
  `405` as `standalone-puppet-effect-route` with empty `routeRisk`. This
  removes the specific flat-card route hazard, but Day and Sunset still report
  screen-space drift (`48.55px` and `72.27px`) and the contact sheet still
  shows the crop-sheet probe as a left-side overlay. Keep the protected chain
  stripped; the next question is why the probed utility/crop-sheet contribution
  is visible/placed differently, not whether the route should be Arona-specific.
- next target: split layer `405`'s protected chain by contribution if possible:
  LUT-only versus pulse/waterwaves/shake, while keeping normal policy stripped.
  Layer `469` (`1st flare`) Night-specific authored properties remain the next
  global/default-frame target if protected-chain splitting needs more harness
  support first.

### Slice A: LUT / Color-Grade Parity

Target this first. The Night reference drift is broad and strongly color/tone
weighted, which makes LUT/color-grade behavior the highest-signal target.

Scope:

- compare Day/Sunset/Night debug manifests and effect captures for Arona
  `lut_loader` layers, starting with background `lut-only` layer `82` and the
  other already-promoted regular image-layer LUT paths.
- verify LUT texture selection, lookup texture dimensions, shader uniforms,
  effect strength, blend mode, and color-space/gamma handling.
- keep protected crop-sheet, fullscreen utility, and mixed composelayer
  color-grade paths blocked unless a later slice proves them separately.

Acceptance:

- no Arona-specific renderer branch.
- `tools/compare-arona-reference.sh` still exits `review`/0 and writes
  artifacts.
- Night RMSE and RGB drift move materially toward WE without increasing Day or
  Sunset drift enough to hide regressions.
- Arona Day/Sunset/Night smoke variants still pass or produce expected review
  artifacts for human comparison.

2026-06-09 evidence-only stop:

- current comparator artifact:
  `/tmp/yakkai-arona-lut-color-parity/baseline/summary.json`
- current Day/Sunset/Night RMSE: `0.0881986` / `0.0758847` / `0.0697778`.
- regular Sunset/Night `lut-only` layers are already closely explained by the
  manifest-resolved candidates. Trusted regular layers mostly report
  `manifestRank=1`; the trusted rank-2 layers remain very close
  (`314` RMSE `0.00431478`, `82` RMSE `0.00337407`).
- the largest default-frame regression is `2143 -> 405 ARONA_CROP_SHEET`,
  stage `material-output-12-0`, with `stepRmse=0.509021` and
  `referenceDelta=+0.336093`.
- post-LUT attribution classifies as `final-frame-improved-after-lut`, with
  last-LUT reference RMSE `0.474167`, final reference RMSE `0.074727`, and
  downstream delta `-0.39944`.
- selected hypothesis: `not-lut`. Do not patch regular `lut_loader`, texture
  decode, regular LUT color-space handling, or regular LUT policy from this
  evidence. The next Arona target should be the protected puppet/default-RT
  boundary around layer `405`.

### Slice B: Background Blur / Post-Processing Layers

Target after LUT parity improves or evidence proves LUT is not the primary
cause. The current logs show stripped `Post-processing Layer==BLUR` utility
layers, but Phase 3.12 indicated the Arona fullscreen blur probe was a smaller
delta than LUT/background work.

Scope:

- keep broad fullscreen/utility blur blocked.
- use high-risk probes to compare blur input/output/final-publish artifacts
  against the WE contact sheet.
- only consider a structural allow path if a narrow non-carrier blur candidate
  exists and does not affect puppet alpha or background clear-color sentinels.

Acceptance:

- no broad `blur` family allowlist.
- `3476236738` background sentinel remains green.
- Arona comparator contact sheet shows improved background separation or
  softness without washed-out output.

### Slice C: `ARONA_CROP_SHEET` Protected Character Chain

Target only after background/global parity is closer. This path is high risk
because it touches the protected puppet crop sheet and mixes LUT with pulse,
shake, waterwaves, and other effects.

Scope:

- diagnostics first: capture exact chain shape, pass inputs, output alpha, and
  final publish routing.
- do not change normal policy until a renderer-level puppet effect publish path
  can be proven with before/after artifacts.
- use `tools/compare-arona-reference.sh --debug-effect-captures --debug-effect-probe-layers 405`
  plus `tools/arona_protected_puppet_lab.py --normal-summary <normal-summary>`
  to inspect protected-chain probe captures without treating the probed final
  frame as a fix.

Acceptance:

- sleeping character, desk, flare arc, and alpha boundaries remain intact.
- no rectangular/gray occlusion like the earlier puppet mixed-chain probes.
- comparator and smoke runs show improvement that is visible in the character
  path, not just global frame metrics.

2026-06-10 protected puppet/default-RT stop:

- artifact root:
  `/tmp/yakkai-arona-protected-default-rt-boundary/`
- normal comparator Day/Sunset/Night RMSE:
  `0.0906713` / `0.075915` / `0.0692575`.
- full layer `405` probe Day/Sunset/Night RMSE:
  `0.0871007` / `0.0763021` / `0.0712025`.
- prefix probe recovery:
  - prefix `0`: `0.0802839` / `0.0925228` / `0.18191`
  - prefix `1`: `0.0863583` / `0.0986711` / `0.187231`
  - prefix `2`: `0.0862968` / `0.10019` / `0.187643`
  - prefix `7`: `0.0865852` / `0.0767716` / `0.0711158`
  - prefix `11`: `0.0880205` / `0.07639` / `0.0712083`
- prefix lab reports:
  `/tmp/yakkai-arona-protected-default-rt-boundary/prefix-labs/`
- selected hypothesis: `effect-chain-recovery`, not
  `default-rt-composition`. Prefix `1/2/7/11` all keep the promoted generic
  route stable (`card` input, `puppet-skinned-mesh` final display,
  `effect-layer-node-final-publish`), and
  `default-before-effect->default-after-effect` remains `stable`.
- no renderer behavior change was made from this evidence. The Night gap is
  reduced only after the LUT/waterwaves prefix enters the chain, so the next
  renderer target should be a narrower effect-chain boundary with Windows
  pass evidence rather than another puppet route/default-RT patch.
- prefix `12` was not treated as evidence in this slice because the current
  headless Qt harness session aborts under `xvfb-run` in
  `QGuiApplicationPrivate::createEventDispatcherEv` before renderer logging.
  A real-session run reached first frame, so this is a tooling/runtime
  blocker, not an Arona render-boundary result.

2026-06-10 layer `405` prefix 2-7 effect-chain stop:

- artifact root:
  `/tmp/yakkai-arona-prefix-2-7-effect-chain/`
- prefix RMSE table:
  - prefix `2`: `0.0858372` / `0.0989135` / `0.187149`
  - prefix `3`: `0.0858825` / `0.081035` / `0.0709242`
  - prefix `4`: `0.0863312` / `0.0787674` / `0.0713879`
  - prefix `5`: `0.0863936` / `0.079858` / `0.0718435`
  - prefix `6`: `0.0861431` / `0.0792446` / `0.0709687`
  - prefix `7`: `0.0862672` / `0.0789933` / `0.0713856`
- first recovery boundary: prefix `2 -> 3`, where layer `405` adds the first
  `LUT Loader` pair after the two pulse passes.
- protected puppet lab evidence: route/default-RT stayed stable across the
  boundary (`card` input, `puppet-skinned-mesh` final,
  `effect-layer-node-final-publish`, and
  `default-before-effect->default-after-effect=stable`).
- selected hypothesis: `protected-local-lut-boundary`. The prefix-3 all-LUT
  drilldown found layer `405`'s actual local LUT stage
  `material-output-local-3-0` closely matches the local model
  (`best rmse=0.006367853`, manifest candidate `0.006378575`), while prefix `7`
  only looked bad for layer `405` because later effects left the lab with
  `missing-local-material-output`.
- no renderer behavior change was made and no Arona-specific production logic
  was added. This evidence stops short of a safe generic renderer patch.
- next target: downstream final-publish/composite drift or Windows per-pass
  evidence for layer `405`; do not restart from route/default-RT, regular
  `lut_loader`, texture decode, or regular LUT policy unless new evidence
  contradicts this slice.
- harness/tooling notes: the first local capture attempt hit `/tmp` disk quota
  while writing prefix `3`; stale Yakkai scratch artifacts were removed and the
  fresh prefix `3..7` run completed. No Qt event-dispatcher blocker recurred in
  this slice.

2026-06-10 layer `405` final-publish/composite boundary stop:

- artifact root:
  `/tmp/yakkai-arona-final-publish-composite-boundary/`
- normal comparator Day/Sunset/Night RMSE:
  `0.0885455` / `0.0758132` / `0.0716605`.
- prefix RMSE table:
  - prefix `2`: `0.0861582` / `0.100437` / `0.186719`
  - prefix `3`: `0.085912` / `0.0769291` / `0.0708945`
  - prefix `7`: `0.0861397` / `0.0766664` / `0.0729421`
- protected puppet lab evidence: route/default-RT stayed stable across the
  boundary (`card` input, `puppet-skinned-mesh` final,
  `effect-layer-node-final-publish`, and
  `default-before-effect->default-after-effect=stable`).
- prefix `3` local LUT evidence matched the model. Layer `405` Night best RMSE
  was `0.00552772032096982` with manifest RMSE `0.005535931792110205`; Sunset
  best/manifest RMSE was `0.00455861771479249`. Prefix `7` layer `405` reported
  `missing-local-material-output`, so it is not evidence of bad LUT shader math.
- final-publish color-drift remained directional evidence only because
  final-publish is a post-frame `_rt_default` dump, not an isolated final
  display node capture.
- Windows inventory found optional prefixes and waterwaves shader-debug
  evidence, but not layer `405` final-publish/composite pass-boundary captures,
  final publish input texture, default framebuffer before/after, or
  final-publish blend/SRV metadata.
- selected outcome: `windows-per-pass-needed`. No renderer behavior change was
  made and no Arona-specific production logic was added.

2026-06-10 layer `405` Windows final-publish/composite evidence intake:

- expected bundle path:
  `yakkai_arona/captures/layer405_final_publish_composite/`
- actual bundle path:
  `yakkai_arona/layer405_final_publish_composite/`
- analysis summary:
  `/tmp/yakkai-arona-windows-final-publish-composite-analysis/summary.md`
- bundle status: `partial_evidence_exported_unmapped_variants`.
- requested Day/Sunset/Night variants remain missing; the received evidence has
  two unmapped representatives, `capture_one_frame90` and
  `capture_two_frame104`.
- both representatives include effect input, prefix `3`, prefix `7`, final
  publish input, default before/after, LUT-pair SRVs, final-publish blend/SRV
  metadata, and lower-ribbon/transparent-edge pixel histories.
- Windows final publish event `621` samples SRV0 resource `374`
  (`4160x2923`, `R8G8B8A8_UNORM`) into swapchain resource `65`
  (`2560x1440`, `R8G8B8A8_UNORM`) with blend state
  `color=(SrcAlpha Add InvSrcAlpha)`, `alpha=(SrcAlpha Add InvSrcAlpha)`, and
  `writeMask=7`. Treat `writeMask=7` as RGB-only final publish evidence.
- pixel history supports the color-mask reading: transparent alpha-zero samples
  do not change the default target, while partial-alpha samples change RGB and
  keep default alpha opaque.
- At this partial-intake stage, Yakkai prefix `3`/`7` manifests recorded final
  blend mode `1`, route
  `card -> puppet-skinned-mesh -> effect-layer-node-final-publish`, and
  `_rt_default` post-frame capture, but did not expose the actual Vulkan final
  color write mask. Current code could use RGB-only or RGBA masks depending on
  camera/global target state, so this is not yet a safe renderer patch.
- direct `final-publish-input` versus default-delta image comparison needs
  registration because the Windows layer input is `4160x2923` and the swapchain
  target is `2560x1440`.
- selected outcome from the partial intake was `windows-behavior-not-yet-modeled`.
  No renderer behavior change was made and no Arona-specific production logic
  was added.
- fresh Windows archive `yakkai_arona/layer405_final_publish_composite_fresh.zip`
  supersedes the source-manifest blocker. It validates as
  `complete_fresh_live_rdc_labeled_variants` with capture-session labels:
  day `timeofday=1`, sunset `timeofday=2`, and night `timeofday=3`.
- fresh pass mappings:
  - day: effect input `344 / 377`, prefix `3` `394 / 377`, final layer output
    `601 / 374`, final publish `621 / 374`.
  - sunset: effect input `527 / 1736`, prefix `3` `577 / 1736`, final layer
    output `804 / 1736`, final publish `824 / 1736`.
  - night: effect input `902 / 1769`, prefix `3` `952 / 1769`, final layer
    output `1179 / 1769`, final publish `1199 / 1769`.
- all fresh variants publish to swapchain resource `65` (`2560x1440`,
  `R8G8B8A8_UNORM`) from `4160x2923` layer targets with final-publish blend
  `color=(SrcAlpha Add InvSrcAlpha)`, `alpha=(SrcAlpha Add InvSrcAlpha)`, and
  `writeMask=7`.
- `tools/arona_layer405_fresh_publish_compare.py` now performs the Yakkai-side
  intake. After rebuilding the backend and rerunning the Arona comparator into
  `/tmp/yakkai-layer405-fresh-yakkai-rerun`, Yakkai manifests expose
  `debugEffectPassStates` candidates for `_rt_default` final publish with
  `blendMode=1`, `blendEnabled=true`, `colorMask=RGB`, and `colorMaskBits=7`.
  The comparator classifies the selected pass as
  `yakkai-final-publish-rgb-mask`, so final-publish color-mask parity matches
  the fresh Windows RGB-only evidence.
- bounded layer-local-to-default image registration remains weak for all fresh
  variants: Day/Sunset/Night after RMSE
  `0.41506639` / `0.41790435` / `0.29333428`, all classified as
  `weak-or-unregistered-match`. This means the next target should be stronger
  pixel-history/default-delta registration keyed to known final-publish sample
  points, not threshold tuning and not a renderer color-mask patch.
- the default-delta oracle now performs that pixel-history keyed check. The
  real run in `/tmp/yakkai-layer405-default-delta-oracle` still reports
  `yakkai-final-publish-rgb-mask`, but every variant/sample is
  `default-delta-mismatch`: lower-ribbon delta RMSE is
  Day/Sunset/Night `0.20989248` / `0.22991677` / `0.18721128`, and
  transparent-edge delta RMSE is `0.26572305` / `0.00554594` /
  `0.00226412`. All sampled Yakkai deltas have zero magnitude at the checked
  default-target pixels, while Windows final-publish pixel history changes RGB.
  The locator follow-up in `/tmp/yakkai-layer405-default-delta-locator`
  confirms every `default-before-effect -> default-after-effect` sample is
  `missing-default-delta` with zero magnitude and no nearest nonzero delta, but
  every optional `default-after-effect -> final-publish` boundary sample is
  `delta-at-windows-sample`. Lower-ribbon boundary magnitudes are
  Day/Sunset/Night `1.08272946` / `1.19851887` / `0.9906832`; transparent-edge
  magnitudes are `0.04913712` / `0.14280111` / `0.19422655`. The next target is
  a generic debug capture timing/final-publish boundary investigation around
  layer `405`, not color-mask parity, not projection/source-coordinate mapping,
  not registration threshold tuning, and not another Windows capture request.
- the final-publish boundary follow-up is now complete. Effect-layer final
  publish nodes register debug-only `final-display-before` and
  `final-display-after` captures around the actual final material node. The real
  run in `/tmp/yakkai-arona-final-publish-boundary-run3` and comparator output
  in `/tmp/yakkai-layer405-final-publish-boundary-compare` still show
  `default-before-effect -> default-after-effect` as `missing-default-delta`,
  but `layerFinalPublishBoundary` is `delta-at-windows-sample` for
  Day/Sunset/Night. That resolves the old miss as a capture-timing artifact and
  leaves the next candidate as isolated final-publish output parity versus
  Windows, not another route/color-mask/source-coordinate patch.
- the isolated final-publish output parity follow-up is now complete. The real
  run in `/tmp/yakkai-layer405-isolated-publish-parity` classifies Day/Night as
  `isolated-publish-mismatch` and Sunset as `isolated-publish-mixed` using
  `final-display-before -> final-display-after`. Lower-ribbon samples mismatch
  in all variants with negative delta cosine, while only Sunset's
  transparent-edge sample is a directional match. That moves the next candidate
  to layer `405` content-stage attribution before final publish, not
  final-publish routing, color masks, source-coordinate mapping, or another
  Windows capture request for this question.
- the content-stage attribution follow-up is now complete. The real run in
  `/tmp/yakkai-layer405-content-stage-attribution` classifies Day/Sunset/Night
  as `content-stage-mismatch`. Raw `effect-input` is near-exact and `prefix-3`
  is close, but `prefix-7` and `final-publish-input` diverge for all variants.
  That moves the next candidate to the later layer-local visible-effect
  progression before final publish, not layer loading, final-publish routing,
  color masks, source-coordinate mapping, or another Windows capture request for
  this question.
- the content-transition attribution follow-up is now complete. The real run in
  `/tmp/yakkai-layer405-content-transition-attribution` classifies
  Day/Sunset/Night as `content-transition-mismatch`, with
  `prefix-3-to-prefix-7` as the worst transition for every variant. That moves
  the next candidate to the middle layer-local visible-effect block before final
  publish, not the later final-publish input transition.
- the content-range attribution follow-up is now complete. The real run in
  `/tmp/yakkai-layer405-content-range-attribution` classifies Day/Sunset/Night
  as `content-range-mismatch`. Day still chooses the single
  `material-output-5-0 -> material-output-6-0` waterwaves step, Night chooses
  `material-output-1-0 -> material-output-3-0` (`pulse -> pulse -> lut`), and
  Sunset only gets closest by spanning `effect-input -> material-output-12-0`
  across nearly the full chain. That rules out a simple adjacent-vs-cumulative
  range attribution mistake for the Windows `prefix-3 -> prefix-7` block.
- the middle-block microscope follow-up is now complete. The real run in
  `/tmp/yakkai-layer405-middle-block-microscope` classifies Day/Sunset/Night as
  `middle-block-incomplete-progress`. Day's selected first waterwaves step
  `material-output-2-0 -> material-output-3-0` moves away from Windows
  `prefix-7`, Sunset's same step moves slightly toward but remains far off, and
  Night's large helpful LUT step is followed by a smaller waterwaves regression
  at `material-output-4-0 -> material-output-5-0`.
- the selected-step metadata follow-up is now complete. The real run in
  `/tmp/yakkai-layer405-selected-step-metadata` preserves the shader/material
  state for Day `2 -> 3` (`pulse -> waterwaves`), Sunset `2 -> 3`
  (`pulse -> lut_loader`), Night `2 -> 3` (`pulse -> lut_loader`), and Night
  `4 -> 5` (`waterwaves -> waterwaves`). It also writes
  `middle-block-windows-request.md`, which is the current precise Windows-side
  ask for every internal pass output between Windows `prefix-3` and `prefix-7`
  with event ids, SRV/RTV bindings, constants, blend state, and full-resolution
  pass PNGs.
- no additional Windows capture is currently required for the color-mask
  question. No renderer behavior change was made and no Arona-specific
  production logic was added. Any future color-mask or blend-mode renderer
  change needs a separate renderer-fix plan with before/after Arona comparison,
  smoke/deep-smoke validation, and human visual review.

### Slice D: Particles, Bokeh, And Minor Effects

Target after global tone and background/character effect paths are closer. These
are likely visible polish differences, but they should not block the first
Arona parity improvements.

Scope:

- inventory missing/suppressed particle families from Arona logs.
- choose one isolated fixture or one Arona particle family at a time.
- keep particle fixes separate from LUT/blur/composition fixes so visual drift
  remains attributable.

### Slice E: SceneScript And Media-Integration Noise

Track separately unless a concrete missing visual ties back to it. Current
non-fatal SceneScript errors and unsupported media integration layers are noisy,
but the comparator evidence points first at color/effect parity.

Current diagnostic state:

- `tools/scene-script-log-summary.py` summarizes SceneScript binding layers,
  unsupported media-integration placeholders, and runtime gaps by class/API/layer.
- `tools/validate-scene.sh` reports visible SceneScript gaps as warnings and
  keeps media/runtime-only or harmless gaps as triage counts.
- `3326873240` is the first review fixture: local validation resolves 20
  QuickJS binding layers and reports visible runtime gaps plus media-only
  placeholders. It now has deep-only Morning, Day, Dusk, Night, and Day Night
  Gradient smoke variants for visual time-mode review.
- `3301291394` is now active deep text SceneScript coverage: local validation
  resolves Clock/Date text bindings, reports per-property `origin` and `text`
  binding counts, and logs generated text-layer representation.

Scope:

- reduce log noise only when it corresponds to a missing visible feature.
- do not combine SceneScript expansion with LUT/blur policy changes.

### Required Gates For Every Arona Parity Slice

- clear shader cache before render validation.
- run `tools/compare-arona-reference.sh` before and after the change.
- inspect `summary.json`, `contact-sheet.png`, and per-variant logs.
- run Arona Day/Sunset/Night smoke variants or the deep suite when pixels
  change.
- run `tools/validate-scene.sh 3228578419 8000` and
  `tools/validate-scene.sh 3476236738 10000` for renderer-policy changes.
- ask for human visual review when metrics improve but the contact sheet is
  visually ambiguous.
