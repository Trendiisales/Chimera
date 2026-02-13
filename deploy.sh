#!/bin/bash

# CHIMERA GUI Fix Deployment Script
# This script deploys the clean architecture fix

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}CHIMERA GUI FIX - Clean Architecture${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Configuration
CHIMERA_ROOT=${1:-"/root/chimera"}
BACKUP_DIR="${CHIMERA_ROOT}/backup_$(date +%Y%m%d_%H%M%S)"

echo -e "${YELLOW}Configuration:${NC}"
echo "CHIMERA_ROOT: $CHIMERA_ROOT"
echo "BACKUP_DIR: $BACKUP_DIR"
echo ""

# Check if CHIMERA_ROOT exists
if [ ! -d "$CHIMERA_ROOT" ]; then
    echo -e "${RED}ERROR: Directory $CHIMERA_ROOT does not exist${NC}"
    echo "Usage: $0 [/path/to/chimera]"
    exit 1
fi

# Create backup directory
echo -e "${YELLOW}Step 1: Creating backup...${NC}"
mkdir -p "$BACKUP_DIR"

# Backup main.cpp
if [ -f "$CHIMERA_ROOT/src/main.cpp" ]; then
    cp "$CHIMERA_ROOT/src/main.cpp" "$BACKUP_DIR/main.cpp.backup"
    echo "✓ Backed up main.cpp"
else
    echo -e "${RED}WARNING: $CHIMERA_ROOT/src/main.cpp not found${NC}"
fi

# Copy new main.cpp
echo -e "${YELLOW}Step 2: Deploying new main.cpp...${NC}"
if [ -f "main.cpp" ]; then
    cp main.cpp "$CHIMERA_ROOT/src/main.cpp"
    echo "✓ Deployed main.cpp"
else
    echo -e "${RED}ERROR: main.cpp not found in current directory${NC}"
    exit 1
fi

# Rebuild
echo -e "${YELLOW}Step 3: Rebuilding binary...${NC}"
cd "$CHIMERA_ROOT"
mkdir -p build
cd build

if cmake .. && make -j4; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}ERROR: Build failed${NC}"
    echo "Restoring backup..."
    cp "$BACKUP_DIR/main.cpp.backup" "$CHIMERA_ROOT/src/main.cpp"
    exit 1
fi

# Deploy dashboard
echo -e "${YELLOW}Step 4: Deploying dashboard...${NC}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -f "$SCRIPT_DIR/chimera_dashboard.html" ]; then
    cp "$SCRIPT_DIR/chimera_dashboard.html" "$CHIMERA_ROOT/chimera_dashboard.html"
    echo "✓ Dashboard deployed to $CHIMERA_ROOT/chimera_dashboard.html"
    
    # Optional: deploy to /var/www/html if nginx is installed
    if [ -d "/var/www/html" ]; then
        sudo cp "$SCRIPT_DIR/chimera_dashboard.html" /var/www/html/chimera.html 2>/dev/null && \
        echo "✓ Dashboard also deployed to /var/www/html/chimera.html" || \
        echo "⚠ Could not deploy to /var/www/html (nginx not installed?)"
    fi
else
    echo -e "${YELLOW}⚠ chimera_dashboard.html not found, skipping dashboard deployment${NC}"
fi

# Get server IP
SERVER_IP=$(curl -s ifconfig.me 2>/dev/null || hostname -I | awk '{print $1}')

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}DEPLOYMENT COMPLETE${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo ""
echo "1. Start the engine:"
echo "   cd $CHIMERA_ROOT/build"
echo "   ./chimera_engine"
echo ""
echo "2. Connect dashboard:"
echo "   Option A - Local file:"
echo "     • Download chimera_dashboard.html to your Mac"
echo "     • Open in browser"
echo "     • Enter: ws://$SERVER_IP:7777"
echo ""
echo "   Option B - Web server (if nginx installed):"
echo "     • Open: http://$SERVER_IP/chimera.html"
echo ""
echo "3. Verify WebSocket is listening:"
echo "   netstat -tuln | grep 7777"
echo ""
echo -e "${YELLOW}Backup location:${NC} $BACKUP_DIR"
echo ""
echo -e "${GREEN}Clean architecture deployed successfully!${NC}"
