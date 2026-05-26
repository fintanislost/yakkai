#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_ROOT="$ROOT/wallpapers/io.team7.yakkai"
OUTPUT_DIR="$ROOT/dist"
VERSION="$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || date +%Y%m%d)"
TARGET="linux-$(uname -m)"
SOURCE_REF="HEAD"
BUILD=1
STRIP_BINARIES=1

usage() {
    cat <<'USAGE'
Usage: scripts/package-release.sh [options]

Build and package Yakkai release assets:
  - prebuilt Plasma wallpaper package tarball
  - source tarball fallback
  - SHA256SUMS

Options:
  --output-dir DIR  Write release assets to DIR. Default: ./dist
  --version VALUE   Use VALUE in asset names. Default: git describe output
  --target VALUE    Use VALUE in the prebuilt asset name. Default: linux-$(uname -m)
  --source-ref REF  Git ref used for the source tarball. Default: HEAD
  --skip-build      Package the currently staged wallpaper package without rebuilding.
  --no-strip        Do not strip copied shared libraries in the prebuilt package.
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

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "$1 not found"
}

sanitize_asset_part() {
    local value="$1"
    value="${value//\//-}"
    value="${value// /-}"
    printf '%s\n' "$value"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output-dir)
            [[ $# -ge 2 ]] || fail "--output-dir requires a value"
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --version)
            [[ $# -ge 2 ]] || fail "--version requires a value"
            VERSION="$2"
            shift 2
            ;;
        --target)
            [[ $# -ge 2 ]] || fail "--target requires a value"
            TARGET="$2"
            shift 2
            ;;
        --source-ref)
            [[ $# -ge 2 ]] || fail "--source-ref requires a value"
            SOURCE_REF="$2"
            shift 2
            ;;
        --skip-build)
            BUILD=0
            shift
            ;;
        --no-strip)
            STRIP_BINARIES=0
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

VERSION="$(sanitize_asset_part "$VERSION")"
TARGET="$(sanitize_asset_part "$TARGET")"

require_command git
require_command tar
require_command gzip
require_command sha256sum

[[ -d "$PACKAGE_ROOT" ]] || fail "missing package root: ${PACKAGE_ROOT#$ROOT/}"

if [[ "$BUILD" -eq 1 ]]; then
    "$ROOT/scripts/install-local.sh" --no-install
else
    "$ROOT/scripts/check-package.sh"
fi

binary_asset_root="yakkai-plasma6-${TARGET}-${VERSION}"
source_asset_root="yakkai-source-${VERSION}"
binary_asset="${binary_asset_root}.tar.gz"
source_asset="${source_asset_root}.tar.gz"

temp_root="$(mktemp -d)"
cleanup() {
    rm -rf "$temp_root"
}
trap cleanup EXIT

mkdir -p "$OUTPUT_DIR"

binary_root="$temp_root/$binary_asset_root"
mkdir -p "$binary_root/wallpapers"
cp -a "$PACKAGE_ROOT" "$binary_root/wallpapers/"
cp "$ROOT/LICENSE" "$binary_root/LICENSE"
cp "$ROOT/README.md" "$binary_root/README.md"

find "$binary_root/wallpapers/io.team7.yakkai" -type d -name __pycache__ -prune -exec rm -rf {} +
find "$binary_root/wallpapers/io.team7.yakkai" -type f -name '*.pyc' -delete

cat > "$binary_root/README-release.md" <<README_RELEASE
# Yakkai Prebuilt Plasma Package

Version: $VERSION
Target: $TARGET

This archive contains a prebuilt Plasma 6 wallpaper package with the native
Wallpaper Engine scene QML module already staged.

Install or update for the current user:

\`\`\`bash
./install.sh
\`\`\`

If the wallpaper package fails to load because Qt, KDE Frameworks, FFmpeg, or
other native library versions do not match this build target, use the matching
\`yakkai-source-$VERSION.tar.gz\` release asset and run:

\`\`\`bash
./scripts/install-local.sh
\`\`\`
README_RELEASE

cat > "$binary_root/install.sh" <<'INSTALL_SCRIPT'
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_ROOT="$ROOT/wallpapers/io.team7.yakkai"

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

[[ -d "$PACKAGE_ROOT" ]] || fail "missing package root: $PACKAGE_ROOT"
command -v kpackagetool6 >/dev/null 2>&1 || fail "kpackagetool6 not found"

installed_dir="${XDG_DATA_HOME:-$HOME/.local/share}/plasma/wallpapers/io.team7.yakkai"
if [[ -d "$installed_dir" ]]; then
    kpackagetool6 -t Plasma/Wallpaper -u "$PACKAGE_ROOT"
else
    kpackagetool6 -t Plasma/Wallpaper -i "$PACKAGE_ROOT"
fi

printf 'Installed Yakkai package: %s\n' "$installed_dir"
printf 'Reopen Plasma wallpaper settings and choose Yakkai.\n'
INSTALL_SCRIPT
chmod +x "$binary_root/install.sh"

if [[ "$STRIP_BINARIES" -eq 1 ]]; then
    if command -v strip >/dev/null 2>&1; then
        find "$binary_root/wallpapers/io.team7.yakkai/contents/imports" \
            -type f -name '*.so' -exec strip --strip-unneeded {} + 2>/dev/null || \
            warn "failed to strip one or more shared libraries"
    else
        warn "strip not found; leaving shared libraries unstripped"
    fi
fi

tar -C "$temp_root" -czf "$OUTPUT_DIR/$binary_asset" "$binary_asset_root"
git -C "$ROOT" archive --format=tar --prefix="$source_asset_root/" "$SOURCE_REF" | \
    gzip -n > "$OUTPUT_DIR/$source_asset"

(
    cd "$OUTPUT_DIR"
    sha256sum "$binary_asset" "$source_asset" > SHA256SUMS
)

printf 'Created release assets in %s:\n' "$OUTPUT_DIR"
printf '  %s\n' "$binary_asset"
printf '  %s\n' "$source_asset"
printf '  SHA256SUMS\n'
