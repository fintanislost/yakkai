#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_ROOT="$ROOT/wallpapers/io.team7.yakkai"
RUN_KPACKAGE=1

usage() {
    cat <<'USAGE'
Usage: scripts/check-package.sh [--skip-kpackage]

Validate the staged Plasma wallpaper package in wallpapers/io.team7.yakkai.

Options:
  --skip-kpackage   Skip the throwaway kpackagetool6 install check.
  -h, --help        Show this help.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

warn() {
    printf 'WARN: %s\n' "$*" >&2
}

check_file() {
    local path="$1"
    [[ -f "$path" ]] || fail "missing required file: ${path#$ROOT/}"
}

check_nonempty_file() {
    local path="$1"
    [[ -s "$path" ]] || fail "missing or empty generated file: ${path#$ROOT/}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-kpackage)
            RUN_KPACKAGE=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument: $1"
            ;;
    esac
done

[[ -d "$PACKAGE_ROOT" ]] || fail "missing package root: ${PACKAGE_ROOT#$ROOT/}"

check_file "$PACKAGE_ROOT/metadata.json"
check_file "$PACKAGE_ROOT/contents/config/main.xml"
check_file "$PACKAGE_ROOT/contents/ui/main.qml"
check_file "$PACKAGE_ROOT/contents/ui/config.qml"

IMPORT_ROOT="$PACKAGE_ROOT/contents/imports/io/team7/scene"
check_nonempty_file "$IMPORT_ROOT/qmldir"
check_nonempty_file "$IMPORT_ROOT/libyakkai_scene_backend.so"
check_nonempty_file "$IMPORT_ROOT/libyakkai_scene_backendplugin.so"
check_nonempty_file "$IMPORT_ROOT/yakkai_scene_backend.qmltypes"

qml_files=(
    "$PACKAGE_ROOT/contents/ui/main.qml"
    "$PACKAGE_ROOT/contents/ui/config.qml"
    "$PACKAGE_ROOT/contents/ui/PlaylistConfig.qml"
    "$PACKAGE_ROOT/contents/ui/PlaylistPlayer.qml"
)

if command -v qmllint >/dev/null 2>&1; then
    qmllint "${qml_files[@]}"
else
    warn "qmllint not found; skipping QML lint"
fi

if [[ "$RUN_KPACKAGE" -eq 1 ]]; then
    command -v kpackagetool6 >/dev/null 2>&1 || fail "kpackagetool6 not found; install KDE Plasma package tools or pass --skip-kpackage"
    temp_install_root="$(mktemp -d)"
    cleanup() {
        rm -rf "$temp_install_root"
    }
    trap cleanup EXIT
    kpackagetool6 -t Plasma/Wallpaper -i "$PACKAGE_ROOT" -p "$temp_install_root" >/dev/null
fi

printf 'Package check passed: %s\n' "${PACKAGE_ROOT#$ROOT/}"
