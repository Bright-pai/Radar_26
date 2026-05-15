#!/usr/bin/env bash
set -euo pipefail

# Base build/runtime dependencies.
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  pkg-config \
  libopencv-dev \
  libgtk-3-dev \
  libgl1-mesa-dev

cat <<'EOF'

[INFO] System packages installed.

Next steps you still need to do manually:
1. Install NVIDIA driver + CUDA Toolkit.
2. Install TensorRT (libnvinfer + headers).
3. (Optional) Install Daheng SDK and make sure libgxiapi.so is in LD_LIBRARY_PATH.

Example:
  export LD_LIBRARY_PATH=/opt/DahengSDK/lib:/usr/lib:${LD_LIBRARY_PATH}

Build command:
  cd Radar_26
  cmake -S . -B build -DTENSORRT_ROOT=/usr
  cmake --build build -j$(nproc)

EOF
