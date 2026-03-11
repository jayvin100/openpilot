#!/bin/bash
# Sets up MASt3R-SLAM in the smartcar directory.
# Clones the repo, creates a uv venv, installs dependencies,
# downloads model checkpoints, and applies the live dataset patch.
#
# Requirements: uv, CUDA toolkit, NVIDIA GPU
#
# Usage:
#   cd smartcar && ./setup_slam.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SLAM_DIR="$SCRIPT_DIR/MASt3R-SLAM"

if [ -d "$SLAM_DIR" ]; then
  echo "MASt3R-SLAM directory already exists. Remove it first to re-setup."
  exit 1
fi

echo "=== Cloning MASt3R-SLAM ==="
git clone --recursive https://github.com/rmurai0610/MASt3R-SLAM.git "$SLAM_DIR"

echo "=== Creating venv with Python 3.11 ==="
cd "$SLAM_DIR"
uv venv --python 3.11 .venv
source .venv/bin/activate

echo "=== Installing PyTorch 2.5.1 + CUDA 12.4 ==="
uv pip install torch==2.5.1 torchvision==0.20.1 --index-url https://download.pytorch.org/whl/cu124

echo "=== Installing dependencies ==="
uv pip install setuptools wheel
uv pip install opencv-python numpy scipy natsort tqdm pyyaml

# mast3r (thirdparty)
uv pip install -e thirdparty/mast3r --no-build-isolation
# lietorch
uv pip install lietorch

# pyimgui needs cython<3
uv pip install "cython<3"
uv pip install thirdparty/in3d/thirdparty/pyimgui --no-build-isolation

# in3d (visualization)
uv pip install -e thirdparty/in3d --no-build-isolation

# mast3r-slam itself
uv pip install -e . --no-build-isolation

echo "=== Downloading model checkpoints ==="
mkdir -p checkpoints
cd checkpoints

BASE_URL="https://download.europe.naverlabs.com/ComputerVision/MASt3R"

if [ ! -f MASt3R_ViTLarge_BaseDecoder_512_catmlpdpt_metric.pth ]; then
  wget "$BASE_URL/MASt3R_ViTLarge_BaseDecoder_512_catmlpdpt_metric.pth"
fi
if [ ! -f MASt3R_ViTLarge_BaseDecoder_512_catmlpdpt_metric_retrieval_trainingfree.pth ]; then
  wget "$BASE_URL/MASt3R_ViTLarge_BaseDecoder_512_catmlpdpt_metric_retrieval_trainingfree.pth"
fi
if [ ! -f MASt3R_ViTLarge_BaseDecoder_512_catmlpdpt_metric_retrieval_codebook.pkl ]; then
  wget "$BASE_URL/MASt3R_ViTLarge_BaseDecoder_512_catmlpdpt_metric_retrieval_codebook.pkl"
fi

cd "$SLAM_DIR"

echo "=== Applying live dataset patch ==="
git apply "$SCRIPT_DIR/live_dataset.patch"

echo "=== Done! ==="
echo "Run SLAM with: ./run_slam.sh <comma4_ip>"
