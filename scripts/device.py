#!/usr/bin/env python3
"""See and drive the comma 3X screen over SSH.

  scripts/device.py screen [out.png]        save what the device shows (default: screens/latest.png)
  scripts/device.py tap X Y                 tap at UI coordinates (2160x1080, origin top-left)
  scripts/device.py swipe X1 Y1 X2 Y2 [ms]  drag from one point to another (default 300 ms)
  scripts/device.py push TREE FILE...       copy files from a worktree (paths relative to TREE) into /data/openpilot
  scripts/device.py restart-ui              restart the UI process (picks up pushed files)

Screenshots need the screenshot hook from patches/ (openpilot/jay/screenshot.py) and the screen to be on.
Touches are injected into the touchscreen's evdev node, so the UI sees them as real finger input.
Pushed files are for quick testing only: the next reboot swaps the finalized build back in.
"""
import os
import subprocess
import sys

HOST = os.environ.get("COMMA_HOST", "comma@192.168.0.138")
KEY = os.path.expanduser(os.environ.get("COMMA_KEY", "~/.ssh/id_ed25519_comma"))
SSH = ["ssh", "-i", KEY, "-o", "ConnectTimeout=5", HOST]
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

REQUEST_PATH = "/tmp/jay_screenshot_request"
SCREENSHOT_PATH = "/tmp/jay_screenshot.png"

UI_W, UI_H = 2160, 1080
TOUCH_W, TOUCH_H = 1080, 2160  # the panel is portrait, the UI is landscape


def ui_to_touch(x: float, y: float) -> tuple[int, int]:
  return round(TOUCH_W - y), round(x)


# Runs on the device. Writes Linux input_events (struct timeval, u16 type, u16 code, s32 value) to the touchscreen.
INJECT = r'''
import struct, sys, time
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
BTN_TOUCH, ABS_X, ABS_Y, ABS_MT_SLOT, ABS_MT_X, ABS_MT_Y, ABS_MT_TRACKING_ID = 0x14a, 0x00, 0x01, 0x2f, 0x35, 0x36, 0x39
points = [tuple(map(int, p.split(","))) for p in sys.argv[2:]]
step = float(sys.argv[1])
with open("/dev/input/event2", "wb", buffering=0) as f:
  def ev(t, c, v):
    now = time.time()
    f.write(struct.pack("qqHHi", int(now), int(now % 1 * 1e6), t, c, v))
  def pos(x, y):
    ev(EV_ABS, ABS_MT_X, x); ev(EV_ABS, ABS_MT_Y, y); ev(EV_ABS, ABS_X, x); ev(EV_ABS, ABS_Y, y)
  ev(EV_ABS, ABS_MT_SLOT, 0); ev(EV_ABS, ABS_MT_TRACKING_ID, int(time.time()) % 60000)
  pos(*points[0]); ev(EV_KEY, BTN_TOUCH, 1); ev(EV_SYN, 0, 0)
  for x, y in points[1:]:
    time.sleep(step); pos(x, y); ev(EV_SYN, 0, 0)
  time.sleep(max(step, 0.05))
  ev(EV_ABS, ABS_MT_TRACKING_ID, -1); ev(EV_KEY, BTN_TOUCH, 0); ev(EV_SYN, 0, 0)
'''


def inject(points: list[tuple[float, float]], step: float) -> None:
  args = [f"{tx},{ty}" for tx, ty in (ui_to_touch(x, y) for x, y in points)]
  subprocess.run(SSH + ["python3", "-", str(step), *args], input=INJECT.encode(), check=True)


def screen(out: str) -> None:
  script = f'''
rm -f {SCREENSHOT_PATH}; touch {REQUEST_PATH}
for i in $(seq 1 50); do [ -f {SCREENSHOT_PATH} ] && exit 0; sleep 0.1; done
rm -f {REQUEST_PATH}; exit 3
'''
  r = subprocess.run(SSH + [script])
  if r.returncode == 3:
    sys.exit("No frame captured: the screen is probably off. Tap it (e.g. `tap 1080 540` on an empty spot) and retry.")
  r.check_returncode()
  os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
  subprocess.run(["scp", "-q", "-i", KEY, f"{HOST}:{SCREENSHOT_PATH}", out], check=True)
  print(out)


UI_PATTERN = "^/usr/local/venv/bin/python -m openpilot[.]selfdrive[.]ui[.]ui"
START_UI = ("cd /data/openpilot; source launch_env.sh; PYTHONPATH=/data/openpilot setsid nohup "
            "/usr/local/venv/bin/python -m openpilot.selfdrive.ui.ui >/tmp/jay_ui.log 2>&1 </dev/null & disown")


def push(tree: str, files: list[str]) -> None:
  for f in files:
    dest = f"/data/openpilot/{f}"
    subprocess.run(SSH + [f"mkdir -p {os.path.dirname(dest)}"], check=True)
    subprocess.run(["scp", "-q", "-i", KEY, os.path.join(tree, f), f"{HOST}:{dest}"], check=True)
    print(dest)


def restart_ui() -> None:
  # pkill -f would match this ssh session's own command line, so look the PID up with an anchored pattern
  subprocess.run(SSH + [f'pid=$(pgrep -f "{UI_PATTERN}"); [ -n "$pid" ] && kill $pid; sleep 1'], check=True)
  # a separate ssh call, so the new UI isn't tied to the session that killed the old one
  subprocess.run(SSH + [START_UI], check=True)
  print("UI restarted, log: /tmp/jay_ui.log")


def main() -> None:
  if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
    sys.exit(__doc__)
  cmd, args = sys.argv[1], sys.argv[2:]
  if cmd == "screen":
    screen(args[0] if args else os.path.join(REPO, "screens", "latest.png"))
  elif cmd == "tap" and len(args) == 2:
    inject([(float(args[0]), float(args[1]))], 0.05)
  elif cmd == "swipe" and len(args) in (4, 5):
    x1, y1, x2, y2 = map(float, args[:4])
    duration = (float(args[4]) if len(args) == 5 else 300) / 1000
    steps = max(int(duration / 0.016), 2)
    inject([(x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps) for i in range(steps + 1)], duration / steps)
  elif cmd == "push" and len(args) >= 2:
    push(args[0], args[1:])
  elif cmd == "restart-ui" and not args:
    restart_ui()
  else:
    sys.exit(__doc__)


if __name__ == "__main__":
  main()
