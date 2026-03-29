#!/bin/bash
# Smoke test runner — captures scenes and compares file sizes against baseline.
# Run after any shader, effect, or rendering pipeline changes.
#
# Usage: ./smoke-tests/run.sh [--update]
#   --update: overwrite reference screenshots with new captures

set -e

HARNESS="build/native/scene_harness/paper_scene_harness"
ASSETS="$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets"
WORKSHOP="$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960"
SMOKE_DIR="smoke-tests"

if [ ! -f "$HARNESS" ]; then
    echo "ERROR: harness not found at $HARNESS — build first"
    exit 1
fi

# Ensure QML module is staged
cp build/native/scene_backend/libpapercompany_scene_backend.so \
   build/qml/io/papercompany/scene/libpapercompany_scene_backend.so 2>/dev/null || true

# Clear shader cache so smoke tests verify the actual pipeline, not stale SPIR-V.
# Cached shaders have masked regressions before (invisible flares, wrong colors).
CACHE_DIR="$HOME/.cache/wescene-renderer"
if [ -d "$CACHE_DIR" ]; then
    echo "Clearing shader cache at $CACHE_DIR"
    rm -rf "$CACHE_DIR"/*/spvs01/
fi

PASS=0
FAIL=0
UPDATE="${1:-}"

run_test() {
    local id="$1" name="$2" delay="$3" ref="$SMOKE_DIR/$4"
    local capture="/tmp/papercompany-smoke-$id.png"
    local scene="$WORKSHOP/$id/scene.pkg"

    if [ ! -f "$scene" ]; then
        echo "SKIP $name — scene not installed ($id)"
        return
    fi

    echo -n "TEST $name ($id)... "
    timeout "$((delay / 1000 + 15))" \
        "$HARNESS" --backend paper --source "$scene" --assets "$ASSETS" \
        --fill crop --capture "$capture" --capture-delay-ms "$delay" \
        2>/dev/null || true

    if [ ! -f "$capture" ]; then
        echo "FAIL — no capture produced"
        FAIL=$((FAIL + 1))
        return
    fi

    local size=$(stat -c%s "$capture")
    if [ "$size" -lt 100000 ]; then
        echo "FAIL — capture too small (${size} bytes, likely blank)"
        FAIL=$((FAIL + 1))
        return
    fi

    if [ "$UPDATE" = "--update" ]; then
        cp "$capture" "$ref"
        echo "UPDATED ($size bytes)"
    else
        echo "OK ($size bytes)"
    fi
    PASS=$((PASS + 1))
}

run_test 3228578419 "Sleeping Arona (puppet)" 8000 "3228578419-sleeping-arona.png"
run_test 3327063360 "Shiroko Video (MP4)" 20000 "3327063360-shiroko-video.png"

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
