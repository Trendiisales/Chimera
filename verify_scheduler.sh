#!/bin/zsh
set -e

cd /Users/jo/Chimera || exit 1

rm -rf build
mkdir build
cd build
cmake ..
cmake --build . -j

./chimera &
PID=$!
sleep 2

curl -s http://localhost:9102/ > /tmp/metrics_a.txt
sleep 2
curl -s http://localhost:9102/ > /tmp/metrics_b.txt

diff -u /tmp/metrics_a.txt /tmp/metrics_b.txt || true

kill $PID
wait $PID 2>/dev/null || true
