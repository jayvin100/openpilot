#!/bin/bash
# Run MASt3R-SLAM with integrated WebRTC streaming from comma4
#
# Usage:
#   ./run_slam.sh [comma4_ip] [--fps 5] [--no-viz]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SLAM_DIR="$SCRIPT_DIR/MASt3R-SLAM"
COMMA_IP="${1:-192.168.61.62}"
FPS=5

# Parse args
shift 2>/dev/null || true
EXTRA_ARGS=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --fps) FPS="$2"; shift 2 ;;
        *) EXTRA_ARGS="$EXTRA_ARGS $1"; shift ;;
    esac
done

# Kill any old SLAM / streaming processes
echo "=== Cleaning up old processes ==="
pkill -f 'webrtc_stream.py' 2>/dev/null || true
pkill -f 'MASt3R-SLAM/main.py' 2>/dev/null || true
# Kill other instances of this script (but not ourselves)
pkill -f 'run_slam.sh' --signal KILL --older-than 1s 2>/dev/null || true
sleep 1

# Kill leftover GPU processes (not tritonserver)
for pid in $(nvidia-smi --query-compute-apps=pid,name --format=csv,noheader 2>/dev/null | grep -v tritonserver | awk -F',' '{print $1}'); do
    echo "Killing stale GPU process $pid"
    kill -9 "$pid" 2>/dev/null || true
done
sleep 1

# Activate venv
source "$SLAM_DIR/.venv/bin/activate"

echo "=== Starting SLAM with WebRTC from $COMMA_IP (${FPS}fps) ==="
cd "$SLAM_DIR"
exec python -u main.py --dataset "webrtc:${COMMA_IP}:wideRoad:${FPS}" --config config/live.yaml $EXTRA_ARGS
