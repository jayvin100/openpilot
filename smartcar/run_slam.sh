#!/bin/bash
# Run MASt3R-SLAM on live frames from the comma4
#
# Usage:
#   ./run_slam.sh <comma4_ip> [--no-viz]
#
# This starts the frame streamer in the background, waits for enough frames,
# then launches MASt3R-SLAM on the captured frames.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SLAM_DIR="$SCRIPT_DIR/MASt3R-SLAM"
FRAMES_DIR="/tmp/smolcar_frames"
COMMA_IP="${1:-192.168.61.62}"
EXTRA_ARGS="${@:2}"

# Activate venv
source "$SLAM_DIR/.venv/bin/activate"

# Clean old frames
rm -rf "$FRAMES_DIR"
mkdir -p "$FRAMES_DIR"

echo "=== Starting frame capture from comma4 at $COMMA_IP ==="
python -u "$SCRIPT_DIR/stream.py" --ip "$COMMA_IP" --out "$FRAMES_DIR" --fps 10 &
STREAM_PID=$!

# Wait for some frames to accumulate
echo "Waiting for frames..."
while [ $(ls "$FRAMES_DIR"/*.png 2>/dev/null | wc -l) -lt 5 ]; do
    sleep 1
done
echo "Got initial frames, starting SLAM..."

# Run MASt3R-SLAM in live mode on the frames directory
cd "$SLAM_DIR"
python -u main.py --dataset "live:$FRAMES_DIR" --config config/base.yaml $EXTRA_ARGS

# Cleanup
kill $STREAM_PID 2>/dev/null
wait $STREAM_PID 2>/dev/null
echo "Done."
