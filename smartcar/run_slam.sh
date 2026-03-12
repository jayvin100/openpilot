#!/bin/bash
# Run ORB-SLAM3 (mono-inertial) with WebRTC + IMU from comma4
#
# Usage:
#   ./run_slam.sh [comma4_ip] [--fps 10] [--no-viz]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ORBSLAM_DIR="$SCRIPT_DIR/ORB_SLAM3"
COMMA_IP="${1:-192.168.61.62}"
FPS=10
NO_VIZ=""

# Parse args
shift 2>/dev/null || true
while [[ $# -gt 0 ]]; do
    case "$1" in
        --fps) FPS="$2"; shift 2 ;;
        --no-viz) NO_VIZ="--no-viewer"; shift ;;
        *) shift ;;
    esac
done

# Kill any old processes
echo "=== Cleaning up old processes ==="
pkill -f 'webrtc_feeder.py' 2>/dev/null || true
pkill -f 'webrtc_stream.py' 2>/dev/null || true
pkill -f 'mono_live' 2>/dev/null || true
pkill -f 'MASt3R-SLAM/main.py' 2>/dev/null || true
pkill -f 'run_slam.sh' --signal KILL --older-than 1s 2>/dev/null || true
sleep 1

# Kill leftover GPU processes (not tritonserver)
for pid in $(nvidia-smi --query-compute-apps=pid,name --format=csv,noheader 2>/dev/null | grep -v tritonserver | awk -F',' '{print $1}'); do
    echo "Killing stale GPU process $pid"
    kill -9 "$pid" 2>/dev/null || true
done

# Ensure camerad + encoderd are running on comma4 (not managed by manager on this checkout)
echo "=== Ensuring camerad + encoderd on comma4 ==="
OLD_OP="/data/safe_staging/old_openpilot"
ssh -o ConnectTimeout=5 comma@"$COMMA_IP" "
    pgrep -f 'camerad' > /dev/null || (cd /data/openpilot && PYTHONPATH=/data/openpilot nohup $OLD_OP/system/camerad/camerad > /tmp/camerad.log 2>&1 &)
    pgrep -f 'encoderd' > /dev/null || (cd /data/openpilot && PYTHONPATH=/data/openpilot nohup $OLD_OP/system/loggerd/encoderd --stream > /tmp/encoderd.log 2>&1 &)
    sleep 1
    pgrep -c camerad && echo 'camerad: ok' || echo 'camerad: FAILED'
    pgrep -c encoderd && echo 'encoderd: ok' || echo 'encoderd: FAILED'
" 2>/dev/null

MONO_LIVE="$ORBSLAM_DIR/Examples/Monocular/mono_live"

# Auto-setup ORB-SLAM3 if not built
if [[ ! -f "$MONO_LIVE" ]]; then
    echo "ORB-SLAM3 not built, running setup..."
    bash "$SCRIPT_DIR/setup_orbslam.sh"
fi

# Always copy latest config from patches
cp "$SCRIPT_DIR/orbslam_patches/comma4_wide.yaml" "$ORBSLAM_DIR/comma4_wide.yaml"

VOCAB="$ORBSLAM_DIR/Vocabulary/ORBvoc.txt"
SETTINGS="$ORBSLAM_DIR/comma4_wide.yaml"

export LD_LIBRARY_PATH="$ORBSLAM_DIR/lib:$ORBSLAM_DIR/Thirdparty/DBoW2/lib:$ORBSLAM_DIR/Thirdparty/g2o/lib:/usr/local/lib:${LD_LIBRARY_PATH:-}"

# Use smartcar venv
PYTHON="$SCRIPT_DIR/.venv/bin/python"

echo "=== Starting ORB-SLAM3 mono-inertial with WebRTC from $COMMA_IP (${FPS}fps) ==="

exec "$PYTHON" "$SCRIPT_DIR/webrtc_feeder.py" --ip "$COMMA_IP" --fps "$FPS" --camera wideRoad \
    | "$MONO_LIVE" "$VOCAB" "$SETTINGS" $NO_VIZ
