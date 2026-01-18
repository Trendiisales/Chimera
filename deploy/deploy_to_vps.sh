#!/bin/bash
set -e
VPS_USER=ubuntu
VPS_HOST=YOUR_VPS_IP
VPS_DIR=/opt/chimera/bin
ssh $VPS_USER@$VPS_HOST "mkdir -p $VPS_DIR"
scp build-linux/bin/* $VPS_USER@$VPS_HOST:$VPS_DIR/
ssh $VPS_USER@$VPS_HOST "chmod +x $VPS_DIR/*"
