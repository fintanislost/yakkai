# Phase 3.10a Visual Sentinel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a hard-fail validator sentinel for the current `3476236738` flat-background regression.

**Architecture:** Keep the validator Bash script as the renderer orchestration entrypoint, but move scene-specific visual math into a focused Python helper. The helper evaluates configured regions against clear-color leakage criteria and returns a concise pass/fail message.

**Tech Stack:** Bash, Python 3 standard library, ImageMagick, existing smoke-test `unittest` suite.

---

### Task 1: Sentinel Helper

**Files:**
- Create: `tools/scene_visual_sentinels.py`
- Create: `smoke-tests/test_scene_visual_sentinels.py`

- [x] Add failing unit tests for clear-color leakage classification.
- [ ] Implement `RegionStats`, `is_clear_color_leak`, and scene result aggregation.
- [ ] Add ImageMagick-backed region stats collection for validator use.

### Task 2: Validator Integration

**Files:**
- Modify: `tools/validate-scene.sh`

- [ ] Call `tools/scene_visual_sentinels.py` for `3476236738`.
- [ ] Report the result through the existing `check` function.
- [ ] Confirm the validator hard-fails today for the known flat background.

### Task 3: Documentation And Verification

**Files:**
- Modify: `README.md`
- Modify: `SCENE_DEV_PROCESS.md`
- Modify: `docs/renderer-effect-candidate-backlog.md`

- [ ] Document the new hard-fail visual sentinel and the current `3476236738` failure.
- [ ] Run unit tests, `tools/validate-scene.sh 3228578419 8000`, and `tools/validate-scene.sh 3476236738 10000`.
