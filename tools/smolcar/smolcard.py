#!/usr/bin/env python3
"""
smolcard — drive a Hiwonder Ackermann chassis directly from the comma4.

Subscribes to openpilot's testJoystick messages (same as joystickd) and
translates them into motor/steering commands over USB serial to the
Hiwonder STM32 controller board.

Usage:
  # enable joystick mode, then run:
  python3 tools/smolcar/smolcard.py

  # send joystick input from another terminal:
  python3 tools/joystick/joystick_control.py
"""
import time

from cereal import messaging
from openpilot.common.realtime import DT_CTRL, Ratekeeper
from openpilot.common.params import Params

from openpilot.tools.smolcar.board import Board

# Ackermann config
MAX_SPEED = 8.0          # max motor speed (pulse count per 10ms)
SERVO_CENTER = 1500      # steering servo center pulse
SERVO_RANGE = 444        # max deflection from center (~40 degrees)
STEERING_SERVO_ID = 3    # PWM servo channel for steering


def main():
  params = Params()
  params.put_bool("JoystickDebugMode", True)

  board = Board()
  board.set_buzzer(1900, 0.1, 0.05, 2)  # beep to confirm connection
  print("smolcard: board connected, waiting for joystick input...")

  sm = messaging.SubMaster(['testJoystick'], frequency=1. / DT_CTRL)
  rk = Ratekeeper(100, print_delay_threshold=None)

  try:
    while True:
      sm.update(0)

      # testJoystick axes: [0] = throttle/brake (-1 to 1), [1] = steering (-1 to 1)
      stale = sm.recv_frame['testJoystick'] == 0 or \
              (sm.frame - sm.recv_frame['testJoystick']) * DT_CTRL > 0.2

      if stale:
        throttle = 0.0
        steer = 0.0
      else:
        axes = sm['testJoystick'].axes
        throttle = max(-1.0, min(1.0, axes[0]))
        steer = max(-1.0, min(1.0, axes[1]))

      # motors: positive throttle = forward
      # motors 1,2 on one side, 3,4 on the other (reversed)
      speed = throttle * MAX_SPEED
      board.set_motor_speed([(1, speed), (2, speed), (3, -speed), (4, -speed)])

      # steering: negative steer = right, positive = left
      pulse = SERVO_CENTER + int(steer * SERVO_RANGE)
      board.set_steering(pulse, servo_id=STEERING_SERVO_ID)

      rk.keep_time()

  except KeyboardInterrupt:
    pass
  finally:
    print("smolcard: stopping")
    board.close()


if __name__ == "__main__":
  main()
