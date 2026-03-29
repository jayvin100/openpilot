#!/usr/bin/env python3
"""
Pinball frame streamer: reads camera frames via VisionIPC, sends raw NV12
over TCP at half resolution, receives control bytes and publishes
carControl for the pinball car controller.

Protocol (TCP, port 8042):
  On connect, device sends: [uint16 width][uint16 height] (4 bytes, big-endian)
  Device → Host: raw NV12 frames (width * height * 3 // 2 bytes each, continuous)
  Host → Device: [1 byte control bitfield] (B=0x01, F=0x02, J=0x08)
"""
import time
import socket
import struct
import threading
import numpy as np
import cereal.messaging as messaging
from msgq.visionipc import VisionIpcClient, VisionStreamType
from openpilot.common.realtime import config_realtime_process, Ratekeeper

PORT = 8042


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

  ctrl_state = 0
  ctrl_lock = threading.Lock()

  # TCP server
  server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
  server.bind(('0.0.0.0', PORT))
  server.listen(1)

  rk = Ratekeeper(100, print_delay_threshold=None)

  while True:
    print(f"framestreamer: waiting for connection on :{PORT}...")
    conn, addr = server.accept()
    print(f"framestreamer: connected from {addr}")
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    conn.sendall(struct.pack('>HH', half_w, half_h))

    running = True

    def recv_loop():
      nonlocal running, ctrl_state
      while running:
        try:
          data = conn.recv(1024)
          if not data:
            running = False
            break
          with ctrl_lock:
            ctrl_state = data[-1]
        except Exception:
          running = False
          break

    threading.Thread(target=recv_loop, daemon=True).start()

    # single-threaded main loop: publish controls at 100Hz, send frames when ready
    while running:
      # publish controls every tick (100Hz) — must stay in the RT main thread
      with ctrl_lock:
        state = ctrl_state

      msg = messaging.new_message('carControl', valid=True)
      msg.carControl.enabled = True
      msg.carControl.actuators.gas = 1.0 if (state & 0x02) else 0.0    # F = left flipper
      msg.carControl.actuators.brake = 1.0 if (state & 0x08) else 0.0  # J = right flipper
      msg.carControl.actuators.accel = -1.0 if (state & 0x01) else 0.0 # B = start
      pm.send('carControl', msg)

      cs_msg = messaging.new_message('controlsState', valid=True)
      cs_msg.controlsState.lateralControlState.init('debugState')
      pm.send('controlsState', cs_msg)

      # grab a frame if available (non-blocking)
      buf = vipc.recv(timeout_ms=0)
      if buf is not None:
        y_full = np.array(buf.data[:buf.uv_offset], dtype=np.uint8).reshape(-1, buf.stride)[:H, :W]
        uv_full = np.array(buf.data[buf.uv_offset:buf.uv_offset + buf.stride * (H // 2)], dtype=np.uint8) \
                      .reshape(-1, buf.stride)[:H // 2, :W]
        y_half = y_full[::2, ::2]
        uv_half = uv_full[::2].reshape(-1, W // 2, 2)[:, ::2, :].reshape(H // 4, half_w)
        nv12 = np.ascontiguousarray(np.vstack([y_half, uv_half]))
        try:
          conn.sendall(nv12.tobytes())
        except Exception:
          running = False
          break

      rk.keep_time()

    with ctrl_lock:
      ctrl_state = 0
    conn.close()
    print("framestreamer: disconnected, waiting for reconnect...")


if __name__ == "__main__":
  main()
