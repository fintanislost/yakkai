# Phase 3.3 Stripped Effect Diagnostics Design

## Context

Phase 3.2 found no visible alpha/composition loss in preserved effect chains, but it also exposed a diagnostic blind spot: `3476236738` produced a final image and pass-state records, yet zero `effect-input`, `effect-output`, or `final-publish` captures because the current puppet strip policy clears stripped effect chains before debug capture registration.

Slice 3.3 should make stripped effect candidates visible in the debug manifest before attempting any behavior change. The slice may re-enable one effect family only when the new diagnostics show an obviously low-risk candidate. The default successful outcome remains diagnostics-only.

## Goals

- Record stripped effect candidates in `--debug-effect-captures` manifests even when the renderer does not build an effect chain for them.
- Preserve normal rendering behavior by default.
- Explain each stripped candidate with stable layer metadata, effect names, material shader names, policy decision fields, and strip reason.
- Update the manifest summary helper so stripped candidates are visible without manual JSON inspection.
- Permit one narrow policy-level re-enable only if diagnostics show a very low-risk family.
- Preserve existing protected visuals, especially Sleeping Arona (`3228578419`).

## Non-Goals

- Do not remove the puppet-scene strip policy wholesale.
- Do not re-enable puppet layer effects.
- Do not re-enable fullscreen, composelayer utility, solidlayer, projectlayer, or fullscreenlayer carrier effects in this slice.
- Do not re-enable known heavy effects such as audio or lightshaft.
- Do not re-enable known Arona background LUT/blur paths.
- Do not make generated diagnostic captures committed baselines.
- Do not require Plasma-live capture as a gate.

## Stress-Test Decisions

- Start with manifest-only stripped candidate diagnostics. Do not add debug-only shadow rendering in this slice.
- If a candidate is safe enough to re-enable, make it a normal narrow policy change rather than a runtime/debug flag.
- "One re-enable" means one narrow policy reason or effect family, not one individual layer and not multiple unrelated effect families.
- `3476236738` may identify a candidate, but `3228578419` remains the regression guardrail. If the family appears in Arona's dangerous background paths or changes Arona's output, do not re-enable it.
- "Safe enough" requires strict manifest evidence: non-puppet, non-fullscreen, non-utility, non-heavy, no LUT/blur/color-grade style shader names, and no match with known dangerous Arona background layers.
- Use a separate top-level `strippedCandidates` manifest array. Do not put manifest-only candidates in `captures`, because they are not failed dumps.
- Empty `strippedCandidates` should not make the harness fail globally. Scene-specific checks can require candidates for `3476236738`.
- Keep the summary helper compact by default: candidate counts, policy reasons, and top layer ids/names. Defer full effect/shader dumps to a later verbose mode if needed.
- Do not promote the older untracked high-level Phase 3 effect-alpha doc in this slice; it needs reconciliation with the Phase 3.2 no-code result first.
- Automated checks should assert presence and structure for `3476236738`, not exact candidate counts.
- If re-enable visuals are ambiguous, defer the re-enable by default and land diagnostics only.

## Diagnostic Model

The existing capture manifest records live render-target dumps for preserved effect chains. Slice 3.3 adds a second class of manifest entry: stripped candidates. These entries describe the layer and the policy decision, but they do not point to dumped render targets because no effect render graph exists for those layers.

The manifest should make this distinction explicit. A stripped candidate should include:

- scene id and scene type
- layer id, name, image path, alpha, and visible effect count
- policy fields: `keepLayer`, `keepEffects`, `strippedEffects`, `forceAlphaOne`, and `reason`
- effect names and material shader names
- no render-target dump requirement

The manifest should use a separate top-level `strippedCandidates` array. Stripped candidates must not be added to `captures`, because they are not dump requests and must not influence capture failure status.

`tools/effect-capture-summary.py` should report stripped candidates separately from successful capture stages and failed dumps. Default output should stay compact: total candidate count, policy reason counts, and representative layer ids/names.

## Re-Enable Gate

One re-enable is allowed only after the diagnostics identify a boring, low-risk candidate. The allowlist must be policy-level and narrow. It must not change behavior for broad effect categories.

Candidate requirements:

- not a puppet mesh layer
- not fullscreen
- not a utility carrier layer
- not a composelayer utility
- no audio, lightshaft, or other known heavy effect names or first material shaders
- not a known Arona background LUT/blur path
- stable under `3228578419`, `3476236738`, and quick smoke validation

If no candidate satisfies these requirements, or if the visual result is ambiguous, Slice 3.3 should land diagnostics only and document the blocked families for a later slice.

## Candidate Scenes

### `3476236738`

Primary target for this slice. Phase 3.2 showed it currently has zero effect captures under `--debug-effect-captures` because its candidate effect chains are stripped before capture registration.

Expected Slice 3.3 outcome:

- manifest contains stripped candidate entries
- summary helper lists stripped candidates and reasons
- automated checks require at least one well-structured stripped candidate, but do not lock exact candidate counts
- validator still exits `0`
- no new shader/material failures
- color diversity and composition remain stable

### `3228578419` Sleeping Arona

Protected regression scene. It must keep its existing preserved flare/lens/hash behavior while exposing any stripped background candidates as manifest-only diagnostic records.

Expected visual invariants:

- brown desk remains visible
- sleeping character remains correctly placed on the desk
- rainbow flare arc remains present without white rectangles or black overlays
- sparkle particles remain visible
- background does not wash out to a blue-white tint

## Implementation Boundaries

Likely change areas:

- `native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.hpp`
- `native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.cpp`
- `native/scene_backend/vendor/upstream_debug/src/WPSceneParser.cpp`
- `native/scene_backend/vendor/upstream_debug/src/Policy/EffectPolicy.hpp`
- `native/scene_backend/vendor/upstream_debug/src/Policy/EffectPolicy.cpp`
- `native/scene_backend/tests/test_scene_policies.cpp`
- `tools/effect-capture-summary.py`
- `README.md`

The first implementation step should add manifest-only stripped candidate records without changing render behavior. Any allowlist re-enable must be a later step in the same slice and only after diagnostics show a specific low-risk target.

## Validation Gate

Run these before considering the slice mergeable:

```bash
cmake --build build --target yakkai_scene_harness yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
PYTHONPYCACHEPREFIX=/tmp/yakkai-pycache-controller python3 -m py_compile tools/effect-capture-summary.py
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
qmllint -I build/qml native/scene_harness/qml/Main.qml native/scene_harness/qml/YakkaiSceneViewerHarness.qml native/scene_harness/qml/SystemSceneViewerHarness.qml
./scripts/check-package.sh
git diff --check
```

Render validation must clear shader cache first:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
tools/validate-scene.sh 3228578419 8000
tools/validate-scene.sh 3476236738 10000
./smoke-tests/run.sh --suite quick --strict
```

Run release smoke if the slice re-enables any effect family.

Manual visual comparison is required for `3228578419` and any scene affected by an allowlist re-enable. Check camera angle, visible elements, character position, desk/background visibility, color balance, composition, flare intensity, particles, and absence of black/white overlays.

## Success Criteria

- `3476236738` manifests include at least one well-structured stripped candidate record under `--debug-effect-captures`.
- The summary helper clearly reports stripped candidates separately from dumped capture stages.
- Existing preserved effect-chain captures remain coherent for `3228578419`.
- Normal rendering does not regress when no re-enable is selected.
- If one family is re-enabled, it is represented by a narrow policy decision, has focused tests, and passes render validation plus unambiguous manual visual review.
- README documents the updated debug manifest workflow.

## Investigation Result

Slice 3.3 landed manifest-only stripped effect diagnostics. The debug manifest now exposes top-level `strippedCandidates` for effect chains removed by policy before render graph construction. Generated artifacts stayed in `/tmp` and were not promoted to baselines.

No effect family was re-enabled in this slice. The `3476236738` diagnostics exposed multiple stripped families, including waterflow, waterwaves, opacity, shine, iris, blur, audio, and color grading paths. The `3228578419` Arona guardrail also exposed stripped LUT, blur/fullscreen, character, and background paths. That set did not meet the strict low-risk threshold for a single narrow allowlist, so re-enable work remains deferred to a candidate-specific follow-up plan.

Phase 3.4 records those blocked families in `docs/renderer-effect-candidate-backlog.md`.
