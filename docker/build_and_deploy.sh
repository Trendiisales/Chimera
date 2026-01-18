#!/bin/bash
set -e

IMAGE=chimera-builder
TMP_ROOT="/tmp/chimera_build"
REPO_DIR="$TMP_ROOT/repo"

GIT_REPO_URL=https://github.com/Trendiisales/Chimera.git
GIT_BRANCH=main

VPS_USER=ubuntu
VPS_HOST=ec2-15-168-16-103.ap-northeast-3.compute.amazonaws.com
VPS_DIR=/opt/chimera/bin

SSH_KEY=/Users/jo/.ssh/ChimeraKey.pem

echo "================================================="
echo "[LOCK] SOURCE = GITHUB ONLY"
echo "[LOCK] REPO   = $GIT_REPO_URL"
echo "[LOCK] BRANCH = $GIT_BRANCH"
echo "[LOCK] VPS    = $VPS_USER@$VPS_HOST"
echo "[LOCK] BIN    = $VPS_DIR"
echo "================================================="

if [ ! -f "$SSH_KEY" ]; then
  echo "ERROR: SSH key not found: $SSH_KEY"
  exit 1
fi

echo "[CLEAN] Reset temp build dir"
rm -rf "$TMP_ROOT"
mkdir -p "$TMP_ROOT"

echo "[GIT] Cloning fresh repo"
git clone --depth 1 --branch "$GIT_BRANCH" "$GIT_REPO_URL" "$REPO_DIR"

echo "[DOCKER] Ensuring builder exists"
docker buildx inspect chimera-builder >/dev/null 2>&1 || docker buildx create --name chimera-builder --use

echo "[DOCKER] Building minimal builder image"
docker buildx build --platform linux/amd64 -t $IMAGE --load - << 'DOCKER_EOF'
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /build
CMD ["/bin/bash"]
DOCKER_EOF

echo "[BUILD] Compiling from clean GitHub checkout"
docker run --rm \
  --platform linux/amd64 \
  -v "$REPO_DIR":/build \
  -w /build \
  $IMAGE \
  bash -c "
    set -e
    echo '[DOCKER] Commit:'
    git rev-parse HEAD || true
    echo '[DOCKER] Engines present:'
    ls engines || true

    rm -rf build-linux
    mkdir build-linux
    cd build-linux
    cmake -G Ninja ..
    ninja

    echo '[DOCKER] Normalizing output into build-linux/bin'
    mkdir -p bin
    find . -maxdepth 1 -type f -perm -111 -exec cp {} bin/ \;
    ls -lh bin
  "

echo "[DEPLOY] Connecting to VPS"
ssh -i "$SSH_KEY" "$VPS_USER@$VPS_HOST" "sudo mkdir -p $VPS_DIR && sudo chown $VPS_USER:$VPS_USER $VPS_DIR"

echo "[DEPLOY] Uploading binaries"
scp -i "$SSH_KEY" "$REPO_DIR/build-linux/bin/"* "$VPS_USER@$VPS_HOST:$VPS_DIR/"

echo "[DEPLOY] Setting permissions"
ssh -i "$SSH_KEY" "$VPS_USER@$VPS_HOST" "chmod +x $VPS_DIR/*"

echo "[CLEAN] Removing temp build dir"
rm -rf "$TMP_ROOT"

echo "[DONE] Chimera deployed from GitHub source"
