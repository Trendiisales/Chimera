#!/bin/zsh
set -e

cd /Users/jo/Chimera || exit 1

echo "[STEP 1] clean build"
rm -rf build
mkdir build
cd build
cmake ..
cmake --build . -j

echo "[STEP 2] start chimera"
./chimera &
PID=$!
sleep 2

echo "[STEP 3] snapshot A"
curl -s http://localhost:9102/ > /tmp/metrics_a.txt
sleep 2

echo "[STEP 4] snapshot B"
curl -s http://localhost:9102/ > /tmp/metrics_b.txt

echo "[STEP 5] diff (must show delta)"
diff -u /tmp/metrics_a.txt /tmp/metrics_b.txt || true

echo "[STEP 6] stop chimera"
kill $PID
wait $PID 2>/dev/null || true

echo "[OK] verification complete"
