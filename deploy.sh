#!/bin/bash

#
# Chimera Production - Deployment Script
# Safely deploys the unified trading system
#

set -e

echo "============================================================"
echo "  Chimera Production Deployment"
echo "============================================================"
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if we're in the right directory
if [ ! -f "main.cpp" ]; then
    echo -e "${RED}ERROR: main.cpp not found. Run this script from chimera_production directory.${NC}"
    exit 1
fi

# Check for FIX credentials
echo -e "${YELLOW}[1/6] Checking FIX credentials...${NC}"
if [ -z "$FIX_USERNAME" ] || [ -z "$FIX_PASSWORD" ]; then
    echo -e "${RED}ERROR: FIX credentials not set.${NC}"
    echo ""
    echo "Set them with:"
    echo "  export FIX_USERNAME='live.blackbull.8077780'"
    echo "  export FIX_PASSWORD='8077780'"
    echo ""
    echo "Or add to ~/.bashrc for persistence."
    exit 1
fi
echo -e "${GREEN}✓ Credentials found${NC}"
echo ""

# Check dependencies
echo -e "${YELLOW}[2/6] Checking dependencies...${NC}"

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}ERROR: cmake not found${NC}"
    echo "Install with: sudo apt-get install cmake"
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo -e "${RED}ERROR: g++ not found${NC}"
    echo "Install with: sudo apt-get install build-essential"
    exit 1
fi

if ! ldconfig -p | grep -q libssl; then
    echo -e "${RED}ERROR: OpenSSL not found${NC}"
    echo "Install with: sudo apt-get install libssl-dev"
    exit 1
fi

echo -e "${GREEN}✓ All dependencies found${NC}"
echo ""

# Verify critical files
echo -e "${YELLOW}[3/6] Verifying file structure...${NC}"

REQUIRED_FILES=(
    "main.cpp"
    "CMakeLists.txt"
    "core/V2Desk.hpp"
    "core/V2Runtime.hpp"
    "engines/StructuralMomentumEngine.hpp"
    "engines/CompressionBreakEngine.hpp"
    "engines/StopCascadeEngine.hpp"
    "engines/MicroImpulseEngine.hpp"
    "config/V2Config.hpp"
)

MISSING=0
for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo -e "${RED}✗ Missing: $file${NC}"
        MISSING=1
    fi
done

if [ $MISSING -eq 1 ]; then
    echo -e "${RED}ERROR: Critical files missing. Copy from Chimera_Baseline.tar.gz${NC}"
    exit 1
fi

echo -e "${GREEN}✓ All critical files present${NC}"
echo ""

# Count engines
ENGINE_COUNT=$(find engines -name "*.hpp" | wc -l)
echo "Found $ENGINE_COUNT engine files"
echo ""

# Build
echo -e "${YELLOW}[4/6] Building...${NC}"

mkdir -p build
cd build

cmake .. || {
    echo -e "${RED}ERROR: CMake configuration failed${NC}"
    exit 1
}

make -j$(nproc) || {
    echo -e "${RED}ERROR: Compilation failed${NC}"
    exit 1
}

echo -e "${GREEN}✓ Build successful${NC}"
echo ""

# Verify binary
if [ ! -f "chimera" ]; then
    echo -e "${RED}ERROR: Binary 'chimera' not found after build${NC}"
    exit 1
fi

# Check shadow mode
echo -e "${YELLOW}[5/6] Verifying shadow mode...${NC}"

if grep -q "shadow_gate.set_shadow(true)" ../main.cpp; then
    echo -e "${GREEN}✓ Shadow mode ENABLED (safe)${NC}"
else
    echo -e "${RED}WARNING: Shadow mode might be DISABLED${NC}"
    echo "Edit main.cpp and set: shadow_gate.set_shadow(true);"
    read -p "Continue anyway? (yes/NO): " response
    if [ "$response" != "yes" ]; then
        echo "Deployment cancelled."
        exit 1
    fi
fi
echo ""

# Final checklist
echo -e "${YELLOW}[6/6] Pre-flight checklist...${NC}"
echo ""
echo "System configuration:"
echo "  - Binary: $(pwd)/chimera"
echo "  - FIX User: $FIX_USERNAME"
echo "  - FIX Password: [REDACTED]"
echo "  - Telemetry: http://localhost:8080"
echo ""

# Deployment options
echo "============================================================"
echo "  Deployment Options"
echo "============================================================"
echo ""
echo "1) Start in SHADOW mode (recommended)"
echo "2) Start with DEBUG logging"
echo "3) Start as background service"
echo "4) Cancel"
echo ""
read -p "Select option [1-4]: " option

case $option in
    1)
        echo ""
        echo -e "${GREEN}Starting Chimera in SHADOW mode...${NC}"
        echo ""
        echo "Press Ctrl+C to stop"
        echo "Monitor telemetry: curl http://localhost:8080"
        echo ""
        sleep 2
        ./chimera
        ;;
    2)
        echo ""
        echo -e "${GREEN}Starting Chimera with DEBUG logging...${NC}"
        echo ""
        ./chimera 2>&1 | tee chimera.log
        ;;
    3)
        echo ""
        echo -e "${GREEN}Starting Chimera as background service...${NC}"
        nohup ./chimera > chimera.log 2>&1 &
        PID=$!
        echo "Chimera started with PID: $PID"
        echo "Logs: tail -f build/chimera.log"
        echo "Stop: kill $PID"
        ;;
    4)
        echo "Deployment cancelled."
        exit 0
        ;;
    *)
        echo "Invalid option. Deployment cancelled."
        exit 1
        ;;
esac
