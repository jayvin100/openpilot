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
import struct
import sys
import threading
import time

import aiohttp
from aiortc import RTCPeerConnection, RTCSessionDescription

# Pre-compute IMU message header
IMU_HEADER = b'I'
IMU_STRUCT = struct.Struct('<7d')


async def run(args, write_lock):
    pc = RTCPeerConnection()
    t_last = time.time()
    frame_count = 0
    imu_count = 0

    last_accel = None  # (ax, ay, az)

    # Buffer for IMU packets — written to stdout in batches
    imu_buf = bytearray()

    @pc.on("track")
    def on_track(track):
        if track.kind == "video":
            asyncio.ensure_future(recv_video(track))

    # Create data channel BEFORE the offer — webrtcd detects it and sends outgoing services on it
    dc = pc.createDataChannel("data", ordered=True)

    @dc.on("message")
    def on_dc_message(message):
        nonlocal imu_count, last_accel
        # Fast path: check message type prefix before full JSON parse
        if b'"accelerometer"' in (message if isinstance(message, bytes) else message.encode()):
            try:
                msg = json.loads(message)
            except (json.JSONDecodeError, TypeError):
                return
            v = msg["data"]["acceleration"]["v"]
            last_accel = (float(v[0]), float(v[1]), float(v[2]))

        elif b'"gyroscope"' in (message if isinstance(message, bytes) else message.encode()):
            try:
                msg = json.loads(message)
            except (json.JSONDecodeError, TypeError):
                return
            v = msg["data"]["gyroUncalibrated"]["v"]

            if last_accel is not None:
                ts = time.monotonic()
                imu_buf.extend(IMU_HEADER)
                imu_buf.extend(IMU_STRUCT.pack(ts, *last_accel, float(v[0]), float(v[1]), float(v[2])))

                imu_count += 1
                # Flush every ~10 IMU packets (~100ms worth) instead of every packet
                if imu_count % 10 == 0:
                    with write_lock:
                        sys.stdout.buffer.write(imu_buf)
                        sys.stdout.buffer.flush()
                    imu_buf.clear()

                if imu_count % 500 == 0:
                    print(f"[feeder] {imu_count} IMU packets from data channel",
                          file=sys.stderr, flush=True)
        # else: ignore carState and other messages without parsing

    async def recv_video(track):
        nonlocal t_last, frame_count
        while True:
            try:
                frame = await track.recv()
            except Exception as e:
                print(f"Track ended: {e}", file=sys.stderr, flush=True)
                break

            t_now = time.time()
            dt = t_now - t_last
            if dt < 1.0 / args.fps:
                continue
            t_last = t_now

            img = frame.to_ndarray(format="bgr24")
            h, w = img.shape[:2]
            data = img.tobytes()
            timestamp = time.monotonic()

            # Flush any pending IMU data, then write frame
            with write_lock:
                if imu_buf:
                    sys.stdout.buffer.write(imu_buf)
                    imu_buf.clear()
                sys.stdout.buffer.write(b'F')
                sys.stdout.buffer.write(struct.pack('<d', timestamp))
                sys.stdout.buffer.write(struct.pack('<III', w, h, len(data)))
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()

            frame_count += 1
            if frame_count % 10 == 0:
                print(f"[feeder] {frame_count} frames  {w}x{h}  dt={1000*dt:.0f}ms  imu={imu_count}",
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
        while True:
            await asyncio.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        await pc.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="192.168.61.62")
    parser.add_argument("--camera", default="wideRoad", choices=["road", "wideRoad", "driver"])
    parser.add_argument("--fps", type=int, default=5)
    args = parser.parse_args()

    write_lock = threading.Lock()
    asyncio.run(run(args, write_lock))


if __name__ == "__main__":
    main()
