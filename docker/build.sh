#!/bin/bash
set -e
IMAGE=chimera-builder
docker build -t $IMAGE -f docker/Dockerfile docker
docker run --rm -v "$(pwd)":/build -w /build $IMAGE bash -c "
rm -rf build-linux
mkdir build-linux
cd build-linux
cmake -G Ninja ..
ninja
"
