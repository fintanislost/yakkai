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
EFFECT_DEBUG_DIR="$OUTDIR/effect-captures-$SCENE_ID"
EFFECT_MANIFEST="$EFFECT_DEBUG_DIR/manifest.json"

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
rm -rf "$EFFECT_DEBUG_DIR"
HARNESS_STATUS=0
"$HARNESS" --backend paper \
    --source "$SCENE_PKG" \
    --assets "$ASSETS" \
    --fill crop \
    --capture "$CAPTURE" \
    --capture-delay-ms "$CAPTURE_DELAY" \
    --debug-effect-captures "$EFFECT_DEBUG_DIR" \
    > "$LOG" 2>&1 || HARNESS_STATUS=$?

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

if [ "$HARNESS_STATUS" -eq 0 ]; then
    check "Harness execution" "PASS" "exit=0"
else
    check "Harness execution" "FAIL" "exit=$HARNESS_STATUS"
fi

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

# Effect capture manifest checks
if [ -f "$EFFECT_MANIFEST" ]; then
    if WATER_COUNTS=$(python3 - "$EFFECT_MANIFEST" <<'PY'
import json
import sys

manifest_path = sys.argv[1]
with open(manifest_path, "r", encoding="utf-8") as handle:
    manifest = json.load(handle)

if manifest.get("status") != "ok":
    raise SystemExit(f"manifest status is {manifest.get('status')!r}")

def required_list(name):
    value = manifest.get(name)
    if not isinstance(value, list):
        raise SystemExit(f"manifest field {name!r} is not a list")
    return value

captures = required_list("captures")
stripped_candidates = required_list("strippedCandidates")

failures = manifest.get("failures", [])
if not isinstance(failures, list):
    raise SystemExit("manifest field 'failures' is not a list")
if failures:
    raise SystemExit(f"manifest has {len(failures)} top-level failures")

for record in captures:
    if not isinstance(record, dict):
        raise SystemExit("manifest capture entry is not an object")
    if record.get("failed") is True or record.get("completed") is False:
        label = record.get("label") or record.get("stage") or "unknown"
        reason = record.get("failureReason") or "unknown"
        raise SystemExit(f"manifest capture failed: {label}: {reason}")

def capture_layer(record):
    layer = record.get("layer") if isinstance(record, dict) else None
    return layer if isinstance(layer, dict) else {}

def candidate_layer(record):
    if not isinstance(record, dict):
        return {}
    layer = record.get("layer")
    return layer if isinstance(layer, dict) else record

def layer_key(layer):
    return str(layer.get("layerId", "unknown")) + ":" + str(layer.get("layerName") or "unnamed")

allowed = {}
for record in captures:
    layer = capture_layer(record)
    policy = layer.get("policy") if isinstance(layer.get("policy"), dict) else {}
    if layer.get("candidateRisk") == "simple-water" and policy.get("keepEffects") is True and policy.get("strippedEffects") is not True:
        allowed[layer_key(layer)] = layer

stripped_simple = 0
stripped_mixed = 0
for candidate in stripped_candidates:
    layer = candidate_layer(candidate)
    if layer.get("candidateRisk") == "simple-water":
        stripped_simple += 1
    if layer.get("candidateRisk") == "mixed-chain":
        stripped_mixed += 1

print(f"allowed_simple_water={len(allowed)}")
print(f"stripped_simple_water={stripped_simple}")
print(f"stripped_mixed_chain={stripped_mixed}")
PY
); then
        check "Effect capture manifest" "PASS" "$EFFECT_MANIFEST"
        ALLOWED_SIMPLE_WATER=$(printf '%s\n' "$WATER_COUNTS" | awk -F= '/^allowed_simple_water=/{print $2}')
        STRIPPED_SIMPLE_WATER=$(printf '%s\n' "$WATER_COUNTS" | awk -F= '/^stripped_simple_water=/{print $2}')
        STRIPPED_MIXED_CHAIN=$(printf '%s\n' "$WATER_COUNTS" | awk -F= '/^stripped_mixed_chain=/{print $2}')
        : "${ALLOWED_SIMPLE_WATER:=0}"
        : "${STRIPPED_SIMPLE_WATER:=0}"
        : "${STRIPPED_MIXED_CHAIN:=0}"

        if [ "$SCENE_ID" = "3476236738" ]; then
            if [ "$ALLOWED_SIMPLE_WATER" -gt 0 ]; then
                check "Allowed simple-water candidates" "PASS" "$ALLOWED_SIMPLE_WATER allowed"
            else
                check "Allowed simple-water candidates" "FAIL" "expected at least one allowed simple-water candidate"
            fi
            if [ "$STRIPPED_SIMPLE_WATER" -eq 0 ]; then
                check "Simple-water no longer stripped" "PASS" "0 stripped"
            else
                check "Simple-water no longer stripped" "FAIL" "$STRIPPED_SIMPLE_WATER stripped"
            fi
            if [ "$STRIPPED_MIXED_CHAIN" -gt 0 ]; then
                check "Mixed water chains remain stripped" "PASS" "$STRIPPED_MIXED_CHAIN stripped"
            else
                check "Mixed water chains remain stripped" "WARN" "no mixed-chain stripped candidates found"
            fi
        elif [ "$SCENE_ID" = "3228578419" ]; then
            if [ "$ALLOWED_SIMPLE_WATER" -eq 0 ]; then
                check "Protected scene simple-water block" "PASS" "0 allowed"
            else
                check "Protected scene simple-water block" "FAIL" "$ALLOWED_SIMPLE_WATER allowed"
            fi
        fi
    else
        check "Effect capture manifest" "FAIL" "could not parse or validate $EFFECT_MANIFEST"
    fi
else
    check "Effect capture manifest" "FAIL" "missing $EFFECT_MANIFEST"
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
