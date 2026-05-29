# Phase 3.10: High-Risk Effect Probe Path

## Goal

Add a scene-harness-only diagnostic path that can explicitly re-enable selected stripped high-risk blur, LUT, or color-grading effect layers for capture comparison.

Normal Plasma rendering and default harness renders must stay unchanged.

## Constraints

- Keep the existing `--debug-effect-probe-layers` behavior limited to stripped puppet mixed chains.
- Add a separate high-risk probe list so probe manifests identify risky overrides clearly.
- Require `--debug-effect-captures` for any probe list.
- Only probe layers explicitly named by ID.
- Only probe layers stripped by `puppet-alpha-strip`.
- Only high-risk probe layers whose candidate diagnostics include blur, LUT, or color-grading families.
- Document command usage and manifest output.

## Implementation Slices

1. Add failing tests for high-risk probe eligibility and manifest serialization.
2. Extend `EffectCaptureConfig` with `highRiskProbeLayerIds`.
3. Add helper logic that returns `high-risk-layer-id-probe` only for explicitly requested high-risk stripped layers.
4. Carry a new harness option, `--debug-effect-probe-high-risk-layers`, through QML and scene properties.
5. Update README and harness docs.
6. Run focused unit tests, package/QML checks as applicable, and render validations for the Arona/Shiroko scenes.

## Validation Targets

- `cmake --build build --target yakkai_scene_policy_tests yakkai_scene_harness -j2`
- `build/native/scene_backend/yakkai_scene_policy_tests`
- `git diff --check`
- Clear shader cache before scene validation.
- `tools/validate-scene.sh 3228578419 8000`
- `tools/validate-scene.sh 3476236738 10000`
- Probe captures for known high-risk IDs:
  - Arona `3228578419`: `53,155`
  - Shiroko `3476236738`: `137,365`

## Probe Findings

- `3228578419` high-risk probe on `53,155` completes and records both layers
  with `high-risk-layer-id-probe`.
- `3476236738` high-risk probe on `137,365` completes and records both layers
  with `high-risk-layer-id-probe`.
- Visual review of `3476236738` still shows a large flat gray mid/background
  area in the normal and probed captures. That is an existing renderer parity
  gap, not a reason to broaden effect policy in this slice.
