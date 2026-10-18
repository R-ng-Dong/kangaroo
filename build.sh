#!/bin/bash

# ============================================================
# esp32App - ESP-IDF build, flash and monitor script
# ============================================================
# One script for the whole cycle. It finds the ESP-IDF install and
# drives idf.py - no need to source export.sh first, and no second
# script for flashing.
#
# Usage:
#   ./build.sh                          build (incremental)
#   ./build.sh rebuild                  fullclean, then build
#   ./build.sh clean                    remove the build directory
#   ./build.sh flash                    build, then flash
#   ./build.sh monitor                  open the serial monitor (no build)
#   ./build.sh menuconfig               open the config menu
#   ./build.sh reconfigure              regenerate the build configuration
#   ./build.sh --list                   show the ESP-IDF installs that were found
#
# Options:
#   --idf <path>          use a specific ESP-IDF installation
#   --port <dev>          serial device for flash/monitor, e.g. /dev/ttyUSB0
#                         (default: whatever esptool auto-detects)
#   --release             also apply sdkconfig.defaults.release;
#                         regenerates sdkconfig when it is not already release
#   -B, --build-dir <d>   build directory (default: <project>/build)
#   -h, --help            this help
#
# Everything after "--" is passed straight to idf.py.
#
# A plain build is incremental, because that is what an edit-build loop needs;
# "rebuild" is the clean one. flash rebuilds incrementally too, so repeated
# flashing stays fast. monitor never builds: it just attaches to the port,
# so a stale or missing build directory costs nothing.
#
# ESP-IDF search order:
#   1. --idf argument
#   2. idf.py already in PATH (shell already exported)
#   3. IDF_PATH environment variable
#   4. installs registered in ~/.espressif/idf-env.json
#   5. common install locations, newest version first

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FORCE_IDF=""
LIST_ONLY=0
FOUND_BY=""
PORT=""
ACTION="build"
RELEASE=0
BUILD_DIR="$PROJECT_DIR/build"

# ------------------------------------------------------------
# argument parsing
# ------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        --idf)
            [ $# -ge 2 ] || { echo "[ERROR] --idf needs a path"; exit 1; }
            FORCE_IDF="$2"; shift 2 ;;
        --idf=*)
            FORCE_IDF="${1#--idf=}"; shift ;;
        --port)
            [ $# -ge 2 ] || { echo "[ERROR] --port needs a serial device, e.g. /dev/ttyUSB0"; exit 1; }
            PORT="$2"; shift 2 ;;
        --port=*)
            PORT="${1#--port=}"; shift ;;
        -B|--build-dir)
            [ $# -ge 2 ] || { echo "[ERROR] $1 needs a path"; exit 1; }
            BUILD_DIR="$2"; shift 2 ;;
        --build-dir=*)
            BUILD_DIR="${1#--build-dir=}"; shift ;;
        --release)
            RELEASE=1; shift ;;
        --list|-l)
            LIST_ONLY=1; shift ;;
        build|rebuild|clean|flash|monitor|menuconfig|reconfigure)
            ACTION="$1"; shift ;;
        --)
            shift; break ;;
        -h|--help)
            awk 'NR >= 3 && /^#/ { sub(/^# ?/, ""); print; next } NR >= 3 { exit }' "${BASH_SOURCE[0]}"
            exit 0 ;;
        *)
            echo "[ERROR] Unknown argument: $1"
            echo "        Run './build.sh --help' for the list."
            exit 1 ;;
    esac
done
EXTRA_ARGS=("$@")

# ------------------------------------------------------------
# helpers
# ------------------------------------------------------------

# A directory counts as an ESP-IDF install if export.sh + tools/idf.py exist.
is_idf_dir() {
    [ -n "$1" ] && [ -f "$1/export.sh" ] && [ -f "$1/tools/idf.py" ]
}

add_candidate() {
    local p="${1%/}"
    is_idf_dir "$p" || return 0
    case " ${CANDIDATES[*]-} " in
        *" $p "*) return 0 ;;
    esac
    CANDIDATES+=("$p")
}

# Best-effort version string, used to label an install.
idf_version_of() {
    local dir="$1" v="" hdr="$1/components/esp_common/include/esp_idf_version.h"
    if [ -f "$dir/version.txt" ]; then
        v="$(head -n1 "$dir/version.txt" 2>/dev/null | tr -d '[:space:]')"
    elif [ -f "$hdr" ]; then
        # #define ESP_IDF_VERSION_MAJOR 5 / _MINOR 1 / _PATCH 3
        v="$(awk '/#define ESP_IDF_VERSION_(MAJOR|MINOR|PATCH)/ {printf "%s%s", sep, $3; sep="."} END {print ""}' "$hdr")"
    elif git -C "$dir" describe --tags >/dev/null 2>&1; then
        v="$(git -C "$dir" describe --tags 2>/dev/null)"
    fi
    v="${v#v}"
    [ -n "$v" ] || v="$(basename "$dir")"
    printf '%s' "$v"
}

# Sort key: anything that is not a version number ranks last.
idf_sort_key() {
    case "$(idf_version_of "$1")" in
        [0-9]*) idf_version_of "$1" ;;
        *) echo "0.0.0" ;;
    esac
}

# Sourcing export.sh is the only reliable "does this install really work?"
# test (it fails when the toolchains/python env are missing).
# IDF_PYTHON_ENV_PATH is dropped first: it pins the python env to the version
# that was exported before, which aborts the activation of a different version.
# The exported idf.py must also belong to this install, otherwise a stale
# idf.py left on PATH by an already-exported ESP-IDF would pass the check.
is_usable() {
    bash -c '
        unset IDF_PYTHON_ENV_PATH
        source "$1/export.sh" >/dev/null 2>&1 || exit 1
        idf_py="$(command -v idf.py 2>/dev/null)" || exit 1
        case "$idf_py" in "$1"/tools/*) exit 0 ;; *) exit 1 ;; esac
    ' _ "$1"
}

sort_candidates_newest_first() {
    local p
    for p in "${CANDIDATES[@]}"; do
        printf '%s|%s\n' "$(idf_sort_key "$p")" "$p"
    done | sort -t'|' -k1,1Vr -k2,2 | cut -d'|' -f2-
}

# ------------------------------------------------------------
# candidate discovery
# ------------------------------------------------------------
CANDIDATES=()

# 1. --idf
if [ -n "$FORCE_IDF" ]; then
    is_idf_dir "$FORCE_IDF" || {
        echo "[ERROR] ESP-IDF not found at: $FORCE_IDF"
        exit 1
    }
    add_candidate "$FORCE_IDF"
    FOUND_BY="--idf"
fi

# 2. idf.py already in PATH
if [ -z "$FORCE_IDF" ] && command -v idf.py >/dev/null 2>&1; then
    add_candidate "$(cd "$(dirname "$(command -v idf.py)")/.." && pwd)"
    FOUND_BY="PATH (already exported)"
fi

# 3. IDF_PATH environment variable
if [ -z "$FORCE_IDF" ] && [ -n "${IDF_PATH:-}" ]; then
    if is_idf_dir "$IDF_PATH"; then
        add_candidate "$IDF_PATH"
        [ -n "$FOUND_BY" ] || FOUND_BY="IDF_PATH environment variable"
    else
        echo "[WARN] IDF_PATH is set but not a valid ESP-IDF install, ignoring:"
        echo "       $IDF_PATH"
        IDF_PATH=""
    fi
fi

# 4. installs registered by idf_tools.py
if [ -z "$FORCE_IDF" ] && command -v python3 >/dev/null 2>&1 && [ -f "$HOME/.espressif/idf-env.json" ]; then
    while IFS= read -r p; do
        add_candidate "$p"
    done < <(python3 - "$HOME/.espressif/idf-env.json" <<'PY' 2>/dev/null
import json, sys
try:
    data = json.load(open(sys.argv[1]))
except Exception:
    sys.exit(0)
for entry in (data.get("idfInstalled") or {}).values():
    path = entry.get("path")
    if path:
        print(path)
PY
)
fi

# 5. common install locations (auto mode only)
if [ -z "$FORCE_IDF" ] && [ -z "$FOUND_BY" ]; then
    for p in \
        "$HOME"/ESP-IDF/idf/*/*/esp-idf \
        "$HOME"/ESP-IDF/idf/*/esp-idf \
        "$HOME"/ESP-IDF/*/esp-idf \
        "$HOME"/ESP-IDF/esp-idf \
        "$HOME"/esp/esp-idf \
        "$HOME"/esp/*/esp-idf \
        /mnt/Espressif/*/esp-idf \
        /mnt/Espressif/* \
        /mnt/*/esp-idf \
        /opt/esp-idf \
        /opt/*/esp-idf \
        "$PROJECT_DIR"/../esp-idf \
        "$PROJECT_DIR"/esp-idf
    do
        add_candidate "$p"
    done
fi

if [ "${#CANDIDATES[@]}" -eq 0 ]; then
    echo "[ERROR] No ESP-IDF installation found. Searched:"
    echo "  - \$IDF_PATH / idf.py in PATH"
    echo "  - ~/.espressif/idf-env.json (idf_tools.py installs)"
    echo "  - \$HOME/ESP-IDF/**, \$HOME/esp/*, /mnt/Espressif/*, /opt/*, sibling repos"
    echo ""
    echo "Install one with:"
    echo "  git clone -b v5.1.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf"
    echo "  ~/esp/esp-idf/install.sh esp32c3"
    echo "or point this script at an existing one:"
    echo "  ./build.sh --idf /path/to/esp-idf"
    exit 1
fi

# newest version first (already-picked source wins if it is usable)
SORTED_CANDIDATES=()
while IFS= read -r p; do
    SORTED_CANDIDATES+=("$p")
done < <(sort_candidates_newest_first)
CANDIDATES=("${SORTED_CANDIDATES[@]}")

if [ "$LIST_ONLY" -eq 1 ]; then
    echo "ESP-IDF installations found:"
    for p in "${CANDIDATES[@]}"; do
        marker="  "
        is_usable "$p" && marker="* "
        printf '  %s%-10s %s\n' "$marker" "v$(idf_version_of "$p")" "$p"
    done
    echo ""
    echo "(* = exports successfully)"
    exit 0
fi

# ------------------------------------------------------------
# pick an install that actually exports
# ------------------------------------------------------------
IDF_DIR=""
if [ -n "$FOUND_BY" ] && is_usable "${CANDIDATES[0]}"; then
    IDF_DIR="${CANDIDATES[0]}"
else
    for p in "${CANDIDATES[@]}"; do
        if is_usable "$p"; then
            IDF_DIR="$p"
            FOUND_BY="${FOUND_BY:-auto-search}"
            break
        fi
    done
fi

if [ -z "$IDF_DIR" ]; then
    echo "[ERROR] Found ESP-IDF installs, but none can be exported:"
    for p in "${CANDIDATES[@]}"; do
        echo "  - v$(idf_version_of "$p")  $p"
    done
    echo ""
    echo "Toolchains are probably missing. Install them with:"
    echo "  python3 ${CANDIDATES[0]}/tools/idf_tools.py install"
    echo "  python3 ${CANDIDATES[0]}/tools/idf_tools.py install-python-env"
    exit 1
fi

# ------------------------------------------------------------
# load environment
# ------------------------------------------------------------
# If another ESP-IDF is already exported in this shell, IDF_PYTHON_ENV_PATH
# points at that version's python env. Sourcing export.sh on top would then
# check/activate the wrong venv, abort, and leave IDF_PATH, idf.py and the
# python env pointing at three different installs. Drop it so the chosen
# install selects its own python env.
unset IDF_PYTHON_ENV_PATH

EXPORT_LOG="$(mktemp)"
# shellcheck disable=SC1091
source "$IDF_DIR/export.sh" >"$EXPORT_LOG" 2>&1 || true

# export.sh returns 0 even when activation failed, so verify that the idf.py
# we will actually run belongs to the selected installation.
IDF_PY_RESOLVED="$(command -v idf.py 2>/dev/null || true)"
if [ "$(dirname "$IDF_PY_RESOLVED")" != "$IDF_DIR/tools" ]; then
    echo "[ERROR] Failed to export ESP-IDF environment: $IDF_DIR"
    echo "        idf.py resolved to: ${IDF_PY_RESOLVED:-<not found>}"
    echo "        IDF_PATH now       : ${IDF_PATH:-<unset>}"
    echo "        python env now     : ${IDF_PYTHON_ENV_PATH:-<unset>}"
    echo ""
    tail -n 20 "$EXPORT_LOG"
    rm -f "$EXPORT_LOG"
    echo ""
    echo "Another ESP-IDF version seems to be exported in this shell. Start a"
    echo "clean shell, or run this script with:"
    echo "  env -u IDF_PATH -u IDF_PYTHON_ENV_PATH ./build.sh"
    exit 1
fi
rm -f "$EXPORT_LOG"
export IDF_PATH="$IDF_DIR"

# ------------------------------------------------------------
# configuration mode
# ------------------------------------------------------------
cd "$PROJECT_DIR"

# The default chain is common + target-specific. --release appends the
# release overlay on top. The root CMakeLists.txt only fills in its own
# defaults when -D SDKCONFIG_DEFAULTS was not given, so this -D is
# authoritative here and always lists the full chain.
D_ARGS=()
if [ "$RELEASE" -eq 1 ]; then
    D_ARGS+=(-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32c3;sdkconfig.defaults.release")
    MODE="release"
else
    D_ARGS+=(-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32c3")
    MODE="debug"
fi

# sdkconfig is generated from SDKCONFIG_DEFAULTS and then kept, so switching
# between debug and release only takes effect if the old file is removed. The
# two files differ in log level, which is what identifies the generated file:
# only the release defaults turn the default level down to ERROR.
if [ -f "$PROJECT_DIR/sdkconfig" ]; then
    CURRENT_MODE="debug"
    grep -q '^CONFIG_LOG_DEFAULT_LEVEL_ERROR=y' "$PROJECT_DIR/sdkconfig" && CURRENT_MODE="release"
    if [ "$CURRENT_MODE" != "$MODE" ]; then
        echo "[INFO] sdkconfig was generated for a $CURRENT_MODE build, regenerating for $MODE"
        rm -f "$PROJECT_DIR/sdkconfig"
    fi
fi

IDF_ARGS=(-B "$BUILD_DIR" "${D_ARGS[@]}")
# -p/--port is a global idf.py option; it only matters for the serial actions.
PORT_ARGS=()
[ -n "$PORT" ] && PORT_ARGS=(-p "$PORT")

echo "========================================"
echo " ESP-IDF Build"
echo "========================================"
echo "IDF_PATH     : $IDF_PATH"
echo "IDF version  : v$(idf_version_of "$IDF_DIR") ($FOUND_BY)"
echo "PROJECT_DIR  : $PROJECT_DIR"
echo "Build mode   : $MODE"
echo "Build dir    : $BUILD_DIR"
echo "Action       : $ACTION${PORT:+ (port: $PORT)}"
echo "========================================"

# ------------------------------------------------------------
# helpers that need the environment
# ------------------------------------------------------------

# fullclean refuses to run when build/ was produced by another IDF version (or
# is half-written after an interrupted build) and would abort the script.
fullclean() {
    echo "[INFO] Cleaning $BUILD_DIR..."
    if ! idf.py "${IDF_ARGS[@]}" fullclean; then
        echo "[WARN] idf.py fullclean failed, removing $BUILD_DIR manually"
        rm -rf "$BUILD_DIR"
    fi
}

# idf.py size plus the raw image size, so a build ends with the numbers that
# matter for a 4 MB part.
print_size() {
    local bin="" bytes
    for b in "$BUILD_DIR/esp32App.bin" "$BUILD_DIR"/*.bin; do
        case "$b" in
            *bootloader*|*partition-table*) continue ;;
        esac
        [ -f "$b" ] && { bin="$b"; break; }
    done
    echo ""
    idf.py "${IDF_ARGS[@]}" size || true
    if [ -n "$bin" ] && [ -f "$bin" ]; then
        bytes="$(wc -c < "$bin" | tr -d '[:space:]')"
        printf '  Binary: %s bytes (%s%% of 4MB flash)\n' \
            "$bytes" "$(awk -v b="$bytes" 'BEGIN { printf "%.1f", b / 4194304 * 100 }')"
    else
        echo "  Binary: not found"
    fi
}

# ------------------------------------------------------------
# build / flash / monitor
# ------------------------------------------------------------
case "$ACTION" in
    build)
        idf.py "${IDF_ARGS[@]}" build "${EXTRA_ARGS[@]}"
        print_size
        ;;
    rebuild)
        fullclean
        # sdkconfig.defaults is only read when sdkconfig is absent, so a rebuild
        # - which is the "start over" action - has to drop sdkconfig too or an
        # edit to either defaults file would be silently ignored.
        if [ -f "$PROJECT_DIR/sdkconfig" ]; then
            echo "[INFO] Removing sdkconfig; the rebuild regenerates it from SDKCONFIG_DEFAULTS"
            rm -f "$PROJECT_DIR/sdkconfig"
        fi
        idf.py "${IDF_ARGS[@]}" build "${EXTRA_ARGS[@]}"
        print_size
        ;;
    clean)
        fullclean
        ;;
    flash)
        idf.py "${IDF_ARGS[@]}" build "${EXTRA_ARGS[@]}"
        echo "[INFO] Flashing to ${PORT:-auto-detected port}..."
        idf.py "${IDF_ARGS[@]}" "${PORT_ARGS[@]}" flash
        ;;
    monitor)
        echo "[INFO] Monitoring ${PORT:-auto-detected port} (no build)..."
        idf.py "${IDF_ARGS[@]}" "${PORT_ARGS[@]}" monitor "${EXTRA_ARGS[@]}"
        ;;
    menuconfig)
        idf.py "${IDF_ARGS[@]}" menuconfig
        ;;
    reconfigure)
        idf.py "${IDF_ARGS[@]}" reconfigure
        ;;
esac

echo "========================================"
echo " $ACTION completed successfully!"
echo "========================================"
