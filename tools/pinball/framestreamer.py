#!/usr/bin/env python3
"""
Pinball frame streamer: reads camera frames via VisionIPC, sends raw NV12
over TCP at half resolution, receives WASD control bytes and publishes
carControl for the pinball car controller.

Protocol (TCP, port 8042):
  On connect, device sends: [uint16 width][uint16 height] (4 bytes, big-endian)
  Device → Host: raw NV12 frames (width * height * 3 // 2 bytes each, continuous)
  Host → Device: [1 byte control bitfield] (W=0x01, A=0x02, S=0x04, D=0x08)
"""
import time
import socket
import struct
import threading
import numpy as np
import cereal.messaging as messaging
from msgq.visionipc import VisionIpcClient, VisionStreamType
from openpilot.common.realtime import config_realtime_process

PORT = 8042


def send_ctrl(pm, state):
  msg = messaging.new_message('carControl', valid=True)
  msg.carControl.enabled = True
  msg.carControl.actuators.gas = 1.0 if (state & 0x02) else 0.0    # A = left flipper
  msg.carControl.actuators.brake = 1.0 if (state & 0x08) else 0.0  # D = right flipper
  msg.carControl.actuators.accel = -1.0 if (state & 0x01) else 0.0 # W = start
  pm.send('carControl', msg)

  cs_msg = messaging.new_message('controlsState', valid=True)
  cs_msg.controlsState.lateralControlState.init('debugState')
  pm.send('controlsState', cs_msg)


def main():
  config_realtime_process(7, 54)

  pm = messaging.PubMaster(['carControl', 'controlsState'])

  # connect to camerad
  while True:
    available = VisionIpcClient.available_streams("camerad", block=False)
    if available:
      break
    time.sleep(0.1)

  vipc = VisionIpcClient("camerad", VisionStreamType.VISION_STREAM_WIDE_ROAD, True)
  while not vipc.connect(False):
    time.sleep(0.1)

  W, H = vipc.width, vipc.height
  half_w, half_h = W // 2, H // 2
  print(f"framestreamer: camerad {W}x{H}, streaming {half_w}x{half_h} NV12")

  # control state: bitfield from host (W=0x01, A=0x02, S=0x04, D=0x08)
  ctrl_state = 0
  ctrl_lock = threading.Lock()

  # TCP server
  server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
  server.bind(('0.0.0.0', PORT))
  server.listen(1)

  while True:
    print(f"framestreamer: waiting for connection on :{PORT}...")
    conn, addr = server.accept()
    print(f"framestreamer: connected from {addr}")
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    # send frame dimensions once
    conn.sendall(struct.pack('>HH', half_w, half_h))

    running = True

    def recv_loop():
      nonlocal running, ctrl_state
      while running:
        try:
          data = conn.recv(1024)  # drain buffer, use latest byte
          if not data:
            running = False
            break
          with ctrl_lock:
            ctrl_state = data[-1]
          send_ctrl(pm, data[-1])  # publish immediately on input
        except Exception:
          running = False
          break

    threading.Thread(target=recv_loop, daemon=True).start()

    while running:
      buf = vipc.recv()
      if buf is None:
        continue

      # also publish controls every frame as keepalive
      with ctrl_lock:
        send_ctrl(pm, ctrl_state)

      # extract Y and UV planes (removing stride padding)
      y_full = np.array(buf.data[:buf.uv_offset], dtype=np.uint8).reshape(-1, buf.stride)[:H, :W]
      uv_full = np.array(buf.data[buf.uv_offset:buf.uv_offset + buf.stride * (H // 2)], dtype=np.uint8) \
                    .reshape(-1, buf.stride)[:H // 2, :W]

      # subsample to half resolution
      y_half = y_full[::2, ::2]                                                              # (H/2, W/2)
      uv_half = uv_full[::2].reshape(-1, W // 2, 2)[:, ::2, :].reshape(H // 4, half_w)      # (H/4, W/2)
      nv12 = np.ascontiguousarray(np.vstack([y_half, uv_half]))

      try:
        conn.sendall(nv12.tobytes())
      except Exception:
        running = False
        break

    with ctrl_lock:
      ctrl_state = 0
    send_ctrl(pm, 0)
    conn.close()
    print("framestreamer: disconnected, waiting for reconnect...")


if __name__ == "__main__":
  main()
