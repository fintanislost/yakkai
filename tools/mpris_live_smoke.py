#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse


MPRIS_SERVICE_PREFIX = "org.mpris.MediaPlayer2."
MPRIS_OBJECT_PATH = "/org/mpris/MediaPlayer2"
MPRIS_PLAYER_INTERFACE = "org.mpris.MediaPlayer2.Player"
DBUS_PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties"


def sorted_mpris_services(registered_names: list[str]) -> list[str]:
    candidates = [
        name for name in registered_names if name.startswith(MPRIS_SERVICE_PREFIX)
    ]
    return sorted(candidates, key=lambda name: (name.casefold(), name))


def _first_artist(value: Any) -> str:
    if isinstance(value, list):
        return str(value[0]) if value else ""
    return str(value or "")


def _usec_to_seconds(value: Any) -> float:
    try:
        return float(value) / 1000000.0
    except (TypeError, ValueError):
        return 0.0


def normalize_art_url(value: Any) -> str:
    raw = str(value or "").strip()
    if not raw:
        return ""

    parsed = urlparse(raw)
    if parsed.scheme == "file":
        if parsed.netloc not in ("", "localhost"):
            return ""
        return unquote(parsed.path)

    if not parsed.scheme and os.path.isabs(raw):
        return raw

    return ""


def unavailable_media_payload() -> dict[str, Any]:
    return {
        "available": False,
        "playing": False,
        "title": "",
        "artist": "",
        "album": "",
        "duration": 0.0,
        "position": 0.0,
        "albumArtPath": "",
    }


def media_payload(
    service: str,
    playback_status: str,
    metadata: dict[str, Any],
    position_usec: Any,
) -> dict[str, Any]:
    if not service or not metadata:
        return unavailable_media_payload()

    duration = _usec_to_seconds(metadata.get("mpris:length"))
    position = _usec_to_seconds(position_usec)
    if duration > 0.0:
        position = min(position, duration)

    media = unavailable_media_payload()
    media.update(
        {
            "available": True,
            "playing": playback_status.casefold() == "playing",
            "title": str(metadata.get("xesam:title") or ""),
            "artist": _first_artist(metadata.get("xesam:artist")),
            "album": str(metadata.get("xesam:album") or ""),
            "duration": duration,
            "position": position,
            "albumArtPath": normalize_art_url(metadata.get("mpris:artUrl")),
        }
    )
    return media


def _parse_scalar(raw: str) -> Any:
    value = raw.strip()
    if not value:
        return ""

    lowered = value.casefold()
    if lowered == "true":
        return True
    if lowered == "false":
        return False

    try:
        return int(value)
    except ValueError:
        return value


def _parse_list(raw: str) -> list[str]:
    value = raw.strip()
    if value.startswith("[") and value.endswith("]"):
        value = value[1:-1]
    if not value:
        return []
    return [part.strip().strip("\"'") for part in value.split(",") if part.strip()]


def parse_qdbus_metadata(raw: str) -> dict[str, Any]:
    metadata: dict[str, Any] = {}
    known_keys = {
        "mpris:artUrl",
        "mpris:length",
        "xesam:album",
        "xesam:artist",
        "xesam:title",
        "xesam:url",
    }

    for line in raw.splitlines():
        stripped = line.strip()
        if not stripped or ":" not in stripped:
            continue

        matched_key = ""
        for key in known_keys:
            prefix = f"{key}:"
            if stripped.startswith(prefix):
                matched_key = key
                value = stripped[len(prefix) :].strip()
                break
        if not matched_key:
            continue

        if matched_key == "xesam:artist":
            parsed_value = _parse_list(value)
            metadata[matched_key] = parsed_value if parsed_value else [value]
        else:
            metadata[matched_key] = _parse_scalar(value)

    return metadata


@dataclass
class CandidateDiagnostic:
    service: str
    readable: bool
    playbackStatus: str = ""
    error: str = ""

    def to_json(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "service": self.service,
            "readable": self.readable,
        }
        if self.playbackStatus:
            result["playbackStatus"] = self.playbackStatus
        if self.error:
            result["error"] = self.error
        return result


def collect_snapshot(client: Any, target_service: str | None = None) -> dict[str, Any]:
    registered_names = client.list_names()
    if target_service:
        candidates = [target_service] if target_service in registered_names else []
    else:
        candidates = sorted_mpris_services(registered_names)
    if not candidates:
        diagnostic = (
            f"Requested MPRIS media player is not available: {target_service}"
            if target_service
            else "No MPRIS media player is available."
        )
        return {
            "ok": False,
            "diagnostic": diagnostic,
            "selectedService": None,
            "playbackStatus": "",
            "candidates": [],
            "__yakkaiMedia": unavailable_media_payload(),
        }

    selected_service = ""
    selected_status = ""
    diagnostics: list[CandidateDiagnostic] = []
    last_status_error = ""

    for candidate in candidates:
        try:
            status = str(client.get_property(candidate, "PlaybackStatus") or "")
        except Exception as exc:
            last_status_error = str(exc)
            diagnostics.append(
                CandidateDiagnostic(candidate, False, error=last_status_error)
            )
            continue

        diagnostics.append(CandidateDiagnostic(candidate, True, status))
        if not selected_service:
            selected_service = candidate
            selected_status = status
        if status.casefold() == "playing":
            selected_service = candidate
            selected_status = status
            break

    if not selected_service:
        return {
            "ok": False,
            "diagnostic": f"No readable MPRIS media player is available: {last_status_error}",
            "selectedService": None,
            "playbackStatus": "",
            "candidates": [diagnostic.to_json() for diagnostic in diagnostics],
            "__yakkaiMedia": unavailable_media_payload(),
        }

    try:
        playback_status = str(
            client.get_property(selected_service, "PlaybackStatus") or selected_status
        )
    except Exception as exc:
        if not selected_status:
            return {
                "ok": False,
                "diagnostic": (
                    f"Could not read PlaybackStatus from {selected_service}: {exc}"
                ),
                "selectedService": selected_service,
                "playbackStatus": "",
                "candidates": [diagnostic.to_json() for diagnostic in diagnostics],
                "__yakkaiMedia": unavailable_media_payload(),
            }
        playback_status = selected_status

    try:
        metadata = client.get_property(selected_service, "Metadata")
    except Exception as exc:
        return {
            "ok": False,
            "diagnostic": f"Could not read Metadata from {selected_service}: {exc}",
            "selectedService": selected_service,
            "playbackStatus": playback_status,
            "candidates": [diagnostic.to_json() for diagnostic in diagnostics],
            "__yakkaiMedia": unavailable_media_payload(),
        }
    if not isinstance(metadata, dict):
        metadata = {}

    try:
        position_usec = client.get_property(selected_service, "Position")
    except Exception:
        position_usec = 0

    media = media_payload(selected_service, playback_status, metadata, position_usec)
    if media["available"]:
        if last_status_error:
            diagnostic = (
                f"MPRIS media player selected: {selected_service}; one or more "
                f"players could not be queried: {last_status_error}"
            )
        else:
            diagnostic = f"MPRIS media player selected: {selected_service}"
        ok = True
    else:
        diagnostic = f"MPRIS player {selected_service} did not provide media metadata."
        ok = False

    return {
        "ok": ok,
        "diagnostic": diagnostic,
        "selectedService": selected_service,
        "playbackStatus": playback_status,
        "candidates": [diagnostic.to_json() for diagnostic in diagnostics],
        "__yakkaiMedia": media,
    }


class QdbusClient:
    def __init__(self, qdbus: str = "qdbus6", timeout_seconds: float = 2.0):
        self.qdbus = qdbus
        self.timeout_seconds = timeout_seconds

    def _run(self, command: list[str]) -> str:
        try:
            completed = subprocess.run(
                command,
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=self.timeout_seconds,
            )
        except subprocess.CalledProcessError as exc:
            error = exc.stderr.strip() or exc.stdout.strip() or str(exc)
            raise RuntimeError(error) from exc
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f"qdbus call timed out after {self.timeout_seconds}s") from exc
        return completed.stdout.strip()

    def list_names(self) -> list[str]:
        output = self._run(
            [
                self.qdbus,
                "org.freedesktop.DBus",
                "/",
                "org.freedesktop.DBus.ListNames",
            ]
        )
        return [line.strip() for line in output.splitlines() if line.strip()]

    def get_property(self, service: str, property_name: str) -> Any:
        output = self._run(
            [
                self.qdbus,
                service,
                MPRIS_OBJECT_PATH,
                f"{DBUS_PROPERTIES_INTERFACE}.Get",
                MPRIS_PLAYER_INTERFACE,
                property_name,
            ]
        )
        if property_name == "Metadata":
            return parse_qdbus_metadata(output)
        if property_name == "Position":
            return _parse_scalar(output)
        return output.strip()


def check_expectations(snapshot: dict[str, Any], expectations: dict[str, Any]) -> list[str]:
    media = snapshot.get("__yakkaiMedia", {})
    failures: list[str] = []

    expected_service = expectations.get("expect_service")
    if expected_service and snapshot.get("selectedService") != expected_service:
        failures.append(
            f"expected service '{expected_service}', got '{snapshot.get('selectedService')}'"
        )

    expected_status = expectations.get("expect_status")
    if expected_status and snapshot.get("playbackStatus") != expected_status:
        failures.append(
            f"expected playback status '{expected_status}', got '{snapshot.get('playbackStatus')}'"
        )

    expected_title = expectations.get("expect_title")
    if expected_title and media.get("title") != expected_title:
        failures.append(f"expected title '{expected_title}', got '{media.get('title')}'")

    expected_artist = expectations.get("expect_artist")
    if expected_artist and media.get("artist") != expected_artist:
        failures.append(
            f"expected artist '{expected_artist}', got '{media.get('artist')}'"
        )

    expected_album = expectations.get("expect_album")
    if expected_album and media.get("album") != expected_album:
        failures.append(f"expected album '{expected_album}', got '{media.get('album')}'")

    if expectations.get("require_local_art"):
        album_art_path = str(media.get("albumArtPath") or "")
        if not album_art_path or not Path(album_art_path).exists():
            failures.append(
                f"expected local album art path to exist, got '{album_art_path}'"
            )

    return failures


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Probe live MPRIS media metadata and print the normalized Yakkai "
            "__yakkaiMedia payload."
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
        help=(
            "Probe this exact MPRIS service instead of using Yakkai's default "
            "runtime selection order"
        ),
    )
    parser.add_argument("--expect-service", help="Require the selected MPRIS service")
    parser.add_argument("--expect-status", help="Require a PlaybackStatus value")
    parser.add_argument("--expect-title", help="Require the media title")
    parser.add_argument("--expect-artist", help="Require the first artist")
    parser.add_argument("--expect-album", help="Require the album")
    parser.add_argument(
        "--require-local-art",
        action="store_true",
        help="Fail unless normalized albumArtPath points to an existing local file",
    )
    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Pretty-print JSON output",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    client = QdbusClient(args.qdbus, args.timeout)
    try:
        snapshot = collect_snapshot(client, target_service=args.service)
    except Exception as exc:
        snapshot = {
            "ok": False,
            "diagnostic": str(exc),
            "selectedService": None,
            "playbackStatus": "",
            "candidates": [],
            "__yakkaiMedia": unavailable_media_payload(),
        }

    expectations = {
        "expect_service": args.expect_service,
        "expect_status": args.expect_status,
        "expect_title": args.expect_title,
        "expect_artist": args.expect_artist,
        "expect_album": args.expect_album,
        "require_local_art": args.require_local_art,
    }
    expectation_failures = check_expectations(snapshot, expectations)
    if expectation_failures:
        snapshot["ok"] = False
        snapshot["expectationFailures"] = expectation_failures

    if args.pretty:
        print(json.dumps(snapshot, indent=2, sort_keys=True))
    else:
        print(json.dumps(snapshot, sort_keys=True))

    return 0 if snapshot.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
