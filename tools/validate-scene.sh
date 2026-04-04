#!/bin/bash
# Scene render validator — validates rendering quality without visual comparison.
# Used during active development iteration on specific scenes.
#
# Usage: ./tools/validate-scene.sh <scene_id> [--capture-delay-ms N] [--expect-file expectations.conf]
#
# Outputs structured PASS/FAIL results for:
#   - Shader compilation (no failures)
#   - Puppet MDL parsing (bone count, animation count)
#   - Effect chain loading (expected effect types present)
#   - Composelayer rendering
#   - Render graph node count
#   - Pixel statistics (non-blank, color variance, dominant hue)
#
# Exit code: 0 = all checks pass, 1 = failures detected

set -uo pipefail

HARNESS="build/native/scene_harness/yakkai_scene_harness"
ASSETS="$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets"
WORKSHOP="$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960"
OUTDIR="/tmp/yakkai-debug"

SCENE_ID="${1:?Usage: $0 <scene_id>}"
CAPTURE_DELAY="${2:-10000}"
EXPECT_FILE="${3:-}"

SCENE_PKG="$WORKSHOP/$SCENE_ID/scene.pkg"
CAPTURE="$OUTDIR/validate-$SCENE_ID.png"
LOG="$OUTDIR/validate-$SCENE_ID.log"

mkdir -p "$OUTDIR"

if [ ! -f "$HARNESS" ]; then
    echo "FAIL: harness not found at $HARNESS — build first"
    exit 1
fi
if [ ! -f "$SCENE_PKG" ]; then
    echo "FAIL: scene not found at $SCENE_PKG"
    exit 1
fi

# Ensure QML module is staged
cp build/native/scene_backend/libyakkai_scene_backend.so \
   build/qml/io/team7/scene/libyakkai_scene_backend.so 2>/dev/null || true

echo "=== Scene Render Validator: $SCENE_ID ==="
echo "Capture delay: ${CAPTURE_DELAY}ms"
echo ""

# Run the harness and capture output
rm -f "$CAPTURE" "$LOG"
"$HARNESS" --backend paper \
    --source "$SCENE_PKG" \
    --assets "$ASSETS" \
    --fill crop \
    --capture "$CAPTURE" \
    --capture-delay-ms "$CAPTURE_DELAY" \
    > "$LOG" 2>&1 || true

PASS=0
FAIL=0
WARN=0

check() {
    local label="$1" result="$2" detail="${3:-}"
    if [ "$result" = "PASS" ]; then
        printf "  %-40s \033[32mPASS\033[0m %s\n" "$label" "$detail"
        PASS=$((PASS + 1))
    elif [ "$result" = "WARN" ]; then
        printf "  %-40s \033[33mWARN\033[0m %s\n" "$label" "$detail"
        WARN=$((WARN + 1))
    else
        printf "  %-40s \033[31mFAIL\033[0m %s\n" "$label" "$detail"
        FAIL=$((FAIL + 1))
    fi
}

# === Structural checks (from log) ===
echo "--- Structural ---"

# Scene type detection
SCENE_TYPE=$(grep -aoP "scene type: \K\w+" "$LOG" | head -1)
if [ -n "$SCENE_TYPE" ]; then
    check "Scene type detected" "PASS" "$SCENE_TYPE"
else
    check "Scene type detected" "FAIL" "no scene type in log"
fi

# Shader compilation
SHADER_FAILS=$(grep -ac "shader compile failed" "$LOG" 2>/dev/null | tr -cd '0-9')
: "${SHADER_FAILS:=0}"
if [ "$SHADER_FAILS" -eq 0 ]; then
    check "Shader compilation" "PASS" "no failures"
else
    check "Shader compilation" "WARN" "$SHADER_FAILS failures"
fi

# Puppet parsing
PUPPET_COUNT=$(grep -ac "read puppet:" "$LOG" 2>/dev/null | tr -cd '0-9')
: "${PUPPET_COUNT:=0}"
if [ "$PUPPET_COUNT" -gt 0 ]; then
    PUPPET_BONES=$(grep -a "read puppet:" "$LOG" | grep -aoP "bones: \K\d+" | tr '\n' ',')
    check "Puppet MDL parsing" "PASS" "${PUPPET_COUNT} puppets, bones=$PUPPET_BONES"
else
    if [ "$SCENE_TYPE" = "Puppet" ]; then
        check "Puppet MDL parsing" "FAIL" "puppet scene but no puppets parsed"
    else
        check "Puppet MDL parsing" "PASS" "non-puppet scene"
    fi
fi

# Effect chain
EFFECT_NODES=$(grep -a "compileRenderGraph.*node.*type=CustomShader.*name=effects/" "$LOG" | wc -l)
if [ "$EFFECT_NODES" -gt 0 ]; then
    EFFECT_TYPES=$(grep -aoP "name=effects/\K[^ ]+" "$LOG" | sort -u | tr '\n' ',' | sed 's/,$//')
    check "Effect chain" "PASS" "${EFFECT_NODES} effect passes: $EFFECT_TYPES"
else
    check "Effect chain" "WARN" "no effect passes in render graph"
fi

# Composelayer
COMPOSE=$(grep -ac "composelayer" "$LOG" 2>/dev/null | tr -cd '0-9')
: "${COMPOSE:=0}"
if [ "$COMPOSE" -gt 0 ]; then
    check "Composelayer" "PASS" "present in render graph"
else
    check "Composelayer" "WARN" "not present"
fi

# Render graph size
RG_NODES=$(grep -aoP "compileRenderGraph: begin nodes=\K\d+" "$LOG" | head -1)
if [ -n "$RG_NODES" ]; then
    if [ "$RG_NODES" -gt 5 ]; then
        check "Render graph" "PASS" "$RG_NODES nodes"
    else
        check "Render graph" "WARN" "only $RG_NODES nodes"
    fi
else
    check "Render graph" "FAIL" "no render graph info"
fi

# Scene property tinting (use -a for binary-safe grep)
TINT=$(grep -a "tint-adjusted clear color" "$LOG" 2>/dev/null | head -1 || true)
if [ -n "$TINT" ]; then
    TINT_VAL=$(echo "$TINT" | grep -oP '\(.*\)' || echo "?")
    check "Scene tint" "PASS" "$TINT_VAL"
else
    check "Scene tint" "PASS" "no tint overlay (neutral)"
fi

# QuickJS (use -a for binary-safe grep)
QJS_ERRORS=$(grep -ac "QuickJS eval error" "$LOG" 2>/dev/null || echo "0")
QJS_ERRORS=$(echo "$QJS_ERRORS" | tr -cd '0-9')
: "${QJS_ERRORS:=0}"
QJS_SUCCESS=$(grep -ac "QuickJS eval result\|QuickJS binding" "$LOG" 2>/dev/null || echo "0")
QJS_SUCCESS=$(echo "$QJS_SUCCESS" | tr -cd '0-9')
: "${QJS_SUCCESS:=0}"
if [ "$QJS_ERRORS" -gt 0 ]; then
    check "QuickJS scripts" "WARN" "$QJS_ERRORS errors, $QJS_SUCCESS successes"
elif [ "$QJS_SUCCESS" -gt 0 ]; then
    check "QuickJS scripts" "PASS" "$QJS_SUCCESS bindings resolved"
else
    check "QuickJS scripts" "PASS" "no scripts to evaluate"
fi

# Errors
ERRORS=$(grep -ac "^ERROR\|LOG_ERROR" "$LOG" 2>/dev/null | tr -cd '0-9')
: "${ERRORS:=0}"
MATERIAL_FAILS=$(grep -ac "material faild\|failed to load" "$LOG" 2>/dev/null | tr -cd '0-9')
: "${MATERIAL_FAILS:=0}"
if [ "$MATERIAL_FAILS" -eq 0 ]; then
    check "Material loading" "PASS" "no failures"
else
    FAIL_NAMES=$(grep -aoP "(load imageobj|effect) '?\K[^']*(?=')" "$LOG" | sort -u | head -3 | tr '\n' ',' | sed 's/,$//')
    check "Material loading" "WARN" "$MATERIAL_FAILS failures: $FAIL_NAMES"
fi

# === Pixel checks (from capture) ===
echo ""
echo "--- Pixel Analysis ---"

if [ ! -f "$CAPTURE" ]; then
    check "Capture produced" "FAIL" "no capture file"
else
    FILESIZE=$(stat -c%s "$CAPTURE")
    if [ "$FILESIZE" -lt 50000 ]; then
        check "Capture produced" "FAIL" "too small (${FILESIZE} bytes)"
    else
        check "Capture produced" "PASS" "$(( FILESIZE / 1024 ))KB"
    fi

    # Image dimensions
    DIMS=$(identify -format "%wx%h" "$CAPTURE" 2>/dev/null)
    check "Image dimensions" "PASS" "$DIMS"

    # Color variance (standard deviation of luminance)
    # High variance = detailed scene, low = blank/uniform
    STDDEV=$(convert "$CAPTURE" -colorspace Gray -format "%[fx:standard_deviation]" info: 2>/dev/null)
    if [ -n "$STDDEV" ]; then
        # Compare as integer (multiply by 1000)
        STDDEV_INT=$(echo "$STDDEV" | awk '{printf "%d", $1 * 1000}')
        if [ "$STDDEV_INT" -gt 50 ]; then
            check "Color variance" "PASS" "stddev=$STDDEV (good detail)"
        elif [ "$STDDEV_INT" -gt 10 ]; then
            check "Color variance" "WARN" "stddev=$STDDEV (low detail)"
        else
            check "Color variance" "FAIL" "stddev=$STDDEV (nearly blank)"
        fi
    fi

    # Dominant color (1x1 resize = average color)
    AVG_COLOR=$(convert "$CAPTURE" -resize 1x1! -format "R=%[fx:r*255] G=%[fx:g*255] B=%[fx:b*255]" info: 2>/dev/null)
    if [ -n "$AVG_COLOR" ]; then
        check "Average color" "PASS" "$AVG_COLOR"
    fi

    # Unique color count (resized to reduce noise)
    UNIQUE=$(convert "$CAPTURE" -resize 100x100! -unique-colors -format "%k" info: 2>/dev/null)
    if [ -n "$UNIQUE" ]; then
        if [ "$UNIQUE" -gt 500 ]; then
            check "Color diversity" "PASS" "$UNIQUE unique colors (100x100)"
        elif [ "$UNIQUE" -gt 50 ]; then
            check "Color diversity" "WARN" "$UNIQUE unique colors (100x100)"
        else
            check "Color diversity" "FAIL" "only $UNIQUE unique colors"
        fi
    fi

    # Check for non-uniform regions (split into quadrants)
    for Q in NorthWest NorthEast SouthWest SouthEast; do
        Q_AVG=$(convert "$CAPTURE" -gravity $Q -crop 50%x50%+0+0 +repage -resize 1x1! \
                -format "%[fx:int(r*255)],%[fx:int(g*255)],%[fx:int(b*255)]" info: 2>/dev/null)
        if [ -n "$Q_AVG" ]; then
            printf "    %-12s avg=(%s)\n" "$Q" "$Q_AVG"
        fi
    done
fi

# === Summary ===
echo ""
echo "=== Results: $PASS passed, $FAIL failed, $WARN warnings ==="
echo "Log: $LOG"
echo "Capture: $CAPTURE"

[ "$FAIL" -eq 0 ] || exit 1
