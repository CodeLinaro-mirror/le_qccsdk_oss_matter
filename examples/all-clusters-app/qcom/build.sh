#!/bin/bash
# Main build script for QCC730 Matter All-Clusters App

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHIP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BOARD="qcc730mi"
BUILD_DIR="$SCRIPT_DIR/build"

echo "========================================"
echo "QCC730 Matter All-Clusters App Build"
echo "========================================"
echo "CHIP_ROOT: $CHIP_ROOT"
echo "SCRIPT_DIR: $SCRIPT_DIR"
echo "BOARD: $BOARD"
echo "BUILD_DIR: $BUILD_DIR"
echo ""

# Step 1: Check environment
echo "[Step 1/3] Checking environment..."

if [ -z "$ZEPHYR_BASE" ]; then
    echo "Error: ZEPHYR_BASE not set"
    echo "Please run: source setup_env.sh"
    exit 1
fi

if ! command -v west &> /dev/null; then
    echo "Error: west command not found"
    echo "Please ensure Zephyr environment is properly set up"
    exit 1
fi

echo "ZEPHYR_BASE: $ZEPHYR_BASE"
echo "West version: $(west --version)"
echo ""

# Step 2: Build Zephyr application
echo "[Step 2/3] Building Zephyr application..."

cd "$SCRIPT_DIR"
mkdir -p "$BUILD_DIR"

# Detect prj.conf changes to force a pristine rebuild.
# Incremental builds can leave stale .obj files compiled with an older
# autoconf.h (e.g. different CONFIG_NET_STATISTICS_* options), causing
# struct net_if size mismatches and an assertion crash in net_if_set_name.
PRJCONF_HASH_FILE="$BUILD_DIR/.prj_conf_hash"
# Hash both prj.conf AND chip-module/CMakeLists.txt — changes to either require
# a pristine rebuild. chip-module changes affect args.gn (GN build flags) which
# are NOT invalidated by ninja's incremental build detection.
PRJCONF_HASH_CURRENT=$(cat "$SCRIPT_DIR/prj.conf" "$SCRIPT_DIR/chip-module/CMakeLists.txt" 2>/dev/null | md5sum | awk '{print $1}')
PRJCONF_HASH_STORED=$(cat "$PRJCONF_HASH_FILE" 2>/dev/null || echo "")
PRISTINE_FLAG="--pristine=auto"
if [ "$PRJCONF_HASH_CURRENT" != "$PRJCONF_HASH_STORED" ]; then
    echo "prj.conf or chip-module/CMakeLists.txt changed – forcing pristine rebuild"
    PRISTINE_FLAG="--pristine"
fi

# Run west build
west build -b "$BOARD" -d "$BUILD_DIR" $PRISTINE_FLAG 2>&1 | tee "$BUILD_DIR/build.log" || {
    echo ""
    echo "========================================"
    echo "Build Failed!"
    echo "========================================"
    echo "Check build log: $BUILD_DIR/build.log"
    echo ""
    echo "Extracting last 50 lines of errors:"
    tail -50 "$BUILD_DIR/build.log" | grep -i "error" || tail -50 "$BUILD_DIR/build.log"
    echo ""
    exit 1
}

# Save hash so next incremental build can detect changes to prj.conf or chip-module/CMakeLists.txt
mkdir -p "$BUILD_DIR"
echo "$PRJCONF_HASH_CURRENT" > "$PRJCONF_HASH_FILE"

# Step 3: Check build output
echo "[Step 3/3] Checking build output..."

if [ -f "$BUILD_DIR/zephyr/zephyr.elf" ]; then
    echo "Build successful!"
    echo ""
    echo "Firmware files:"
    ls -lh "$BUILD_DIR/zephyr/zephyr."* 2>/dev/null
    echo ""
    echo "Memory usage:"
    size "$BUILD_DIR/zephyr/zephyr.elf" 2>/dev/null || echo "(size command not available)"
    echo ""
else
    echo "Error: zephyr.elf not found"
    exit 1
fi

echo "========================================"
echo "Build Complete!"
echo "========================================"
echo "Firmware: $BUILD_DIR/zephyr/zephyr.elf"
echo ""
echo "Next steps:"
echo "  Flash: ./flash.sh"
echo "  Clean: ./clean.sh"
echo ""
