#!/bin/bash

echo "=== CHIMERA V22 VERIFICATION ==="
echo

echo "1. Checking if chimera is running..."
if pgrep -x "chimera" > /dev/null; then
    echo "✓ chimera process found (PID: $(pgrep -x chimera))"
else
    echo "✗ chimera is NOT running"
    echo "  Run: sudo ./chimera"
    exit 1
fi

echo

echo "2. Testing API endpoint..."
response=$(curl -s http://localhost:8080/api/dashboard)
if [ $? -eq 0 ]; then
    echo "✓ API endpoint responding"
    echo
    echo "   Response preview:"
    echo "$response" | head -c 500
    echo "..."
else
    echo "✗ API endpoint not responding"
    echo "  Port 8080 may not be open"
    exit 1
fi

echo
echo

echo "3. Checking data values..."
fills=$(echo "$response" | grep -o '"fills":[0-9]*' | cut -d: -f2)
uptime=$(echo "$response" | grep -o '"uptime_s":[0-9]*' | cut -d: -f2)

echo "   Uptime: ${uptime}s"
echo "   Total fills: $fills"

if [ "$fills" -eq 0 ]; then
    echo
    echo "⚠ WARNING: No fills yet"
    echo "  This is normal for first 30-60 seconds"
    echo "  Check logs for:"
    echo "    [ROUTER] HUNT MODE ENABLED"
    echo "    [STRAT] submissions"
fi

echo
echo "4. Dashboard URL:"
echo "   http://$(hostname -I | awk '{print $1}'):8080/"
echo
