# Phase 3.2 Alpha Preservation Investigation Design

## Context

Phase 3.1 added opt-in effect capture diagnostics for the standalone scene harness. The next slice should use those captures to find the first point where alpha, color, or composition diverges in effect-chain rendering before changing renderer behavior.

This slice keeps the existing puppet-scene strip policy intact unless a narrow renderer fix must touch the same path. It does not re-enable a stripped effect family. Re-enablement belongs in Slice 3.3 after the alpha preservation issue is understood and covered.

## Goals

- Run before/after effect diagnostics on Sleeping Arona (`3228578419`) and the effect-heavy scene `3476236738`.
- Compare `effect-input`, `effect-output`, `final-publish`, and manifest metadata to locate the first alpha-loss or composition-loss point.
- Land one narrow fix only after the captures identify a specific fault.
- Add focused regression coverage for the fixed path.
- Preserve existing visual baselines for protected scenes.

## Non-Goals

- Do not remove the puppet-scene effect strip policy wholesale.
- Do not re-enable a full effect family in this slice.
- Do not replace the smoke baseline system.
- Do not make Plasma-live desktop capture a required gate.
- Do not commit generated diagnostic captures as baselines.

## Candidate Scenes

### `3228578419` Sleeping Arona

This is the protected regression scene. It exercises puppet rendering, stripped background effects, preserved flare/lens/hash effect chains, sparkle particles, and prior alpha-related failure modes.

Expected visual invariants:

- Brown desk remains visible.
- Sleeping character remains correctly placed on the desk.
- Rainbow flare arc remains present without white rectangles or black overlays.
- Sparkle particles remain visible.
- Background does not wash out to a blue-white tint.

### `3476236738`

This is the effect-heavy diagnostic scene. It has documented history around puppet scenes, alpha writes, water/opacity/shine/iris effects, and render graph behavior. It is the better stress candidate for locating effect-chain alpha problems because it has more active effect families and richer graph state.

Expected validation focus:

- Effect graph structure remains stable or improves.
- Shader and material failure counts do not increase.
- Color variance and diversity do not collapse.
- Average scene color remains in the expected atmosphere range.

## Diagnostic Flow

1. Clear the shader cache before each validation pass:

   ```bash
   rm -rf ~/.cache/wescene-renderer/*/spvs01/
   ```

2. Capture baseline diagnostics for both scenes using `--debug-effect-captures`.
3. Inspect `manifest.json` for preserved versus stripped effect decisions, render target dimensions, pass states, blend/load state, and final publish routing.
4. Compare captured images in this order:

   - `effect-input`
   - `effect-output`
   - `final-publish`
   - normal smoke/validator capture

5. Identify the earliest stage where alpha, color, or geometry diverges.
6. Make only the smallest renderer change that addresses that specific stage.
7. Re-capture diagnostics and compare before/after artifacts.
8. Run structural, smoke, package, and visual validation before merging the slice.

## Fix Boundaries

Allowed fix targets:

- Render target clear alpha.
- Attachment load/store behavior.
- Shader output alpha preservation.
- Color write mask state.
- Blend factors used when publishing an offscreen effect result.
- Copy source selection when effect-chain final output is offscreen.
- Straight-alpha versus premultiplied-alpha handling at a single proven boundary.

Out of scope for this slice:

- Broad shader preprocessing rewrites.
- Global blend mode reinterpretation.
- Effect family policy relaxation.
- SceneScript behavior expansion.
- New baseline format or storage changes.

## Regression Coverage

Regression coverage should be as close to the fault as practical. Prefer a native policy or render-graph test when the bug can be represented without launching the full harness. Use smoke or validator coverage when the fault depends on actual scene assets, shaders, or capture output.

At minimum, the fix must include one of:

- A native test proving the corrected routing/state decision.
- A validator expectation that would fail before the fix.
- A smoke-level artifact comparison with documented before/after captures.

## Validation Gate

Run these checks before considering the slice mergeable:

```bash
cmake --build build --target yakkai_scene_harness yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
qmllint -I build/qml native/scene_harness/qml/Main.qml native/scene_harness/qml/YakkaiSceneViewerHarness.qml native/scene_harness/qml/SystemSceneViewerHarness.qml
./scripts/check-package.sh
```

For render validation:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
tools/validate-scene.sh 3228578419 8000
tools/validate-scene.sh 3476236738 10000
./smoke-tests/run.sh --suite quick --strict
```

Run the release smoke suite when the fix changes shared effect-chain, blend, shader preprocessing, or publish-path behavior.

Manual visual comparison is required for the protected scenes. Check camera angle, visible elements, character position, desk/background visibility, color balance, composition, flare intensity, particles, and absence of black/white overlays.

## Success Criteria

- Diagnostic captures either identify a concrete first failure point, or document that no preserved effect-chain alpha/composition loss is visible.
- One narrow fix lands for the identified failure point, or the no-code investigation result explains why a renderer change is not justified.
- Existing protected visuals do not regress.
- `3228578419` and `3476236738` validator results do not show new structural or pixel-quality failures.
- README or process docs are updated if commands, workflow, or user-facing limitations change.

## Investigation Result

The captured `effect-input`, `effect-output`, and `final-publish` artifacts did not show an alpha/composition loss that justifies a renderer change in Slice 3.2. Generated artifacts stayed in `/tmp` and were not promoted to baselines.

Evidence:

- `3228578419` (`/tmp/yakkai-phase3-2-baseline-3228578419`) produced 18 captures across 6 preserved flare/lens/hash effect layers with no dump failures. Visual review showed the desk, sleeping character, flare arc, sparkle particles, and warm color balance were preserved.
- `3333947217` (`/tmp/yakkai-phase3-2-candidate-3333947217-rerun`) produced 15 captures across 5 preserved flare layers with no dump failures. The first 8000 ms run captured black before the render graph became ready; the 25000 ms rerun produced the expected visible frame and manifest.
- `3329705415` (`/tmp/yakkai-phase3-2-candidate-3329705415`) produced 3 captures for one standard-scene effect layer with no dump failures, and `effect-output` matched `final-publish`.
- `3476236738` (`/tmp/yakkai-phase3-2-baseline-3476236738`) produced a final image but zero effect captures because the current puppet strip policy removes all candidate effect chains before debug capture registration. That makes it a diagnostic coverage gap for this slice, not evidence of an alpha-boundary renderer fault.

Follow-up work should either use scenes with preserved effect chains when testing alpha preservation, or add a separate diagnostic mode for stripped effect candidates before trying to relax the puppet strip policy.
