# Phase 1 Scene Coverage Matrix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a durable scene coverage matrix so every known renderer limitation has at least one active baseline or reviewed candidate before behavior-changing renderer work begins.

**Architecture:** Keep `smoke-tests/scenes.json` as the active render manifest and add a separate coverage matrix for active fixtures, local candidates, and future harness-only gaps. Add a no-render `--coverage` report/check path to the smoke runner so the matrix is validated by tests and easy to review without changing quick/deep/release gate behavior.

**Tech Stack:** Python 3 stdlib, JSON manifests, Markdown docs, existing `smoke-tests/run.sh`, existing `smoke-tests/runner.py` and `smoke-tests/test_runner.py`, existing `tools/validate-scene.sh` for scene-package probes.

---

## Scope Rules

- Phase 1 does not change renderer behavior, shader preprocessing, effect policy, blend modes, video playback policy, SceneScript runtime, model/material/light behavior, or Plasma integration.
- Phase 1 does not add unreviewed candidates to `quick`, `deep`, or `release`.
- Phase 1 may add metadata and report commands that explain which candidates should later become fixtures.
- Local candidate probe artifacts stay under `/tmp` or another artifact directory and do not get committed.
- Do not create commits during execution unless the user explicitly asks. Use review checkpoints instead.
- Update `README.md` and smoke-test docs because this adds workflow commands and files.

## File Structure

- Create `smoke-tests/coverage-matrix.json`: structured coverage buckets, active fixtures, candidate fixtures, harness gaps, and promotion criteria.
- Create `smoke-tests/COVERAGE.md`: human-readable snapshot explaining current active coverage, candidate coverage, known gaps, and exact Workshop links.
- Modify `smoke-tests/runner.py`: add coverage-matrix loading, validation, summary generation, Markdown/JSON report output, and CLI options.
- Modify `smoke-tests/test_runner.py`: add unit tests for matrix validation, bucket summaries, active-manifest cross-checks, and report output.
- Modify `smoke-tests/run.sh`: preserve delegation while allowing the new coverage options to pass through.
- Modify `smoke-tests/README.md`: document the matrix, report command, candidate intake workflow, and promotion rules.
- Modify `README.md`: add the top-level Phase 1 coverage command to the render regression workflow.
- Modify `smoke-tests/ASSETS.md`: link active fixture assets to the coverage matrix and clarify that candidate assets live in `smoke-tests/COVERAGE.md` until promoted.
- Modify `RENDERER_FIXTURE_CANDIDATE_SLICES.md`: align candidate IDs and statuses with the structured matrix after local probe results are recorded.

## Command Contract

The final no-render coverage commands should be:

```bash
./smoke-tests/run.sh --coverage
./smoke-tests/run.sh --coverage --coverage-format json
./smoke-tests/run.sh --coverage --coverage-matrix smoke-tests/coverage-matrix.json
```

`--coverage` exits nonzero only when the matrix is internally invalid or a required coverage bucket has neither active nor candidate coverage. Candidate coverage is enough for Phase 1; it is not enough for later release readiness.

---

### Task 1: Define The Coverage Matrix Data

**Files:**
- Create: `smoke-tests/coverage-matrix.json`
- Test: `python3 -m json.tool smoke-tests/coverage-matrix.json`

- [ ] **Step 1: Create the structured matrix**

Add `smoke-tests/coverage-matrix.json` with active baselines, locally identified candidates, web/video harness gaps, and promotion criteria.

```json
{
  "version": 1,
  "buckets": [
    {
      "id": "puppet-effects-alpha",
      "name": "Puppet Effects And Alpha",
      "requiredForPhase": 3,
      "minimumStatus": "active",
      "coverage": [
        {
          "sceneId": "3228578419",
          "status": "active",
          "source": "smoke-tests/scenes.json",
          "notes": "Sleeping Arona covers puppet rendering, flare/lens layers, particles, alpha-sensitive composition, and existing effect stripping constraints."
        }
      ]
    },
    {
      "id": "main-video-texture",
      "name": "Main Embedded Video Texture",
      "requiredForPhase": 4,
      "minimumStatus": "active",
      "coverage": [
        {
          "sceneId": "3327063360",
          "status": "active",
          "source": "smoke-tests/scenes.json",
          "notes": "Shiroko Night Video covers main embedded video texture playback, particles, effect chain, and decoder timing tolerance."
        }
      ]
    },
    {
      "id": "effect-chain-regression",
      "name": "Effect Chain Regression Replay",
      "requiredForPhase": 3,
      "minimumStatus": "candidate",
      "coverage": [
        {
          "sceneId": "1591277437",
          "status": "candidate",
          "source": "RENDERER_FIXTURE_CANDIDATE_SLICES.md",
          "notes": "Spider-Verse candidate covers godrays, shake, pulse, and a prior visual regression class."
        }
      ]
    },
    {
      "id": "scene-script-bindings",
      "name": "SceneScript Bindings And Runtime",
      "requiredForPhase": 6,
      "minimumStatus": "candidate",
      "coverage": [
        {
          "sceneId": "3301291394",
          "status": "candidate",
          "source": "RENDERER_FIXTURE_CANDIDATE_SLICES.md",
          "notes": "Alya Clock and Date covers clock/date script bindings and user property toggles."
        },
        {
          "sceneId": "3326873240",
          "status": "candidate",
          "source": "RENDERER_FIXTURE_CANDIDATE_SLICES.md",
          "notes": "Elaina Day Night Gradient covers non-puppet SceneScript and day-night tint/property behavior."
        }
      ]
    },
    {
      "id": "overlay-video-texture",
      "name": "Overlay Or Small Embedded Video Texture",
      "requiredForPhase": 4,
      "minimumStatus": "candidate",
      "coverage": [
        {
          "sceneId": "2788691565",
          "status": "candidate",
          "source": "RENDERER_FIXTURE_CANDIDATE_SLICES.md",
          "notes": "Girl and Fluorescent Beach covers embedded mp4 references inside a scene package."
        }
      ]
    },
    {
      "id": "static-model-material-lighting",
      "name": "Static Model, Material, And Lighting",
      "requiredForPhase": 5,
      "minimumStatus": "candidate",
      "coverage": [
        {
          "sceneId": "1576514332",
          "status": "candidate",
          "source": "RENDERER_FIXTURE_CANDIDATE_SLICES.md",
          "notes": "Cyber City Parkour covers non-puppet models, camera framing, material fallback, composelayers, and lights."
        }
      ]
    },
    {
      "id": "we-web-wallpaper",
      "name": "Wallpaper Engine Web Project",
      "requiredForPhase": "stretch",
      "minimumStatus": "requiresHarness",
      "coverage": [
        {
          "sceneId": "1509243786",
          "status": "requiresHarness",
          "source": "RENDERER_FIXTURE_CANDIDATE_SLICES.md",
          "notes": "CWAV Engine is a web project and must stay out of the native scene manifest until a web or Plasma-live harness exists."
        }
      ]
    },
    {
      "id": "we-video-wallpaper",
      "name": "Wallpaper Engine Video Project",
      "requiredForPhase": "stretch",
      "minimumStatus": "requiresHarness",
      "coverage": [
        {
          "sceneId": "2478419118",
          "status": "requiresHarness",
          "source": "RENDERER_FIXTURE_CANDIDATE_SLICES.md",
          "notes": "Blue Archive Shiroko Live2D is a plain WE video project and needs a video wallpaper harness before strict gating."
        }
      ]
    }
  ],
  "promotionCriteria": [
    "Candidate is locally installed and still matches the documented project type.",
    "Scene-package candidates pass tools/validate-scene.sh when that tool supports the package.",
    "Candidate artifacts are generated with ./smoke-tests/run.sh --suite deep --write-candidates after it is temporarily added to a non-release run.",
    "Manual review confirms camera angle, visible elements, color balance, composition, effect intensity, and expected motion.",
    "Promoted candidates enter deep first and release only after repeated strict runs are stable."
  ]
}
```

- [ ] **Step 2: Validate JSON syntax**

Run:

```bash
python3 -m json.tool smoke-tests/coverage-matrix.json
```

Expected: pretty-printed JSON exits `0`.

---

### Task 2: Add Coverage Matrix Unit Tests

**Files:**
- Modify: `smoke-tests/test_runner.py`

- [ ] **Step 1: Add a test for loading and validating coverage status values**

Append tests to `RunnerCoreTests`:

```python
    def test_validate_coverage_matrix_rejects_unknown_status(self):
        matrix = {
            "version": 1,
            "buckets": [
                {
                    "id": "bad",
                    "name": "Bad Bucket",
                    "minimumStatus": "candidate",
                    "coverage": [{"sceneId": "1", "status": "mystery", "source": "x", "notes": "bad"}],
                }
            ],
        }
        manifest = {"scenes": []}

        errors = runner.validate_coverage_matrix(matrix, manifest)

        self.assertIn("bucket bad scene 1 has unknown status mystery", errors)
```

- [ ] **Step 2: Add a test that active manifest scenes must appear in the matrix**

Append:

```python
    def test_validate_coverage_matrix_requires_active_manifest_entries(self):
        matrix = {"version": 1, "buckets": []}
        manifest = {"scenes": [{"id": "3228578419", "name": "Sleeping Arona"}]}

        errors = runner.validate_coverage_matrix(matrix, manifest)

        self.assertIn("active scene 3228578419 is missing from coverage matrix", errors)
```

- [ ] **Step 3: Add a test that required buckets can be satisfied by candidates**

Append:

```python
    def test_coverage_bucket_summary_accepts_candidate_for_phase_one(self):
        matrix = {
            "version": 1,
            "buckets": [
                {
                    "id": "scene-script-bindings",
                    "name": "SceneScript Bindings And Runtime",
                    "minimumStatus": "candidate",
                    "coverage": [{"sceneId": "3301291394", "status": "candidate", "source": "doc", "notes": "script"}],
                }
            ],
        }

        summaries = runner.coverage_bucket_summaries(matrix)

        self.assertEqual(summaries[0]["id"], "scene-script-bindings")
        self.assertEqual(summaries[0]["bestStatus"], "candidate")
        self.assertTrue(summaries[0]["satisfied"])
```

- [ ] **Step 4: Add a test for Markdown output**

Append:

```python
    def test_format_coverage_markdown_includes_bucket_scene_and_status(self):
        summaries = [
            {
                "id": "main-video-texture",
                "name": "Main Embedded Video Texture",
                "minimumStatus": "active",
                "bestStatus": "active",
                "satisfied": True,
                "sceneIds": ["3327063360"],
            }
        ]

        markdown = runner.format_coverage_markdown(summaries)

        self.assertIn("| main-video-texture | Main Embedded Video Texture | active | active | yes | 3327063360 |", markdown)
```

- [ ] **Step 5: Run the targeted tests and confirm they fail**

Run:

```bash
PYTHONPATH=smoke-tests python3 -m unittest test_runner.RunnerCoreTests.test_validate_coverage_matrix_rejects_unknown_status test_runner.RunnerCoreTests.test_validate_coverage_matrix_requires_active_manifest_entries test_runner.RunnerCoreTests.test_coverage_bucket_summary_accepts_candidate_for_phase_one test_runner.RunnerCoreTests.test_format_coverage_markdown_includes_bucket_scene_and_status
```

Expected: tests fail because the coverage functions do not exist.

---

### Task 3: Implement Coverage Matrix Helpers

**Files:**
- Modify: `smoke-tests/runner.py`

- [ ] **Step 1: Add status ordering and loader helpers**

Add near the existing `RESULT_ORDER` and `load_manifest` helpers:

```python
COVERAGE_STATUS_ORDER = {
    "missing": 0,
    "requiresHarness": 1,
    "candidate": 2,
    "active": 3,
}


def load_coverage_matrix(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def coverage_status_meets(actual: str, required: str) -> bool:
    if actual not in COVERAGE_STATUS_ORDER:
        raise ValueError(f"unknown coverage status: {actual}")
    if required not in COVERAGE_STATUS_ORDER:
        raise ValueError(f"unknown required coverage status: {required}")
    return COVERAGE_STATUS_ORDER[actual] >= COVERAGE_STATUS_ORDER[required]
```

- [ ] **Step 2: Add summary generation**

Add:

```python
def coverage_bucket_summaries(matrix: dict[str, Any]) -> list[dict[str, Any]]:
    summaries: list[dict[str, Any]] = []
    for bucket in matrix.get("buckets", []):
        coverage = bucket.get("coverage", [])
        statuses = [entry.get("status", "missing") for entry in coverage]
        known_statuses = [status for status in statuses if status in COVERAGE_STATUS_ORDER]
        best_status = "missing"
        if known_statuses:
            best_status = max(known_statuses, key=lambda status: COVERAGE_STATUS_ORDER[status])
        minimum = str(bucket.get("minimumStatus", "candidate"))
        satisfied = minimum in COVERAGE_STATUS_ORDER and coverage_status_meets(best_status, minimum)
        summaries.append({
            "id": bucket.get("id", ""),
            "name": bucket.get("name", ""),
            "minimumStatus": minimum,
            "bestStatus": best_status,
            "satisfied": satisfied,
            "sceneIds": [str(entry.get("sceneId", "")) for entry in coverage if entry.get("sceneId")],
        })
    return summaries
```

- [ ] **Step 3: Add validation**

Add:

```python
def validate_coverage_matrix(matrix: dict[str, Any], manifest: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    matrix_scene_ids: set[str] = set()
    bucket_ids: set[str] = set()

    for bucket in matrix.get("buckets", []):
        bucket_id = str(bucket.get("id", ""))
        if not bucket_id:
            errors.append("coverage bucket is missing id")
            continue
        if bucket_id in bucket_ids:
            errors.append(f"duplicate coverage bucket id {bucket_id}")
        bucket_ids.add(bucket_id)

        minimum = str(bucket.get("minimumStatus", "candidate"))
        if minimum not in COVERAGE_STATUS_ORDER:
            errors.append(f"bucket {bucket_id} has unknown minimumStatus {minimum}")

        for entry in bucket.get("coverage", []):
            scene_id = str(entry.get("sceneId", ""))
            status = str(entry.get("status", ""))
            if scene_id:
                matrix_scene_ids.add(scene_id)
            else:
                errors.append(f"bucket {bucket_id} has coverage entry without sceneId")
            if status not in COVERAGE_STATUS_ORDER:
                errors.append(f"bucket {bucket_id} scene {scene_id} has unknown status {status}")

    for scene in manifest.get("scenes", []):
        scene_id = str(scene.get("id", ""))
        if scene_id and scene_id not in matrix_scene_ids:
            errors.append(f"active scene {scene_id} is missing from coverage matrix")

    for summary in coverage_bucket_summaries(matrix):
        if not summary["satisfied"]:
            errors.append(
                f"bucket {summary['id']} requires {summary['minimumStatus']} coverage but best status is {summary['bestStatus']}"
            )

    return errors
```

- [ ] **Step 4: Add Markdown and JSON formatting helpers**

Add:

```python
def format_coverage_markdown(summaries: list[dict[str, Any]]) -> str:
    lines = [
        "# Render Coverage Matrix",
        "",
        "| Bucket | Name | Required | Best | Satisfied | Scenes |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for summary in summaries:
        satisfied = "yes" if summary["satisfied"] else "no"
        scenes = ", ".join(summary["sceneIds"])
        lines.append(
            f"| {summary['id']} | {summary['name']} | {summary['minimumStatus']} | {summary['bestStatus']} | {satisfied} | {scenes} |"
        )
    return "\n".join(lines) + "\n"


def format_coverage_json(summaries: list[dict[str, Any]]) -> str:
    return json.dumps({"coverage": summaries}, indent=2, sort_keys=True) + "\n"
```

- [ ] **Step 5: Run targeted tests and confirm they pass**

Run the same command from Task 2 Step 5.

Expected: all four targeted tests pass.

---

### Task 4: Add The Coverage CLI Path

**Files:**
- Modify: `smoke-tests/runner.py`
- Modify: `smoke-tests/test_runner.py`
- Modify: `smoke-tests/run.sh`

- [ ] **Step 1: Add parser options**

In `build_parser()`, add:

```python
    parser.add_argument("--coverage", action="store_true")
    parser.add_argument("--coverage-matrix", default="smoke-tests/coverage-matrix.json")
    parser.add_argument("--coverage-format", choices=["markdown", "json"], default="markdown")
```

- [ ] **Step 2: Add a runner entry branch for coverage**

In `main()`, after parsing args and before ImageMagick/harness dependency checks, add:

```python
    if args.coverage:
        matrix_path = (root / args.coverage_matrix).resolve() if not Path(args.coverage_matrix).is_absolute() else Path(args.coverage_matrix)
        matrix = load_coverage_matrix(matrix_path)
        manifest = load_manifest(root / args.manifest)
        summaries = coverage_bucket_summaries(matrix)
        errors = validate_coverage_matrix(matrix, manifest)
        if args.coverage_format == "json":
            print(format_coverage_json(summaries), end="")
        else:
            print(format_coverage_markdown(summaries), end="")
        for error in errors:
            print(f"coverage error: {error}", file=sys.stderr)
        return 1 if errors else 0
```

Use the existing `root = repo_root()` local variable if `main()` already defines it before this branch; otherwise define it before the coverage branch.

- [ ] **Step 3: Add a CLI test for coverage output**

Append to `smoke-tests/test_runner.py`:

```python
    def test_main_coverage_prints_markdown_without_render_dependencies(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "smoke-tests").mkdir()
            manifest = root / "smoke-tests" / "scenes.json"
            matrix = root / "smoke-tests" / "coverage-matrix.json"
            manifest.write_text(json.dumps({"version": 1, "scenes": [{"id": "1"}]}), encoding="utf-8")
            matrix.write_text(json.dumps({
                "version": 1,
                "buckets": [
                    {
                        "id": "bucket",
                        "name": "Bucket",
                        "minimumStatus": "active",
                        "coverage": [{"sceneId": "1", "status": "active", "source": "test", "notes": "ok"}],
                    }
                ],
            }), encoding="utf-8")

            with mock.patch.object(runner, "repo_root", return_value=root), \
                 mock.patch.object(sys, "argv", ["runner.py", "--coverage"]), \
                 io.StringIO() as output, redirect_stdout(output):
                code = runner.main()

            self.assertEqual(code, 0)
            self.assertIn("| bucket | Bucket | active | active | yes | 1 |", output.getvalue())
```

- [ ] **Step 4: Run CLI test**

Run:

```bash
PYTHONPATH=smoke-tests python3 -m unittest test_runner.RunnerCoreTests.test_main_coverage_prints_markdown_without_render_dependencies
```

Expected: pass.

- [ ] **Step 5: Confirm the shell wrapper passes coverage args through**

Run:

```bash
./smoke-tests/run.sh --coverage
```

Expected: Markdown table with all buckets and exit `0`.

---

### Task 5: Write The Human Coverage Snapshot

**Files:**
- Create: `smoke-tests/COVERAGE.md`
- Modify: `smoke-tests/ASSETS.md`
- Modify: `smoke-tests/README.md`
- Modify: `README.md`

- [ ] **Step 1: Generate the machine-backed summary**

Run:

```bash
./smoke-tests/run.sh --coverage > /tmp/yakkai-coverage.md
```

Expected: `/tmp/yakkai-coverage.md` contains the generated table and the command exits `0`.

- [ ] **Step 2: Create `smoke-tests/COVERAGE.md`**

Create `smoke-tests/COVERAGE.md` with the generated table followed by explanatory sections:

````markdown
# Render Coverage Matrix

This file summarizes which Wallpaper Engine renderer limitations have active baselines, candidate fixtures, or deferred harness requirements. It is a human snapshot of `smoke-tests/coverage-matrix.json`; regenerate the table with:

```bash
./smoke-tests/run.sh --coverage
```

## Active Baselines

- `3228578419` Sleeping Arona: puppet, flare/lens, particles, alpha-sensitive composition.
- `3327063360` Shiroko Night Video: main embedded video texture, particles, effect chain.

## Candidate Fixtures

- `1591277437` Spider-Verse: effect-chain regression replay for godrays, shake, and pulse.
- `3301291394` Alya Clock and Date: SceneScript clock/date bindings and user properties.
- `3326873240` Elaina Day Night Gradient: non-puppet SceneScript and tint/property behavior.
- `2788691565` Girl and Fluorescent Beach: small or overlay embedded video texture policy.
- `1576514332` Cyber City Parkour: static models, material fallback, camera framing, composelayers, and lights.

## Harness Gaps

- `1509243786` CWAV Engine: Wallpaper Engine web project; requires a web or Plasma-live capture harness.
- `2478419118` Blue Archive Shiroko Live2D: Wallpaper Engine video project; requires a video wallpaper or Plasma-live capture harness.

## Promotion Rule

Candidate scenes must stay out of active suites until they have reviewed candidate artifacts and promoted PNG baselines. Promote into `deep` first, then into `release` only after repeated strict runs are stable.
````

- [ ] **Step 3: Update `smoke-tests/ASSETS.md`**

Add a short paragraph after the active fixture table:

```markdown
Candidate and deferred fixtures are tracked in `smoke-tests/COVERAGE.md` and `smoke-tests/coverage-matrix.json`. Keep this file focused on active smoke-test assets; move a candidate here only after baseline promotion.
```

- [ ] **Step 4: Update `smoke-tests/README.md`**

Add the coverage command to the command block:

```bash
./smoke-tests/run.sh --coverage
```

Add a short coverage section:

```markdown
## Coverage Matrix

`smoke-tests/coverage-matrix.json` tracks active fixtures, candidate fixtures, and harness gaps by renderer limitation. Run `./smoke-tests/run.sh --coverage` before starting a renderer phase to confirm the phase has active or candidate coverage. Candidate coverage does not change quick/deep/release behavior until a scene is reviewed and promoted into `smoke-tests/scenes.json`.
```

- [ ] **Step 5: Update `README.md`**

In the render regression checks section, add:

```bash
./smoke-tests/run.sh --coverage
```

Add:

```markdown
Use the coverage command before renderer phase work to confirm the limitation has an active baseline or candidate fixture. It performs no rendering and does not promote candidates into active smoke suites.
```

---

### Task 6: Probe Local Candidates And Record Results

**Files:**
- Modify: `smoke-tests/coverage-matrix.json`
- Modify: `smoke-tests/COVERAGE.md`
- Modify: `RENDERER_FIXTURE_CANDIDATE_SLICES.md`

- [ ] **Step 1: Confirm each scene candidate exists locally**

Run:

```bash
for id in 1591277437 3301291394 3326873240 2788691565 1576514332; do
  test -f "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/$id/project.json" && printf "present %s\n" "$id" || printf "missing %s\n" "$id"
done
```

Expected on the current workstation: each listed candidate prints `present <id>`. If any candidate prints `missing <id>`, keep it in the matrix with status `candidate` only if the previous scan evidence still exists in `RENDERER_FIXTURE_CANDIDATE_SLICES.md`; otherwise change its status to `missing` and add a note explaining that it needs reinstall before promotion.

- [ ] **Step 2: Validate supported scene-package candidates**

Run:

```bash
for id in 1591277437 3301291394 3326873240 2788691565 1576514332; do
  tools/validate-scene.sh "$id" || printf "validate-scene review needed for %s\n" "$id"
done
```

Expected: the command may return nonzero for candidates whose current renderer output is known incomplete. Nonzero output does not block Phase 1; record the observed status and keep the candidate out of active gates.

- [ ] **Step 3: Record probe status in the matrix notes**

For each coverage entry, append a sentence to `notes`:

```json
"Local probe on 2026-05-27 found the Workshop item installed; validate-scene result recorded in RENDERER_FIXTURE_CANDIDATE_SLICES.md."
```

If validation was nonzero, use:

```json
"Local probe on 2026-05-27 found the Workshop item installed; validate-scene needs review before baseline promotion."
```

- [ ] **Step 4: Update the candidate slices document**

For each candidate section in `RENDERER_FIXTURE_CANDIDATE_SLICES.md`, add:

```markdown
- Phase 1 status: locally present on 2026-05-27; not active in `smoke-tests/scenes.json`; coverage tracked in `smoke-tests/coverage-matrix.json`.
```

For candidates that failed structural validation, use:

```markdown
- Phase 1 status: locally present on 2026-05-27; structural validation needs review before baseline promotion; coverage tracked in `smoke-tests/coverage-matrix.json`.
```

- [ ] **Step 5: Regenerate the coverage snapshot table**

Run:

```bash
./smoke-tests/run.sh --coverage > /tmp/yakkai-coverage.md
```

Copy the generated table into the top of `smoke-tests/COVERAGE.md` while preserving the explanatory sections below it.

---

### Task 7: Full Validation

**Files:**
- All Phase 1 files

- [ ] **Step 1: Run JSON syntax checks**

Run:

```bash
python3 -m json.tool smoke-tests/scenes.json
python3 -m json.tool smoke-tests/coverage-matrix.json
```

Expected: both commands exit `0`.

- [ ] **Step 2: Run unit tests**

Run:

```bash
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
```

Expected: all tests pass.

- [ ] **Step 3: Run coverage report**

Run:

```bash
./smoke-tests/run.sh --coverage
./smoke-tests/run.sh --coverage --coverage-format json
```

Expected: both commands exit `0`; Markdown and JSON outputs list all buckets.

- [ ] **Step 4: Verify active render suites are unchanged**

Run:

```bash
./smoke-tests/run.sh --suite quick --list --keep-shader-cache
./smoke-tests/run.sh --suite deep --list --keep-shader-cache
./smoke-tests/run.sh --suite release --list --keep-shader-cache
```

Expected: each command lists only the current active fixtures:

```text
3228578419 Sleeping Arona required=True
3327063360 Shiroko Night Video required=True
```

- [ ] **Step 5: Run package validation**

Run:

```bash
./scripts/check-package.sh
```

Expected: `Package check passed: wallpapers/io.team7.yakkai`.

- [ ] **Step 6: Run strict release smoke only if candidate metadata edits touched active manifest or runner rendering paths**

Run when needed:

```bash
./smoke-tests/run.sh --suite release --strict --require-assets --artifacts /tmp/yakkai-phase1-release-check
```

Expected: Arona and Shiroko pass. If Phase 1 only touched coverage reporting and docs, this run can be skipped with a note that Phase 0 already locked active render behavior and Phase 1 did not change render execution.

---

## Review Checkpoint

Before merging Phase 1 back to `release/renderer-limitations`, review:

- `smoke-tests/scenes.json` still contains only active baselined scenes.
- `smoke-tests/coverage-matrix.json` contains every known limitation bucket from `RENDERER_LIMITATIONS_PHASE_PLAN.md`.
- `smoke-tests/COVERAGE.md` includes links or IDs for every candidate someone needs to install later.
- `./smoke-tests/run.sh --coverage` exits `0`.
- No candidate artifact directories, generated review clips, logs, or `/tmp` outputs are staged.
