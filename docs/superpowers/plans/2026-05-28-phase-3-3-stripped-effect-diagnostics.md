# Phase 3.3 Stripped Effect Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add manifest-only diagnostics for effect chains stripped before render graph construction, then decide whether one extremely low-risk effect family can be re-enabled.

**Architecture:** Extend the existing debug manifest model with a separate top-level `strippedCandidates` array that is independent from render-target dump requests. Record candidates in `WPSceneParser` before `effectObjects.clear()` strips them, update the summary helper and docs, then use asset-backed diagnostics to decide whether a separate candidate-specific re-enable is justified. Normal rendering remains unchanged unless a later decision explicitly selects one safe allowlist.

**Tech Stack:** C++20, Qt 6 harness/QML, vendored Vulkan renderer, nlohmann/json manifests, Python 3 summary helper, existing native policy tests, existing smoke and validator scripts.

---

## Scope

This plan implements `docs/superpowers/specs/2026-05-28-phase-3-3-stripped-effect-diagnostics-design.md`.

The default implementation outcome is diagnostics-only. Do not re-enable any effect family in this plan until the new manifest data has been generated and reviewed. If the diagnostics expose a truly low-risk candidate, pause and write a short candidate-specific plan update before changing `EffectPolicy`.

Do not commit unless the user explicitly asks. The repository instructions override the plan skill's generic frequent-commit guidance.

## File Structure

- Modify: `native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.hpp`
  Adds the public API for manifest-only stripped candidates.
- Modify: `native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.cpp`
  Serializes `strippedCandidates` separately from `captures`.
- Modify: `native/scene_backend/vendor/upstream_debug/src/Scene/include/Scene/Scene.h`
  Stores stripped candidate records on `Scene`.
- Modify: `native/scene_backend/vendor/upstream_debug/src/WPSceneParser.cpp`
  Records `puppet-alpha-strip` candidates before clearing effect objects.
- Modify: `native/scene_backend/tests/test_scene_policies.cpp`
  Adds native regression coverage for stripped candidate recording and manifest serialization.
- Modify: `tools/effect-capture-summary.py`
  Reports `strippedCandidates` compactly.
- Create: `smoke-tests/test_effect_capture_summary.py`
  Adds unit coverage for summary helper output.
- Modify: `README.md`
  Documents that debug manifests include both dumped captures and manifest-only stripped candidates.
- Modify when decision is diagnostics-only: `docs/superpowers/specs/2026-05-28-phase-3-3-stripped-effect-diagnostics-design.md`
  Records the final no-reenable investigation result.
- Modify only after a reviewed candidate-specific plan: `native/scene_backend/vendor/upstream_debug/src/Policy/EffectPolicy.hpp`
- Modify only after a reviewed candidate-specific plan: `native/scene_backend/vendor/upstream_debug/src/Policy/EffectPolicy.cpp`

## Task 1: Add Native Stripped Candidate Manifest Model

**Files:**
- Modify: `native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.hpp`
- Modify: `native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.cpp`
- Modify: `native/scene_backend/vendor/upstream_debug/src/Scene/include/Scene/Scene.h`
- Modify: `native/scene_backend/tests/test_scene_policies.cpp`

- [ ] **Step 1: Add the failing native test**

In `native/scene_backend/tests/test_scene_policies.cpp`, add these includes near the top:

```cpp
#include "Scene/Scene.h"

#include <filesystem>
#include <fstream>
#include <iterator>
```

Inside the anonymous namespace, after `effectNode()`, add:

```cpp
std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}
```

At the end of `testEffectCaptureDebug()`, before the closing brace, add:

```cpp
    {
        const auto outDir =
            std::filesystem::temp_directory_path() / "yakkai-stripped-candidate-policy-test";
        std::filesystem::remove_all(outDir);

        wallpaper::Scene scene;
        scene.scene_id = "unit-scene";
        scene.debugEffectCaptures = {
            .outputDir = outDir.string(),
            .commandLine = "unit --debug-effect-captures " + outDir.string(),
        };

        wallpaper::debug::EffectCaptureLayerInfo layer;
        layer.sceneId = "unit-scene";
        layer.sceneType = "Puppet";
        layer.layerName = "Water background";
        layer.layerImage = "materials/water.png";
        layer.layerId = 42;
        layer.visibleEffectCount = 2;
        layer.alpha = 0.75f;
        layer.keepLayer = true;
        layer.keepEffects = false;
        layer.strippedEffects = true;
        layer.policyReason = "puppet-alpha-strip";
        layer.effectNames = {"waterwaves", "opacity"};
        layer.materialShaders = {"effects/waterwaves", "effects/opacity"};

        wallpaper::debug::recordStrippedEffectCandidate(scene, layer);

        check(scene.debugEffectStrippedCandidates.size() == 1,
              "stripped candidate is stored separately from capture records");
        check(scene.debugEffectCaptureRecords.empty(),
              "stripped candidate does not create dump capture records");
        check(wallpaper::debug::writeEffectCaptureManifest(scene),
              "manifest writes stripped candidates");

        const std::string manifest = readTextFile(scene.debugEffectCaptures.manifestPath());
        check(manifest.find("\"status\": \"ok\"") != std::string::npos,
              "stripped candidates do not fail manifest status");
        check(manifest.find("\"strippedCandidates\"") != std::string::npos,
              "manifest includes strippedCandidates array");
        check(manifest.find("\"layerName\": \"Water background\"") != std::string::npos,
              "manifest includes stripped candidate layer name");
        check(manifest.find("\"reason\": \"puppet-alpha-strip\"") != std::string::npos,
              "manifest includes stripped candidate policy reason");

        std::filesystem::remove_all(outDir);
    }
```

- [ ] **Step 2: Run the failing native test**

Run:

```bash
cmake --build build --target yakkai_scene_policy_tests -j2
```

Expected: build fails because `recordStrippedEffectCandidate` and `debugEffectStrippedCandidates` do not exist yet.

- [ ] **Step 3: Add the public API and storage**

In `native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.hpp`, add this declaration after `recordEffectPassState(...)`:

```cpp
void recordStrippedEffectCandidate(Scene& scene, const EffectCaptureLayerInfo& layer);
```

In `native/scene_backend/vendor/upstream_debug/src/Scene/include/Scene/Scene.h`, add this member after `debugEffectPassStates`:

```cpp
    std::vector<wallpaper::debug::EffectCaptureLayerInfo> debugEffectStrippedCandidates;
```

- [ ] **Step 4: Serialize stripped candidates separately**

In `native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.cpp`, add this function after `recordEffectPassState(...)`:

```cpp
void recordStrippedEffectCandidate(Scene& scene, const EffectCaptureLayerInfo& layer)
{
    if (!scene.debugEffectCaptures.enabled()) {
        return;
    }
    scene.debugEffectStrippedCandidates.push_back(layer);
}
```

In `writeEffectCaptureManifest(...)`, after the `passStates` loop and before `nlohmann::json manifest = { ... }`, add:

```cpp
    nlohmann::json strippedCandidates = nlohmann::json::array();
    for (const auto& candidate : scene.debugEffectStrippedCandidates) {
        strippedCandidates.push_back(layerToJson(candidate));
    }
```

Then add `strippedCandidates` to the manifest object:

```cpp
        {"strippedCandidates", strippedCandidates},
```

The manifest initializer should include `strippedCandidates` alongside `captures` and `passStates`:

```cpp
    nlohmann::json manifest = {
        {"status", failed ? "failed" : "ok"},
        {"sceneId", scene.scene_id},
        {"commandLine", scene.debugEffectCaptures.commandLine},
        {"captureCount", scene.debugEffectCaptureRecords.size()},
        {"captures", captures},
        {"passStates", passStates},
        {"strippedCandidates", strippedCandidates},
    };
```

- [ ] **Step 5: Run the native test**

Run:

```bash
cmake --build build --target yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
```

Expected: build exits `0`; policy tests exit `0` and print `scene policy tests passed`.

## Task 2: Record Stripped Candidates in the Parser

**Files:**
- Modify: `native/scene_backend/vendor/upstream_debug/src/WPSceneParser.cpp`

- [ ] **Step 1: Confirm current asset behavior before parser wiring**

Clear the shader cache:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

Run a debug capture on `3476236738`:

```bash
OUT="/tmp/yakkai-phase3-3-before-3476236738"
rm -rf "$OUT"
mkdir -p "$OUT"
build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3476236738/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --hide-info-overlay \
  --fill crop \
  --capture "$OUT/final.png" \
  --capture-delay-ms 10000 \
  --debug-effect-captures "$OUT"
```

Check the current manifest:

```bash
python3 -c 'import json; data=json.load(open("/tmp/yakkai-phase3-3-before-3476236738/manifest.json", encoding="utf-8")); print(len(data.get("strippedCandidates", [])))'
```

Expected before this task's parser change: prints `0`.

- [ ] **Step 2: Record candidates before clearing effects**

In `native/scene_backend/vendor/upstream_debug/src/WPSceneParser.cpp`, locate:

```cpp
    if (puppetEffectDecision.reason == "puppet-alpha-strip") {
        LOG_INFO("stripping %d effects from layer (alpha fix): name=%s",
                 count_eff, wpimgobj.name.c_str());
        count_eff = 0;
        hasEffect = false;
        effectObjects.clear();
        if (! puppetEffectDecision.keepLayer) return;
    } else if (puppetEffectDecision.reason == "essential-effect" &&
               puppetEffectDecision.forceAlphaOne) {
```

Replace it with:

```cpp
    if (puppetEffectDecision.reason == "puppet-alpha-strip") {
        if (context.scene && context.scene->debugEffectCaptures.enabled()) {
            wallpaper::debug::recordStrippedEffectCandidate(*context.scene, effectCaptureInfo);
        }
        LOG_INFO("stripping %d effects from layer (alpha fix): name=%s",
                 count_eff, wpimgobj.name.c_str());
        count_eff = 0;
        hasEffect = false;
        effectObjects.clear();
        if (! puppetEffectDecision.keepLayer) return;
    } else if (puppetEffectDecision.reason == "essential-effect" &&
               puppetEffectDecision.forceAlphaOne) {
```

This must be before `effectObjects.clear()` so effect names and material shaders remain available in `effectCaptureInfo`.

- [ ] **Step 3: Build and run native tests**

Run:

```bash
cmake --build build --target yakkai_scene_harness yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
```

Expected: both commands exit `0`.

- [ ] **Step 4: Verify `3476236738` now emits stripped candidates**

Clear the shader cache:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

Run:

```bash
OUT="/tmp/yakkai-phase3-3-after-candidates-3476236738"
rm -rf "$OUT"
mkdir -p "$OUT"
build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3476236738/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --hide-info-overlay \
  --fill crop \
  --capture "$OUT/final.png" \
  --capture-delay-ms 10000 \
  --debug-effect-captures "$OUT"
```

Run:

```bash
python3 -c 'import json; data=json.load(open("/tmp/yakkai-phase3-3-after-candidates-3476236738/manifest.json", encoding="utf-8")); candidates=data.get("strippedCandidates", []); assert candidates, "expected stripped candidates"; assert any(c.get("policy", {}).get("reason") == "puppet-alpha-strip" for c in candidates), "expected puppet-alpha-strip candidate"; assert all(c.get("layerId") is not None and c.get("policy", {}).get("strippedEffects") is True and c.get("effectNames") for c in candidates), "candidate structure missing required fields"; print(f"strippedCandidates={len(candidates)}")'
```

Expected: command exits `0` and prints `strippedCandidates=N`, where `N` is greater than `0`.

## Task 3: Update the Manifest Summary Helper

**Files:**
- Modify: `tools/effect-capture-summary.py`
- Create: `smoke-tests/test_effect_capture_summary.py`

- [ ] **Step 1: Add failing Python unit coverage**

Create `smoke-tests/test_effect_capture_summary.py` with:

```python
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SUMMARY = REPO_ROOT / "tools" / "effect-capture-summary.py"


class EffectCaptureSummaryTests(unittest.TestCase):
    def test_reports_stripped_candidates_compactly(self):
        manifest = {
            "sceneId": "3476236738",
            "captures": [],
            "passStates": [],
            "strippedCandidates": [
                {
                    "sceneId": "3476236738",
                    "sceneType": "Puppet",
                    "layerId": 101,
                    "layerName": "Water BG",
                    "layerImage": "materials/water.png",
                    "visibleEffectCount": 2,
                    "alpha": 1.0,
                    "policy": {
                        "keepLayer": True,
                        "keepEffects": False,
                        "strippedEffects": True,
                        "forceAlphaOne": False,
                        "reason": "puppet-alpha-strip",
                    },
                    "effectNames": ["waterwaves", "opacity"],
                    "materialShaders": ["effects/waterwaves", "effects/opacity"],
                },
                {
                    "sceneId": "3476236738",
                    "sceneType": "Puppet",
                    "layerId": 102,
                    "layerName": "Utility",
                    "layerImage": "models/util/solidlayer.json",
                    "visibleEffectCount": 1,
                    "alpha": 1.0,
                    "policy": {
                        "keepLayer": False,
                        "keepEffects": False,
                        "strippedEffects": True,
                        "forceAlphaOne": False,
                        "reason": "puppet-alpha-strip",
                    },
                    "effectNames": ["blur"],
                    "materialShaders": ["effects/blur"],
                },
            ],
        }

        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")

            completed = subprocess.run(
                [sys.executable, str(SUMMARY), str(path)],
                check=True,
                text=True,
                stdout=subprocess.PIPE,
            )

        self.assertIn("strippedCandidates=2", completed.stdout)
        self.assertIn("stripped-candidate-reasons:", completed.stdout)
        self.assertIn("  - puppet-alpha-strip: 2", completed.stdout)
        self.assertIn("stripped-candidate-layers:", completed.stdout)
        self.assertIn("  - 101:Water BG reason=puppet-alpha-strip effects=2 shaders=2", completed.stdout)
        self.assertIn("  - 102:Utility reason=puppet-alpha-strip effects=1 shaders=1", completed.stdout)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the failing Python unit test**

Run:

```bash
python3 -m unittest smoke-tests/test_effect_capture_summary.py
```

Expected: test fails because the summary helper does not print stripped candidate information yet.

- [ ] **Step 3: Add compact stripped candidate summary logic**

In `tools/effect-capture-summary.py`, add this function after `capture_layer(...)`:

```python
def candidate_layer(record):
    layer = record.get("layer")
    return layer if isinstance(layer, dict) else record
```

Add this function after `decision_for_layer(...)`:

```python
def candidate_reason(candidate):
    policy = candidate_layer(candidate).get("policy")
    if isinstance(policy, dict):
        return str(policy.get("reason", "unknown"))
    return "unknown"
```

In `main()`, after:

```python
    pass_states = as_list(manifest.get("passStates"))
```

add:

```python
    stripped_candidates = as_list(manifest.get("strippedCandidates"))
```

Change the existing summary line:

```python
    print(f"layers={len(layers)} passStates={len(pass_states)} failures={len(failures)}")
```

to:

```python
    print(f"layers={len(layers)} passStates={len(pass_states)} failures={len(failures)} strippedCandidates={len(stripped_candidates)}")
```

After the `decisions:` block and before `layer-stage-counts:`, add:

```python
    if stripped_candidates:
        reasons = Counter(candidate_reason(candidate) for candidate in stripped_candidates)
        print("stripped-candidate-reasons:")
        for reason, count in sorted(reasons.items()):
            print(f"  - {reason}: {count}")

        print("stripped-candidate-layers:")
        for candidate in stripped_candidates[:10]:
            layer = candidate_layer(candidate)
            policy = layer.get("policy") if isinstance(layer.get("policy"), dict) else {}
            layer_id = layer.get("layerId", "unknown")
            layer_name = layer.get("layerName") or "unnamed"
            reason = policy.get("reason", "unknown")
            effects = len(as_list(layer.get("effectNames")))
            shaders = len(as_list(layer.get("materialShaders")))
            print(f"  - {layer_id}:{layer_name} reason={reason} effects={effects} shaders={shaders}")
        if len(stripped_candidates) > 10:
            print(f"  ... {len(stripped_candidates) - 10} more")
```

- [ ] **Step 4: Run Python tests**

Run:

```bash
PYTHONPYCACHEPREFIX=/tmp/yakkai-pycache-controller python3 -m py_compile tools/effect-capture-summary.py
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
```

Expected: both commands exit `0`; unittest output includes the new summary helper test.

## Task 4: Update Debug Workflow Documentation

**Files:**
- Modify: `README.md`
- Modify: `native/scene_harness/README.md`

- [ ] **Step 1: Update top-level README effect debug text**

In `README.md`, replace:

```markdown
`--debug-effect-captures` is harness-only and off by default. It writes `manifest.json` plus `effect-input`, `effect-output`, and `final-publish` TGA captures for effect-chain layers. These captures are run artifacts for investigation; PNG smoke baselines remain the committed source of truth.
```

with:

```markdown
`--debug-effect-captures` is harness-only and off by default. It writes `manifest.json` plus `effect-input`, `effect-output`, and `final-publish` TGA captures for preserved effect-chain layers. The manifest also includes a top-level `strippedCandidates` array for effect chains that policy removed before render graph construction; these entries are metadata only and do not represent failed dumps. These captures are run artifacts for investigation; PNG smoke baselines remain the committed source of truth.
```

- [ ] **Step 2: Update harness README manifest description**

In `native/scene_harness/README.md`, replace:

```markdown
The debug manifest records scene id, layer id/name/type, `EffectPolicy` preserve/strip reason, effect names, material shaders, render targets, render-target dimensions/format, pass load operations, blend state, color masks, output paths, and capture status. A debug-enabled run exits non-zero if the manifest is missing or reports failed captures. Debug captures are investigation artifacts and should not be committed as smoke baselines.
```

with:

```markdown
The debug manifest records scene id, layer id/name/type, `EffectPolicy` preserve/strip reason, effect names, material shaders, render targets, render-target dimensions/format, pass load operations, blend state, color masks, output paths, and capture status. It also records manifest-only `strippedCandidates` for effect chains removed by policy before render graph construction. A debug-enabled run exits non-zero if the manifest is missing or reports failed render-target captures; an empty `strippedCandidates` array is not a failure by itself. Debug captures are investigation artifacts and should not be committed as smoke baselines.
```

- [ ] **Step 3: Run doc-adjacent checks**

Run:

```bash
git diff --check
./scripts/check-package.sh
```

Expected: both commands exit `0`.

## Task 5: Generate Diagnostics and Decide Re-Enable Scope

**Files:**
- Generated, do not commit: `/tmp/yakkai-phase3-3-*`
- Modify when diagnostics-only: `docs/superpowers/specs/2026-05-28-phase-3-3-stripped-effect-diagnostics-design.md`
- Do not modify `EffectPolicy` in this task.

- [ ] **Step 1: Capture `3476236738` diagnostics**

Clear shader cache:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

Run:

```bash
OUT="/tmp/yakkai-phase3-3-diagnostics-3476236738"
rm -rf "$OUT"
mkdir -p "$OUT"
build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3476236738/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --hide-info-overlay \
  --fill crop \
  --capture "$OUT/final.png" \
  --capture-delay-ms 10000 \
  --debug-effect-captures "$OUT"
tools/effect-capture-summary.py "$OUT/manifest.json"
```

Expected: harness exits `0`; summary prints `strippedCandidates=N`, where `N` is greater than `0`, and at least one `puppet-alpha-strip` reason.

- [ ] **Step 2: Capture `3228578419` diagnostics**

Clear shader cache:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

Run:

```bash
OUT="/tmp/yakkai-phase3-3-diagnostics-3228578419"
rm -rf "$OUT"
mkdir -p "$OUT"
build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg" \
  --assets "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets" \
  --hide-info-overlay \
  --fill crop \
  --capture "$OUT/final.png" \
  --capture-delay-ms 8000 \
  --debug-effect-captures "$OUT"
tools/effect-capture-summary.py "$OUT/manifest.json"
```

Expected: harness exits `0`; preserved flare/lens captures remain present; any stripped candidates are metadata-only.

- [ ] **Step 3: Run structural candidate assertions**

Run:

```bash
python3 -c 'import json; data=json.load(open("/tmp/yakkai-phase3-3-diagnostics-3476236738/manifest.json", encoding="utf-8")); candidates=data.get("strippedCandidates", []); assert candidates, "3476236738 should expose stripped candidates"; assert any(c.get("policy", {}).get("reason") == "puppet-alpha-strip" for c in candidates), "missing puppet-alpha-strip candidate"; required=("layerId","layerName","layerImage","visibleEffectCount","policy","effectNames","materialShaders"); missing=[key for c in candidates for key in required if key not in c]; assert not missing, f"missing candidate fields: {missing[:5]}"; print(f"validated strippedCandidates={len(candidates)}")'
```

Expected: exits `0` and prints `validated strippedCandidates=N`, where `N` is greater than `0`.

- [ ] **Step 4: Classify whether a re-enable candidate exists**

Run:

```bash
python3 -c 'import json; data=json.load(open("/tmp/yakkai-phase3-3-diagnostics-3476236738/manifest.json", encoding="utf-8")); candidates=data.get("strippedCandidates", []); heavy=("audio","lightshaft","blur","lut","colorgrade","colorgrading","colorcorrection"); safe=[]; blocked=[]; 
for c in candidates:
    name=(c.get("layerName") or "").lower()
    image=(c.get("layerImage") or "").lower()
    effects=[str(x).lower() for x in c.get("effectNames", [])]
    shaders=[str(x).lower() for x in c.get("materialShaders", [])]
    text=" ".join([name,image,*effects,*shaders])
    policy=c.get("policy", {})
    is_utility=any(token in image for token in ("solidlayer","projectlayer","fullscreenlayer","composelayer"))
    is_heavy=any(token in text for token in heavy)
    if policy.get("keepLayer") is True and policy.get("strippedEffects") is True and not is_utility and not is_heavy:
        safe.append((c.get("layerId"), c.get("layerName"), c.get("effectNames"), c.get("materialShaders")))
    else:
        blocked.append((c.get("layerId"), c.get("layerName")))
print("safeCandidateCount=", len(safe))
for row in safe[:10]:
    print("safeCandidate=", row)
print("blockedCandidateCount=", len(blocked))'
```

Expected: command prints candidate counts for human review. This command does not authorize a re-enable by itself.

- [ ] **Step 5: Record the decision**

If the output from Step 4 is empty, contains only utility/heavy/LUT/blur/color-grade candidates, or visual safety is ambiguous, write:

```bash
printf '%s\n' \
  'Phase 3.3 re-enable decision: diagnostics-only' \
  'reason: no manifest-only candidate met the strict low-risk threshold' \
  '3476236738 artifacts: /tmp/yakkai-phase3-3-diagnostics-3476236738' \
  '3228578419 artifacts: /tmp/yakkai-phase3-3-diagnostics-3228578419' \
  > /tmp/yakkai-phase3-3-reenable-decision.txt
```

If exactly one narrow family appears safe enough to consider, write:

```bash
printf '%s\n' \
  'Phase 3.3 re-enable decision: candidate-specific plan required' \
  'reason: one candidate family needs reviewed allowlist plan before policy changes' \
  '3476236738 artifacts: /tmp/yakkai-phase3-3-diagnostics-3476236738' \
  '3228578419 artifacts: /tmp/yakkai-phase3-3-diagnostics-3228578419' \
  > /tmp/yakkai-phase3-3-reenable-decision.txt
```

Expected: `/tmp/yakkai-phase3-3-reenable-decision.txt` exists. If it says `candidate-specific plan required`, stop implementation and present the candidate evidence before editing `EffectPolicy`.

## Task 6: Document Diagnostics-Only Result

Use this task only when `/tmp/yakkai-phase3-3-reenable-decision.txt` says `diagnostics-only`.

**Files:**
- Modify: `docs/superpowers/specs/2026-05-28-phase-3-3-stripped-effect-diagnostics-design.md`

- [ ] **Step 1: Add investigation result**

Append this section to `docs/superpowers/specs/2026-05-28-phase-3-3-stripped-effect-diagnostics-design.md`:

```markdown
## Investigation Result

Slice 3.3 landed manifest-only stripped effect diagnostics. The debug manifest now exposes top-level `strippedCandidates` for effect chains removed by policy before render graph construction. Generated artifacts stayed in `/tmp` and were not promoted to baselines.

No effect family was re-enabled in this slice because the manifest-only candidates did not meet the strict low-risk threshold, or the visual safety of re-enabling them was ambiguous. Re-enable work remains deferred to a candidate-specific follow-up plan.
```

- [ ] **Step 2: Re-run doc checks**

Run:

```bash
rg -n "TBD|TODO|placeholder|\\?\\?" docs/superpowers/specs/2026-05-28-phase-3-3-stripped-effect-diagnostics-design.md
git diff --check
```

Expected: `rg` exits `1` with no matches; `git diff --check` exits `0`.

## Task 7: Candidate-Specific Re-Enable Plan Gate

Use this task only when `/tmp/yakkai-phase3-3-reenable-decision.txt` says `candidate-specific plan required`.

**Files:**
- Do not modify renderer behavior in this task.
- Read: `/tmp/yakkai-phase3-3-diagnostics-3476236738/manifest.json`
- Read: `/tmp/yakkai-phase3-3-diagnostics-3228578419/manifest.json`

- [ ] **Step 1: Prepare candidate evidence**

Run:

```bash
cat /tmp/yakkai-phase3-3-reenable-decision.txt
tools/effect-capture-summary.py /tmp/yakkai-phase3-3-diagnostics-3476236738/manifest.json
tools/effect-capture-summary.py /tmp/yakkai-phase3-3-diagnostics-3228578419/manifest.json
```

Expected: output identifies the candidate family and shows why Arona is not affected by a dangerous path.

- [ ] **Step 2: Stop before policy changes**

Do not edit `native/scene_backend/vendor/upstream_debug/src/Policy/EffectPolicy.*` in this plan. Present the candidate evidence and write a short candidate-specific plan update that includes:

```text
- exact policy predicate
- exact candidate family/effect/shader names
- focused native tests
- before/after diagnostics
- release smoke requirement
```

Expected: implementation pauses for review instead of making an unplanned allowlist change.

## Task 8: Full Validation

**Files:**
- Generated, do not commit: `/tmp/yakkai-debug`
- Generated, do not commit: `/tmp/yakkai-smoke/*`

- [ ] **Step 1: Build and run native policy tests**

Run:

```bash
cmake --build build --target yakkai_scene_harness yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
```

Expected: both commands exit `0`.

- [ ] **Step 2: Run Python and QML/package checks**

Run:

```bash
PYTHONPYCACHEPREFIX=/tmp/yakkai-pycache-controller python3 -m py_compile tools/effect-capture-summary.py
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
qmllint -I build/qml native/scene_harness/qml/Main.qml native/scene_harness/qml/YakkaiSceneViewerHarness.qml native/scene_harness/qml/SystemSceneViewerHarness.qml
./scripts/check-package.sh
git diff --check
```

Expected: all commands exit `0`.

- [ ] **Step 3: Run scene validators with fresh shader cache**

Clear shader cache:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

Run:

```bash
tools/validate-scene.sh 3228578419 8000
tools/validate-scene.sh 3476236738 10000
```

Expected: both validators exit `0`. `3476236738` may still warn about no effect passes if no family was re-enabled; that warning is acceptable for diagnostics-only.

- [ ] **Step 4: Run quick strict smoke**

Clear shader cache:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

Run:

```bash
./smoke-tests/run.sh --suite quick --strict
```

Expected: exits `0`; prints `PASS 3228578419 Sleeping Arona` and `PASS 3327063360 Shiroko Night Video`.

- [ ] **Step 5: Run release smoke only if a candidate-specific re-enable was added**

If no effect family was re-enabled, skip this step and record that it was not required.

If a candidate-specific re-enable was added after updating this plan, run:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
./smoke-tests/run.sh --suite release --strict --require-assets
```

Expected: exits `0`.

## Task 9: Visual Review and Final Branch Check

**Files:**
- Review: all modified tracked files.
- Generated, do not commit: `/tmp/yakkai-phase3-3-*`

- [ ] **Step 1: Visually review Sleeping Arona**

Open the newest quick-smoke still or `/tmp/yakkai-phase3-3-diagnostics-3228578419/final.png`.

Check:

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

Expected: no regression against accepted Arona visuals.

- [ ] **Step 2: Visually review `3476236738`**

Open `/tmp/yakkai-phase3-3-diagnostics-3476236738/final.png` or `/tmp/yakkai-debug/validate-3476236738.png`.

Check:

```text
camera angle
major visible elements
foreground/background composition
color balance
absence of new overlays or blank regions
```

Expected: no regression from Phase 3.2 diagnostics.

- [ ] **Step 3: Inspect final status and diff**

Run:

```bash
git status --short --branch
git diff --stat
git diff --name-only
```

Expected: changes are limited to:

```text
README.md
native/scene_harness/README.md
native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.hpp
native/scene_backend/vendor/upstream_debug/src/Debug/EffectCaptureDebug.cpp
native/scene_backend/vendor/upstream_debug/src/Scene/include/Scene/Scene.h
native/scene_backend/vendor/upstream_debug/src/WPSceneParser.cpp
native/scene_backend/tests/test_scene_policies.cpp
tools/effect-capture-summary.py
smoke-tests/test_effect_capture_summary.py
docs/superpowers/specs/2026-05-28-phase-3-3-stripped-effect-diagnostics-design.md
docs/superpowers/plans/2026-05-28-phase-3-3-stripped-effect-diagnostics.md
```

If a candidate-specific re-enable is later approved, `EffectPolicy.hpp` and `EffectPolicy.cpp` may also be present.

- [ ] **Step 4: Final report**

Report:

```text
- whether Slice 3.3 is diagnostics-only or stopped for a candidate-specific allowlist plan
- stripped candidate counts for 3476236738 and 3228578419
- verification commands and exit statuses
- visual review result
- final git status
```
