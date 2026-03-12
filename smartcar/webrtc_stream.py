#!/usr/bin/env python3
"""
WebRTC client that connects to openpilot's webrtcd on a comma4 device.
Receives H264 video frames via WebRTC and saves them as .npy files for SLAM.
Also sends joystick commands over the data channel.

Replaces the SSH+ffmpeg pipeline with direct WebRTC — much lower latency.

Usage:
  python3 webrtc_stream.py --ip 192.168.61.62
"""
import argparse
import asyncio
import json
import os
import time

import aiohttp
import numpy as np
from aiortc import RTCPeerConnection, RTCSessionDescription
from aiortc.contrib.media import MediaBlackhole


async def run(args):
    out_dir = args.out
    os.makedirs(out_dir, exist_ok=True)
    # Clean old frames
    for f in os.listdir(out_dir):
        if f.endswith(".npy"):
            os.remove(os.path.join(out_dir, f))

    pc = RTCPeerConnection()

    frame_idx = 0
    t_last = time.time()

    @pc.on("connectionstatechange")
    def on_conn_state():
        print(f"Connection: {pc.connectionState}", flush=True)

    @pc.on("iceconnectionstatechange")
    def on_ice_state():
        print(f"ICE: {pc.iceConnectionState}", flush=True)

    @pc.on("icegatheringstatechange")
    def on_ice_gather():
        print(f"ICE gathering: {pc.iceGatheringState}", flush=True)

    @pc.on("track")
    def on_track(track):
        nonlocal frame_idx, t_last
        print(f"Track received: {track.kind}", flush=True)
        if track.kind == "video":
            asyncio.ensure_future(recv_video(track))

    async def recv_video(track):
        nonlocal frame_idx, t_last
        while True:
            try:
                frame = await track.recv()
            except Exception as e:
                print(f"Track ended: {e}", flush=True)
                break

            # Convert to numpy BGR array
            img = frame.to_ndarray(format="bgr24")
            t_now = time.time()

            # Throttle to target fps
            dt = t_now - t_last
            if dt < 1.0 / args.fps:
                continue
            t_last = t_now

            fname = os.path.join(out_dir, f"{frame_idx:06d}.npy")
            tmpname = os.path.join(out_dir, f".tmp_{frame_idx:06d}.npy")
            np.save(tmpname, img)
            os.rename(tmpname, fname)

            if frame_idx % 10 == 0:
                print(f"[{frame_idx:4d}] {img.shape[1]}x{img.shape[0]} dt={1000*dt:.0f}ms", flush=True)
            frame_idx += 1

    @pc.on("datachannel")
    def on_datachannel(channel):
        print(f"Data channel: {channel.label}", flush=True)

    # Create data channel for joystick (must be created before offer)
    dc = pc.createDataChannel("data", ordered=True)

    @dc.on("open")
    def on_open():
        print("Data channel open", flush=True)

    @dc.on("message")
    def on_message(msg):
        pass  # Ignore incoming messages (carState etc.)

    # Add transceiver to receive video (one per camera, no audio)
    pc.addTransceiver("video", direction="recvonly")

    # Create offer
    offer = await pc.createOffer()
    await pc.setLocalDescription(offer)

    # Wait for ICE gathering
    while pc.iceGatheringState != "complete":
        await asyncio.sleep(0.1)

    # Send offer to webrtcd
    url = f"http://{args.ip}:5001/stream"
    payload = {
        "sdp": pc.localDescription.sdp,
        "cameras": [args.camera],
        "bridge_services_in": ["testJoystick"],
        "bridge_services_out": ["carState"],
    }

    print(f"Connecting to {url} camera={args.camera}...", flush=True)
    async with aiohttp.ClientSession() as session:
        async with session.post(url, json=payload) as resp:
            if resp.status != 200:
                print(f"Signaling failed: {resp.status} {await resp.text()}", flush=True)
                return
            answer = await resp.json()

    await pc.setRemoteDescription(
        RTCSessionDescription(sdp=answer["sdp"], type=answer["type"])
    )
    print("WebRTC connected!", flush=True)

    # Keep running
    try:
        while True:
            await asyncio.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        await pc.close()
        print(f"{frame_idx} frames captured", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="192.168.61.62", help="comma4 IP")
    parser.add_argument("--out", default="/dev/shm/smolcar_frames", help="output dir")
    parser.add_argument("--camera", default="road", choices=["road", "wideRoad", "driver"])
    parser.add_argument("--fps", type=int, default=5, help="target output FPS")
    args = parser.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
