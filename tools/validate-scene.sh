#!/bin/bash
# Scene render validator — validates rendering quality without visual comparison.
# Used during active development iteration on specific scenes.
#
# Usage: ./tools/validate-scene.sh <scene_id> [capture_delay_ms] [expect_file]
#        ./tools/validate-scene.sh <scene_id> [capture_delay_ms] --probe-high-risk-layers ids
#        ./tools/validate-scene.sh <scene_id> [capture_delay_ms] --probe-layers ids [--probe-max-effects n] [--probe-puppet-final-mesh layer-card|image-space] [--probe-puppet-route-only]
#        ./tools/validate-scene.sh <scene_id> [capture_delay_ms] --puppet-simulation off|diagnostic|runtime
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
CACHE_ROOT="$HOME/.cache/wescene-renderer"

SCENE_ID="${1:?Usage: $0 <scene_id>}"
CAPTURE_DELAY="10000"
EXPECT_FILE=""
PROBE_LAYERS="${YAKKAI_PROBE_LAYERS:-}"
PROBE_HIGH_RISK_LAYERS="${YAKKAI_PROBE_HIGH_RISK_LAYERS:-}"
PROBE_MAX_EFFECTS="${YAKKAI_PROBE_MAX_EFFECTS:-}"
PROBE_PUPPET_FINAL_MESH="${YAKKAI_PROBE_PUPPET_FINAL_MESH:-}"
PROBE_PUPPET_ROUTE_ONLY="${YAKKAI_PROBE_PUPPET_ROUTE_ONLY:-}"
PUPPET_SIMULATION="${YAKKAI_PUPPET_SIMULATION:-}"

shift
if [ "${1:-}" != "" ] && [[ "${1:-}" != --* ]]; then
    CAPTURE_DELAY="$1"
    shift
fi
if [ "${1:-}" != "" ] && [[ "${1:-}" != --* ]]; then
    EXPECT_FILE="$1"
    shift
fi
while [ "$#" -gt 0 ]; do
    case "$1" in
        --probe-layers)
            PROBE_LAYERS="${2:-}"
            if [ -z "$PROBE_LAYERS" ]; then
                echo "FAIL: --probe-layers requires a comma-separated layer id list"
                exit 1
            fi
            shift 2
            ;;
        --probe-high-risk-layers)
            PROBE_HIGH_RISK_LAYERS="${2:-}"
            if [ -z "$PROBE_HIGH_RISK_LAYERS" ]; then
                echo "FAIL: --probe-high-risk-layers requires a comma-separated layer id list"
                exit 1
            fi
            shift 2
            ;;
        --probe-max-effects)
            PROBE_MAX_EFFECTS="${2:-}"
            if [[ ! "$PROBE_MAX_EFFECTS" =~ ^[0-9]+$ ]]; then
                echo "FAIL: --probe-max-effects requires a non-negative integer"
                exit 1
            fi
            shift 2
            ;;
        --probe-puppet-final-mesh)
            PROBE_PUPPET_FINAL_MESH="${2:-}"
            if [ "$PROBE_PUPPET_FINAL_MESH" != "layer-card" ] && [ "$PROBE_PUPPET_FINAL_MESH" != "image-space" ]; then
                echo "FAIL: --probe-puppet-final-mesh requires layer-card or image-space"
                exit 1
            fi
            shift 2
            ;;
        --probe-puppet-route-only)
            PROBE_PUPPET_ROUTE_ONLY="1"
            shift
            ;;
        --expect-file)
            EXPECT_FILE="${2:-}"
            if [ -z "$EXPECT_FILE" ]; then
                echo "FAIL: --expect-file requires a path"
                exit 1
            fi
            shift 2
            ;;
        --puppet-simulation)
            PUPPET_SIMULATION="${2:-}"
            if [ -z "$PUPPET_SIMULATION" ]; then
                echo "FAIL: --puppet-simulation requires off, diagnostic, or runtime"
                exit 1
            fi
            shift 2
            ;;
        *)
            echo "FAIL: unknown argument: $1"
            exit 1
            ;;
    esac
done

if [ -n "$PROBE_LAYERS" ] && [ -n "$PROBE_HIGH_RISK_LAYERS" ]; then
    echo "FAIL: use either --probe-layers or --probe-high-risk-layers, not both"
    exit 1
fi
if [ -n "$PROBE_MAX_EFFECTS" ] && [ -z "$PROBE_LAYERS" ] && [ -z "$PROBE_HIGH_RISK_LAYERS" ]; then
    echo "FAIL: --probe-max-effects requires --probe-layers or --probe-high-risk-layers"
    exit 1
fi
if [ -n "$PROBE_PUPPET_FINAL_MESH" ] && [ -z "$PROBE_LAYERS" ] && [ -z "$PROBE_HIGH_RISK_LAYERS" ]; then
    echo "FAIL: --probe-puppet-final-mesh requires --probe-layers or --probe-high-risk-layers"
    exit 1
fi
if [ -n "$PROBE_PUPPET_ROUTE_ONLY" ] && [ "$PROBE_PUPPET_ROUTE_ONLY" != "0" ] &&
   [ -z "$PROBE_LAYERS" ] && [ -z "$PROBE_HIGH_RISK_LAYERS" ]; then
    echo "FAIL: --probe-puppet-route-only requires --probe-layers or --probe-high-risk-layers"
    exit 1
fi

PROBE_KIND=""
PROBE_IDS=""
PROBE_TITLE=""
PROBE_ARGS=()
PUPPET_SIMULATION_ARGS=()
if [ -n "$PROBE_LAYERS" ]; then
    PROBE_KIND="layer"
    PROBE_IDS="$PROBE_LAYERS"
    PROBE_TITLE="Explicit Probe Replay"
    PROBE_ARGS=(--debug-effect-probe-layers "$PROBE_LAYERS")
elif [ -n "$PROBE_HIGH_RISK_LAYERS" ]; then
    PROBE_KIND="high-risk"
    PROBE_IDS="$PROBE_HIGH_RISK_LAYERS"
    PROBE_TITLE="High-Risk Probe Replay"
    PROBE_ARGS=(--debug-effect-probe-high-risk-layers "$PROBE_HIGH_RISK_LAYERS")
fi
if [ -n "$PROBE_MAX_EFFECTS" ]; then
    PROBE_ARGS+=(--debug-effect-probe-max-effects "$PROBE_MAX_EFFECTS")
fi
if [ -n "$PROBE_PUPPET_FINAL_MESH" ]; then
    PROBE_ARGS+=(--debug-puppet-effect-final-mesh "$PROBE_PUPPET_FINAL_MESH")
fi
if [ -n "$PROBE_PUPPET_ROUTE_ONLY" ] && [ "$PROBE_PUPPET_ROUTE_ONLY" != "0" ]; then
    PROBE_ARGS+=(--debug-puppet-effect-route-only)
fi
if [ -n "$PUPPET_SIMULATION" ]; then
    PUPPET_SIMULATION_ARGS=(--puppet-simulation "$PUPPET_SIMULATION")
fi

SCENE_PKG="$WORKSHOP/$SCENE_ID/scene.pkg"
CAPTURE="$OUTDIR/validate-$SCENE_ID.png"
LOG="$OUTDIR/validate-$SCENE_ID.log"
EFFECT_DEBUG_DIR="$OUTDIR/effect-captures-$SCENE_ID"
EFFECT_MANIFEST="$EFFECT_DEBUG_DIR/manifest.json"
PROBE_ROUTE_SUFFIX="effects"
if [ -n "$PROBE_PUPPET_ROUTE_ONLY" ] && [ "$PROBE_PUPPET_ROUTE_ONLY" != "0" ]; then
    PROBE_ROUTE_SUFFIX="route-only"
fi
PROBE_SUFFIX=$(printf '%s-%s-max-%s-final-%s-%s' "$PROBE_KIND" "$PROBE_IDS" "${PROBE_MAX_EFFECTS:-all}" "${PROBE_PUPPET_FINAL_MESH:-default}" "$PROBE_ROUTE_SUFFIX" | tr -c '[:alnum:]' '_')
PROBE_CAPTURE="$OUTDIR/validate-$SCENE_ID-probe-$PROBE_SUFFIX.png"
PROBE_LOG="$OUTDIR/validate-$SCENE_ID-probe-$PROBE_SUFFIX.log"
PROBE_EFFECT_DEBUG_DIR="$OUTDIR/effect-captures-$SCENE_ID-probe-$PROBE_SUFFIX"
PROBE_EFFECT_MANIFEST="$PROBE_EFFECT_DEBUG_DIR/manifest.json"

mkdir -p "$OUTDIR"

clear_shader_cache() {
    local count=0
    if [ -d "$CACHE_ROOT" ]; then
        count=$(find "$CACHE_ROOT" -path '*/spvs01' -type d 2>/dev/null | wc -l | tr -cd '0-9')
        find "$CACHE_ROOT" -path '*/spvs01' -type d -exec rm -rf {} + 2>/dev/null || true
    fi
    : "${count:=0}"
    echo "Cleared shader cache entries: $count"
}

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
if [ -n "$PROBE_LAYERS" ]; then
    echo "Probe layers: $PROBE_LAYERS"
fi
if [ -n "$PROBE_HIGH_RISK_LAYERS" ]; then
    echo "High-risk probe layers: $PROBE_HIGH_RISK_LAYERS"
fi
if [ -n "$PROBE_MAX_EFFECTS" ]; then
    echo "Probe max effects: $PROBE_MAX_EFFECTS"
fi
if [ -n "$PROBE_PUPPET_FINAL_MESH" ]; then
    echo "Probe puppet final mesh: $PROBE_PUPPET_FINAL_MESH"
fi
if [ -n "$PROBE_PUPPET_ROUTE_ONLY" ] && [ "$PROBE_PUPPET_ROUTE_ONLY" != "0" ]; then
    echo "Probe puppet route-only: enabled"
fi
if [ -n "$PUPPET_SIMULATION" ]; then
    echo "Puppet simulation: $PUPPET_SIMULATION"
fi
echo ""

# Run the harness and capture output
clear_shader_cache
rm -f "$CAPTURE" "$LOG"
rm -rf "$EFFECT_DEBUG_DIR"
HARNESS_STATUS=0
unset YAKKAI_PUPPET_SIMULATION
"$HARNESS" --backend paper \
    --source "$SCENE_PKG" \
    --assets "$ASSETS" \
    --fill crop \
    --capture "$CAPTURE" \
    --capture-delay-ms "$CAPTURE_DELAY" \
    --debug-effect-captures "$EFFECT_DEBUG_DIR" \
    "${PUPPET_SIMULATION_ARGS[@]}" \
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

if SCRIPT_SUMMARY=$(python3 tools/scene-script-log-summary.py "$LOG" 2>&1); then
    SCRIPT_BINDINGS=$(printf '%s\n' "$SCRIPT_SUMMARY" | awk -F= '/^scene-script-bindings=/{print $2}')
    SCRIPT_MEDIA_LAYERS=$(printf '%s\n' "$SCRIPT_SUMMARY" | awk -F= '/^unsupported-media-integration-layers=/{print $2}')
    SCRIPT_GAPS=$(printf '%s\n' "$SCRIPT_SUMMARY" | awk -F= '/^scene-script-gaps-total=/{print $2}')
    SCRIPT_VISIBLE=$(printf '%s\n' "$SCRIPT_SUMMARY" | awk '/  - visible:/{print $3}')
    SCRIPT_MEDIA=$(printf '%s\n' "$SCRIPT_SUMMARY" | awk '/  - media-runtime-only:/{print $3}')
    SCRIPT_HARMLESS=$(printf '%s\n' "$SCRIPT_SUMMARY" | awk '/  - harmless:/{print $3}')
    SCRIPT_UNKNOWN=$(printf '%s\n' "$SCRIPT_SUMMARY" | awk '/  - unknown:/{print $3}')
    : "${SCRIPT_BINDINGS:=0}"
    : "${SCRIPT_MEDIA_LAYERS:=0}"
    : "${SCRIPT_GAPS:=0}"
    : "${SCRIPT_VISIBLE:=0}"
    : "${SCRIPT_MEDIA:=0}"
    : "${SCRIPT_HARMLESS:=0}"
    : "${SCRIPT_UNKNOWN:=0}"
    SCRIPT_DETAIL="bindings=${SCRIPT_BINDINGS}; media-layers=${SCRIPT_MEDIA_LAYERS}; gaps=${SCRIPT_GAPS}; visible=${SCRIPT_VISIBLE}; media-runtime-only=${SCRIPT_MEDIA}; harmless=${SCRIPT_HARMLESS}; unknown=${SCRIPT_UNKNOWN}; detail=${LOG}"
    if [ "$SCRIPT_VISIBLE" -gt 0 ]; then
        check "SceneScript runtime gaps" "WARN" "$SCRIPT_DETAIL"
    elif [ "$SCRIPT_GAPS" -gt 0 ]; then
        check "SceneScript runtime gaps" "PASS" "$SCRIPT_DETAIL"
    else
        check "SceneScript runtime gaps" "PASS" "$SCRIPT_DETAIL"
    fi
else
    check "SceneScript runtime gaps" "WARN" "could not summarize script gaps"
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
        if PUPPET_ROUTE_GUARD=$(python3 tools/effect_route_guards.py "$EFFECT_MANIFEST" 2>&1); then
            check "Puppet effect viewport guard" "PASS" "$PUPPET_ROUTE_GUARD"
        else
            check "Puppet effect viewport guard" "FAIL" "$PUPPET_ROUTE_GUARD"
        fi
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

    if [ "$SCENE_ID" = "3476236738" ]; then
        if VISUAL_SENTINEL=$(python3 tools/scene_visual_sentinels.py "$SCENE_ID" "$CAPTURE" --log "$LOG" 2>&1); then
            check "Background visual sentinel" "PASS" "$VISUAL_SENTINEL"
        else
            check "Background visual sentinel" "FAIL" "$VISUAL_SENTINEL"
        fi
    fi
fi

if [ -n "$PROBE_KIND" ]; then
    echo ""
    echo "--- $PROBE_TITLE ---"
    clear_shader_cache
    rm -f "$PROBE_CAPTURE" "$PROBE_LOG"
    rm -rf "$PROBE_EFFECT_DEBUG_DIR"
    PROBE_STATUS=0
    "$HARNESS" --backend paper \
        --source "$SCENE_PKG" \
        --assets "$ASSETS" \
        --fill crop \
        --capture "$PROBE_CAPTURE" \
        --capture-delay-ms "$CAPTURE_DELAY" \
        --debug-effect-captures "$PROBE_EFFECT_DEBUG_DIR" \
        "${PUPPET_SIMULATION_ARGS[@]}" \
        "${PROBE_ARGS[@]}" \
        > "$PROBE_LOG" 2>&1 || PROBE_STATUS=$?

    if [ "$PROBE_STATUS" -eq 0 ]; then
        check "Probe harness execution" "PASS" "exit=0"
    else
        check "Probe harness execution" "FAIL" "exit=$PROBE_STATUS"
    fi

    if [ -f "$PROBE_EFFECT_MANIFEST" ]; then
        if PROBE_SUMMARY=$(tools/effect-capture-summary.py "$PROBE_EFFECT_MANIFEST" 2>&1); then
            if [ "$PROBE_KIND" = "high-risk" ]; then
                PROBE_DETAIL=$(printf '%s\n' "$PROBE_SUMMARY" \
                    | awk '/^high-risk-probe-layers:/{flag=1; print; next} /^layer-stage-counts:/{flag=0} flag{print}' \
                    | tr '\n' '; ' \
                    | cut -c1-420)
            else
                PROBE_DETAIL=$(tools/effect-candidate-inventory.py "$PROBE_EFFECT_MANIFEST" 2>/dev/null \
                    | awk '/^records:/{flag=1; next} flag && /probe-only/{print}' \
                    | tr '\n' '; ' \
                    | cut -c1-420)
            fi
            check "Probe effect manifest" "PASS" "$PROBE_EFFECT_MANIFEST"
            check "Probe report" "PASS" "${PROBE_DETAIL:-no probe-only rows}"
        else
            check "Probe effect manifest" "FAIL" "could not summarize $PROBE_EFFECT_MANIFEST"
        fi
    else
        check "Probe effect manifest" "FAIL" "missing $PROBE_EFFECT_MANIFEST"
    fi

    if [ -f "$PROBE_CAPTURE" ]; then
        PROBE_FILESIZE=$(stat -c%s "$PROBE_CAPTURE")
        if [ "$PROBE_FILESIZE" -lt 50000 ]; then
            check "Probe capture produced" "FAIL" "too small (${PROBE_FILESIZE} bytes)"
        else
            check "Probe capture produced" "PASS" "$(( PROBE_FILESIZE / 1024 ))KB"
        fi

        if [ -f "$CAPTURE" ]; then
            PROBE_RMSE_RAW=$(compare -metric RMSE "$CAPTURE" "$PROBE_CAPTURE" null: 2>&1 || true)
            PROBE_RMSE=$(printf '%s' "$PROBE_RMSE_RAW" | sed -n 's/.*(\([^)]*\)).*/\1/p')
            : "${PROBE_RMSE:=$PROBE_RMSE_RAW}"
            check "Probe final-frame delta" "PASS" "baseline-vs-probe RMSE=$PROBE_RMSE"
        fi

        if VISUAL_SENTINEL=$(python3 tools/scene_visual_sentinels.py "$SCENE_ID" "$PROBE_CAPTURE" --baseline "$CAPTURE" --log "$PROBE_LOG" 2>&1); then
            VISUAL_SENTINEL_ONE_LINE=$(printf '%s' "$VISUAL_SENTINEL" | tr '\n' '; ' | cut -c1-420)
            check "Probe visual sentinel/delta" "PASS" "$VISUAL_SENTINEL_ONE_LINE"
        else
            VISUAL_SENTINEL_ONE_LINE=$(printf '%s' "$VISUAL_SENTINEL" | tr '\n' '; ' | cut -c1-420)
            check "Probe visual sentinel/delta" "FAIL" "$VISUAL_SENTINEL_ONE_LINE"
        fi
    else
        check "Probe capture produced" "FAIL" "missing $PROBE_CAPTURE"
    fi
fi

# === Summary ===
echo ""
echo "=== Results: $PASS passed, $FAIL failed, $WARN warnings ==="
echo "Log: $LOG"
echo "Capture: $CAPTURE"
if [ -n "$PROBE_KIND" ]; then
    echo "Probe log: $PROBE_LOG"
    echo "Probe capture: $PROBE_CAPTURE"
fi

[ "$FAIL" -eq 0 ] || exit 1
