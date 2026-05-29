# Phase 3.10a Visual Sentinel Design

## Goal

Insert a hard-fail visual gate before further renderer work so `3476236738`
and future effect-heavy scenes cannot pass validation while large background
regions are clear-color leakage.

## Evidence

The `3476236738` validator log and manifest already identify high-risk
background candidates:

- layer `137` (`桌左`): `blur-only`, `hasBlurFamily`
- layer `365` (`可调整组合层`): `blur-color-grade-composelayer`,
  `hasBlurFamily`, `hasColorGradingFamily`

The final capture shows broad flat regions close to the logged
`tint-adjusted clear color`. Broad pixel-health checks still pass because the
characters, desks, and window preserve enough detail.

## Approach

Add a scene-specific visual sentinel helper used by `tools/validate-scene.sh`.
For `3476236738`, the helper checks multiple background regions away from the
local harness overlay. A region is considered leaked when it is both close to
the scene clear color and low-detail. The scene hard-fails when at least two
configured regions match that leak pattern.

This slice does not fix rendering. It makes the current background issue fail
explicitly so later renderer slices can turn the sentinel green.

## Components

- `tools/scene_visual_sentinels.py`: computes region statistics with
  ImageMagick and evaluates scene-specific sentinels.
- `tools/validate-scene.sh`: calls the helper for known scene sentinels and
  reports the result as a normal validator check.
- `smoke-tests/test_scene_visual_sentinels.py`: unit tests for clear-color
  leakage classification and scene failure/pass thresholds.
- `README.md`, `SCENE_DEV_PROCESS.md`, and
  `docs/renderer-effect-candidate-backlog.md`: document the hard-fail gate and
  current `3476236738` failure.

## Acceptance

- `tools/validate-scene.sh 3476236738 10000` fails today with a clear
  background visual sentinel message.
- `tools/validate-scene.sh 3228578419 8000` still passes.
- Unit tests cover the sentinel rule without rendering.
- The normal smoke suite remains unchanged.
