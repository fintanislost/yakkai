# Phase 3.4 Candidate Backlog Design

## Goal

Phase 3.4 turns the Phase 3.3 stripped-candidate diagnostics into a durable
candidate backlog. It does not change renderer behavior.

## Scope

- Add a versioned backlog document for blocked stripped effect families.
- Point README known limitations to the backlog so users and contributors can
  find the current evidence.
- Keep `EffectPolicy`, blend modes, shader preprocessing, parser behavior, and
  smoke baselines unchanged.

## Source Evidence

The backlog is based on the Phase 3.3 manifests:

- `3476236738`: 13 stripped candidates, zero dump captures, zero dump failures.
- `3228578419`: 11 stripped candidates plus 18 preserved flare/lens captures
  across 6 layers, zero dump failures.

The important result is mixed evidence. `3476236738` has simple-looking
waterflow and waterwaves layers, but also mixed opacity, shine, iris, blur,
audio, and color-grading paths. Sleeping Arona exposes LUT, blur/fullscreen,
character, and background paths. That combination is not safe enough for a
single broad family allowlist.

## Output Files

- `docs/renderer-effect-candidate-backlog.md`
  - canonical backlog table for stripped effect families
  - current blockers
  - required next slice for each family
  - validation gates for future candidate-specific work
- `README.md`
  - known limitations section links to the backlog
- `docs/superpowers/specs/2026-05-28-phase-3-4-candidate-backlog-design.md`
  - records the scope and evidence for this docs-only slice

## Validation

Because this slice is documentation-only, validation is lightweight:

```bash
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
build/native/scene_backend/yakkai_scene_policy_tests
./scripts/check-package.sh
git diff --check
```

No render validation is required unless renderer code changes.
