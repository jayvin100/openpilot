#!/usr/bin/env python3
"""
WebRTC + IMU feeder for ORB-SLAM3 mono-inertial.

Receives video frames via WebRTC video track and IMU data via WebRTC data
channel from comma4's webrtcd. Writes interleaved binary messages to stdout
for mono_live to consume.

Protocol:
  'I' + 7 doubles (timestamp, ax, ay, az, gx, gy, gz)
  'F' + 1 double (timestamp) + 3 uint32 (w, h, nbytes) + BGR pixels

Usage:
  python3 webrtc_feeder.py | ./mono_live vocab settings
"""
import argparse
import asyncio
import json
import signal
import struct
import sys
import threading
import time

import aiohttp
from aiortc import RTCPeerConnection, RTCSessionDescription

# Ignore SIGPIPE so we get BrokenPipeError instead of dying
signal.signal(signal.SIGPIPE, signal.SIG_IGN)

# Pre-compute struct for IMU packet (header byte + 7 doubles)
IMU_STRUCT = struct.Struct('<7d')
IMU_PKT_SIZE = 1 + IMU_STRUCT.size  # 'I' + 56 bytes


async def run(args, write_lock):
    pc = RTCPeerConnection()
    frame_count = 0
    imu_count = 0
    pipe_broken = False

    last_accel = None  # (ax, ay, az)
    last_gyro = None   # (gx, gy, gz)

    # Buffer for IMU packets — flushed to stdout on every frame
    # IMPORTANT: all access must be under write_lock (data channel callback runs on a different thread)
    imu_buf = bytearray()

    @pc.on("track")
    def on_track(track):
        if track.kind == "video":
            asyncio.ensure_future(recv_video(track))

    # Create data channel BEFORE the offer — webrtcd detects it and sends outgoing services on it
    dc = pc.createDataChannel("data", ordered=True)

    @dc.on("message")
    def on_dc_message(message):
        nonlocal imu_count, last_accel, last_gyro
        # Fast path: check message type prefix before full JSON parse
        raw = message if isinstance(message, bytes) else message.encode()
        if b'"accelerometer"' in raw:
            try:
                msg = json.loads(raw)
            except (json.JSONDecodeError, TypeError):
                return
            v = msg["data"]["acceleration"]["v"]
            last_accel = (float(v[0]), float(v[1]), float(v[2]))

        elif b'"gyroscope"' in raw:
            try:
                msg = json.loads(raw)
            except (json.JSONDecodeError, TypeError):
                return
            v = msg["data"]["gyroUncalibrated"]["v"]
            last_gyro = (float(v[0]), float(v[1]), float(v[2]))

            if last_accel is not None:
                ts = time.perf_counter()
                # Build complete packet, then append atomically under lock
                pkt = b'I' + IMU_STRUCT.pack(ts, *last_accel, *last_gyro)
                with write_lock:
                    imu_buf.extend(pkt)

                imu_count += 1
                if imu_count % 500 == 0:
                    print(f"[feeder] {imu_count} IMU packets from data channel",
                          file=sys.stderr, flush=True)

    async def recv_video(track):
        nonlocal frame_count, pipe_broken
        raw_count = 0
        skip = max(1, 20 // args.fps)  # 20fps input: skip=2 for 10fps, skip=4 for 5fps
        while not pipe_broken:
            try:
                frame = await track.recv()
            except Exception as e:
                print(f"Track ended: {e}", file=sys.stderr, flush=True)
                break

            raw_count += 1
            if raw_count % skip != 0:
                continue

            img = frame.to_ndarray(format="bgr24")
            h, w = img.shape[:2]
            data = img.tobytes()

            # Grab pending IMU under lock (fast), then write everything without the lock
            # so the data channel thread isn't blocked during the ~30ms 3MB frame write
            try:
                with write_lock:
                    pending_imu = bytes(imu_buf) if imu_buf else b''
                    imu_buf.clear()
                # All stdout writes are only from this coroutine, no lock needed
                if pending_imu:
                    sys.stdout.buffer.write(pending_imu)
                # Bracketing IMU sample at frame timestamp for preintegration
                timestamp = time.perf_counter()
                if last_accel is not None and last_gyro is not None:
                    sys.stdout.buffer.write(b'I')
                    sys.stdout.buffer.write(IMU_STRUCT.pack(timestamp, *last_accel, *last_gyro))
                sys.stdout.buffer.write(b'F')
                sys.stdout.buffer.write(struct.pack('<d', timestamp))
                sys.stdout.buffer.write(struct.pack('<III', w, h, len(data)))
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
            except BrokenPipeError:
                print("[feeder] mono_live pipe broken, exiting", file=sys.stderr, flush=True)
                pipe_broken = True
                break

            frame_count += 1
            if frame_count % 10 == 0:
                imu_per_frame = imu_count / max(frame_count, 1)
                print(f"[feeder] {frame_count} frames  {w}x{h}  imu_avg={imu_per_frame:.1f}/frame",
                      file=sys.stderr, flush=True)

    pc.addTransceiver("video", direction="recvonly")
    offer = await pc.createOffer()
    await pc.setLocalDescription(offer)

    while pc.iceGatheringState != "complete":
        await asyncio.sleep(0.1)

    url = f"http://{args.ip}:5001/stream"
    payload = {
        "sdp": pc.localDescription.sdp,
        "cameras": [args.camera],
        "bridge_services_in": ["testJoystick"],
        "bridge_services_out": ["accelerometer", "gyroscope"],
    }

    print(f"Connecting to {url} camera={args.camera}...", file=sys.stderr, flush=True)
    async with aiohttp.ClientSession() as session:
        async with session.post(url, json=payload) as resp:
            if resp.status != 200:
                print(f"Signaling failed: {resp.status} {await resp.text()}",
                      file=sys.stderr, flush=True)
                return
            answer = await resp.json()

    await pc.setRemoteDescription(
        RTCSessionDescription(sdp=answer["sdp"], type=answer["type"])
    )
    print("WebRTC connected!", file=sys.stderr, flush=True)

    try:
        while not pipe_broken:
            await asyncio.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        await pc.close()
        if pipe_broken:
            sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="192.168.61.62")
    parser.add_argument("--camera", default="wideRoad", choices=["road", "wideRoad", "driver"])
    parser.add_argument("--fps", type=int, default=10)
    args = parser.parse_args()

    write_lock = threading.Lock()
    asyncio.run(run(args, write_lock))


if __name__ == "__main__":
    main()
