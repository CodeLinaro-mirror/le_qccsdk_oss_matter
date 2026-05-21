#!/bin/bash
# Setup environment for QCC730 Matter App build
#
# Usage:
#   source setup_env.sh [ZEPHYR_BASE] [ZEPHYR_SDK_INSTALL_DIR]
#
# Arguments (both optional — if omitted, the script checks env vars or prompts):
#   ZEPHYR_BASE            Path to Zephyr RTOS source directory
#   ZEPHYR_SDK_INSTALL_DIR Path to Zephyr SDK toolchain (e.g. zephyr-sdk-0.17.4)
#
# Examples:
#   source setup_env.sh /path/to/zephyr /path/to/zephyr-sdk-0.17.4
#   source setup_env.sh                          # uses env vars or prompts
#   ZEPHYR_BASE=/path/to/zephyr source setup_env.sh
#
# Note: This script must be sourced (not executed) so that exported variables
#       remain in the calling shell:  source setup_env.sh  or  . setup_env.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHIP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

echo "========================================"
echo "Setting up QCC730 Matter Build Environment"
echo "========================================"
echo "CHIP_ROOT: $CHIP_ROOT"
echo ""

# ── Step 1: Resolve ZEPHYR_BASE ──────────────────────────────────────────────
# Priority: command-line arg > env var > prompt

if [ -n "$1" ]; then
    ZEPHYR_BASE_INPUT="$1"
elif [ -n "$ZEPHYR_BASE" ]; then
    ZEPHYR_BASE_INPUT="$ZEPHYR_BASE"
else
    echo "ZEPHYR_BASE is not set."
    echo "Please enter the path to the Zephyr RTOS source directory."
    echo "  Example: /path/to/qcc730_sdk/zephyr"
    echo -n "ZEPHYR_BASE> "
    read -r ZEPHYR_BASE_INPUT
fi

# Validate
ZEPHYR_BASE_INPUT="$(eval echo "$ZEPHYR_BASE_INPUT")"   # expand ~ if present
if [ -z "$ZEPHYR_BASE_INPUT" ]; then
    echo ""
    echo "ERROR: ZEPHYR_BASE cannot be empty."
    echo "  Set it before sourcing:  export ZEPHYR_BASE=/path/to/zephyr"
    echo "  Or pass as argument:     source setup_env.sh /path/to/zephyr"
    return 1 2>/dev/null || exit 1
fi

if [ ! -f "$ZEPHYR_BASE_INPUT/CMakeLists.txt" ]; then
    echo ""
    echo "ERROR: '$ZEPHYR_BASE_INPUT' does not look like a Zephyr source directory."
    echo "  Expected: $ZEPHYR_BASE_INPUT/CMakeLists.txt"
    echo "  Please provide the correct path to the Zephyr RTOS source."
    return 1 2>/dev/null || exit 1
fi

export ZEPHYR_BASE="$ZEPHYR_BASE_INPUT"
echo "ZEPHYR_BASE:    $ZEPHYR_BASE"

# ── Step 2: Resolve ZEPHYR_SDK_INSTALL_DIR ───────────────────────────────────
# Priority: command-line arg 2 > env var > auto-detect common locations > prompt

if [ -n "$2" ]; then
    SDK_INPUT="$2"
elif [ -n "$ZEPHYR_SDK_INSTALL_DIR" ] && [ -d "$ZEPHYR_SDK_INSTALL_DIR" ]; then
    SDK_INPUT="$ZEPHYR_SDK_INSTALL_DIR"
else
    # Try common locations silently (glob for any zephyr-sdk-* version)
    SDK_INPUT=""
    for candidate in \
        "$HOME/zephyr-sdk-"* \
        "/opt/zephyr-sdk-"* \
        "/usr/local/zephyr-sdk-"*; do
        if [ -d "$candidate" ]; then
            SDK_INPUT="$candidate"
            break
        fi
    done

    if [ -z "$SDK_INPUT" ]; then
        echo ""
        echo "ZEPHYR_SDK_INSTALL_DIR is not set and could not be auto-detected."
        echo "Please enter the path to the Zephyr SDK toolchain directory."
        echo "  Example: /path/to/zephyr-sdk-0.17.4"
        echo -n "ZEPHYR_SDK_INSTALL_DIR> "
        read -r SDK_INPUT
    fi
fi

SDK_INPUT="$(eval echo "$SDK_INPUT")"
if [ -z "$SDK_INPUT" ]; then
    echo ""
    echo "ERROR: ZEPHYR_SDK_INSTALL_DIR cannot be empty."
    echo "  Set it before sourcing:  export ZEPHYR_SDK_INSTALL_DIR=/path/to/zephyr-sdk-0.17.4"
    return 1 2>/dev/null || exit 1
fi

if [ ! -d "$SDK_INPUT" ]; then
    echo ""
    echo "ERROR: '$SDK_INPUT' is not a valid directory."
    echo "  Please provide the correct path to the Zephyr SDK toolchain."
    return 1 2>/dev/null || exit 1
fi

export ZEPHYR_SDK_INSTALL_DIR="$SDK_INPUT"
echo "ZEPHYR_SDK:     $ZEPHYR_SDK_INSTALL_DIR"

# ── Step 3: Set Zephyr toolchain variant ─────────────────────────────────────
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
unset GNUARMEMB_TOOLCHAIN_PATH

# ── Step 4: Verify west is available ─────────────────────────────────────────
if ! command -v west &> /dev/null; then
    echo ""
    echo "ERROR: 'west' command not found in PATH."
    echo "  Please activate a Python virtual environment that has west installed."
    echo "  Examples:"
    echo "    source /path/to/.venv_310/bin/activate"
    echo "    source /path/to/connectedhomeip/.environment/pigweed-venv/bin/activate"
    echo "  Then re-run: source setup_env.sh"
    return 1 2>/dev/null || exit 1
fi
echo "west:           $(west --version)"

# ── Step 5: Add Matter tools (gn, ninja, zap-cli) to PATH ───────────────────
# Directly prepend the CIPD pigweed bin and pigweed-venv to PATH.
# This avoids calling scripts/activate.sh which triggers a full pigweed
# bootstrap (slow, requires network/pip) and is not needed when the
# .environment directory is already populated.
_PIGWEED_VENV="$CHIP_ROOT/.environment/pigweed-venv"
if [ -d "$_PIGWEED_VENV" ]; then
    export PATH="$CHIP_ROOT/.environment/cipd/packages/pigweed:$CHIP_ROOT/.environment/cipd/packages/pigweed/bin:$_PIGWEED_VENV/bin:$PATH"
    echo "Matter tools:   $CHIP_ROOT/.environment/cipd/packages/pigweed/bin"
else
    echo "Warning: Matter .environment not found at $CHIP_ROOT/.environment"
    echo "  Run: source $CHIP_ROOT/scripts/bootstrap.sh  to populate it first."
fi
unset _PIGWEED_VENV

# ── Step 6: Final verification ────────────────────────────────────────────────
echo ""
ERRORS=0

# ── Step 6a: Set ZAP_INSTALL_PATH from CIPD packages ─────────────────────────
# generate.py looks for zap-cli in: ZAP_DEVELOPMENT_PATH > ZAP_INSTALL_PATH > PATH
# The pigweed activate.sh stores PW_ZAP_CIPD_INSTALL_DIR in actions.json but does
# NOT export it as a shell variable. Set ZAP_INSTALL_PATH explicitly here.
_ZAP_CIPD_DIR="$CHIP_ROOT/.environment/cipd/packages/zap"
if [ -z "$ZAP_INSTALL_PATH" ] && [ -x "$_ZAP_CIPD_DIR/zap-cli" ]; then
    export ZAP_INSTALL_PATH="$_ZAP_CIPD_DIR"
    echo "ZAP_INSTALL_PATH: $ZAP_INSTALL_PATH"
elif [ -n "$ZAP_INSTALL_PATH" ]; then
    echo "ZAP_INSTALL_PATH: $ZAP_INSTALL_PATH (pre-set)"
else
    echo "Warning: zap-cli not found at $_ZAP_CIPD_DIR — ZAP generation may fail."
    echo "  Set ZAP_INSTALL_PATH manually or run: source scripts/bootstrap.sh"
fi
unset _ZAP_CIPD_DIR

# ── Step 6b: Set _PW_ACTUAL_ENVIRONMENT_ROOT for GN build ────────────────────
# build/config/compiler/BUILD.gn imports build_overrides/pigweed_environment.gni
# which calls getenv("_PW_ACTUAL_ENVIRONMENT_ROOT"). This must be set so GN can
# locate the generated pigweed_environment.gni in the .environment directory.
if [ -z "$_PW_ACTUAL_ENVIRONMENT_ROOT" ]; then
    _ENV_ROOT_FILE="$CHIP_ROOT/.environment/env_root.txt"
    if [ -f "$_ENV_ROOT_FILE" ]; then
        export _PW_ACTUAL_ENVIRONMENT_ROOT="$(cat "$_ENV_ROOT_FILE")"
        echo "_PW_ACTUAL_ENVIRONMENT_ROOT: $_PW_ACTUAL_ENVIRONMENT_ROOT"
    else
        export _PW_ACTUAL_ENVIRONMENT_ROOT="$CHIP_ROOT/.environment"
        echo "_PW_ACTUAL_ENVIRONMENT_ROOT: $_PW_ACTUAL_ENVIRONMENT_ROOT (default)"
    fi
fi

if ! command -v west &> /dev/null; then
    echo "ERROR: west not available after environment setup."
    ERRORS=$((ERRORS+1))
fi

if ! command -v gn &> /dev/null; then
    echo "Warning: GN not found (needed for libCHIP.a build)."
else
    echo "GN:             $(which gn)"
fi

if [ $ERRORS -gt 0 ]; then
    echo ""
    echo "ERROR: Environment setup incomplete. Fix the errors above and re-run."
    return 1 2>/dev/null || exit 1
fi

echo ""
echo "========================================"
echo "Environment Setup Complete!"
echo "========================================"
echo "  ZEPHYR_BASE:            $ZEPHYR_BASE"
echo "  ZEPHYR_SDK_INSTALL_DIR: $ZEPHYR_SDK_INSTALL_DIR"
echo "  ZEPHYR_TOOLCHAIN:       $ZEPHYR_TOOLCHAIN_VARIANT"
echo "  ZAP_INSTALL_PATH:       $ZAP_INSTALL_PATH"
echo "  _PW_ACTUAL_ENV_ROOT:    $_PW_ACTUAL_ENVIRONMENT_ROOT"
echo ""
echo "You can now run: ./build.sh"
echo ""
