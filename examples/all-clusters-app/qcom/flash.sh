#!/bin/bash
# Flash firmware to QCC730MI board

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "========================================"
echo "Flashing QCC730MI Matter App"
echo "========================================"

if [ ! -f "$BUILD_DIR/zephyr/zephyr.elf" ]; then
    echo "Error: Firmware not found at $BUILD_DIR/zephyr/zephyr.elf"
    echo "Please run ./build.sh first"
    exit 1
fi

cd "$SCRIPT_DIR"

echo "Flashing firmware..."
west flash -d "$BUILD_DIR" -- --all

if [ $? -eq 0 ]; then
    echo ""
    echo "========================================"
    echo "Flash Complete!"
    echo "========================================"
    echo "You can now monitor the device:"
    echo "  west debug -d $BUILD_DIR"
    echo "  or"
    echo "  minicom -D /dev/ttyUSB0 -b 115200"
    echo ""
else
    echo "Error: Flash failed"
    exit 1
fi

