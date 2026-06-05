#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SMOKE_TESTS = ROOT / "smoke-tests"
if str(SMOKE_TESTS) not in sys.path:
    sys.path.insert(0, str(SMOKE_TESTS))
import runner as smoke_runner


@dataclass(frozen=True)
class AronaVariant:
    name: str
    label: str
    timeofday: str
    reference_relative_path: Path


@dataclass(frozen=True)
class CompareConfig:
    repo_root: Path
    reference_root: Path
    output_root: Path
    capture_delay_ms: int = 8000
    width: int = 1280
    height: int = 720
    debug_effect_captures: bool = False
    debug_effect_probe_layers: tuple[int, ...] = ()
    debug_effect_probe_channelmap_slots: tuple[int, ...] = ()
    debug_effect_probe_max_effects: int | None = None
    debug_puppet_animation_layer_overrides: str = ""
    register_comparison: bool = True


@dataclass(frozen=True)
class RegistrationCandidate:
    scale: float
    offset_x: int
    offset_y: int


@dataclass(frozen=True)
class RegistrationResult:
    candidate: RegistrationCandidate
    rmse: float
    structure_rmse: float
    yakkai: Path
    diff: Path
    structure_reference: Path
    structure_yakkai: Path
    structure_diff: Path


@dataclass(frozen=True)
class VariantResult:
    name: str
    status: str
    reference: str
    yakkai: str | None = None
    normalizedReference: str | None = None
    diff: str | None = None
    registeredYakkai: str | None = None
    registeredDiff: str | None = None
    structureReference: str | None = None
    registeredStructureYakkai: str | None = None
    registeredStructureDiff: str | None = None
    log: str | None = None
    effectManifest: str | None = None
    metrics: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class CompareResult:
    status: str
    message: str
    outputDir: str | None = None
    contactSheet: str | None = None
    summary: str | None = None
    registeredContactSheet: str | None = None
    variants: list[VariantResult] = field(default_factory=list)


VARIANTS: tuple[AronaVariant, ...] = (
    AronaVariant("day", "Day", "1", Path("day/still.png")),
    AronaVariant("sunset", "Sunset", "2", Path("sunset/still.png")),
    AronaVariant("night", "Night", "3", Path("night/still.png")),
)

RESULT_ORDER = {"pass": 0, "review": 1, "skip": 2, "fail": 3}

REGISTRATION_SCALES: tuple[float, ...] = (0.92, 0.96, 1.0, 1.04, 1.08, 1.12)
REGISTRATION_OFFSET_FRACTIONS: tuple[float, ...] = (-0.025, 0.0, 0.025)
REGISTRATION_STRUCTURE_METRIC = "grayscale-contrast-320x180"


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")


def scene_properties_json(variant: AronaVariant, source: Path) -> str:
    scene = {
        "id": f"3228578419-{variant.name}",
        "scenePropertyOverrides": {"timeofday": variant.timeofday},
    }
    result = smoke_runner.scene_properties_json_for_scene(scene, source)
    if result is None:
        return "{}"
    return result


def parse_probe_layer_ids(value: str) -> tuple[int, ...]:
    value = value.strip()
    if not value:
        return ()
    ids: list[int] = []
    for raw in value.split(","):
        token = raw.strip()
        if not token:
            raise ValueError("empty probe layer id")
        try:
            layer_id = int(token)
        except ValueError as exc:
            raise ValueError(f"invalid probe layer id: {token}") from exc
        if layer_id <= 0:
            raise ValueError(f"invalid probe layer id: {token}")
        if layer_id not in ids:
            ids.append(layer_id)
    return tuple(ids)


def parse_probe_channelmap_slots(value: str) -> tuple[int, ...]:
    value = value.strip()
    if not value:
        return ()
    slots: list[int] = []
    for raw in value.split(","):
        token = raw.strip()
        if not token:
            raise ValueError("empty probe channelmap slot")
        try:
            slot = int(token)
        except ValueError as exc:
            raise ValueError(f"invalid probe channelmap slot: {token}") from exc
        if slot < 0 or slot > 63:
            raise ValueError(f"invalid probe channelmap slot: {token}")
        if slot not in slots:
            slots.append(slot)
    return tuple(slots)


def format_probe_layer_ids(layer_ids: tuple[int, ...]) -> str:
    return ",".join(str(layer_id) for layer_id in layer_ids)


def worst_status(left: str, right: str) -> str:
    if left not in RESULT_ORDER:
        raise ValueError(f"unknown status: {left}")
    if right not in RESULT_ORDER:
        raise ValueError(f"unknown status: {right}")
    if RESULT_ORDER[left] >= RESULT_ORDER[right]:
        return left
    return right


def run_checked(command: list[str]) -> str:
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    if completed.returncode != 0:
        command_text = shlex.join(str(part) for part in command)
        stderr = completed.stderr.strip()
        stdout = completed.stdout.strip()
        details = [f"command failed ({completed.returncode}): {command_text}"]
        if stderr:
            details.append(f"stderr: {stderr}")
        if stdout:
            details.append(f"stdout: {stdout}")
        raise RuntimeError("\n".join(details))
    return completed.stdout.strip()


def rgb_means(commands: smoke_runner.ImageMagickCommands, image: Path) -> list[float]:
    output = run_checked(
        commands.convert
        + [
            str(image),
            "-format",
            "%[fx:mean.r] %[fx:mean.g] %[fx:mean.b]",
            "info:",
        ]
    )
    parts = output.split()
    if len(parts) != 3:
        raise RuntimeError(f"could not parse RGB means from {image}: {output!r}")
    try:
        return [float(part) for part in parts]
    except ValueError as exc:
        raise RuntimeError(f"could not parse RGB means from {image}: {output!r}") from exc


def normalize_reference(
    commands: smoke_runner.ImageMagickCommands,
    reference: Path,
    normalized: Path,
    dimensions: str,
) -> None:
    normalized.parent.mkdir(parents=True, exist_ok=True)
    run_checked(
        commands.convert
        + [
            str(reference),
            "-resize",
            f"{dimensions}^",
            "-gravity",
            "center",
            "-extent",
            dimensions,
            str(normalized),
        ]
    )


def registration_candidates(width: int, height: int) -> tuple[RegistrationCandidate, ...]:
    x_offsets = sorted({round(width * fraction) for fraction in REGISTRATION_OFFSET_FRACTIONS})
    y_offsets = sorted({round(height * fraction) for fraction in REGISTRATION_OFFSET_FRACTIONS})
    return tuple(
        RegistrationCandidate(scale=scale, offset_x=offset_x, offset_y=offset_y)
        for scale in REGISTRATION_SCALES
        for offset_x in x_offsets
        for offset_y in y_offsets
    )


def registration_structure_dimensions(width: int, height: int) -> str:
    structure_width = max(1, width // 4)
    structure_height = max(1, height // 4)
    return f"{structure_width}x{structure_height}!"


def build_registration_structure_image(
    commands: smoke_runner.ImageMagickCommands,
    source: Path,
    output: Path,
    width: int,
    height: int,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    run_checked(
        commands.convert
        + [
            str(source),
            "-background",
            "black",
            "-alpha",
            "remove",
            "-alpha",
            "off",
            "-colorspace",
            "Gray",
            "-resize",
            registration_structure_dimensions(width, height),
            "-auto-level",
            "-contrast-stretch",
            "1%x1%",
            str(output),
        ]
    )


def transform_yakkai_for_registration(
    commands: smoke_runner.ImageMagickCommands,
    source: Path,
    output: Path,
    width: int,
    height: int,
    candidate: RegistrationCandidate,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    scaled_width = max(1, round(width * candidate.scale))
    scaled_height = max(1, round(height * candidate.scale))
    run_checked(
        commands.convert
        + [
            str(source),
            "-resize",
            f"{scaled_width}x{scaled_height}!",
            "-background",
            "black",
            "-gravity",
            "center",
            "-extent",
            f"{width}x{height}{candidate.offset_x:+d}{candidate.offset_y:+d}",
            str(output),
        ]
    )


def register_yakkai_to_reference(
    commands: smoke_runner.ImageMagickCommands,
    reference: Path,
    yakkai: Path,
    output_dir: Path,
    width: int,
    height: int,
    candidates: tuple[RegistrationCandidate, ...] | None = None,
) -> RegistrationResult:
    search_candidates = candidates or registration_candidates(width, height)
    if not search_candidates:
        raise RuntimeError("registration candidate list is empty")

    work_dir = output_dir / "registration-work"
    structure_reference = output_dir / "reference.structure.png"
    build_registration_structure_image(commands, reference, structure_reference, width, height)
    candidate_yakkai = work_dir / "candidate.png"
    candidate_structure = work_dir / "candidate-structure.png"
    best_candidate: RegistrationCandidate | None = None
    best_structure_rmse: float | None = None
    best_rgb_rmse: float | None = None
    for candidate in search_candidates:
        transform_yakkai_for_registration(commands, yakkai, candidate_yakkai, width, height, candidate)
        build_registration_structure_image(commands, candidate_yakkai, candidate_structure, width, height)
        structure_rmse = smoke_runner.compare_rmse(commands, structure_reference, candidate_structure, None)
        rgb_rmse = smoke_runner.compare_rmse(commands, reference, candidate_yakkai, None)
        score = (structure_rmse, rgb_rmse)
        best_score = (
            best_structure_rmse if best_structure_rmse is not None else math.inf,
            best_rgb_rmse if best_rgb_rmse is not None else math.inf,
        )
        if score < best_score:
            best_structure_rmse = structure_rmse
            best_rgb_rmse = rgb_rmse
            best_candidate = candidate

    if best_candidate is None or best_structure_rmse is None:
        raise RuntimeError("registration did not produce a candidate")

    registered_yakkai = output_dir / "yakkai.registered.png"
    registered_diff = output_dir / "registered-diff.png"
    structure_yakkai = output_dir / "yakkai.registered.structure.png"
    structure_diff = output_dir / "registered-structure-diff.png"
    transform_yakkai_for_registration(commands, yakkai, registered_yakkai, width, height, best_candidate)
    registered_rmse = smoke_runner.compare_rmse(commands, reference, registered_yakkai, registered_diff)
    build_registration_structure_image(commands, registered_yakkai, structure_yakkai, width, height)
    registered_structure_rmse = smoke_runner.compare_rmse(commands, structure_reference, structure_yakkai, structure_diff)
    return RegistrationResult(
        candidate=best_candidate,
        rmse=registered_rmse,
        structure_rmse=registered_structure_rmse,
        yakkai=registered_yakkai,
        diff=registered_diff,
        structure_reference=structure_reference,
        structure_yakkai=structure_yakkai,
        structure_diff=structure_diff,
    )


def montage_command(commands: smoke_runner.ImageMagickCommands) -> list[str]:
    if commands.convert and Path(commands.convert[0]).name == "magick":
        return [commands.convert[0], "montage"]
    montage = shutil.which("montage")
    if montage:
        return [montage]
    return ["montage"]


def compare_variant_images(
    variant: AronaVariant,
    rendered: VariantResult,
    config: CompareConfig,
    commands: smoke_runner.ImageMagickCommands,
) -> VariantResult:
    if rendered.status != "pass" or rendered.yakkai is None:
        return rendered

    variant_dir = Path(rendered.yakkai).parent
    normalized = variant_dir / "reference.normalized.png"
    diff = variant_dir / "diff.png"
    reference = config.reference_root / variant.reference_relative_path
    dimensions = f"{config.width}x{config.height}"
    yakkai = Path(rendered.yakkai)
    try:
        normalize_reference(commands, reference, normalized, dimensions)
        rmse = smoke_runner.compare_rmse(commands, normalized, yakkai, diff)
        metrics = dict(rendered.metrics)
        metrics.update(
            {
                "rmse": rmse,
                "referenceMeanRgb": rgb_means(commands, normalized),
                "yakkaiMeanRgb": rgb_means(commands, yakkai),
            }
        )
        registered_yakkai: str | None = None
        registered_diff: str | None = None
        structure_reference: str | None = None
        registered_structure_yakkai: str | None = None
        registered_structure_diff: str | None = None
        if config.register_comparison:
            try:
                registration = register_yakkai_to_reference(
                    commands,
                    normalized,
                    yakkai,
                    variant_dir,
                    config.width,
                    config.height,
                )
                registered_yakkai = str(registration.yakkai)
                registered_diff = str(registration.diff)
                structure_reference = str(registration.structure_reference)
                registered_structure_yakkai = str(registration.structure_yakkai)
                registered_structure_diff = str(registration.structure_diff)
                metrics.update(
                    {
                        "registeredRmse": registration.rmse,
                        "registrationStructureRmse": registration.structure_rmse,
                        "registrationMetric": REGISTRATION_STRUCTURE_METRIC,
                        "registrationScale": registration.candidate.scale,
                        "registrationOffsetX": registration.candidate.offset_x,
                        "registrationOffsetY": registration.candidate.offset_y,
                        "registrationRmseImprovement": rmse - registration.rmse,
                    }
                )
            except Exception as exc:
                metrics["registrationError"] = str(exc)
        return VariantResult(
            name=rendered.name,
            status="review",
            reference=rendered.reference,
            yakkai=rendered.yakkai,
            normalizedReference=str(normalized),
            diff=str(diff),
            registeredYakkai=registered_yakkai,
            registeredDiff=registered_diff,
            structureReference=structure_reference,
            registeredStructureYakkai=registered_structure_yakkai,
            registeredStructureDiff=registered_structure_diff,
            log=rendered.log,
            effectManifest=rendered.effectManifest,
            metrics=metrics,
        )
    except Exception as exc:
        metrics = dict(rendered.metrics)
        metrics["error"] = str(exc)
        return VariantResult(
            name=rendered.name,
            status="fail",
            reference=rendered.reference,
            yakkai=rendered.yakkai,
            normalizedReference=str(normalized),
            diff=str(diff),
            registeredYakkai=None,
            registeredDiff=None,
            structureReference=None,
            registeredStructureYakkai=None,
            registeredStructureDiff=None,
            log=rendered.log,
            effectManifest=rendered.effectManifest,
            metrics=metrics,
        )


def comparable_variants(variants: list[VariantResult]) -> list[VariantResult]:
    comparable: list[VariantResult] = []
    for result in variants:
        if result.status != "review":
            continue
        if not (result.normalizedReference and result.yakkai and result.diff):
            continue
        if not (
            Path(result.normalizedReference).is_file()
            and Path(result.yakkai).is_file()
            and Path(result.diff).is_file()
        ):
            continue
        comparable.append(result)
    return comparable


def registered_comparable_variants(variants: list[VariantResult]) -> list[VariantResult]:
    comparable: list[VariantResult] = []
    for result in variants:
        if result.status != "review":
            continue
        if not (result.normalizedReference and result.registeredYakkai and result.registeredDiff):
            continue
        if not (
            Path(result.normalizedReference).is_file()
            and Path(result.registeredYakkai).is_file()
            and Path(result.registeredDiff).is_file()
        ):
            continue
        comparable.append(result)
    return comparable


def write_contact_sheet(
    commands: smoke_runner.ImageMagickCommands,
    variants: list[VariantResult],
    output: Path,
) -> None:
    images: list[str] = []
    for result in comparable_variants(variants):
        images += [result.normalizedReference or "", result.yakkai or "", result.diff or ""]
    if not images:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    run_checked(
        montage_command(commands)
        + images
        + [
            "-tile",
            "3x",
            "-geometry",
            "+8+8",
            str(output),
        ]
    )


def write_registered_contact_sheet(
    commands: smoke_runner.ImageMagickCommands,
    variants: list[VariantResult],
    output: Path,
) -> None:
    images: list[str] = []
    for result in registered_comparable_variants(variants):
        images += [result.normalizedReference or "", result.registeredYakkai or "", result.registeredDiff or ""]
    if not images:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    run_checked(
        montage_command(commands)
        + images
        + [
            "-tile",
            "3x",
            "-geometry",
            "+8+8",
            str(output),
        ]
    )


def result_to_json(result: CompareResult) -> dict[str, Any]:
    data = asdict(result)
    for variant in data.get("variants", []):
        if variant.get("effectManifest") is None:
            variant.pop("effectManifest", None)
    return data


def write_summary(result: CompareResult, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result_to_json(result), indent=2, sort_keys=True) + "\n", encoding="utf-8")


def clear_shader_cache(cache_root: Path) -> list[Path]:
    removed: list[Path] = []
    if not cache_root.exists():
        return removed
    for path in cache_root.glob("*/spvs01"):
        if path.is_dir():
            shutil.rmtree(path)
            removed.append(path)
    return removed


def missing_reference_stills(reference_root: Path) -> list[Path]:
    return [
        reference_root / variant.reference_relative_path
        for variant in VARIANTS
        if not (reference_root / variant.reference_relative_path).is_file()
    ]


def default_harness(repo: Path) -> Path:
    return repo / "build/native/scene_harness/yakkai_scene_harness"


def default_assets() -> Path:
    return Path.home() / ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets"


def default_source() -> Path:
    return Path.home() / ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960/3228578419/scene.pkg"


def default_shader_cache_root() -> Path:
    return Path.home() / ".cache/wescene-renderer"


def render_variant(
    variant: AronaVariant,
    config: CompareConfig,
    *,
    harness: Path,
    source: Path,
    assets: Path,
    run_command=smoke_runner.run_command,
) -> VariantResult:
    variant_dir = config.output_root / variant.name
    yakkai_path = variant_dir / "yakkai.png"
    log_path = variant_dir / "harness.log"
    variant_dir.mkdir(parents=True, exist_ok=True)
    scene = {
        "id": f"3228578419-{variant.name}",
        "backend": "paper",
        "fill": "crop",
    }
    command = smoke_runner.build_harness_base_command(
        harness,
        scene,
        source,
        assets,
        {"width": config.width, "height": config.height},
        scene_properties_json(variant, source),
    )
    command += ["--capture", str(yakkai_path), "--capture-delay-ms", str(config.capture_delay_ms)]
    effect_manifest: Path | None = None
    if config.debug_effect_captures:
        effect_dir = variant_dir / "effect-captures"
        effect_manifest = effect_dir / "manifest.json"
        command += ["--debug-effect-captures", str(effect_dir)]
        if config.debug_effect_probe_layers:
            command += [
                "--debug-effect-probe-layers",
                format_probe_layer_ids(config.debug_effect_probe_layers),
            ]
        if config.debug_effect_probe_max_effects is not None:
            command += [
                "--debug-effect-probe-max-effects",
                str(config.debug_effect_probe_max_effects),
            ]
        if config.debug_puppet_animation_layer_overrides:
            command += [
                "--debug-puppet-animation-layer-overrides",
                config.debug_puppet_animation_layer_overrides,
            ]
    timeout_seconds = math.ceil(config.capture_delay_ms / 1000) + 30
    try:
        code = run_command(command, log_path, timeout_seconds)
    except OSError as exc:
        return VariantResult(
            name=variant.name,
            status="fail",
            reference=str(config.reference_root / variant.reference_relative_path),
            yakkai=str(yakkai_path),
            log=str(log_path),
            metrics={"error": f"failed to launch harness: {exc}", "command": shlex.join(command)},
        )
    if code != 0:
        return VariantResult(
            name=variant.name,
            status="fail",
            reference=str(config.reference_root / variant.reference_relative_path),
            yakkai=str(yakkai_path),
            log=str(log_path),
            metrics={"error": f"harness capture exited {code}", "command": shlex.join(command)},
        )
    if not yakkai_path.exists():
        return VariantResult(
            name=variant.name,
            status="fail",
            reference=str(config.reference_root / variant.reference_relative_path),
            yakkai=str(yakkai_path),
            log=str(log_path),
            effectManifest=str(effect_manifest) if effect_manifest and effect_manifest.exists() else None,
            metrics={"error": f"missing yakkai capture: {yakkai_path}", "command": shlex.join(command)},
        )
    return VariantResult(
        name=variant.name,
        status="pass",
        reference=str(config.reference_root / variant.reference_relative_path),
        yakkai=str(yakkai_path),
        log=str(log_path),
        effectManifest=str(effect_manifest) if effect_manifest and effect_manifest.exists() else None,
    )


def compare_all(config: CompareConfig) -> CompareResult:
    if not config.reference_root.is_dir():
        return CompareResult("skip", f"reference root not found: {config.reference_root}")

    missing = missing_reference_stills(config.reference_root)
    if missing:
        missing_text = ", ".join(str(path) for path in missing)
        return CompareResult("skip", f"missing reference stills: {missing_text}")

    harness = default_harness(config.repo_root)
    source = default_source()
    assets = default_assets()
    if not harness.is_file():
        return CompareResult("fail", f"harness not found: {harness}")
    if not source.is_file():
        return CompareResult("skip", f"scene not installed: {source}")
    if not assets.is_dir():
        return CompareResult("fail", f"assets directory not found: {assets}")

    config.output_root.mkdir(parents=True, exist_ok=True)
    try:
        clear_shader_cache(default_shader_cache_root())
    except OSError as exc:
        return CompareResult(
            "fail",
            f"failed to clear shader cache before rendering: {exc}",
            str(config.output_root),
        )

    rendered = [
        render_variant(variant, config, harness=harness, source=source, assets=assets)
        for variant in VARIANTS
    ]
    try:
        imagemagick = smoke_runner.require_imagemagick()
    except RuntimeError as exc:
        return CompareResult(
            "fail",
            f"ImageMagick unavailable for reference comparison: {exc}",
            str(config.output_root),
            variants=rendered,
        )

    by_name = {variant.name: variant for variant in VARIANTS}
    variants = [
        compare_variant_images(by_name[result.name], result, config, imagemagick)
        if result.name in by_name and result.status == "pass"
        else result
        for result in rendered
    ]
    worst = "pass"
    for result in variants:
        worst = worst_status(worst, result.status)

    contact_sheet_path = config.output_root / "contact-sheet.png"
    comparable = comparable_variants(variants)
    contact_sheet: str | None = None
    if comparable:
        try:
            write_contact_sheet(imagemagick, comparable, contact_sheet_path)
            if contact_sheet_path.exists():
                contact_sheet = str(contact_sheet_path)
        except Exception as exc:
            variants.append(
                VariantResult(
                    name="contact-sheet",
                    status="fail",
                    reference="",
                    metrics={"error": str(exc)},
                )
            )
            worst = worst_status(worst, "fail")

    registered_contact_sheet_path = config.output_root / "contact-sheet-registered.png"
    registered_comparable = registered_comparable_variants(variants)
    registered_contact_sheet: str | None = None
    if registered_comparable:
        try:
            write_registered_contact_sheet(imagemagick, registered_comparable, registered_contact_sheet_path)
            if registered_contact_sheet_path.exists():
                registered_contact_sheet = str(registered_contact_sheet_path)
        except Exception as exc:
            variants.append(
                VariantResult(
                    name="contact-sheet-registered",
                    status="fail",
                    reference="",
                    metrics={"error": str(exc)},
                )
            )
            worst = worst_status(worst, "fail")

    summary_path = config.output_root / "summary.json"
    result = CompareResult(
        status=worst,
        message="compared Arona WE references",
        outputDir=str(config.output_root),
        contactSheet=contact_sheet,
        summary=str(summary_path),
        registeredContactSheet=registered_contact_sheet,
        variants=variants,
    )
    write_summary(result, summary_path)
    return result


def parse_args(argv: list[str]) -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Compare Yakkai Arona renders against local Windows Wallpaper Engine references.")
    parser.add_argument("--reference-root", default=str(root / "yakkai_arona"))
    parser.add_argument("--output-root", default=str(Path("/tmp/yakkai-arona-reference") / timestamp()))
    parser.add_argument("--capture-delay-ms", type=int, default=8000)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument(
        "--debug-effect-captures",
        action="store_true",
        help="Write per-variant harness effect capture manifests beside comparator artifacts.",
    )
    parser.add_argument(
        "--debug-effect-probe-layers",
        default="",
        help="Comma-separated stripped puppet mixed-chain layer IDs to render only inside debug effect captures. Requires --debug-effect-captures.",
    )
    parser.add_argument(
        "--debug-effect-probe-max-effects",
        type=int,
        default=None,
        help="Limit forced debug-probe layers to their first N visible effects. Requires --debug-effect-captures and --debug-effect-probe-layers.",
    )
    parser.add_argument(
        "--debug-effect-probe-channelmap-slots",
        default="",
        help="Comma-separated puppet channelmap blend slots to force active for stripped puppet debug probes. Requires --debug-effect-captures and --debug-effect-probe-layers.",
    )
    parser.add_argument(
        "--debug-puppet-animation-layer-overrides",
        default="",
        help="Forward harness-only puppet animation layer overrides. Requires --debug-effect-captures.",
    )
    parser.add_argument(
        "--skip-registration",
        action="store_true",
        help="Skip registered scale/offset comparison artifacts and metrics.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = repo_root()
    try:
        debug_effect_probe_layers = parse_probe_layer_ids(args.debug_effect_probe_layers)
        debug_effect_probe_channelmap_slots = parse_probe_channelmap_slots(
            args.debug_effect_probe_channelmap_slots
        )
    except ValueError as exc:
        print(f"fail: {exc}", file=sys.stderr)
        return 1
    if debug_effect_probe_layers and not args.debug_effect_captures:
        print("fail: --debug-effect-probe-layers requires --debug-effect-captures", file=sys.stderr)
        return 1
    if debug_effect_probe_channelmap_slots and not args.debug_effect_captures:
        print("fail: --debug-effect-probe-channelmap-slots requires --debug-effect-captures", file=sys.stderr)
        return 1
    if debug_effect_probe_channelmap_slots and not debug_effect_probe_layers:
        print("fail: --debug-effect-probe-channelmap-slots requires --debug-effect-probe-layers", file=sys.stderr)
        return 1
    if debug_effect_probe_channelmap_slots:
        print(
            "fail: --debug-effect-probe-channelmap-slots is quarantined because the derived channelmap render path produces glitchy puppet fragments",
            file=sys.stderr,
        )
        return 1
    if args.debug_puppet_animation_layer_overrides and not args.debug_effect_captures:
        print("fail: --debug-puppet-animation-layer-overrides requires --debug-effect-captures", file=sys.stderr)
        return 1
    if args.debug_effect_probe_max_effects is not None:
        if args.debug_effect_probe_max_effects < 0:
            print("fail: --debug-effect-probe-max-effects must be non-negative", file=sys.stderr)
            return 1
        if not args.debug_effect_captures:
            print("fail: --debug-effect-probe-max-effects requires --debug-effect-captures", file=sys.stderr)
            return 1
        if not debug_effect_probe_layers:
            print("fail: --debug-effect-probe-max-effects requires --debug-effect-probe-layers", file=sys.stderr)
            return 1
    result = compare_all(
        CompareConfig(
            repo_root=root,
            reference_root=Path(args.reference_root),
            output_root=Path(args.output_root),
            capture_delay_ms=args.capture_delay_ms,
            width=args.width,
            height=args.height,
            debug_effect_captures=args.debug_effect_captures,
            debug_effect_probe_layers=debug_effect_probe_layers,
            debug_effect_probe_channelmap_slots=debug_effect_probe_channelmap_slots,
            debug_effect_probe_max_effects=args.debug_effect_probe_max_effects,
            debug_puppet_animation_layer_overrides=args.debug_puppet_animation_layer_overrides,
            register_comparison=not args.skip_registration,
        )
    )
    print(f"{result.status}: {result.message}")
    if result.outputDir:
        print(f"Artifacts: {result.outputDir}")
    if result.contactSheet:
        print(f"Contact sheet: {result.contactSheet}")
    if result.registeredContactSheet:
        print(f"Registered contact sheet: {result.registeredContactSheet}")
    if result.summary:
        print(f"Summary: {result.summary}")
    for variant in result.variants:
        if "rmse" in variant.metrics:
            print(f"{variant.name}: {variant.status} rmse={variant.metrics['rmse']}")
        elif "error" in variant.metrics:
            print(f"{variant.name}: {variant.status} error={variant.metrics['error']}")
        else:
            print(f"{variant.name}: {variant.status}")
    return 0 if result.status in ("pass", "review", "skip") else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
