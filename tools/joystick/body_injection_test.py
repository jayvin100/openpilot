#!/usr/bin/env python3
"""
Comma Body injection test via joystick commands.

Injects: full forward -> stop (5s) -> full backward
Publishes testJoystick messages at 100Hz, same as joystick_control.py.

Usage:
  python tools/joystick/body_injection_test.py
  python tools/joystick/body_injection_test.py --forward-duration 3 --backward-duration 3 --stop-duration 5
"""

import argparse
import time

from cereal import messaging
from openpilot.common.params import Params
from openpilot.common.realtime import Ratekeeper


def publish_phase(pm: messaging.PubMaster, rk: Ratekeeper, accel: float, duration: float, label: str):
  print(f"  {label}: accel={accel:.1f} for {duration:.1f}s")
  end_time = time.monotonic() + duration
  while time.monotonic() < end_time:
    msg = messaging.new_message('testJoystick')
    msg.valid = True
    msg.testJoystick.axes = [accel, 0.0]  # [accel, steer]
    pm.send('testJoystick', msg)
    rk.keep_time()


def main():
  parser = argparse.ArgumentParser(description='Body injection test: forward -> stop -> backward')
  parser.add_argument('--forward-duration', type=float, default=5.0, help='Seconds at full forward')
  parser.add_argument('--backward-duration', type=float, default=5.0, help='Seconds at full backward')
  parser.add_argument('--stop-duration', type=float, default=3.0, help='Seconds at zero')
  args = parser.parse_args()

  params = Params()
  params.put_bool('JoystickDebugMode', True)

  pm = messaging.PubMaster(['testJoystick'])
  rk = Ratekeeper(100, print_delay_threshold=None)

  print("Body injection test starting...")
  print(f"  Sequence: full forward ({args.forward_duration}s) -> stop ({args.stop_duration}s) -> full backward ({args.backward_duration}s)")

  # Allow joystickd to start and connect
  time.sleep(1.0)

  publish_phase(pm, rk, -1.0, args.forward_duration, "FULL FORWARD")
  publish_phase(pm, rk, 0.0, args.stop_duration, "STOP")
  publish_phase(pm, rk, 1.0, args.backward_duration, "FULL BACKWARD")

  # Send zero for a bit to cleanly stop
  publish_phase(pm, rk, 0.0, 0.5, "COOLDOWN")

  print("Done.")


if __name__ == '__main__':
  main()
