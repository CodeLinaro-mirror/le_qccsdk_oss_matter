#!/bin/bash
# Clean build artifacts

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
CHIP_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
MATTER_OUT="$CHIP_ROOT/out/qcc730-zephyr"

echo "========================================"
echo "Cleaning QCC730 Matter App"
echo "========================================"

# Clean Zephyr build
if [ -d "$BUILD_DIR" ]; then
    echo "Removing Zephyr build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# Ask if user wants to clean Matter libraries
read -p "Clean Matter libraries? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if [ -d "$MATTER_OUT" ]; then
        echo "Removing Matter build directory: $MATTER_OUT"
        rm -rf "$MATTER_OUT"
    fi
fi

echo ""
echo "========================================"
echo "Clean Complete!"
echo "========================================"
echo "Run ./build.sh to rebuild"
echo ""

