#!/usr/bin/env python3
import argparse
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any

import mpris_live_smoke


def _local_art_exists(album_art_path: str) -> bool:
    return bool(album_art_path) and Path(album_art_path).exists()


def classify_snapshot(snapshot: dict[str, Any]) -> dict[str, Any]:
    media = snapshot.get("__yakkaiMedia", {})
    status = str(snapshot.get("playbackStatus") or "")
    album_art_path = str(media.get("albumArtPath") or "")
    available = bool(media.get("available"))
    playing = bool(media.get("playing"))
    title = str(media.get("title") or "")
    artist = str(media.get("artist") or "")
    album = str(media.get("album") or "")
    duration = float(media.get("duration") or 0.0)

    issues: list[str] = []
    if not snapshot.get("ok"):
        diagnostic = str(snapshot.get("diagnostic") or "")
        if "metadata" in diagnostic.casefold() or not any(
            [title, artist, album, duration, album_art_path]
        ):
            issues.append("missing-metadata")
        else:
            issues.append("probe-failed")

    if available and not any([title, artist, album, duration]):
        issues.append("missing-metadata")

    if available and not album_art_path:
        issues.append("missing-art")
    elif album_art_path and not _local_art_exists(album_art_path):
        issues.append("missing-art-file")

    if available and status and not playing:
        issues.append("not-playing")

    return {
        "ok": bool(snapshot.get("ok")),
        "available": available,
        "playing": playing,
        "status": status,
        "title": title,
        "artist": artist,
        "album": album,
        "duration": duration,
        "albumArtPath": album_art_path,
        "localAlbumArtExists": _local_art_exists(album_art_path),
        "issues": sorted(set(issues)),
    }


def _probe(
    client: Any,
    name: str,
    service: str | None = None,
) -> dict[str, Any]:
    snapshot = mpris_live_smoke.collect_snapshot(client, target_service=service)
    return {
        "name": name,
        "service": service,
        "classification": classify_snapshot(snapshot),
        "snapshot": snapshot,
    }


def _summary(probes: list[dict[str, Any]]) -> dict[str, Any]:
    status_counts: Counter[str] = Counter()
    issue_counts: Counter[str] = Counter()
    for probe in probes:
        classification = probe["classification"]
        status = classification.get("status")
        if status:
            status_counts[str(status)] += 1
        for issue in classification.get("issues", []):
            issue_counts[str(issue)] += 1

    return {
        "totalProbes": len(probes),
        "okProbes": sum(1 for probe in probes if probe["classification"]["ok"]),
        "availableProbes": sum(
            1 for probe in probes if probe["classification"]["available"]
        ),
        "playingProbes": sum(
            1 for probe in probes if probe["classification"]["playing"]
        ),
        "statusCounts": dict(sorted(status_counts.items())),
        "issueCounts": dict(sorted(issue_counts.items())),
    }


def build_matrix(
    client: Any,
    services: list[str] | None = None,
    *,
    include_default_selection: bool = True,
) -> dict[str, Any]:
    discovered_services = mpris_live_smoke.sorted_mpris_services(client.list_names())
    exact_services = services if services is not None else discovered_services

    probes: list[dict[str, Any]] = []
    if include_default_selection:
        probes.append(_probe(client, "default-selection"))

    for service in exact_services:
        probes.append(_probe(client, f"exact-service: {service}", service))

    return {
        "tool": "mpris_compat_matrix",
        "services": discovered_services,
        "requestedServices": exact_services,
        "probes": probes,
        "summary": _summary(probes),
    }


def _cell(value: Any) -> str:
    text = str(value)
    return text.replace("|", "\\|").replace("\n", " ")


def render_markdown(matrix: dict[str, Any]) -> str:
    lines = [
        "# MPRIS Compatibility Matrix",
        "",
        "## Summary",
        "",
        f"- Services discovered: {len(matrix.get('services', []))}",
        f"- Total probes: {matrix['summary']['totalProbes']}",
        f"- OK probes: {matrix['summary']['okProbes']}",
        f"- Available media probes: {matrix['summary']['availableProbes']}",
        f"- Playing media probes: {matrix['summary']['playingProbes']}",
        f"- Status counts: `{json.dumps(matrix['summary']['statusCounts'], sort_keys=True)}`",
        f"- Issue counts: `{json.dumps(matrix['summary']['issueCounts'], sort_keys=True)}`",
        "",
        "## Probes",
        "",
        "| Probe | Service | OK | Available | Playing | Status | Title | Artist | Issues | Diagnostic |",
        "| --- | --- | ---: | ---: | ---: | --- | --- | --- | --- | --- |",
    ]

    for probe in matrix.get("probes", []):
        classification = probe["classification"]
        snapshot = probe["snapshot"]
        lines.append(
            "| "
            + " | ".join(
                [
                    _cell(probe["name"]),
                    _cell(probe.get("service") or "runtime default"),
                    _cell(classification["ok"]),
                    _cell(classification["available"]),
                    _cell(classification["playing"]),
                    _cell(classification["status"]),
                    _cell(classification["title"]),
                    _cell(classification["artist"]),
                    _cell(", ".join(classification["issues"]) or "none"),
                    _cell(snapshot.get("diagnostic", "")),
                ]
            )
            + " |"
        )

    return "\n".join(lines) + "\n"


def write_artifacts(matrix: dict[str, Any], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "summary.json").write_text(
        json.dumps(matrix, indent=2, sort_keys=True) + "\n"
    )
    (output_dir / "summary.md").write_text(render_markdown(matrix))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run a read-only MPRIS provider compatibility matrix using "
            "tools/mpris_live_smoke.py normalization rules."
        )
    )
    parser.add_argument(
        "--qdbus",
        default="qdbus6",
        help="qdbus executable to use",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=2.0,
        help="Per-qdbus-call timeout in seconds",
    )
    parser.add_argument(
        "--service",
        action="append",
        default=None,
        help="Exact MPRIS service to probe; repeat to limit exact probes",
    )
    parser.add_argument(
        "--no-default-selection",
        action="store_true",
        help="Skip the Yakkai-like default service-selection probe",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path.cwd() / "tmp" / "mpris-compat-matrix",
        help="Directory for summary.json and summary.md",
    )
    parser.add_argument(
        "--fail-on-issues",
        action="store_true",
        help="Exit nonzero when any probe reports a classification issue",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    client = mpris_live_smoke.QdbusClient(args.qdbus, args.timeout)
    matrix = build_matrix(
        client,
        services=args.service,
        include_default_selection=not args.no_default_selection,
    )
    write_artifacts(matrix, args.output_dir)

    print(f"Wrote {args.output_dir / 'summary.json'}")
    print(f"Wrote {args.output_dir / 'summary.md'}")
    if args.fail_on_issues and matrix["summary"]["issueCounts"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
