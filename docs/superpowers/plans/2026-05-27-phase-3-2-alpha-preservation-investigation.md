# Phase 3.2 Alpha Preservation Investigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Use effect-capture diagnostics to identify the first alpha/composition loss point in effect-chain rendering, then land one narrow verified fix without relaxing the puppet effect strip policy.

**Architecture:** The slice starts with artifact generation through the existing `--debug-effect-captures` harness path, then classifies the failure stage using manifest and image comparisons. The code change is selected from a small decision tree after evidence identifies whether the bug is input capture, effect output, final publish, render pass state, copy routing, or blend/alpha handling. Validation stays asset-backed through `tools/validate-scene.sh`, smoke tests, and manual visual review.

**Tech Stack:** C++20, Qt 6 harness/QML, vendored Vulkan renderer, nlohmann/json manifests, existing `yakkai_scene_harness`, existing `yakkai_scene_policy_tests`, existing smoke and validator scripts.

---

## Scope

This plan implements Slice 3.2 from `docs/superpowers/specs/2026-05-27-phase-3-2-alpha-preservation-investigation-design.md`.

Do not re-enable stripped effect families in this plan. Do not remove the puppet-scene strip policy. Do not commit generated captures. Do not change shader preprocessing unless the captures prove shader output alpha is the first failing boundary.

## File Structure

- Create: `docs/superpowers/specs/2026-05-27-phase-3-2-alpha-preservation-investigation-design.md`
  Durable approved design for this slice.
- Create: `docs/superpowers/plans/2026-05-27-phase-3-2-alpha-preservation-investigation.md`
  This implementation plan.
- Create: `tools/effect-capture-summary.py`
  Reads a debug capture manifest and prints stable per-layer summaries for preserved/stripped decisions, capture stage counts, pass state alpha/blend/load data, and failed dumps.
- Modify: `README.md`
  Documents the manifest summary helper in the effect-capture workflow and tools layout.
- Modify when evidence requires it: `native/scene_backend/tests/test_scene_policies.cpp`
  Adds a regression test for the selected narrow fix when the fault can be represented at policy/render-graph metadata level.
- Modify when evidence requires it: `native/scene_backend/vendor/upstream_debug/src/VulkanRender/CustomShaderPass.cpp`
  Candidate target for render pass load/clear/blend/color-mask/alpha preservation fixes.
- Modify when evidence requires it: `native/scene_backend/vendor/upstream_debug/src/VulkanRender/VulkanRender.cpp`
  Candidate target for debug dump, final publish copy, and offscreen output routing fixes.
- Modify when evidence requires it: `native/scene_backend/vendor/upstream_debug/src/VulkanRender/SceneToRenderGraph.cpp`
  Candidate target for render graph copy-command ordering or final-output source selection fixes.
- Modify when evidence requires it: `native/scene_backend/vendor/upstream_debug/src/Scene/SceneImageEffectLayer.cpp`
  Candidate target for effect-chain final output resolution.
- Modify when behavior, commands, or limitations change: `README.md`
  Required by repo instructions.
- Modify when validation workflow changes: `SCENE_DEV_PROCESS.md`
  Required by repo instructions if tooling or validator workflow changes.

## Task 1: Establish Baseline and Assets

**Files:**
- Read: `ARONA_SITREP.md`
- Read: `SCENE_DEV_PROCESS.md`
- Read: `docs/superpowers/specs/2026-05-27-phase-3-2-alpha-preservation-investigation-design.md`
- Read: `README.md`

- [ ] **Step 1: Confirm branch and status**

Run:

```bash
git status --short --branch
```

Expected: branch is `phase/3-2-alpha-preservation`. Only the new spec/plan files should be untracked at the start of this slice.

- [ ] **Step 2: Confirm candidate scene packages exist**

Run:

```bash
test -f "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg"
test -f "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3476236738/scene.pkg"
```

Expected: both commands exit `0`. If `3476236738` is missing, stop and ask whether to switch to another local effect-heavy candidate.

- [ ] **Step 3: Build the harness and policy tests**

Run:

```bash
cmake --build build --target yakkai_scene_harness yakkai_scene_policy_tests -j2
```

Expected: build exits `0`. Existing vendor warnings are acceptable.

- [ ] **Step 4: Run policy tests before render work**

Run:

```bash
build/native/scene_backend/yakkai_scene_policy_tests
```

Expected: exits `0` and prints `scene policy tests passed`.

## Task 2: Add Manifest Summary Tool

**Files:**
- Create: `tools/effect-capture-summary.py`
- Modify: `README.md`

- [ ] **Step 1: Create the summary tool**

Create `tools/effect-capture-summary.py` with this content:

```python
#!/usr/bin/env python3
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path


def as_list(value):
    return value if isinstance(value, list) else []


def get_stage(record):
    return str(record.get("stage", "unknown"))


def capture_layer(record):
    layer = record.get("layer")
    return layer if isinstance(layer, dict) else {}


def capture_key(record):
    layer = capture_layer(record)
    layer_id = layer.get("layerId")
    layer_name = layer.get("layerName")
    if layer_id is not None or layer_name:
        return f"{layer_id if layer_id is not None else 'unknown'}:{layer_name or 'unnamed'}"

    if record.get("layerKey"):
        return str(record["layerKey"])
    if record.get("key"):
        return str(record["key"])
    if record.get("label"):
        return str(record["label"])

    return "unknown"


def unique_layers(manifest_layers, captures):
    if manifest_layers:
        return manifest_layers

    layers = {}
    for record in captures:
        layer = capture_layer(record)
        if not layer:
            continue
        layers.setdefault(capture_key(record), layer)
    return list(layers.values())


def capture_failures(top_level_failures, captures):
    failures = list(top_level_failures)
    for record in captures:
        if record.get("failed") or record.get("completed") is False:
            failures.append({
                "stage": get_stage(record),
                "path": record.get("path", "unknown"),
                "reason": record.get("failureReason") or record.get("reason", "unknown"),
            })
    return failures


def decision_for_layer(layer):
    if layer.get("effectDecision") or layer.get("decision"):
        decision = str(layer.get("effectDecision", layer.get("decision", "unknown")))
        reason = str(layer.get("effectDecisionReason", layer.get("reason", "unknown")))
        return decision, reason

    policy = layer.get("policy")
    if not isinstance(policy, dict):
        return "unknown", "unknown"

    if policy.get("keepLayer") is False:
        decision = "skip-layer"
    elif policy.get("strippedEffects"):
        decision = "strip-effects"
    elif policy.get("keepEffects") is True:
        decision = "keep-effects"
    elif policy.get("keepEffects") is False:
        decision = "drop-effects"
    else:
        decision = "unknown"

    return decision, str(policy.get("reason", "unknown"))


def main():
    if len(sys.argv) != 2:
        print("usage: tools/effect-capture-summary.py /path/to/manifest.json", file=sys.stderr)
        return 2

    manifest_path = Path(sys.argv[1])
    with manifest_path.open("r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    captures = as_list(manifest.get("captures"))
    pass_states = as_list(manifest.get("passStates"))
    layers = unique_layers(as_list(manifest.get("layers")), captures)
    failures = capture_failures(as_list(manifest.get("failures")), captures)

    stage_counts = Counter(get_stage(record) for record in captures)
    layer_stage_counts = defaultdict(Counter)
    for record in captures:
        layer_stage_counts[capture_key(record)][get_stage(record)] += 1

    print(f"manifest={manifest_path}")
    print(f"scene={manifest.get('sceneId', 'unknown')}")
    print(f"wallpaper={manifest.get('wallpaperPath', 'unknown')}")
    print(f"captures={len(captures)} stages={dict(sorted(stage_counts.items()))}")
    print(f"layers={len(layers)} passStates={len(pass_states)} failures={len(failures)}")

    if failures:
        print("failures:")
        for failure in failures:
            print(f"  - stage={failure.get('stage', 'unknown')} path={failure.get('path', 'unknown')} reason={failure.get('reason', 'unknown')}")

    decisions = Counter()
    for layer in layers:
        decisions[decision_for_layer(layer)] += 1

    if decisions:
        print("decisions:")
        for (decision, reason), count in sorted(decisions.items()):
            print(f"  - {decision} reason={reason} count={count}")

    if layer_stage_counts:
        print("layer-stage-counts:")
        for key, counts in sorted(layer_stage_counts.items()):
            print(f"  - {key}: {dict(sorted(counts.items()))}")

    if pass_states:
        print("pass-states:")
        for state in pass_states:
            name = state.get("passName", state.get("name", state.get("output", "unknown")))
            src = state.get("sourceTexture", state.get("src", "unknown"))
            dst = state.get("targetTexture", state.get("dst", state.get("output", "unknown")))
            load = state.get("colorLoadOp", state.get("loadOp", "unknown"))
            blend = state.get("blendMode", "unknown")
            preserve = state.get("preserveOutput", state.get("preserve", "unknown"))
            color_mask = state.get("colorMask", "unknown")
            depth_load = state.get("depthLoadOp", "unknown")
            blend_enabled = state.get("blendEnabled", "unknown")
            uses_depth = state.get("usesDepth", "unknown")
            print(f"  - name={name} src={src} dst={dst} load={load} blend={blend} preserve={preserve} colorMask={color_mask} depthLoad={depth_load} blendEnabled={blend_enabled} usesDepth={uses_depth}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Make the tool executable**

Run:

```bash
chmod +x tools/effect-capture-summary.py
```

Expected: command exits `0`.

- [ ] **Step 3: Run syntax validation**

Run:

```bash
python3 -m py_compile tools/effect-capture-summary.py
```

Expected: command exits `0`.

- [ ] **Step 4: Document the helper**

In `README.md`, add this command under the effect-chain debugging example:

```bash
tools/effect-capture-summary.py /tmp/yakkai-effect-debug/manifest.json
```

Also add `tools/effect-capture-summary.py` to the repository layout under `tools/`.

Expected: the new command and file layout are documented with the workflow change.

## Task 3: Capture Baseline Diagnostics

**Files:**
- Generated, do not commit: `/tmp/yakkai-phase3-2-baseline-*`

- [ ] **Step 1: Clear shader cache**

Run:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

Expected: command exits `0`.

- [ ] **Step 2: Capture Sleeping Arona diagnostics**

Run:

```bash
OUT="/tmp/yakkai-phase3-2-baseline-3228578419"
rm -rf "$OUT"
mkdir -p "$OUT"
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --hide-info-overlay \
  --fill crop \
  --capture "$OUT/final.png" \
  --capture-delay-ms 8000 \
  --debug-effect-captures "$OUT"
```

Expected: command exits `0`; `$OUT/manifest.json` and `$OUT/final.png` exist.

- [ ] **Step 3: Capture scene `3476236738` diagnostics**

Run:

```bash
OUT="/tmp/yakkai-phase3-2-baseline-3476236738"
rm -rf "$OUT"
mkdir -p "$OUT"
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3476236738/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --hide-info-overlay \
  --fill crop \
  --capture "$OUT/final.png" \
  --capture-delay-ms 10000 \
  --debug-effect-captures "$OUT"
```

Expected: command exits `0`; `$OUT/manifest.json` and `$OUT/final.png` exist.

- [ ] **Step 4: Summarize both manifests**

Run:

```bash
tools/effect-capture-summary.py /tmp/yakkai-phase3-2-baseline-3228578419/manifest.json
tools/effect-capture-summary.py /tmp/yakkai-phase3-2-baseline-3476236738/manifest.json
```

Expected: both commands exit `0` and print capture counts, layer-stage counts, and pass-state summaries.

## Task 4: Classify the First Failure Boundary

**Files:**
- Read: `/tmp/yakkai-phase3-2-baseline-3228578419/manifest.json`
- Read: `/tmp/yakkai-phase3-2-baseline-3476236738/manifest.json`
- Read generated TGA captures in those directories.

- [ ] **Step 1: Inspect manifest structure**

Run:

```bash
jq '.captures | length' /tmp/yakkai-phase3-2-baseline-3228578419/manifest.json
jq '.captures | length' /tmp/yakkai-phase3-2-baseline-3476236738/manifest.json
jq '.failures // []' /tmp/yakkai-phase3-2-baseline-3228578419/manifest.json
jq '.failures // []' /tmp/yakkai-phase3-2-baseline-3476236738/manifest.json
```

Expected: capture counts are greater than zero. Failures are either empty or point to a specific dump path that becomes the first bug to fix.

- [ ] **Step 2: Compare stage images visually**

Open representative `effect-input`, `effect-output`, `final-publish`, and `final.png` artifacts for both scenes using the visual companion or local image viewer.

Expected classification:

```text
input-bad: effect-input is already missing expected texture/color/alpha.
effect-output-bad: effect-input is sane, but effect-output loses alpha/color/composition.
publish-bad: effect-output is sane, but final-publish or final.png loses alpha/color/composition.
manifest-only-bad: images are sane, but manifest shows stale copy source, failed dumps, or impossible pass state.
no-bug-visible: captures and final output match expected visuals; document and stop before making renderer changes.
```

- [ ] **Step 3: Record classification in the working notes**

Append the classification and artifact paths to a scratch file outside the commit set. Use exactly one of these commands.

For input faults:

```bash
printf '%s\n' \
  'Phase 3.2 classification: input-bad' \
  'scene 3228578419 artifacts: /tmp/yakkai-phase3-2-baseline-3228578419' \
  'scene 3476236738 artifacts: /tmp/yakkai-phase3-2-baseline-3476236738' \
  'selected task: Task 5A' \
  > /tmp/yakkai-phase3-2-classification.txt
```

For effect output faults:

```bash
printf '%s\n' \
  'Phase 3.2 classification: effect-output-bad' \
  'scene 3228578419 artifacts: /tmp/yakkai-phase3-2-baseline-3228578419' \
  'scene 3476236738 artifacts: /tmp/yakkai-phase3-2-baseline-3476236738' \
  'selected task: Task 5B' \
  > /tmp/yakkai-phase3-2-classification.txt
```

For final publish faults:

```bash
printf '%s\n' \
  'Phase 3.2 classification: publish-bad' \
  'scene 3228578419 artifacts: /tmp/yakkai-phase3-2-baseline-3228578419' \
  'scene 3476236738 artifacts: /tmp/yakkai-phase3-2-baseline-3476236738' \
  'selected task: Task 5C' \
  > /tmp/yakkai-phase3-2-classification.txt
```

For manifest-only faults:

```bash
printf '%s\n' \
  'Phase 3.2 classification: manifest-only-bad' \
  'scene 3228578419 artifacts: /tmp/yakkai-phase3-2-baseline-3228578419' \
  'scene 3476236738 artifacts: /tmp/yakkai-phase3-2-baseline-3476236738' \
  'selected task: Task 5C' \
  > /tmp/yakkai-phase3-2-classification.txt
```

When no visible bug is found:

```bash
printf '%s\n' \
  'Phase 3.2 classification: no-bug-visible' \
  'scene 3228578419 artifacts: /tmp/yakkai-phase3-2-baseline-3228578419' \
  'scene 3476236738 artifacts: /tmp/yakkai-phase3-2-baseline-3476236738' \
  'selected task: Task 5D' \
  > /tmp/yakkai-phase3-2-classification.txt
```

Expected: `/tmp/yakkai-phase3-2-classification.txt` records the evidence used to choose Task 5A, 5B, 5C, 5D, or no code change.

## Task 5A: Fix Effect Input Fault

Use this task only when Task 4 classifies `input-bad`.

**Files:**
- Modify: `native/scene_backend/vendor/upstream_debug/src/WPSceneParser.cpp`
- Modify when covered by policy-level behavior: `native/scene_backend/tests/test_scene_policies.cpp`
- Modify when workflow docs change: `README.md`

- [ ] **Step 1: Identify the input source**

Run:

```bash
rg -n "RegisterEffectCapture|effect-input|debugEffect|EffectCapture" native/scene_backend/vendor/upstream_debug/src
```

Expected: locate the registration point that maps a layer texture into the `effect-input` capture record.

- [ ] **Step 2: Patch only the source mapping**

Change only the input texture key or layer metadata used to register `effect-input`. Do not change effect policy or blend modes in this task.

Expected: `effect-input` for the failing layer changes from missing/incorrect content to the layer texture that enters the effect chain.

- [ ] **Step 3: Add a policy/helper regression when possible**

If the bug is metadata or key resolution, add a test to `native/scene_backend/tests/test_scene_policies.cpp` that builds the failing key shape and checks the expected capture key. If the bug depends on live scene textures, document that it is covered by the before/after debug captures and validator instead.

Expected: the new test fails before the source-mapping patch and passes after it, or a short note in the final report explains why a native test was not practical.

## Task 5B: Fix Effect Output Alpha or Render-Pass Fault

Use this task only when Task 4 classifies `effect-output-bad`.

**Files:**
- Modify: `native/scene_backend/vendor/upstream_debug/src/VulkanRender/CustomShaderPass.cpp`
- Modify when final output resolution is involved: `native/scene_backend/vendor/upstream_debug/src/Scene/SceneImageEffectLayer.cpp`
- Modify when covered by policy-level behavior: `native/scene_backend/tests/test_scene_policies.cpp`
- Modify when workflow docs change: `README.md`

- [ ] **Step 1: Locate the pass state for the failing layer**

Run:

```bash
jq '.passStates // []' /tmp/yakkai-phase3-2-baseline-3228578419/manifest.json
jq '.passStates // []' /tmp/yakkai-phase3-2-baseline-3476236738/manifest.json
```

Expected: the failing layer has pass-state entries showing source texture, target texture, load operation, blend mode, preserve-output flag, and color/depth state.

- [ ] **Step 2: Patch the proven render-pass boundary**

Patch the smallest proven boundary in `CustomShaderPass.cpp`:

```text
If clear alpha is wrong, change only the clear value for the effect target.
If load op is wrong, change only the load op for the pass class that loses alpha.
If color mask is wrong, change only the mask for alpha-preserving effect output.
If blend factors are wrong, change only the publish/effect blend factors proven by the manifest.
```

Expected: `effect-output` preserves alpha/color for the failing layer after recapture.

- [ ] **Step 3: Add regression coverage**

Add a native regression in `native/scene_backend/tests/test_scene_policies.cpp` if the fixed decision is represented by a helper, enum mapping, or command property. If the fix is Vulkan render-pass state that is only observable in an asset-backed render, use Task 6 recapture plus Task 7 validator/smoke gates as the regression evidence.

Expected: either a native test fails before and passes after the patch, or asset-backed captures prove the fix at the failing boundary.

## Task 5C: Fix Final Publish or Copy Routing Fault

Use this task only when Task 4 classifies `publish-bad` or `manifest-only-bad`.

**Files:**
- Modify: `native/scene_backend/vendor/upstream_debug/src/VulkanRender/VulkanRender.cpp`
- Modify when graph command ordering is involved: `native/scene_backend/vendor/upstream_debug/src/VulkanRender/SceneToRenderGraph.cpp`
- Modify when final output selection is involved: `native/scene_backend/vendor/upstream_debug/src/Scene/SceneImageEffectLayer.cpp`
- Modify: `native/scene_backend/tests/test_scene_policies.cpp`

- [ ] **Step 1: Locate final publish records**

Run:

```bash
jq '.captures[] | select(.stage == "final-publish")' /tmp/yakkai-phase3-2-baseline-3228578419/manifest.json
jq '.captures[] | select(.stage == "final-publish")' /tmp/yakkai-phase3-2-baseline-3476236738/manifest.json
```

Expected: final-publish records show the texture key copied or dumped for the failing layer.

- [ ] **Step 2: Patch the proven copy source**

Patch only the path that chooses or drains the final output copy source. Keep the existing behavior for non-effect layers and already-preserved flare/lens/hash effect chains.

Expected: `final-publish` and `final.png` use the same corrected final effect output that was visible in `effect-output`.

- [ ] **Step 3: Add regression coverage**

Add or extend a policy test in `native/scene_backend/tests/test_scene_policies.cpp` for the final output selector or copy-command ordering. Use fixture data shaped like the failing manifest entry.

Expected: test fails before the routing patch and passes after it.

## Task 5D: Document No Code Change

Use this task only when Task 4 classifies `no-bug-visible`.

**Files:**
- Modify: `docs/superpowers/specs/2026-05-27-phase-3-2-alpha-preservation-investigation-design.md`
- Modify when limitation text changes: `README.md`

- [ ] **Step 1: Record why no renderer change is justified**

Add a short "Investigation Result" section to the Slice 3.2 spec with:

```markdown
## Investigation Result

The captured `effect-input`, `effect-output`, and `final-publish` artifacts for `3228578419` and `3476236738` did not show an alpha/composition loss that justifies a renderer change in Slice 3.2. Generated artifacts stayed in `/tmp` and were not promoted to baselines.
```

Expected: no code files are modified in this branch.

## Task 6: Re-Capture After the Selected Fix

**Files:**
- Generated, do not commit: `/tmp/yakkai-phase3-2-after-*`

- [ ] **Step 1: Rebuild after code changes**

Run:

```bash
cmake --build build --target yakkai_scene_harness yakkai_scene_policy_tests -j2
```

Expected: build exits `0`.

- [ ] **Step 2: Re-run policy tests**

Run:

```bash
build/native/scene_backend/yakkai_scene_policy_tests
```

Expected: tests exit `0`.

- [ ] **Step 3: Clear shader cache**

Run:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

Expected: command exits `0`.

- [ ] **Step 4: Capture after-fix diagnostics for both scenes**

Run:

```bash
OUT="/tmp/yakkai-phase3-2-after-3228578419"
rm -rf "$OUT"
mkdir -p "$OUT"
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --hide-info-overlay \
  --fill crop \
  --capture "$OUT/final.png" \
  --capture-delay-ms 8000 \
  --debug-effect-captures "$OUT"

OUT="/tmp/yakkai-phase3-2-after-3476236738"
rm -rf "$OUT"
mkdir -p "$OUT"
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3476236738/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --hide-info-overlay \
  --fill crop \
  --capture "$OUT/final.png" \
  --capture-delay-ms 10000 \
  --debug-effect-captures "$OUT"
```

Expected: both commands exit `0` and both manifests exist.

- [ ] **Step 5: Summarize after-fix manifests**

Run:

```bash
tools/effect-capture-summary.py /tmp/yakkai-phase3-2-after-3228578419/manifest.json
tools/effect-capture-summary.py /tmp/yakkai-phase3-2-after-3476236738/manifest.json
```

Expected: summaries show no new dump failures and stage counts remain coherent.

## Task 7: Validate Render Behavior

**Files:**
- Generated, do not commit: `/tmp/yakkai-debug`
- Generated, do not commit: `/tmp/yakkai-smoke/*`

- [ ] **Step 1: Run validators**

Run:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
tools/validate-scene.sh 3228578419 8000
tools/validate-scene.sh 3476236738 10000
```

Expected: both validators exit `0`, with no new shader/material/render-graph failures.

- [ ] **Step 2: Run quick strict smoke**

Run:

```bash
./smoke-tests/run.sh --suite quick --strict
```

Expected: quick suite exits `0`.

- [ ] **Step 3: Run release smoke if shared rendering changed**

Run this when Task 5B or 5C changed shared effect-chain, blend, shader preprocessing, or publish-path behavior:

```bash
./smoke-tests/run.sh --suite release --strict --require-assets
```

Expected: release suite exits `0`.

- [ ] **Step 4: Run package and QML checks**

Run:

```bash
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
qmllint -I build/qml native/scene_harness/qml/Main.qml native/scene_harness/qml/YakkaiSceneViewerHarness.qml native/scene_harness/qml/SystemSceneViewerHarness.qml
./scripts/check-package.sh
git diff --check
```

Expected: all commands exit `0`.

## Task 8: Visual Review and Documentation

**Files:**
- Modify when workflow changes: `README.md`
- Modify when tooling changes: `SCENE_DEV_PROCESS.md`
- Read generated review clips and captures in `/tmp/yakkai-smoke/*`

- [ ] **Step 1: Compare Sleeping Arona visually**

Use the newest smoke artifact for `3228578419` and compare against the committed baseline. Check:

```text
camera angle
brown desk visibility
sleeping character position
rainbow flare arc intensity
sparkle particles
absence of black diamonds
absence of white rectangles
absence of washed-out blue/white background
```

Expected: no regression against the accepted baseline.

- [ ] **Step 2: Compare scene `3476236738` visually**

Open the validator capture at `/tmp/yakkai-debug/validate-3476236738.png` and compare against the pre-fix capture or known accepted visual notes from `SCENE_DEV_PROCESS.md`.

Expected: camera angle, major visible elements, color balance, and composition are maintained or improved.

- [ ] **Step 3: Update docs only if behavior or workflow changed**

If a user-facing limitation improves or the validation workflow changes, update `README.md` or `SCENE_DEV_PROCESS.md` in the same change. If only internal renderer behavior changed with no new command or limitation change, do not add doc churn.

Expected: docs reflect any changed commands, workflow, or limitations.

## Task 9: Final Branch Check

**Files:**
- Review: all modified tracked files.

- [ ] **Step 1: Inspect status**

Run:

```bash
git status --short --branch
git diff --stat
```

Expected: tracked changes are limited to this slice. Generated `/tmp` artifacts are absent from git status.

- [ ] **Step 2: Review diff for accidental broad changes**

Run:

```bash
git diff -- README.md SCENE_DEV_PROCESS.md native/scene_backend native/scene_harness tools docs/superpowers
```

Expected: no unrelated refactors, no committed generated captures, and no effect-family re-enable unless the plan was explicitly revised.

- [ ] **Step 3: Stop for review before merge**

Do not merge or push automatically. Report:

```text
classification result
chosen fix task
files changed
validation commands and outcomes
visual artifacts reviewed
remaining risk
```

Expected: the branch is ready for code review or a merge decision.
