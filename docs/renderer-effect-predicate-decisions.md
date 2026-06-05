# Renderer Effect Predicate Decisions

This document records generic renderer allow predicates, human visual gates, and
hard blockers. Production predicates must not use wallpaper ids, layer ids,
layer names, or animation ids. Those ids can appear only as evidence
references.

## Template

### <class-name>

- State:
- Predicate:
- Reject conditions:
- Evidence:
- Tests:
- Visual review:
- Decision:

## Decisions

### `simple-water`

- State: `production-allowed` for the existing narrow predicate.
- Predicate: preserve isolated regular image-layer water effects when the
  classified candidate is water-only, not puppet, not protected puppet, not
  fullscreen, not composelayer, and not a utility carrier.
- Reject conditions: any mixed family, puppet path, protected crop-sheet path,
  fullscreen layer, composelayer, utility carrier, audio/media carrier, or
  missing classification keeps the path stripped.
- Evidence: `3476236738` currently exposes seven allowed `simple-water`
  records in the generic inventory snapshot; `3228578419` remains protected
  from broad simple-water enablement.
- Tests: native policy tests cover simple water classification and rejected
  mixed/carrier/puppet cases. `tools/validate-scene.sh` checks that
  `3476236738` has allowed simple-water candidates and `3228578419` has no
  allowed simple-water candidates.
- Visual review: `3476236738` background sentinel guards the former flat gray
  clear-color regression; broader visual review is still required for any
  widened water predicate.
- Decision: keep allowed narrowly; do not widen into puppet, protected puppet,
  composelayer, fullscreen, utility, or audio paths.

### `composelayer-water-only`

- State: `production-allowed` for the narrow composelayer water-only
  predicate, after human visual approval.
- Predicate: preserve a water-only effect chain only when classification
  reports `candidateEffectClass=composelayer-water-only` and
  `candidateChainShape=water-composelayer`. This covers composelayer carriers
  whose visible effects are water-family only and excludes utility carriers,
  fullscreen layers, puppet layers, protected puppet paths, audio carriers,
  blur/LUT/color-grade mixes, and unknown mixed chains.
- Reject conditions: utility water, fullscreen water, mixed water/non-water
  chains, puppet paths, protected puppet paths, audio carriers, missing
  classification, crashes, material failures, non-generic routing, or human
  visual rejection remain stripped or probe-only.
- Evidence: Arona layer `239` is the current fixture. Baseline diagnostic
  validation initially reported it as stripped with `reason=puppet-alpha-strip`.
  The explicit probe run `tools/validate-scene.sh 3228578419 8000 --probe-layers
  239` passed with 23 checks, 0 failures, one SceneScript warning, and
  `baseline-vs-probe RMSE=0.0487605`; the probe inventory row was
  `composelayer-water-only probe-only water-composelayer layer=239`. After the
  production predicate, fresh validation reports layer `239` allowed with
  reason `composelayer-water-effect`, final route
  `effect-layer-composite-final-publish`, `publishFinalOutput=true`, empty
  route risk, and water materials `effects/waterripple` plus
  `effects/waterflow`.
- Tests: native policy tests cover stable water-composelayer and water-utility
  classification, explicit probe eligibility, the narrow composelayer water
  allow path, and the utility water reject path. Inventory tests cover inferring
  `composelayer-water-only` and `utility-water-only` from older manifests whose
  explicit class field is absent or `none`, plus allowed composelayer water
  records classifying as `production-allowed`.
- Visual review: approved by human review on 2026-06-01 using
  `/tmp/yakkai-visual-gate/3228578419-carrier-water-review.png`
  (`baseline | probe-layer-239 | diff-rmse-0.0487605`).
- Decision: keep allowed narrowly. Do not widen into utility/fullscreen water,
  protected puppet water, generic puppet paths, audio carriers, or mixed water
  chains without separate evidence and visual approval.

### `protected-puppet-water-lut-pulse-shake`

- State: strict `production-allowed` for recognized protected puppet safe-family
  chains, still under active route/composition investigation.
- Predicate: preserve protected puppet crop-sheet chains only when visible
  effects are limited to recognized water, LUT, pulse, and shake families, the
  policy classifies the path as protected puppet, and the final display route
  uses the repaired generic standalone layer-card path instead of quarantined
  channelmap sidecars.
- Reject conditions: blur, color grading, unknown families, generic puppet mixed
  paths, derived channelmap sidecar probes, utility carriers, fullscreen paths,
  composelayers, and missing route diagnostics.
- Evidence: Arona layer `405` is the current fixture. The 2026-06-01 generic
  inventory snapshot reports protected puppet records with active animation
  slot `2`, route risk empty, final display route
  `standalone-puppet-final-display`, and layer-card final mesh. Fresh baseline
  run `20260601T022327422031Z` classifies Day as
  `protected-puppet-safe-family` and Sunset/Night as `protected-puppet-lut`.
  The protected puppet lab reports layer-local input/output alpha as stable but
  final coverage as `final-screen-drift`; because `final-publish` is a
  post-frame `_rt_default` dump, this is evidence that stronger final-display
  diagnostics are needed before another production route change. Follow-up run
  `20260601T033709055429Z` adds completed `final-display-before/after`
  captures around the standalone final-display node. The lab now classifies
  all three variants as `final-display-boundary-present`, with active slot `2`
  and visible final-display deltas near `0.33` of the 1280x720 frame. The
  final-display delta geometry does not yet align with layer-local
  `effect-output` alpha geometry: all three variants classify as
  `final-display-shape-drift`, with delta/output visible ratios near `1.45`,
  bounds IoU near `0.76`, and centroid drift near `0.062`. A follow-up
  threshold sweep at RGB delta thresholds `1`, `4`, `8`, and `16` out of 255
  keeps all variants in `persistent-shape-drift`, so the drift is not explained
  by weak halo/noise at the single-pixel threshold. A second follow-up
  projection diagnostic maps the layer-local output centroid from output bounds
  into screen-space final-display delta bounds; all variants classify as
  `screen-space-affine-consistent`, with projected centroid drift under `0.02`.
  That points to a coordinate-space/expected-geometry question rather than
  immediate proof of another parent/scale route defect.
- Tests: native policy tests cover protected puppet LUT classification and
  protected-puppet diagnostics, including final-display boundary capture
  registration and manifest serialization; summary tests cover active slot,
  cutout coverage, final-display-boundary lab reporting, and final-display
  alignment, threshold-sensitivity, and screen-space projection classification.
- Visual review: Arona Day/Sunset/Night comparator and deep smoke remain the
  required visual gates. REVIEW status is not a failure, but it is not automatic
  approval for widening.
- Decision: keep strict safe-family allow path. Do not widen to blur,
  color-grade, unknown mixed, or generic puppet paths. The new final-display
  boundary evidence shows the standalone display node contributes, so do not
  make another parent/route fix until the final-display shape evidence is
  checked against expected layer-local or Windows geometry.

### `regular-blur-only`

- State: `production-allowed` for the narrow non-carrier predicate.
- Predicate: preserve a blur chain only when classification reports
  `candidateEffectClass=regular-blur-only` and `candidateChainShape=blur-only`.
  This excludes utility carriers, fullscreen layers, composelayers, puppet
  layers, protected crop-sheet paths, LUT/color-grade mixes, and unknown mixed
  chains.
- Reject conditions: fullscreen blur outside the separate utility-blur
  predicate, composelayer blur, protected puppet blur, mixed blur/color-grade,
  generic puppet paths, audio carriers, and any missing/unknown classification
  remain stripped or probe-only.
- Evidence: the B.2 inventory after validation reported one allowed
  `regular-blur-only` candidate (`3476236738` layer `137`) with reason
  `regular-blur-only-effect`; utility blur is owned by the separate
  `utility-blur` decision below. `tools/validate-scene.sh 3476236738 10000`
  passed with the background sentinel (`19 passed, 0 failed, 2 warnings`), and
  `tools/validate-scene.sh 3228578419 8000` passed during that slice (`17
  passed, 0 failed, 1 warning`). Shallow smoke completed with
  Arona Day in `REVIEW` and Shiroko video in `PASS`; no smoke failure was
  introduced by this predicate. Deep smoke completed with Arona
  Day/Sunset/Night, Elaina Morning/Day/Dusk/Night/Gradient in `REVIEW`, and
  Shiroko video in `PASS`; artifacts are under
  `/tmp/yakkai-smoke/20260601T041115565678Z`.
- Tests: native policy tests assert the `regular-blur-only-effect` allow path
  and existing reject tests keep utility, fullscreen, composelayer,
  protected-puppet, and mixed blur classes out of the predicate.
- Visual review: `3476236738` still needs human review from validator/smoke
  captures before broadening beyond this exact predicate.
- Decision: keep allowed narrowly; do not widen into any carrier, protected,
  puppet, mixed, fullscreen, or color-grade blur path.

### `regular-lut-only`

- State: `production-allowed` for regular image-layer LUT-only chains; route
  evidence now audited.
- Predicate: preserve a LUT chain only when classification reports
  `candidateEffectClass=regular-lut-only` and `candidateChainShape=lut-only`.
  This excludes protected puppet paths, mixed puppet paths, composelayer
  color-grade paths, fullscreen layers, utility carriers, blur mixes, and
  unknown mixed chains.
- Reject conditions: protected puppet LUT, mixed puppet LUT, composelayer
  color-grade, utility/fullscreen carriers, blur/color-grade mixes, and missing
  route/material diagnostics remain separate classes and are not covered by
  this predicate.
- Evidence: fresh inventory reports seven allowed `regular-lut-only` records
  in Arona. The generic inventory route audit classifies all seven as
  `route-complete`: each uses `effect-layer-node-final-publish`, publishes final
  output through the original parent, resets the effect input for layer-local
  rendering, uses card/card effect meshes, has empty route risk, has one final
  published LUT material, and exposes both screen-sized
  `material-output-*` and layer-local `material-output-local-*` capture stages.
- Tests: `tools/test_effect_candidate_inventory.py` covers route-complete and
  missing-local-material-output-capture classifications for regular LUT
  records. Native policy tests already cover the strict `lut-only-effect`
  allow path and reject protected/mixed/carrier alternatives through separate
  classes.
- Visual review: Arona comparator/deep smoke remain required for visual parity;
  route-complete only proves the preserved regular LUT layers have enough
  generic route and capture evidence for shader/color investigations.
- Decision: keep the existing narrow allow path. Do not widen into protected
  puppet LUT, mixed puppet LUT, or the separate composelayer color-grade path.

### `composelayer-color-grade`

- State: `production-allowed` for the narrow composelayer blur/color-grade
  predicate, after human visual approval.
- Predicate: preserve a composelayer blur/color-grade chain only when
  classification reports `candidateEffectClass=composelayer-color-grade` and
  `candidateChainShape=blur-color-grade-composelayer`. This covers the
  structurally validated composelayer final-publish route and excludes regular
  fullscreen blur, utility blur, protected puppet paths, generic puppet paths,
  audio carriers, and unknown mixed chains.
- Reject conditions: missing classification, non-composelayer color-grade,
  protected puppet color-grade, mixed puppet color-grade, fullscreen blur not
  classified through this class, audio/unknown utility carriers, non-generic
  routing, scene-specific ids/names, crashes, shader/material failures, or
  human visual rejection keep the class stripped or probe-only.
- Evidence: baseline inventory originally classified the current fixture as
  `needs-high-risk-probe-route`. The high-risk probe then classified the same
  class as `human-visual-review-required`: it had composelayer checks,
  color-grade plus blur materials, `effect-layer-composite-final-publish`,
  `publishFinalOutput=true`, `effectFinalMeshKind=fullscreen-card`, and a
  forced high-risk probe route. `tools/validate-scene.sh 3476236738 10000
  --probe-high-risk-layers 365` passed structurally and reported
  `baseline-vs-probe RMSE=0.0840355` and background sentinel mean RGB region
  deltas of `10.533` to `17.405`; those numbers proved broad frame mutation,
  not correctness. After the production predicate, fresh validation reports
  layer `365` allowed with reason `composelayer-color-grade-effect`; the
  manifest contains five composelayer-color-grade captures through
  `effect-layer-composite-final-publish`.
- Tests: native policy tests assert the `composelayer-color-grade-effect` allow
  path for a composelayer blur/color-grade carrier.
  `tools/test_effect_candidate_inventory.py` covers baseline
  `needs-high-risk-probe-route`, probe `human-visual-review-required`, normal
  allowed composelayer color-grade records classifying as `production-allowed`,
  and probe-capture deduplication over duplicate stripped metadata.
- Visual review: approved by human review on 2026-06-01 using
  `/tmp/yakkai-visual-gate/3476236738-composelayer-color-grade-review.png`
  (`Normal | Probe_layer_365 | Diff`).
- Decision: keep allowed narrowly. Do not widen into protected puppet
  color-grade, mixed puppet color-grade, generic fullscreen blur,
  audio/unknown utility carriers, or unrelated composelayer families. Metrics
  remain review prompts here, not automatic blockers.

### `utility-blur`

- State: `production-allowed` for the narrow fullscreen utility blur predicate,
  after human visual approval.
- Predicate: preserve a blur chain only when classification reports
  `candidateEffectClass=utility-blur` and `candidateChainShape=blur-utility`.
  This covers fullscreen utility blur carriers with blur-only materials and
  excludes the separate composelayer color-grade predicate, protected puppet
  paths, generic puppet paths, LUT/color-grade mixes, audio carriers, and
  unknown mixed chains.
- Reject conditions: composelayer blur/color-grade outside the separate
  `composelayer-color-grade` predicate, fullscreen blur not classified as
  `utility-blur`, protected puppet blur, generic puppet blur, mixed
  blur/LUT/color-grade chains, audio/unknown utility carriers, missing
  classification, crashes, shader/material failures, or human visual rejection
  remain stripped or probe-only.
- Evidence: baseline inventory originally classified the two current utility
  blur records as `needs-high-risk-probe-route`. The high-risk probe then
  classified the probe records as `human-visual-review-required`: they had
  fullscreen utility-carrier checks, blur materials,
  `effect-layer-fullscreen-final-publish`, `publishFinalOutput=true`,
  `effectFinalMeshKind=fullscreen-card`, `effectInputMeshKind=card`, and
  `effectInputNodeReset=true`. `tools/validate-scene.sh 3228578419 8000
  --probe-high-risk-layers 53,155` passed structurally and reported
  `baseline-vs-probe RMSE=0.0372153`; that number showed visible change, not
  correctness. After the production predicate, fresh validation reports two
  allowed `utility-blur` records with reason `utility-blur-effect`; the
  manifest contains 10 utility-blur captures with that reason.
- Tests: native policy tests assert the `utility-blur-effect` allow path for a
  fullscreen utility blur carrier. `tools/test_effect_candidate_inventory.py`
  covers utility blur probe records classifying as
  `human-visual-review-required` and normal allowed utility blur records
  classifying as `production-allowed`.
- Visual review: approved by human review on 2026-06-01 using
  `/tmp/yakkai-visual-gate/3228578419-utility-blur-review.png`
  (`Normal | Probe_layers_53_155 | Diff`).
- Decision: keep allowed narrowly. Do not widen into unrelated composelayer
  families, protected puppet blur, generic puppet blur, mixed
  blur/LUT/color-grade, audio utility, or unknown utility paths. RMSE remains a
  review prompt, not an automatic accept/reject gate.

### `puppet-mixed`

- State: `deferred-hard-blocked` for production re-enable. Keep stripped under
  `puppet-alpha-strip`.
- Predicate: none. Current evidence does not support a safe generic production
  allow predicate for mixed puppet-layer chains.
- Reject conditions: any puppet layer whose forced probe requires authored
  effect routing through the current generic layer path remains stripped,
  especially chains containing `waterwaves` mixed with opacity, shine, iris,
  LUT, blur, or color-grade families. Scene-specific ids, layer names,
  animation ids, or manual wallpaper exceptions are not acceptable.
- Evidence: normal `tools/validate-scene.sh 3476236738 10000` passes while
  layers `168` and `22` remain stripped. Forced prefix probes show the failure
  starts immediately at the first visible effect, not at later mixed-chain
  effects: `--probe-layers 168,22 --probe-max-effects 0` is close to baseline
  (`RMSE 0.0140`), but `--probe-layers 168 --probe-max-effects 1` enables only
  `effects/waterwaves` and shifts the frame (`RMSE 0.0506`),
  `--probe-layers 22 --probe-max-effects 1` enables only `effects/waterwaves`
  and shifts more strongly (`RMSE 0.0936`), and the combined `N=1` probe reaches
  `RMSE 0.1079`.
- Visual review: the diagnostic review sheet is
  `/tmp/yakkai-visual-gate/3476236738-puppet-mixed-waterwaves-regression.png`.
- Decision: do not re-enable remaining puppet-mixed layers in production.
  Future work must first fix puppet-layer effect source/final-publish routing
  generically, then rerun prefix probes and human visual review before any
  policy predicate changes.
