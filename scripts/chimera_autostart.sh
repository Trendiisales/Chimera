#!/bin/bash
# =============================================================================
# Chimera HFT Autostart Script for WSL
# =============================================================================
# This script starts Chimera and the HTTP server automatically
# 
# INSTALLATION OPTIONS:
#
# Option 1: Add to ~/.bashrc (runs on every terminal open)
#   echo 'source ~/Chimera/scripts/chimera_autostart.sh' >> ~/.bashrc
#
# Option 2: Add to ~/.profile (runs on login)
#   echo 'source ~/Chimera/scripts/chimera_autostart.sh' >> ~/.profile
#
# Option 3: Create a systemd user service (recommended)
#   See chimera.service file in this directory
#
# Option 4: Windows Task Scheduler
#   Create a task that runs: wsl -d Ubuntu -u trader ~/Chimera/scripts/chimera_autostart.sh
# =============================================================================

CHIMERA_DIR="${HOME}/Chimera"
LOG_DIR="${CHIMERA_DIR}/logs"
HTTP_PORT=8080
WS_PORT=7777

# Create log directory if needed
mkdir -p "${LOG_DIR}"

# Check if already running
if pgrep -x "chimera" > /dev/null; then
    echo "[AUTOSTART] Chimera already running (PID: $(pgrep -x chimera))"
else
    echo "[AUTOSTART] Starting Chimera..."
    
    # Start Chimera in background
    cd "${CHIMERA_DIR}/build" || exit 1
    nohup ./chimera > "${LOG_DIR}/chimera_$(date +%Y%m%d_%H%M%S).log" 2>&1 &
    CHIMERA_PID=$!
    
    echo "[AUTOSTART] Chimera started with PID: ${CHIMERA_PID}"
    
    # Wait a moment for it to initialize
    sleep 2
    
    # Verify it's running
    if ps -p ${CHIMERA_PID} > /dev/null; then
        echo "[AUTOSTART] Chimera running successfully"
    else
        echo "[AUTOSTART] ERROR: Chimera failed to start!"
        exit 1
    fi
fi

# Check if HTTP server already running
if pgrep -f "http.server ${HTTP_PORT}" > /dev/null; then
    echo "[AUTOSTART] HTTP server already running on port ${HTTP_PORT}"
else
    echo "[AUTOSTART] Starting HTTP server on port ${HTTP_PORT}..."
    
    cd "${CHIMERA_DIR}" || exit 1
    nohup python3 -m http.server ${HTTP_PORT} --bind 0.0.0.0 > "${LOG_DIR}/http_$(date +%Y%m%d_%H%M%S).log" 2>&1 &
    HTTP_PID=$!
    
    echo "[AUTOSTART] HTTP server started with PID: ${HTTP_PID}"
fi

# Print status
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  CHIMERA v6.72 AUTOSTART COMPLETE"
echo "═══════════════════════════════════════════════════════════════"
echo "  Engine:    Port ${WS_PORT} (WebSocket)"
echo "  Dashboard: http://$(hostname -I | awk '{print $1}'):${HTTP_PORT}/chimera_dashboard.html"
echo "  Logs:      ${LOG_DIR}/"
echo "═══════════════════════════════════════════════════════════════"
