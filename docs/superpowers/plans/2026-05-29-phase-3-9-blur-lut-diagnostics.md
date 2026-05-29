# Phase 3.9 Blur/LUT Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make stripped blur, LUT, and color-grading candidates visible enough in diagnostics to plan renderer parity work without re-enabling any high-risk effect chain.

**Architecture:** Extend `EffectPolicy` classification metadata only. Preserve current render decisions while adding canonical high-risk families, carrier-aware chain shapes, manifest fields, and summary output that calls out blur/LUT/color-grade candidates.

**Tech Stack:** C++ policy tests and debug manifest serialization, Python summary helper tests, QML harness docs, existing scene validator and smoke harness.

---

### Task 1: Classify Blur/LUT/Color-Grade Diagnostics

**Files:**
- Modify: `native/scene_backend/tests/test_scene_policies.cpp`
- Modify: `native/scene_backend/vendor/upstream_debug/src/Policy/EffectPolicy.hpp`
- Modify: `native/scene_backend/vendor/upstream_debug/src/Policy/EffectPolicy.cpp`
- Modify: `native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.cpp`

- [x] **Step 1: Write failing policy tests**

Add cases to `testEffectCandidateClassification()` covering:

```cpp
// Non-water blur keeps render decision blocked but reports blur diagnostics.
// Non-water LUT path reports lut diagnostics.
// Non-water color_grading path reports color-grade diagnostics.
// Fullscreen blur reports a carrier-aware shape.
// Composelayer blur + color_grading reports a carrier-aware shape.
```

Expected fields:

```cpp
candidateRisk == "non-water"
candidateBlockedReason == "no-water-effect-family"
candidateMixFamilies == {"blur"} or {"lut"} or {"color-grade"}
candidateChecks.hasBlurFamily / hasLutFamily / hasColorGradingFamily == true
candidateChainShape == "blur-only" / "lut-only" / "color-grade-only" /
                       "blur-fullscreen" / "blur-color-grade-composelayer"
```

- [x] **Step 2: Run policy tests and verify RED**

Run:

```bash
cmake --build build --target yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
```

Expected: build or test fails because the new fields and chain shapes are not implemented.

- [x] **Step 3: Implement classification metadata**

Add `hasBlurFamily`, `hasLutFamily`, and `hasColorGradingFamily` to `CandidateChecks`.
Collect high-risk families with canonical aliases:

```text
blur: blur
lut: lut, lut_loader
color-grade: color_grading, colorgrading, colorgrade, colorcorrection
```

Keep `decideLayerEffects()` behavior unchanged. Only diagnostic metadata changes.

- [x] **Step 4: Run policy tests and verify GREEN**

Run:

```bash
cmake --build build --target yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
```

Expected: tests pass.

### Task 2: Summarize High-Risk Stripped Candidates

**Files:**
- Modify: `smoke-tests/test_effect_capture_summary.py`
- Modify: `tools/effect-capture-summary.py`

- [x] **Step 1: Write failing summary test**

Add a manifest fixture with stripped candidates for blur, LUT, and color-grade. Assert that summary output includes:

```text
stripped-high-risk-candidates=3
stripped-high-risk-families:
  - blur: 1
  - color-grade: 1
  - lut: 1
stripped-high-risk-layers:
```

- [x] **Step 2: Run summary tests and verify RED**

Run:

```bash
python3 -m unittest smoke-tests.test_effect_capture_summary
```

Expected: test fails because the high-risk section is missing.

- [x] **Step 3: Implement summary section**

Read `candidateMixFamilies` first. If missing, fall back to `effectNames` and `materialShaders` text scanning. Print high-risk candidates after the generic stripped candidate buckets.

- [x] **Step 4: Run summary tests and verify GREEN**

Run:

```bash
python3 -m unittest smoke-tests.test_effect_capture_summary
```

Expected: tests pass.

### Task 3: Docs And Validation

**Files:**
- Modify: `README.md`
- Modify: `native/scene_harness/README.md`
- Modify: `docs/renderer-effect-candidate-backlog.md`

- [x] **Step 1: Update docs**

Document the new blur/LUT/color-grade diagnostic metadata and summary section. State that this is diagnostic-only and does not re-enable those effects.

- [x] **Step 2: Build and test**

Run:

```bash
cmake --build build --target yakkai_scene_policy_tests yakkai_scene_harness -j2
build/native/scene_backend/yakkai_scene_policy_tests
python3 -m unittest smoke-tests.test_effect_capture_summary
```

Expected: all pass.

- [x] **Step 3: Render validations**

Clear shader cache before validation:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
tools/validate-scene.sh 3228578419 8000
tools/validate-scene.sh 3476236738 10000
smoke-tests/run.sh
```

Expected: existing visual guardrails remain green. `3228578419` must not gain allowed simple-water candidates. `3476236738` must retain allowed simple-water candidates.

- [x] **Step 4: Review changed files**

Run:

```bash
git diff --check
git status --short --branch
```

Expected: no whitespace errors, only intended files changed.
