#!/usr/bin/env python3
"""
Host-side pinball controller: receives and displays camera frames,
sends keyboard controls over USB/TCP.

Requirements: pip install raylib opencv-python numpy
Usage:
  python pinball_ctrl.py [device_ip]              # play mode
  python pinball_ctrl.py --calibrate [device_ip]  # start in road cam / bbox calibration mode

Controls: F/J = flippers, B = start/ball, G = toggle camera
Calibrate: click+drag to draw SCORE box (red), then BALL # box (blue). R = reset, Enter = save.
"""
import json
import sys
import time
import socket
import struct
import threading
import numpy as np
import cv2
from raylib import ffi, rl

CALIBRATE = "--calibrate" in sys.argv
args = [a for a in sys.argv[1:] if not a.startswith("--")]
DEVICE_IP = args[0] if args else "192.168.55.1"
PORT = 8042
WINDOW_W, WINDOW_H = 960, 600

BLACK = ffi.new("Color *", [0, 0, 0, 255])[0]
WHITE = ffi.new("Color *", [255, 255, 255, 255])[0]
GREEN = ffi.new("Color *", [0, 255, 0, 255])[0]
DARKGRAY = ffi.new("Color *", [80, 80, 80, 255])[0]
RED = ffi.new("Color *", [230, 40, 40, 255])[0]
BLUE = ffi.new("Color *", [40, 100, 230, 255])[0]

BBOX_COLORS = [RED, BLUE]
BBOX_LABELS = [b"SCORE", b"BALL #"]


def recv_exact(sock, n):
  data = b''
  while len(data) < n:
    chunk = sock.recv(min(n - len(data), 262144))
    if not chunk:
      return None
    data += chunk
  return data


def send_command(sock, payload: dict):
  """Send a JSON command: [0xFF][2 byte length][JSON]."""
  data = json.dumps(payload).encode()
  sock.sendall(b'\xff' + struct.pack('>H', len(data)) + data)


def frame_to_screen(fx, fy, frame_w, frame_h):
  sw = rl.GetScreenWidth()
  sh = rl.GetScreenHeight()
  return int(fx * sw / frame_w), int(fy * sh / frame_h)


def screen_to_frame(sx, sy, frame_w, frame_h):
  sw = rl.GetScreenWidth()
  sh = rl.GetScreenHeight()
  return int(sx * frame_w / sw), int(sy * frame_h / sh)


def main():
  sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  print(f"Connecting to {DEVICE_IP}:{PORT}...")
  sock.connect((DEVICE_IP, PORT))
  sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
  print("Connected!")

  dim_data = recv_exact(sock, 4)
  w, h = struct.unpack('>HH', dim_data)
  frame_size = w * h * 3 // 2
  print(f"Frame: {w}x{h}, NV12 = {frame_size} bytes")

  rl.InitWindow(WINDOW_W, WINDOW_H, b"Pinball")
  rl.SetTargetFPS(60)

  blank = rl.GenImageColor(w, h, BLACK)
  rl.ImageFormat(ffi.addressof(blank), rl.PIXELFORMAT_UNCOMPRESSED_R8G8B8)
  texture = rl.LoadTextureFromImage(blank)
  rl.UnloadImage(blank)

  latest_rgb = None
  frame_lock = threading.Lock()
  connected = True
  frame_count = 0

  def recv_frames():
    nonlocal latest_rgb, connected, frame_count
    while connected:
      try:
        frame_data = recv_exact(sock, frame_size)
        if frame_data is None:
          break
        nv12 = np.frombuffer(frame_data, dtype=np.uint8).reshape(h * 3 // 2, w)
        rgb = cv2.cvtColor(nv12, cv2.COLOR_YUV2RGB_NV12)
        with frame_lock:
          latest_rgb = rgb
          frame_count += 1
      except Exception:
        break
    connected = False

  threading.Thread(target=recv_frames, daemon=True).start()

  road_cam = CALIBRATE
  bboxes = []       # list of (x1, y1, x2, y2) in frame coords
  drawing = False
  draw_start = (0, 0)
  last_fps_time = time.time()
  last_fps_count = 0
  cam_fps = 0

  while not rl.WindowShouldClose() and connected:
    # --- input ---
    if rl.IsKeyPressed(rl.KEY_G):
      road_cam = not road_cam

    ctrl = 0
    if rl.IsKeyDown(rl.KEY_B):                                 ctrl |= 0x01
    if rl.IsKeyDown(rl.KEY_F) or rl.IsKeyDown(rl.KEY_LEFT):   ctrl |= 0x02
    if rl.IsKeyDown(rl.KEY_J) or rl.IsKeyDown(rl.KEY_RIGHT):  ctrl |= 0x08
    if road_cam:                                                ctrl |= 0x10

    try:
      sock.sendall(bytes([ctrl]))
    except Exception:
      break

    # --- bbox drawing (road cam only) ---
    if road_cam:
      mx, my = rl.GetMouseX(), rl.GetMouseY()
      if not drawing and rl.IsMouseButtonPressed(0) and len(bboxes) < 2:
        drawing = True
        draw_start = (mx, my)
      if drawing and rl.IsMouseButtonReleased(0):
        drawing = False
        fx1, fy1 = screen_to_frame(draw_start[0], draw_start[1], w, h)
        fx2, fy2 = screen_to_frame(mx, my, w, h)
        bboxes.append((min(fx1, fx2), min(fy1, fy2), max(fx1, fx2), max(fy1, fy2)))
      if rl.IsKeyPressed(rl.KEY_R):
        bboxes = []
      if rl.IsKeyPressed(rl.KEY_ENTER) and len(bboxes) == 2:
        send_command(sock, {"score": list(bboxes[0]), "ball": list(bboxes[1])})
        print(f"Saved OCR regions: score={bboxes[0]} ball={bboxes[1]}")

    # --- update texture ---
    with frame_lock:
      rgb = latest_rgb
      latest_rgb = None
    if rgb is not None:
      rl.UpdateTexture(texture, ffi.from_buffer(np.ascontiguousarray(rgb).tobytes()))

    # --- draw ---
    rl.BeginDrawing()
    rl.ClearBackground(BLACK)

    screen_w = rl.GetScreenWidth()
    screen_h = rl.GetScreenHeight()
    src = ffi.new("Rectangle *", [0, 0, texture.width, texture.height])
    dst = ffi.new("Rectangle *", [0, 0, screen_w, screen_h])
    rl.DrawTexturePro(texture, src[0], dst[0], ffi.new("Vector2 *", [0, 0])[0], 0.0, WHITE)

    # draw bboxes
    if road_cam:
      for i, (fx1, fy1, fx2, fy2) in enumerate(bboxes):
        sx1, sy1 = frame_to_screen(fx1, fy1, w, h)
        sx2, sy2 = frame_to_screen(fx2, fy2, w, h)
        rl.DrawRectangleLines(sx1, sy1, sx2 - sx1, sy2 - sy1, BBOX_COLORS[i])
        rl.DrawText(BBOX_LABELS[i], sx1, sy1 - 20, 18, BBOX_COLORS[i])

      # in-progress box
      if drawing and rl.IsMouseButtonDown(0):
        cx, cy = rl.GetMouseX(), rl.GetMouseY()
        rx = min(draw_start[0], cx)
        ry = min(draw_start[1], cy)
        rw = abs(cx - draw_start[0])
        rh = abs(cy - draw_start[1])
        color = BBOX_COLORS[min(len(bboxes), 1)]
        rl.DrawRectangleLines(rx, ry, rw, rh, color)

      # instructions
      if len(bboxes) == 0:
        rl.DrawText(b"Draw SCORE box", 10, screen_h - 30, 20, RED)
      elif len(bboxes) == 1:
        rl.DrawText(b"Draw BALL # box", 10, screen_h - 30, 20, BLUE)
      else:
        rl.DrawText(b"Enter: save | R: reset", 10, screen_h - 30, 20, GREEN)

      rl.DrawText(b"ROAD CAM | G: toggle | R: reset", 10, 40, 18, DARKGRAY)

    # key indicators (play mode)
    if not road_cam:
      indicators = [
        (b"B", ctrl & 0x01, screen_w // 2, screen_h - 70),
        (b"F", ctrl & 0x02, screen_w // 2 - 40, screen_h - 35),
        (b"J", ctrl & 0x08, screen_w // 2 + 40, screen_h - 35),
      ]
      for label, pressed, x, y in indicators:
        color = GREEN if pressed else DARKGRAY
        rl.DrawText(label, x - 8, y - 12, 24, color)

    # fps
    now = time.time()
    if now - last_fps_time >= 1.0:
      cam_fps = frame_count - last_fps_count
      last_fps_count = frame_count
      last_fps_time = now
    cam_label = b"road" if road_cam else b"wide"
    rl.DrawText(f"{cam_fps} fps ({cam_label.decode()})".encode(), 10, 10, 24, GREEN)

    rl.EndDrawing()

  rl.UnloadTexture(texture)
  sock.close()
  rl.CloseWindow()


if __name__ == "__main__":
  main()
