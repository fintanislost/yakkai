#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_ROOT="$ROOT/wallpapers/io.team7.yakkai"
DEV_BUILD=0
CLEAN=0
INSTALL=1

usage() {
    cat <<'USAGE'
Usage: scripts/install-local.sh [--dev] [--clean] [--no-install]

Build the native scene backend, stage it into the Plasma wallpaper package,
validate the package, and install or update Yakkai for the current user.

Options:
  --dev         Use ./build and also build the standalone scene harness.
  --clean       Remove the selected build directory before configuring.
  --no-install  Build, stage, and validate without installing the package.
  -h, --help    Show this help.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dev)
            DEV_BUILD=1
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --no-install)
            INSTALL=0
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

if [[ "$DEV_BUILD" -eq 1 ]]; then
    BUILD_DIR="$ROOT/build"
else
    BUILD_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/yakkai/build"
fi

[[ -d "$PACKAGE_ROOT" ]] || fail "missing package root: ${PACKAGE_ROOT#$ROOT/}"
command -v cmake >/dev/null 2>&1 || fail "cmake not found"

if [[ "$INSTALL" -eq 1 ]]; then
    command -v kpackagetool6 >/dev/null 2>&1 || fail "kpackagetool6 not found"
fi

if [[ "$CLEAN" -eq 1 ]]; then
    [[ -n "$BUILD_DIR" && "$BUILD_DIR" != "/" ]] || fail "refusing to clean unsafe build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

cmake -S "$ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target yakkai_stage_wallyakkai_scene_import --parallel

if [[ "$DEV_BUILD" -eq 1 ]]; then
    cmake --build "$BUILD_DIR" --target yakkai_scene_harness --parallel
fi

if [[ "$INSTALL" -eq 1 ]]; then
    "$ROOT/scripts/check-package.sh"
else
    "$ROOT/scripts/check-package.sh" --skip-kpackage
fi

if [[ "$INSTALL" -eq 1 ]]; then
    installed_dir="${XDG_DATA_HOME:-$HOME/.local/share}/plasma/wallpapers/io.team7.yakkai"
    if [[ -d "$installed_dir" ]]; then
        kpackagetool6 -t Plasma/Wallpaper -u "$PACKAGE_ROOT"
    else
        kpackagetool6 -t Plasma/Wallpaper -i "$PACKAGE_ROOT"
    fi
    printf 'Installed Yakkai package: %s\n' "$installed_dir"
else
    printf 'Skipped install; package is staged at %s\n' "$PACKAGE_ROOT"
fi

if [[ "$DEV_BUILD" -eq 1 ]]; then
    printf 'Scene harness: %s\n' "$BUILD_DIR/native/scene_harness/yakkai_scene_harness"
fi

printf 'Done. Reopen Plasma wallpaper settings and choose Yakkai.\n'
