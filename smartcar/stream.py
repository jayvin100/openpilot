#!/usr/bin/env python3
"""
Receives the wide road camera H264 stream from the comma4 via SSH,
decodes it with ffmpeg, and saves frames as PNGs for MASt3R-SLAM.

Usage:
  python3 stream.py --ip 192.168.61.62
"""
import argparse
import os
import re
import subprocess
import sys
import threading
import time

import cv2
import numpy as np


SSH_KEY = "/home/batman/xx/private/key/id_rsa"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REMOTE_SCRIPT = os.path.join(SCRIPT_DIR, "comma4_stream_h264.py")

SOCK_MAP = {
    "wideRoad": "livestreamWideRoadEncodeData",
    "road": "livestreamRoadEncodeData",
    "driver": "livestreamDriverEncodeData",
}

SSH_OPTS = ["-i", SSH_KEY, "-o", "StrictHostKeyChecking=no", "-o", "ConnectTimeout=5"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", required=True, help="comma4 IP address")
    parser.add_argument("--out", default="/dev/shm/smolcar_frames", help="output directory (use /dev/shm for RAM)")
    parser.add_argument("--camera", default="wideRoad", choices=list(SOCK_MAP.keys()))
    parser.add_argument("--fps", type=int, default=10, help="target FPS")
    args = parser.parse_args()

    # Clean and recreate output directory
    if os.path.exists(args.out):
        for f in os.listdir(args.out):
            os.remove(os.path.join(args.out, f))
    os.makedirs(args.out, exist_ok=True)
    service = SOCK_MAP[args.camera]
    target = f"comma@{args.ip}"

    # Copy the streaming script to the device
    print("Copying stream script to comma4...", flush=True)
    subprocess.run(
        ["scp", *SSH_OPTS, REMOTE_SCRIPT, f"{target}:/tmp/stream_h264.py"],
        check=True, capture_output=True,
    )

    ssh_cmd = [
        "ssh", *SSH_OPTS, target,
        f"/usr/local/venv/bin/python3 -u /tmp/stream_h264.py {service}",
    ]

    ffmpeg_cmd = [
        "ffmpeg",
        "-probesize", "131072",
        "-analyzeduration", "1000000",
        "-f", "h264",
        "-i", "pipe:0",
        "-vf", f"fps={args.fps}",
        "-pix_fmt", "bgr24",
        "-f", "image2pipe",
        "-vcodec", "rawvideo",
        "pipe:1",
    ]

    print(f"Streaming {args.camera} from {args.ip}...", flush=True)

    ssh_proc = subprocess.Popen(ssh_cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    ffmpeg_proc = subprocess.Popen(
        ffmpeg_cmd, stdin=ssh_proc.stdout, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    # Read ffmpeg stderr in background to detect resolution
    dims = {"w": 0, "h": 0}

    def read_stderr():
        for line in ffmpeg_proc.stderr:
            decoded = line.decode(errors="replace").strip()
            if "Video:" in decoded:
                m = re.search(r'(\d{3,4})x(\d{3,4})', decoded)
                if m:
                    dims["w"], dims["h"] = int(m.group(1)), int(m.group(2))
                    print(f"Stream resolution: {dims['w']}x{dims['h']}", flush=True)

    threading.Thread(target=read_stderr, daemon=True).start()

    # Wait for resolution detection (needs a keyframe, can take a few seconds)
    for _ in range(150):
        if dims["w"] > 0:
            break
        time.sleep(0.1)

    if dims["w"] == 0:
        dims["w"], dims["h"] = 1344, 760
        print(f"Using default resolution: {dims['w']}x{dims['h']}", flush=True)

    w, h = dims["w"], dims["h"]
    frame_bytes = w * h * 3

    print(f"Saving {w}x{h} frames to {args.out}", flush=True)

    frame_idx = 0
    try:
        while True:
            raw = b""
            while len(raw) < frame_bytes:
                chunk = ffmpeg_proc.stdout.read(frame_bytes - len(raw))
                if not chunk:
                    break
                raw += chunk

            if len(raw) < frame_bytes:
                print("Stream ended.", flush=True)
                break

            frame = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 3)
            fname = os.path.join(args.out, f"{frame_idx:06d}.npy")
            tmpname = os.path.join(args.out, f".tmp_{frame_idx:06d}.npy")
            np.save(tmpname, frame)
            os.rename(tmpname, fname)

            frame_idx += 1

            if frame_idx % 100 == 0:
                print(f"  {frame_idx} frames captured", flush=True)

    except KeyboardInterrupt:
        pass
    finally:
        print(f"{frame_idx} frames saved to {args.out}", flush=True)
        ffmpeg_proc.terminate()
        ssh_proc.terminate()


if __name__ == "__main__":
    main()
