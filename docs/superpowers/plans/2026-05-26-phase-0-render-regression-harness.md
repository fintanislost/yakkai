# Phase 0 Render Regression Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Phase 0 behavior-preserving render regression harness so visual drift is visible before renderer fixes begin.

**Architecture:** Keep `smoke-tests/run.sh` as the stable entry point, but move orchestration into a Python stdlib runner backed by a JSON scene manifest. Extend the scene harness to capture multiple deterministic PNG frames in one process, then compare those PNGs against versioned baselines with ImageMagick metrics while writing logs, diffs, summaries, and optional FFmpeg review clips to artifact directories.

**Tech Stack:** Bash wrapper, Python 3 stdlib, Qt/C++ scene harness, ImageMagick `identify`/`convert`/`compare` or `magick`, optional FFmpeg, existing CMake build.

---

## Scope Rules

- Phase 0 is behavior-preserving. If renderer pixels change, treat that as a regression unless the difference is explained by the new capture/comparison mechanics.
- Do not add Plasma-live capture in this plan.
- Do not modify shader preprocessing, effect policy, blend modes, video decoding policy, SceneScript runtime, model/material/light logic, or QML render behavior except for harness capture plumbing.
- Do not create commits during execution unless the user explicitly asks. Use review checkpoints instead of commit steps.
- Update `README.md`, `smoke-tests/README.md`, and `native/scene_harness/README.md` because this changes workflow and commands.

## File Structure

- Create `smoke-tests/scenes.json`: machine-readable quick/deep/release scene manifest, capture schedules, baseline paths, thresholds, and expectations.
- Create `smoke-tests/ASSETS.md`: human-readable Workshop asset catalog with install links, scene purpose, gates, and visual review notes.
- Create `smoke-tests/runner.py`: Python stdlib orchestration, dependency checks, shader-cache clearing, harness invocation, metrics, result states, artifact writing, optional review clips, and baseline promotion.
- Create `smoke-tests/test_runner.py`: unit tests for manifest parsing, result classification, command selection, artifact path generation, and baseline promotion safety.
- Create `smoke-tests/baselines/<scene-id>/...`: versioned PNG baselines and baseline metadata generated from approved captures.
- Modify `smoke-tests/run.sh`: thin wrapper that delegates to `runner.py` while preserving familiar usage.
- Modify `native/scene_harness/src/main.cpp`: add native multi-capture options while preserving existing `--capture` and `--capture-delay-ms`.
- Modify `native/scene_harness/README.md`: document multi-capture flags and examples.
- Modify `smoke-tests/README.md`: document quick/deep/release gates, result states, artifact layout, baseline update flow, manual review checklist, and asset catalog.
- Modify `README.md`: document the new local render validation workflow at the project level.
- Modify `.gitignore`: ignore generated smoke-test artifacts and review clips while allowing nested baseline PNGs to be tracked.

## Command Contract

The final user-facing commands should be:

```bash
./smoke-tests/run.sh --suite quick
./smoke-tests/run.sh --suite deep
./smoke-tests/run.sh --suite release --strict --require-assets
./smoke-tests/run.sh --suite deep --write-candidates
./smoke-tests/run.sh --promote /tmp/yakkai-smoke/<run-id>
./smoke-tests/run.sh --list
```

`review` results exit zero unless `--strict` is set. `fail` always exits nonzero. `skip` exits zero unless `--require-assets` is set and the skipped scene is required for the selected suite.

---

### Task 1: Add Scene Manifest And Asset Catalog

**Files:**
- Create: `smoke-tests/scenes.json`
- Create: `smoke-tests/ASSETS.md`
- Modify: `.gitignore`
- Test: `python3 -m json.tool smoke-tests/scenes.json`

- [ ] **Step 1: Create the initial manifest**

Add `smoke-tests/scenes.json` with current known scenes and conservative thresholds. Use nested baseline paths so PNGs can be tracked even though root-level `smoke-tests/*.png` is ignored today.

```json
{
  "version": 1,
  "defaults": {
    "backend": "paper",
    "fill": "crop",
    "captureSize": {
      "width": 1280,
      "height": 720
    },
    "thresholds": {
      "rmseReview": 0.015,
      "rmseFail": 0.08,
      "minGrayStddev": 0.01,
      "minUniqueColors": 50,
      "minMotionRmse": 0.002,
      "maxStaticMotionRmse": 0.01
    }
  },
  "paths": {
    "harness": "build/native/scene_harness/yakkai_scene_harness",
    "assets": "${HOME}/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets",
    "workshop": "${HOME}/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960",
    "baselines": "smoke-tests/baselines"
  },
  "scenes": [
    {
      "id": "3228578419",
      "name": "Sleeping Arona",
      "workshopUrl": "https://steamcommunity.com/sharedfiles/filedetails/?id=3228578419",
      "source": "${workshop}/3228578419/scene.pkg",
      "gates": ["quick", "deep", "release"],
      "required": true,
      "features": ["puppet", "flare", "particles", "alpha-sensitive-effects"],
      "captures": [
        {
          "name": "still-8000",
          "timeMs": 8000,
          "baseline": "3228578419/stills/still-8000.png"
        }
      ],
      "sequences": [
        {
          "name": "motion-8000",
          "startMs": 8000,
          "frames": 30,
          "intervalMs": 33,
          "baselineDir": "3228578419/sequences/motion-8000"
        }
      ],
      "expectations": {
        "motion": true,
        "manualReview": ["camera angle", "visible desk/background/character", "lens flare intensity", "halo glow", "sleep particles"]
      }
    },
    {
      "id": "3327063360",
      "name": "Shiroko Night Video",
      "workshopUrl": "https://steamcommunity.com/sharedfiles/filedetails/?id=3327063360",
      "source": "${workshop}/3327063360/scene.pkg",
      "gates": ["quick", "deep", "release"],
      "required": true,
      "features": ["video-texture", "particles", "effect-chain"],
      "captures": [
        {
          "name": "still-20000",
          "timeMs": 20000,
          "baseline": "3327063360/stills/still-20000.png"
        }
      ],
      "sequences": [
        {
          "name": "video-20000",
          "startMs": 20000,
          "frames": 60,
          "intervalMs": 33,
          "baselineDir": "3327063360/sequences/video-20000"
        }
      ],
      "thresholds": {
        "rmseReview": 0.025,
        "rmseFail": 0.12,
        "minMotionRmse": 0.004
      },
      "expectations": {
        "motion": true,
        "manualReview": ["video progression", "green particles", "background composition", "color balance"]
      }
    },
    {
      "id": "3333947217",
      "name": "Arona Flare Candidate",
      "workshopUrl": "https://steamcommunity.com/sharedfiles/filedetails/?id=3333947217",
      "source": "${workshop}/3333947217/scene.pkg",
      "gates": ["deep", "release"],
      "required": false,
      "features": ["flare", "puppet-or-layered-scene", "visual-regression-candidate"],
      "captures": [
        {
          "name": "still-8000",
          "timeMs": 8000,
          "baseline": "3333947217/stills/still-8000.png"
        }
      ],
      "sequences": [
        {
          "name": "motion-8000",
          "startMs": 8000,
          "frames": 45,
          "intervalMs": 33,
          "baselineDir": "3333947217/sequences/motion-8000"
        }
      ],
      "expectations": {
        "motion": true,
        "manualReview": ["flare intensity", "character position", "background tint", "composition"]
      }
    },
    {
      "id": "3329705415",
      "name": "Arona Lens Flare Candidate",
      "workshopUrl": "https://steamcommunity.com/sharedfiles/filedetails/?id=3329705415",
      "source": "${workshop}/3329705415/scene.pkg",
      "gates": ["deep", "release"],
      "required": false,
      "features": ["lens-flare", "effect-intensity-candidate"],
      "captures": [
        {
          "name": "still-8000",
          "timeMs": 8000,
          "baseline": "3329705415/stills/still-8000.png"
        }
      ],
      "sequences": [
        {
          "name": "motion-8000",
          "startMs": 8000,
          "frames": 45,
          "intervalMs": 33,
          "baselineDir": "3329705415/sequences/motion-8000"
        }
      ],
      "thresholds": {
        "rmseReview": 0.02,
        "rmseFail": 0.1
      },
      "expectations": {
        "motion": true,
        "manualReview": ["lens flare strength", "flare placement", "color balance", "composition"]
      }
    }
  ]
}
```

- [ ] **Step 2: Validate the manifest syntax**

Run:

```bash
python3 -m json.tool smoke-tests/scenes.json >/tmp/yakkai-scenes-json.out
```

Expected: command exits `0`.

- [ ] **Step 3: Add the human asset catalog**

Create `smoke-tests/ASSETS.md`:

```markdown
# Smoke Test Assets

These Workshop scenes are used by the local render regression harness. Install them in Wallpaper Engine before running strict deep or release gates.

| Scene ID | Workshop | Gates | Required | Purpose | Visual Review Focus |
| --- | --- | --- | --- | --- | --- |
| 3228578419 | https://steamcommunity.com/sharedfiles/filedetails/?id=3228578419 | quick, deep, release | yes | Puppet, flare, particles, alpha-sensitive composition | camera angle, desk/background/character, lens flare intensity, halo glow, sleep particles |
| 3327063360 | https://steamcommunity.com/sharedfiles/filedetails/?id=3327063360 | quick, deep, release | yes | Main video texture, particles, effect chain | video progression, green particles, background composition, color balance |
| 3333947217 | https://steamcommunity.com/sharedfiles/filedetails/?id=3333947217 | deep, release | no | Arona flare regression candidate | flare intensity, character position, background tint, composition |
| 3329705415 | https://steamcommunity.com/sharedfiles/filedetails/?id=3329705415 | deep, release | no | Lens flare intensity regression candidate | lens flare strength, flare placement, color balance, composition |

Default Steam paths used by the harness:

```bash
~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets
~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960
```

If Steam is installed outside Flatpak, pass `--assets` and `--workshop` to `smoke-tests/run.sh`.
```

- [ ] **Step 4: Update ignored artifact paths**

Modify `.gitignore` so generated runs stay out of git while nested baselines are trackable:

```gitignore
/smoke-tests/artifacts/
/smoke-tests/candidates/
/smoke-tests/*.mp4
/smoke-tests/*.webm
```

Leave the existing `/smoke-tests/*.png` rule in place. Baselines will live under `smoke-tests/baselines/`, so they are not matched by that root-only pattern.

- [ ] **Step 5: Review checkpoint**

Run:

```bash
git diff -- smoke-tests/scenes.json smoke-tests/ASSETS.md .gitignore
```

Expected: only manifest, catalog, and artifact ignore changes are shown.

---

### Task 2: Add Python Runner Core With Unit Tests

**Files:**
- Create: `smoke-tests/runner.py`
- Create: `smoke-tests/test_runner.py`
- Test: `python3 -m unittest discover -s smoke-tests -p 'test_*.py'`

- [ ] **Step 1: Write unit tests for core behavior**

Create `smoke-tests/test_runner.py`:

```python
import json
import tempfile
import unittest
from pathlib import Path

import runner


class RunnerCoreTests(unittest.TestCase):
    def test_expand_path_replaces_home_and_manifest_tokens(self):
        paths = {
            "workshop": "${HOME}/Steam/workshop",
            "baselines": "smoke-tests/baselines",
        }
        expanded = runner.expand_manifest_path("${workshop}/123/scene.pkg", paths, Path("/repo"), {"HOME": "/home/test"})
        self.assertEqual(expanded, Path("/home/test/Steam/workshop/123/scene.pkg"))

    def test_select_scenes_by_suite(self):
        manifest = {
            "scenes": [
                {"id": "a", "gates": ["quick"], "required": True},
                {"id": "b", "gates": ["deep"], "required": False},
            ]
        }
        self.assertEqual([s["id"] for s in runner.select_scenes(manifest, "quick")], ["a"])
        self.assertEqual([s["id"] for s in runner.select_scenes(manifest, "deep")], ["b"])

    def test_classify_rmse_result(self):
        thresholds = {"rmseReview": 0.01, "rmseFail": 0.05}
        self.assertEqual(runner.classify_rmse(0.001, thresholds), "pass")
        self.assertEqual(runner.classify_rmse(0.02, thresholds), "review")
        self.assertEqual(runner.classify_rmse(0.2, thresholds), "fail")

    def test_review_exits_zero_without_strict(self):
        summary = runner.RunSummary()
        summary.add("review")
        self.assertEqual(summary.exit_code(strict=False, require_assets=False), 0)
        self.assertEqual(summary.exit_code(strict=True, require_assets=False), 1)

    def test_required_skip_fails_when_assets_are_required(self):
        summary = runner.RunSummary()
        summary.add("skip", required=True)
        self.assertEqual(summary.exit_code(strict=False, require_assets=False), 0)
        self.assertEqual(summary.exit_code(strict=False, require_assets=True), 1)

    def test_promote_rejects_paths_outside_artifact_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaises(ValueError):
                runner.ensure_relative_to(root / "run", root / "other" / "file.png")

    def test_manifest_loads_valid_json(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "scenes.json"
            path.write_text(json.dumps({"version": 1, "scenes": []}), encoding="utf-8")
            self.assertEqual(runner.load_manifest(path)["version"], 1)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests and confirm they fail because `runner.py` does not exist**

Run:

```bash
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
```

Expected: FAIL with `ModuleNotFoundError: No module named 'runner'`.

- [ ] **Step 3: Add the minimal runner core**

Create `smoke-tests/runner.py` with these initial interfaces:

```python
#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


RESULT_ORDER = {"pass": 0, "review": 1, "skip": 2, "fail": 3}


@dataclass
class RunSummary:
    counts: dict[str, int] = field(default_factory=lambda: {"pass": 0, "review": 0, "skip": 0, "fail": 0})
    required_skips: int = 0

    def add(self, status: str, required: bool = False) -> None:
        if status not in self.counts:
            raise ValueError(f"unknown status: {status}")
        self.counts[status] += 1
        if status == "skip" and required:
            self.required_skips += 1

    def exit_code(self, *, strict: bool, require_assets: bool) -> int:
        if self.counts["fail"] > 0:
            return 1
        if strict and self.counts["review"] > 0:
            return 1
        if require_assets and self.required_skips > 0:
            return 1
        return 0


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def expand_manifest_path(value: str, paths: dict[str, str], root: Path, env: dict[str, str] | None = None) -> Path:
    env = env or os.environ
    expanded = value.replace("${HOME}", env.get("HOME", ""))
    changed = True
    while changed:
        changed = False
        for key, replacement in paths.items():
            token = "${" + key + "}"
            if token in expanded:
                expanded = expanded.replace(token, replacement.replace("${HOME}", env.get("HOME", "")))
                changed = True
    candidate = Path(os.path.expandvars(os.path.expanduser(expanded)))
    if not candidate.is_absolute():
        candidate = root / candidate
    return candidate


def select_scenes(manifest: dict[str, Any], suite: str) -> list[dict[str, Any]]:
    return [scene for scene in manifest.get("scenes", []) if suite in scene.get("gates", [])]


def merged_thresholds(manifest: dict[str, Any], scene: dict[str, Any]) -> dict[str, float]:
    thresholds = dict(manifest.get("defaults", {}).get("thresholds", {}))
    thresholds.update(scene.get("thresholds", {}))
    return thresholds


def classify_rmse(value: float, thresholds: dict[str, float]) -> str:
    if value >= thresholds["rmseFail"]:
        return "fail"
    if value >= thresholds["rmseReview"]:
        return "review"
    return "pass"


def ensure_relative_to(root: Path, path: Path) -> Path:
    resolved_root = root.resolve()
    resolved_path = path.resolve()
    try:
        return resolved_path.relative_to(resolved_root)
    except ValueError as exc:
        raise ValueError(f"{resolved_path} is outside {resolved_root}") from exc


def timestamped_artifact_dir(base: Path) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    path = base / stamp
    path.mkdir(parents=True, exist_ok=False)
    return path


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Yakkai smoke-test render regression runner")
    parser.add_argument("--manifest", default="smoke-tests/scenes.json")
    parser.add_argument("--suite", choices=["quick", "deep", "release"], default="quick")
    parser.add_argument("--artifacts", default="/tmp/yakkai-smoke")
    parser.add_argument("--assets")
    parser.add_argument("--workshop")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--require-assets", action="store_true")
    parser.add_argument("--keep-shader-cache", action="store_true")
    parser.add_argument("--write-candidates", action="store_true")
    parser.add_argument("--promote")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = repo_root()
    manifest = load_manifest((root / args.manifest).resolve())
    scenes = select_scenes(manifest, args.suite)

    if args.list:
        for scene in scenes:
            print(f"{scene['id']} {scene['name']} required={scene.get('required', False)}")
        return 0

    if args.dry_run:
        print(f"DRY-RUN suite={args.suite} scenes={len(scenes)}")
        return 0

    print("runner core is installed; capture/comparison is not enabled in this task slice")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run unit tests**

Run:

```bash
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
```

Expected: PASS for seven tests.

- [ ] **Step 5: Run list and dry-run smoke commands**

Run:

```bash
python3 smoke-tests/runner.py --suite quick --list
python3 smoke-tests/runner.py --suite deep --dry-run
```

Expected: quick list prints `3228578419` and `3327063360`; deep dry-run prints `DRY-RUN suite=deep scenes=4`.

---

### Task 3: Add Native Multi-Capture Support To The Scene Harness

**Files:**
- Modify: `native/scene_harness/src/main.cpp`
- Modify: `native/scene_harness/README.md`
- Test: `cmake --build build --target yakkai_scene_harness`

- [ ] **Step 1: Add capture schedule helpers**

In `native/scene_harness/src/main.cpp`, add these includes:

```cpp
#include <algorithm>
#include <optional>
#include <vector>
```

Inside the anonymous namespace, add:

```cpp
struct CaptureRequest
{
    int timeMs = 0;
    QString fileName;
};

std::optional<int> parseNonNegativeInt(const QString& value)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || parsed < 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::vector<CaptureRequest>> parseCaptureTimes(const QString& value)
{
    std::vector<CaptureRequest> captures;
    const QStringList parts = value.split(',', Qt::SkipEmptyParts);
    for (const QString& rawPart : parts) {
        const QString trimmed = rawPart.trimmed();
        const std::optional<int> parsed = parseNonNegativeInt(trimmed);
        if (!parsed) {
            return std::nullopt;
        }
        captures.push_back(CaptureRequest{*parsed, QStringLiteral("frame-%1ms.png").arg(*parsed, 8, 10, QLatin1Char('0'))});
    }
    std::sort(captures.begin(), captures.end(), [](const CaptureRequest& left, const CaptureRequest& right) {
        return left.timeMs < right.timeMs;
    });
    return captures;
}

std::optional<std::vector<CaptureRequest>> parseCaptureSequence(const QString& value)
{
    const QStringList parts = value.split(':');
    if (parts.size() != 3) {
        return std::nullopt;
    }

    const std::optional<int> startMs = parseNonNegativeInt(parts.at(0).trimmed());
    const std::optional<int> frameCount = parseNonNegativeInt(parts.at(1).trimmed());
    const std::optional<int> intervalMs = parseNonNegativeInt(parts.at(2).trimmed());
    if (!startMs || !frameCount || !intervalMs || *frameCount == 0 || *intervalMs == 0) {
        return std::nullopt;
    }

    std::vector<CaptureRequest> captures;
    captures.reserve(static_cast<size_t>(*frameCount));
    for (int index = 0; index < *frameCount; ++index) {
        const int timeMs = *startMs + index * *intervalMs;
        captures.push_back(CaptureRequest{timeMs, QStringLiteral("frame-%1.png").arg(index, 4, 10, QLatin1Char('0'))});
    }
    return captures;
}

bool saveWindowCapture(QQuickWindow* window, const QString& absolutePath)
{
    const QImage image = window->grabWindow();
    qInfo() << "yakkai_scene_harness: capture size=" << image.size()
            << "devicePixelRatio=" << image.devicePixelRatio()
            << "path=" << absolutePath;
    if (image.isNull()) {
        qWarning() << "yakkai_scene_harness: capture image is null";
        return false;
    }

    QDir().mkpath(QFileInfo(absolutePath).absolutePath());
    if (!image.save(absolutePath)) {
        qWarning() << "yakkai_scene_harness: failed to save capture to" << absolutePath;
        return false;
    }

    return true;
}
```

- [ ] **Step 2: Add command-line options**

Next to the existing capture options, add:

```cpp
    QCommandLineOption captureDirOption(
        QStringList{QStringLiteral("capture-dir")},
        QStringLiteral("Save multiple window captures into the given directory."),
        QStringLiteral("path")
    );
    QCommandLineOption captureTimesOption(
        QStringList{QStringLiteral("capture-times-ms")},
        QStringLiteral("Comma-separated capture timestamps in milliseconds, used with --capture-dir."),
        QStringLiteral("times")
    );
    QCommandLineOption captureSequenceOption(
        QStringList{QStringLiteral("capture-sequence")},
        QStringLiteral("Capture sequence START_MS:FRAME_COUNT:INTERVAL_MS, used with --capture-dir."),
        QStringLiteral("sequence")
    );
```

Register them with:

```cpp
    parser.addOption(captureDirOption);
    parser.addOption(captureTimesOption);
    parser.addOption(captureSequenceOption);
```

- [ ] **Step 3: Parse capture mode and reject invalid combinations**

After existing parser values are read, add:

```cpp
    const QString captureDirPath = parser.value(captureDirOption).trimmed();
    const QString captureTimesValue = parser.value(captureTimesOption).trimmed();
    const QString captureSequenceValue = parser.value(captureSequenceOption).trimmed();
    const bool multiCaptureRequested = !captureDirPath.isEmpty() || !captureTimesValue.isEmpty() || !captureSequenceValue.isEmpty();

    if (!capturePath.isEmpty() && multiCaptureRequested) {
        qWarning() << "yakkai_scene_harness: --capture cannot be combined with --capture-dir, --capture-times-ms, or --capture-sequence";
        return 2;
    }
    if (multiCaptureRequested && captureDirPath.isEmpty()) {
        qWarning() << "yakkai_scene_harness: --capture-dir is required for multi-capture";
        return 2;
    }
    if (!captureTimesValue.isEmpty() && !captureSequenceValue.isEmpty()) {
        qWarning() << "yakkai_scene_harness: use either --capture-times-ms or --capture-sequence, not both";
        return 2;
    }
```

Replace:

```cpp
    if (!capturePath.isEmpty()) {
        app.setQuitOnLastWindowClosed(false);
    }
```

with:

```cpp
    if (!capturePath.isEmpty() || multiCaptureRequested) {
        app.setQuitOnLastWindowClosed(false);
    }
```

- [ ] **Step 4: Schedule single and multi captures**

Replace the existing capture `objectCreated` lambda with one that handles both modes:

```cpp
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app, [&app, capturePath, captureDelayMs, captureDirPath, captureTimesValue, captureSequenceValue](QObject* object, const QUrl&) {
        const bool multiCaptureRequested = !captureDirPath.isEmpty() || !captureTimesValue.isEmpty() || !captureSequenceValue.isEmpty();
        if (capturePath.isEmpty() && !multiCaptureRequested) {
            return;
        }

        auto* quickWindow = qobject_cast<QQuickWindow*>(object);
        if (!quickWindow) {
            qWarning() << "yakkai_scene_harness: capture requested but root object is not a QQuickWindow";
            QCoreApplication::exit(2);
            return;
        }

        QPointer<QQuickWindow> guardedWindow(quickWindow);

        if (!capturePath.isEmpty()) {
            const QString absoluteCapturePath = QFileInfo(capturePath).absoluteFilePath();
            QTimer::singleShot(std::max(captureDelayMs, 0), &app, [guardedWindow, absoluteCapturePath]() {
                if (!guardedWindow) {
                    qWarning() << "yakkai_scene_harness: capture window was destroyed before capture";
                    QCoreApplication::exit(3);
                    return;
                }
                QCoreApplication::exit(saveWindowCapture(guardedWindow, absoluteCapturePath) ? 0 : 5);
            });
            return;
        }

        std::optional<std::vector<CaptureRequest>> parsedCaptures;
        if (!captureTimesValue.isEmpty()) {
            parsedCaptures = parseCaptureTimes(captureTimesValue);
        } else if (!captureSequenceValue.isEmpty()) {
            parsedCaptures = parseCaptureSequence(captureSequenceValue);
        }

        if (!parsedCaptures || parsedCaptures->empty()) {
            qWarning() << "yakkai_scene_harness: invalid multi-capture schedule";
            QCoreApplication::exit(2);
            return;
        }

        const QString absoluteCaptureDir = QFileInfo(captureDirPath).absoluteFilePath();
        QDir().mkpath(absoluteCaptureDir);
        auto remaining = std::make_shared<int>(static_cast<int>(parsedCaptures->size()));
        auto failed = std::make_shared<bool>(false);

        for (const CaptureRequest& capture : *parsedCaptures) {
            const QString absolutePath = QDir(absoluteCaptureDir).filePath(capture.fileName);
            QTimer::singleShot(capture.timeMs, &app, [guardedWindow, absolutePath, remaining, failed]() {
                if (!guardedWindow) {
                    qWarning() << "yakkai_scene_harness: capture window was destroyed before capture";
                    *failed = true;
                } else if (!saveWindowCapture(guardedWindow, absolutePath)) {
                    *failed = true;
                }

                *remaining -= 1;
                if (*remaining == 0) {
                    QCoreApplication::exit(*failed ? 5 : 0);
                }
            });
        }
    });
```

Also add:

```cpp
#include <memory>
```

because the lambda uses `std::shared_ptr`.

- [ ] **Step 5: Build the harness**

Run:

```bash
cmake --build build --target yakkai_scene_harness
```

Expected: build exits `0`.

- [ ] **Step 6: Verify help includes new options**

Run:

```bash
build/native/scene_harness/yakkai_scene_harness --help | grep -E "capture-dir|capture-times-ms|capture-sequence"
```

Expected: all three option names are printed.

- [ ] **Step 7: Update harness README**

Add this to `native/scene_harness/README.md` under useful flags:

```markdown
Capture flags:
- `--capture path --capture-delay-ms ms` keeps the original single-capture behavior.
- `--capture-dir path --capture-times-ms 1000,3000,8000` captures fixed timestamps in one harness process.
- `--capture-dir path --capture-sequence 5000:60:33` captures a sequence starting at 5000ms with 60 frames spaced 33ms apart.

Use multi-capture for render regression tests so timing and scene setup stay inside one process.
```

---

### Task 4: Implement Runner Dependency Checks, Shader Cache Clearing, And Harness Invocation

**Files:**
- Modify: `smoke-tests/runner.py`
- Modify: `smoke-tests/test_runner.py`
- Test: `python3 -m unittest discover -s smoke-tests -p 'test_*.py'`

- [ ] **Step 1: Add tests for dependency selection and shader cache deletion**

Append to `smoke-tests/test_runner.py`:

```python
    def test_select_imagemagick_prefers_magick_when_available(self):
        commands = runner.ImageMagickCommands.from_path({"magick": "/usr/bin/magick"})
        self.assertEqual(commands.identify, ["/usr/bin/magick", "identify"])
        self.assertEqual(commands.compare, ["/usr/bin/magick", "compare"])

    def test_select_imagemagick_uses_legacy_tools(self):
        commands = runner.ImageMagickCommands.from_path({
            "identify": "/usr/bin/identify",
            "convert": "/usr/bin/convert",
            "compare": "/usr/bin/compare",
        })
        self.assertEqual(commands.identify, ["/usr/bin/identify"])
        self.assertEqual(commands.convert, ["/usr/bin/convert"])
        self.assertEqual(commands.compare, ["/usr/bin/compare"])

    def test_clear_shader_cache_removes_spvs01_dirs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stale = root / "abc" / "spvs01"
            stale.mkdir(parents=True)
            (stale / "shader.spv").write_text("stale", encoding="utf-8")
            removed = runner.clear_shader_cache(root)
            self.assertEqual(removed, [stale])
            self.assertFalse(stale.exists())
```

- [ ] **Step 2: Implement dependency and cache helpers**

Add to `smoke-tests/runner.py`:

```python
@dataclass(frozen=True)
class ImageMagickCommands:
    identify: list[str]
    convert: list[str]
    compare: list[str]

    @staticmethod
    def from_path(found: dict[str, str]) -> "ImageMagickCommands":
        if "magick" in found:
            magick = found["magick"]
            return ImageMagickCommands(
                identify=[magick, "identify"],
                convert=[magick],
                compare=[magick, "compare"],
            )
        missing = [name for name in ("identify", "convert", "compare") if name not in found]
        if missing:
            raise RuntimeError("missing ImageMagick commands: " + ", ".join(missing))
        return ImageMagickCommands(
            identify=[found["identify"]],
            convert=[found["convert"]],
            compare=[found["compare"]],
        )


def find_commands(names: list[str]) -> dict[str, str]:
    return {name: path for name in names if (path := shutil.which(name))}


def require_imagemagick() -> ImageMagickCommands:
    found = find_commands(["magick", "identify", "convert", "compare"])
    try:
        return ImageMagickCommands.from_path(found)
    except RuntimeError as exc:
        raise RuntimeError(
            f"{exc}. Install ImageMagick; the render visual gate requires PNG metric comparison."
        ) from exc


def find_ffmpeg() -> str | None:
    return shutil.which("ffmpeg")


def clear_shader_cache(cache_root: Path) -> list[Path]:
    removed: list[Path] = []
    if not cache_root.exists():
        return removed
    for path in cache_root.glob("*/spvs01"):
        if path.is_dir():
            shutil.rmtree(path)
            removed.append(path)
    return removed
```

- [ ] **Step 3: Add harness command building**

Add:

```python
def build_harness_base_command(harness: Path, scene: dict[str, Any], source: Path, assets: Path) -> list[str]:
    return [
        str(harness),
        "--backend",
        scene.get("backend", "paper"),
        "--source",
        str(source),
        "--assets",
        str(assets),
        "--fill",
        scene.get("fill", "crop"),
    ]


def run_command(command: list[str], log_path: Path, timeout_seconds: int) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", errors="replace") as log:
        completed = subprocess.run(
            command,
            stdout=log,
            stderr=subprocess.STDOUT,
            timeout=timeout_seconds,
            check=False,
        )
    return completed.returncode
```

- [ ] **Step 4: Wire dependencies and shader cache into `main`**

In `main`, before running scenes:

```python
    try:
        imagemagick = require_imagemagick()
    except RuntimeError as exc:
        print(f"FAIL dependency: {exc}", file=sys.stderr)
        return 1

    ffmpeg = find_ffmpeg()
    if ffmpeg is None:
        print("WARN dependency: ffmpeg not found; review clips will be skipped", file=sys.stderr)

    if not args.keep_shader_cache:
        removed = clear_shader_cache(Path.home() / ".cache" / "wescene-renderer")
        print(f"Cleared shader cache entries: {len(removed)}")
```

Keep `imagemagick` assigned even before metrics are implemented so later tasks use the same dependency flow. Print the object in dry-run mode to avoid unused-variable churn:

```python
    if args.dry_run:
        print(f"DRY-RUN suite={args.suite} scenes={len(scenes)} imagemagick={imagemagick.identify[0]}")
        return 0
```

- [ ] **Step 5: Run tests**

Run:

```bash
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
```

Expected: PASS.

- [ ] **Step 6: Run dry-run**

Run:

```bash
python3 smoke-tests/runner.py --suite quick --dry-run
```

Expected: prints the selected suite and ImageMagick command path. If ImageMagick is not installed, it exits `1` with install guidance.

---

### Task 5: Implement PNG Capture, Metrics, Diffs, And Result Classification

**Files:**
- Modify: `smoke-tests/runner.py`
- Modify: `smoke-tests/test_runner.py`
- Test: `python3 -m unittest discover -s smoke-tests -p 'test_*.py'`

- [ ] **Step 1: Add tests for RMSE parsing and motion classification**

Append to `smoke-tests/test_runner.py`:

```python
    def test_parse_imagemagick_rmse_normalized_value(self):
        self.assertAlmostEqual(runner.parse_rmse("1922.4 (0.029334)"), 0.029334)
        self.assertAlmostEqual(runner.parse_rmse("0 (0)"), 0.0)

    def test_motion_status_requires_minimum_motion(self):
        thresholds = {"minMotionRmse": 0.004, "maxStaticMotionRmse": 0.01}
        self.assertEqual(runner.classify_motion([0.006, 0.005], True, thresholds), "pass")
        self.assertEqual(runner.classify_motion([0.001, 0.001], True, thresholds), "fail")
        self.assertEqual(runner.classify_motion([0.02, 0.001], False, thresholds), "fail")
        self.assertEqual(runner.classify_motion([0.002, 0.001], False, thresholds), "pass")

    def test_log_scan_fails_shader_and_material_errors(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "harness.log"
            path.write_text("shader compile failed\nmaterial faild\n", encoding="utf-8")
            failures = runner.scan_log_failures(path)
            self.assertIn("shader compile failed", failures)
            self.assertIn("material faild", failures)
```

- [ ] **Step 2: Add metric helpers**

Add to `smoke-tests/runner.py`:

```python
def parse_rmse(output: str) -> float:
    start = output.rfind("(")
    end = output.rfind(")")
    if start == -1 or end == -1 or end <= start:
        raise ValueError(f"could not parse RMSE output: {output!r}")
    return float(output[start + 1:end])


def classify_motion(values: list[float], expected_motion: bool, thresholds: dict[str, float]) -> str:
    if not values:
        return "pass"
    largest = max(values)
    if expected_motion:
        return "pass" if largest >= thresholds["minMotionRmse"] else "fail"
    return "fail" if largest > thresholds["maxStaticMotionRmse"] else "pass"


def read_text_command(command: list[str]) -> str:
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError((completed.stderr or completed.stdout).strip())
    return completed.stdout.strip()


def image_dimensions(commands: ImageMagickCommands, image: Path) -> str:
    return read_text_command(commands.identify + ["-format", "%wx%h", str(image)])


def gray_stddev(commands: ImageMagickCommands, image: Path) -> float:
    return float(read_text_command(commands.convert + [str(image), "-colorspace", "Gray", "-format", "%[fx:standard_deviation]", "info:"]))


def unique_colors(commands: ImageMagickCommands, image: Path) -> int:
    return int(read_text_command(commands.convert + [str(image), "-resize", "100x100!", "-unique-colors", "-format", "%k", "info:"]))


def compare_rmse(commands: ImageMagickCommands, expected: Path, actual: Path, diff: Path | None = None) -> float:
    command = commands.compare + ["-metric", "RMSE", str(expected), str(actual)]
    if diff is None:
        command.append("null:")
    else:
        diff.parent.mkdir(parents=True, exist_ok=True)
        command.append(str(diff))
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    output = (completed.stderr or completed.stdout).strip()
    if completed.returncode not in (0, 1):
        raise RuntimeError(output)
    return parse_rmse(output)


def scan_log_failures(log_path: Path | None) -> list[str]:
    if log_path is None or not log_path.exists():
        return []
    text = log_path.read_text(encoding="utf-8", errors="replace")
    patterns = [
        "shader compile failed",
        "material faild",
        "failed to load",
        "capture image is null",
        "objectCreationFailed",
    ]
    return [pattern for pattern in patterns if pattern in text]
```

- [ ] **Step 3: Add scene execution data structures**

Add:

```python
@dataclass
class FrameResult:
    name: str
    status: str
    actual: str
    baseline: str | None
    diff: str | None
    metrics: dict[str, Any]


@dataclass
class SceneResult:
    id: str
    name: str
    status: str
    required: bool
    log: str | None
    frames: list[FrameResult] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)
```

- [ ] **Step 4: Implement capture execution**

Add:

```python
def run_scene_captures(
    *,
    manifest: dict[str, Any],
    scene: dict[str, Any],
    root: Path,
    run_dir: Path,
    assets_override: str | None,
    workshop_override: str | None,
) -> tuple[str, Path | None, list[Path], list[str]]:
    paths = dict(manifest["paths"])
    if assets_override:
        paths["assets"] = assets_override
    if workshop_override:
        paths["workshop"] = workshop_override

    harness = expand_manifest_path(paths["harness"], paths, root)
    assets = expand_manifest_path(paths["assets"], paths, root)
    source = expand_manifest_path(scene["source"], paths, root)
    scene_dir = run_dir / scene["id"]
    capture_dir = scene_dir / "captures"
    log_path = scene_dir / "harness.log"
    notes: list[str] = []

    if not harness.exists():
        return "fail", None, [], [f"harness not found: {harness}"]
    if not source.exists():
        return "skip", None, [], [f"scene not installed: {source}"]
    if not assets.exists():
        return "fail", None, [], [f"assets directory not found: {assets}"]

    times = [str(capture["timeMs"]) for capture in scene.get("captures", [])]
    command = build_harness_base_command(harness, scene, source, assets)
    if times:
        command += ["--capture-dir", str(capture_dir / "stills"), "--capture-times-ms", ",".join(times)]
        timeout = max(int(value) for value in times) // 1000 + 30
        code = run_command(command, log_path, timeout)
        if code != 0:
            return "fail", log_path, [], [f"harness still capture exited {code}"]

    actuals = sorted((capture_dir / "stills").glob("*.png")) if (capture_dir / "stills").exists() else []

    for sequence in scene.get("sequences", []):
        sequence_dir = capture_dir / sequence["name"]
        sequence_arg = f"{sequence['startMs']}:{sequence['frames']}:{sequence['intervalMs']}"
        command = build_harness_base_command(harness, scene, source, assets)
        command += ["--capture-dir", str(sequence_dir), "--capture-sequence", sequence_arg]
        timeout = (sequence["startMs"] + sequence["frames"] * sequence["intervalMs"]) // 1000 + 30
        code = run_command(command, scene_dir / f"{sequence['name']}.log", timeout)
        if code != 0:
            return "fail", log_path, actuals, [f"harness sequence {sequence['name']} exited {code}"]
        actuals.extend(sorted(sequence_dir.glob("*.png")))

    return "pass", log_path, actuals, notes
```

- [ ] **Step 5: Implement frame evaluation**

Add:

```python
def evaluate_png(
    *,
    commands: ImageMagickCommands,
    actual: Path,
    baseline: Path | None,
    diff: Path | None,
    thresholds: dict[str, float],
) -> FrameResult:
    metrics: dict[str, Any] = {
        "dimensions": image_dimensions(commands, actual),
        "grayStddev": gray_stddev(commands, actual),
        "uniqueColors100x100": unique_colors(commands, actual),
    }
    status = "pass"
    if metrics["grayStddev"] < thresholds["minGrayStddev"]:
        status = "fail"
    if metrics["uniqueColors100x100"] < thresholds["minUniqueColors"]:
        status = "fail"
    if baseline and baseline.exists():
        rmse = compare_rmse(commands, baseline, actual, diff)
        metrics["rmse"] = rmse
        status = max(status, classify_rmse(rmse, thresholds), key=lambda value: RESULT_ORDER[value])
    elif baseline:
        status = max(status, "review", key=lambda value: RESULT_ORDER[value])
        metrics["baselineMissing"] = True

    return FrameResult(
        name=actual.stem,
        status=status,
        actual=str(actual),
        baseline=str(baseline) if baseline else None,
        diff=str(diff) if diff else None,
        metrics=metrics,
    )
```

- [ ] **Step 6: Wire scene execution into `main`**

Replace the current non-dry-run status message in `main` with:

```python
    run_dir = timestamped_artifact_dir(Path(args.artifacts))
    summary = RunSummary()
    scene_results: list[SceneResult] = []

    for scene in scenes:
        thresholds = merged_thresholds(manifest, scene)
        capture_status, log_path, actuals, notes = run_scene_captures(
            manifest=manifest,
            scene=scene,
            root=root,
            run_dir=run_dir,
            assets_override=args.assets,
            workshop_override=args.workshop,
        )
        required = bool(scene.get("required", False))
        if capture_status in ("skip", "fail"):
            summary.add(capture_status, required=required)
            scene_results.append(SceneResult(scene["id"], scene["name"], capture_status, required, str(log_path) if log_path else None, notes=notes))
            print(f"{capture_status.upper()} {scene['id']} {scene['name']}: {'; '.join(notes)}")
            continue

        paths = dict(manifest["paths"])
        baseline_root = expand_manifest_path(paths["baselines"], paths, root)
        scene_frames: list[FrameResult] = []
        scene_status = "pass"
        diffs_dir = run_dir / scene["id"] / "diffs"

        for actual in actuals:
            baseline = None
            if "/stills/" in actual.as_posix():
                capture = next((item for item in scene.get("captures", []) if str(item["timeMs"]) in actual.name), None)
                baseline = baseline_root / capture["baseline"] if capture else None
            else:
                sequence = next((item for item in scene.get("sequences", []) if item["name"] in actual.as_posix()), None)
                baseline = baseline_root / sequence["baselineDir"] / actual.name if sequence else None
            diff = diffs_dir / actual.name if baseline else None
            frame = evaluate_png(commands=imagemagick, actual=actual, baseline=baseline, diff=diff, thresholds=thresholds)
            scene_frames.append(frame)
            scene_status = max(scene_status, frame.status, key=lambda value: RESULT_ORDER[value])

        log_failures = scan_log_failures(log_path)
        if log_failures:
            scene_status = "fail"
            notes.extend(f"log failure: {failure}" for failure in log_failures)

        for sequence in scene.get("sequences", []):
            sequence_dir = run_dir / scene["id"] / "captures" / sequence["name"]
            sequence_frames = sorted(sequence_dir.glob("frame-*.png"))
            motion_values = [
                compare_rmse(imagemagick, left, right)
                for left, right in zip(sequence_frames, sequence_frames[1:])
            ]
            motion_status = classify_motion(
                motion_values,
                bool(scene.get("expectations", {}).get("motion", False)),
                thresholds,
            )
            if motion_status == "fail":
                scene_status = "fail"
                notes.append(f"motion expectation failed for {sequence['name']}: max adjacent RMSE={max(motion_values) if motion_values else 0}")

        summary.add(scene_status, required=required)
        scene_results.append(SceneResult(scene["id"], scene["name"], scene_status, required, str(log_path) if log_path else None, frames=scene_frames, notes=notes))
        print(f"{scene_status.upper()} {scene['id']} {scene['name']} frames={len(scene_frames)}")

    output = {
        "suite": args.suite,
        "strict": args.strict,
        "requireAssets": args.require_assets,
        "writeCandidates": args.write_candidates,
        "counts": summary.counts,
        "scenes": [dataclass_to_json(scene) for scene in scene_results],
    }
    (run_dir / "summary.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
    print(f"Artifacts: {run_dir}")
    return summary.exit_code(strict=args.strict, require_assets=args.require_assets)
```

Add the JSON helper above `main`:

```python
def dataclass_to_json(value: Any) -> Any:
    if hasattr(value, "__dataclass_fields__"):
        return {field_name: dataclass_to_json(getattr(value, field_name)) for field_name in value.__dataclass_fields__}
    if isinstance(value, list):
        return [dataclass_to_json(item) for item in value]
    if isinstance(value, dict):
        return {key: dataclass_to_json(item) for key, item in value.items()}
    return value
```

- [ ] **Step 7: Run unit tests**

Run:

```bash
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
```

Expected: PASS.

- [ ] **Step 8: Run quick suite locally**

Run:

```bash
./smoke-tests/run.sh --suite quick
```

Expected with installed assets: `PASS` or `REVIEW` lines and an artifact directory. Expected without installed assets: `SKIP` lines and exit `0`.

---

### Task 6: Add Optional Review Clips And Two-Step Baseline Promotion

**Files:**
- Modify: `smoke-tests/runner.py`
- Modify: `smoke-tests/test_runner.py`
- Test: `python3 -m unittest discover -s smoke-tests -p 'test_*.py'`

- [ ] **Step 1: Add baseline promotion tests**

Append to `smoke-tests/test_runner.py`:

```python
    def test_promote_copies_only_candidate_pngs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run_dir = root / "run"
            baseline_root = root / "baselines"
            candidate = run_dir / "333" / "captures" / "stills" / "frame-00008000ms.png"
            candidate.parent.mkdir(parents=True)
            candidate.write_bytes(b"png")
            summary = {
                "writeCandidates": True,
                "scenes": [
                    {
                        "id": "333",
                        "frames": [
                            {
                                "actual": str(candidate),
                                "baseline": str(baseline_root / "333" / "stills" / "still-8000.png"),
                            }
                        ],
                    }
                ]
            }
            summary_path = run_dir / "summary.json"
            summary_path.write_text(json.dumps(summary), encoding="utf-8")
            promoted = runner.promote_baselines(run_dir, baseline_root)
            self.assertEqual(promoted, [baseline_root / "333" / "stills" / "still-8000.png"])
            self.assertEqual((baseline_root / "333" / "stills" / "still-8000.png").read_bytes(), b"png")
```

- [ ] **Step 2: Implement optional review clip generation**

Add to `smoke-tests/runner.py`:

```python
def write_review_clip(ffmpeg: str | None, frames_dir: Path, output: Path, fps: int = 30) -> str | None:
    if ffmpeg is None:
        return None
    frames = sorted(frames_dir.glob("frame-*.png"))
    if not frames:
        return None
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        ffmpeg,
        "-y",
        "-framerate",
        str(fps),
        "-pattern_type",
        "glob",
        "-i",
        str(frames_dir / "frame-*.png"),
        "-pix_fmt",
        "yuv420p",
        str(output),
    ]
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    if completed.returncode != 0:
        return None
    return str(output)
```

After each scene is evaluated in `main`, call `write_review_clip` for each sequence directory:

```python
        for sequence in scene.get("sequences", []):
            frames_dir = run_dir / scene["id"] / "captures" / sequence["name"]
            clip = write_review_clip(ffmpeg, frames_dir, run_dir / scene["id"] / "review-clips" / f"{sequence['name']}.mp4")
            if clip:
                print(f"  review clip: {clip}")
```

- [ ] **Step 3: Implement baseline promotion**

Add:

```python
def promote_baselines(run_dir: Path, baseline_root: Path) -> list[Path]:
    summary_path = run_dir / "summary.json"
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    if not summary.get("writeCandidates", False):
        raise ValueError(f"{run_dir} was not created with --write-candidates")
    promoted: list[Path] = []
    for scene in summary.get("scenes", []):
        for frame in scene.get("frames", []):
            actual = Path(frame["actual"])
            baseline_text = frame.get("baseline")
            if not baseline_text:
                continue
            ensure_relative_to(run_dir, actual)
            baseline = Path(baseline_text)
            if not baseline.is_absolute():
                baseline = baseline_root / baseline
            baseline.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(actual, baseline)
            promoted.append(baseline)
    return promoted
```

In `main`, before dependency checks, handle promotion:

```python
    if args.promote:
        paths = manifest["paths"]
        baseline_root = expand_manifest_path(paths["baselines"], paths, root)
        promoted = promote_baselines(Path(args.promote), baseline_root)
        for path in promoted:
            print(f"PROMOTED {path}")
        return 0
```

- [ ] **Step 4: Run tests**

Run:

```bash
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
```

Expected: PASS.

- [ ] **Step 5: Generate candidates, review, and promote baselines**

Run:

```bash
./smoke-tests/run.sh --suite quick --write-candidates
```

Expected with assets installed: artifact directory contains captures, diffs where baselines already exist, `summary.json`, logs, and review clips if FFmpeg is installed.

After manual review, promote the run:

```bash
./smoke-tests/run.sh --promote /tmp/yakkai-smoke/<run-id>
```

Expected: `PROMOTED` lines for baseline PNGs.

---

### Task 7: Replace Shell Logic With Stable Wrapper And Update Documentation

**Files:**
- Modify: `smoke-tests/run.sh`
- Modify: `smoke-tests/README.md`
- Modify: `native/scene_harness/README.md`
- Modify: `README.md`
- Test: `./smoke-tests/run.sh --suite quick --dry-run`

- [ ] **Step 1: Replace `smoke-tests/run.sh` with a wrapper**

Use:

```bash
#!/bin/bash
# Stable entry point for Yakkai local render regression checks.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$ROOT/smoke-tests/runner.py" "$@"
```

- [ ] **Step 2: Verify wrapper dry-run**

Run:

```bash
./smoke-tests/run.sh --suite quick --dry-run
```

Expected: dry-run output from `runner.py`.

- [ ] **Step 3: Rewrite smoke-test README**

Update `smoke-tests/README.md` to include:

```markdown
# Smoke Tests

The smoke-test harness is the local visual regression gate for Wallpaper Engine scene rendering.

## Commands

```bash
./smoke-tests/run.sh --suite quick
./smoke-tests/run.sh --suite deep
./smoke-tests/run.sh --suite release --strict --require-assets
./smoke-tests/run.sh --suite deep --write-candidates
./smoke-tests/run.sh --promote /tmp/yakkai-smoke/<run-id>
./smoke-tests/run.sh --list
```

## Result States

- `pass`: structural checks and visual metrics are within thresholds.
- `review`: visual drift exceeded the warning threshold but not the hard-fail threshold. The command exits zero unless `--strict` is set.
- `fail`: blank frames, missing expected motion, structural errors, shader/material errors, required dependency failures, or large visual drift.
- `skip`: local Workshop assets are missing. Skips exit zero unless `--require-assets` is set for required scenes.

## Baselines

PNG frames under `smoke-tests/baselines/` are the versioned comparison source of truth. Generated MP4/WebM clips, diffs, logs, and summaries are artifacts and stay out of git.

Baseline updates are two-step:

```bash
./smoke-tests/run.sh --suite deep --write-candidates
./smoke-tests/run.sh --promote /tmp/yakkai-smoke/<run-id>
```

Review the artifact bundle before promotion. Check camera angle, visible elements, character position, color balance, composition, effect intensity, and motion.

## Assets

See `smoke-tests/ASSETS.md` for Workshop links and scene purpose.
```

- [ ] **Step 4: Update project README**

Add a concise render validation section to `README.md`:

```markdown
### Render Regression Checks

Use the local smoke-test harness before renderer-risk changes and before releases:

```bash
./smoke-tests/run.sh --suite quick
./smoke-tests/run.sh --suite deep
./smoke-tests/run.sh --suite release --strict --require-assets
```

The quick suite is for normal development loops. The deep suite is required for changes affecting pixels, scene loading, animation timing, video textures, shader preprocessing, effect policy, blend/composition, model/material/light behavior, SceneScript, QML render plumbing, harness capture behavior, or validator behavior.

The harness compares deterministic PNG captures against versioned baselines and writes review artifacts to `/tmp/yakkai-smoke`. Generated review videos are for human inspection only; PNG frames remain the source of truth.
```

- [ ] **Step 5: Update harness README**

Ensure `native/scene_harness/README.md` documents all capture modes from Task 3 and notes that multi-capture is used by the smoke-test runner.

- [ ] **Step 6: Run documentation grep**

Run:

```bash
rg -n "smoke-tests/run.sh|baseline|review clip|capture-sequence|ASSETS.md" README.md smoke-tests/README.md native/scene_harness/README.md
```

Expected: all new workflow terms appear in the relevant docs.

---

### Task 8: Final Local Validation And Review Checkpoint

**Files:**
- Read: `RENDERER_LIMITATIONS_PHASE_PLAN.md`
- Read: `README.md`
- Read: `smoke-tests/README.md`
- Read: `smoke-tests/scenes.json`
- Test: all commands below

- [ ] **Step 1: Run Python unit tests**

Run:

```bash
python3 -m unittest discover -s smoke-tests -p 'test_*.py'
```

Expected: PASS.

- [ ] **Step 2: Build the harness**

Run:

```bash
cmake --build build --target yakkai_scene_harness
```

Expected: build exits `0`.

- [ ] **Step 3: Run wrapper dry-run**

Run:

```bash
./smoke-tests/run.sh --suite quick --dry-run
```

Expected: command exits `0`.

- [ ] **Step 4: Run quick smoke suite**

Run:

```bash
./smoke-tests/run.sh --suite quick
```

Expected with assets installed: scenes produce `pass` or `review`, artifact directory is printed, and missing baselines produce `review` until promoted. Expected without assets: required scenes are `skip`, command exits `0`.

- [ ] **Step 5: Run strict quick suite after baselines exist**

After baselines are promoted, run:

```bash
./smoke-tests/run.sh --suite quick --strict --require-assets
```

Expected on a fully prepared dev machine: command exits `0` with no `review`, `fail`, or required `skip` results.

- [ ] **Step 6: Run package workflow smoke checks**

Run:

```bash
./scripts/install-local.sh --dev --no-install
./scripts/check-package.sh
```

Expected: both commands exit `0`.

- [ ] **Step 7: Review git status**

Run:

```bash
git status --short
```

Expected: implementation files, docs, manifest, and approved baseline PNGs are modified or added. Generated artifacts under `/tmp/yakkai-smoke` and ignored smoke artifact paths do not appear.

- [ ] **Step 8: Human visual review**

Open the artifact directory from the final smoke run and inspect:

- side-by-side actual and baseline PNGs
- generated diff PNGs
- MP4/WebM review clips when FFmpeg is available
- `summary.json`
- harness logs

Review camera angle, visible elements, character position, color balance, composition, effect intensity, and motion. Ask the user about any scene where visual intent is ambiguous.

## Self-Review Checklist

- The plan implements every approved Phase 0 stress-test decision.
- The wrapper keeps `smoke-tests/run.sh` as the stable command.
- The scene harness preserves existing single-capture behavior.
- Real render comparisons use PNG baselines, not encoded video.
- Review clips are optional artifacts and are not required for pass/fail.
- Missing ImageMagick fails with guidance; missing FFmpeg only warns.
- Missing Workshop scenes are skips unless strict release execution requires assets.
- Shader cache clearing is default behavior.
- Plasma-live capture remains out of scope.
- README files are updated because workflow and commands change.
- No commit step is included because repository instructions require an explicit user request before committing.
