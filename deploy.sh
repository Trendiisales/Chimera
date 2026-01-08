#!/bin/bash
# =============================================================================
# CHIMERA CLEAN DEPLOY SCRIPT
# =============================================================================
# Nukes everything and rebuilds from scratch
# Usage: bash deploy.sh Chimera_v4.14.2.zip
# =============================================================================

set -e

ZIPFILE="$1"

if [ -z "$ZIPFILE" ]; then
    echo "Usage: bash deploy.sh <zipfile>"
    echo "Example: bash deploy.sh Chimera_v4.14.2.zip"
    exit 1
fi

if [ ! -f "$ZIPFILE" ]; then
    echo "ERROR: File not found: $ZIPFILE"
    exit 1
fi

echo "============================================="
echo "CHIMERA CLEAN DEPLOY"
echo "============================================="

# Kill any running chimera
echo "[1/7] Killing any running chimera..."
pkill -f chimera 2>/dev/null || true

# Nuke old installation
echo "[2/7] Removing old Chimera installation..."
rm -rf ~/Chimera
rm -rf ~/Chimera_*
rm -rf /tmp/chimera_*

# Clear cmake cache locations
echo "[3/7] Clearing cmake caches..."
rm -rf ~/.cmake/packages/Chimera 2>/dev/null || true

# Extract fresh
echo "[4/7] Extracting $ZIPFILE..."
mkdir -p ~/Chimera
unzip -o "$ZIPFILE" -d ~/Chimera

# Create build directory
echo "[5/7] Creating fresh build directory..."
cd ~/Chimera
rm -rf build
mkdir build
cd build

# Configure
echo "[6/7] Running cmake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo "[7/7] Building..."
make -j$(nproc)

echo ""
echo "============================================="
echo "BUILD COMPLETE"
echo "============================================="
echo "Binary: ~/Chimera/build/chimera"
echo ""
echo "To run:"
echo "  cd ~/Chimera/build && ./chimera"
echo "============================================="
