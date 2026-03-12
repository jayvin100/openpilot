#!/usr/bin/env python3
"""
smolcard — replaces card.py and joystickd for the Hiwonder Ackermann chassis.

Integrates with openpilot's body teleop system:
  - Writes CarParams with notCar=True so webrtcd/bodyteleop processes start
  - Subscribes to testJoystick (from browser via WebRTC)
  - Publishes carState (battery/telemetry back to browser)
  - Drives motors/steering over USB serial to the Hiwonder STM32 board

Enable with: echo -n "1" > /data/params/d/SmolcarEnabled
"""
import time

from cereal import car, messaging
from openpilot.common.params import Params
from openpilot.common.realtime import DT_CTRL, Ratekeeper
from openpilot.common.swaglog import cloudlog

from openpilot.tools.smolcar.board import Board

# Ackermann config
MAX_SPEED = 8.0          # max motor speed (pulse count per 10ms)
SERVO_CENTER = 1500      # steering servo center pulse
SERVO_RANGE = 444        # max deflection from center (~40 degrees)
STEERING_SERVO_ID = 3    # PWM servo channel for steering


def write_car_params(params: Params) -> None:
  """Write CarParams so the manager starts notCar processes (webrtcd, bodyteleop, bridge)."""
  CP = car.CarParams.new_message()
  CP.notCar = True
  CP.carFingerprint = "HIWONDER_ACKERMANN"
  CP.openpilotLongitudinalControl = True
  CP.passive = False

  cp_bytes = CP.to_bytes()
  params.put("CarParams", cp_bytes)
  params.put_nonblocking("CarParamsCache", cp_bytes)
  params.put_nonblocking("CarParamsPersistent", cp_bytes)


def publish_car_state(pm: messaging.PubMaster, frame: int, battery: float = 0.0) -> None:
  """Publish carState so webrtcd can send telemetry back to the browser."""
  cs_msg = messaging.new_message('carState')
  cs_msg.valid = True
  CS = cs_msg.carState
  CS.fuelGauge = battery
  CS.vEgo = 0.0
  CS.canValid = True
  pm.send('carState', cs_msg)

  # also publish controlsState so joystickd doesn't complain if it's running
  ctrl_msg = messaging.new_message('controlsState')
  ctrl_msg.valid = True
  pm.send('controlsState', ctrl_msg)

  # carParams every 50s
  if frame % int(50. / DT_CTRL) == 0:
    cp_msg = messaging.new_message('carParams')
    cp_msg.valid = True
    pm.send('carParams', cp_msg)


def main():
  params = Params()

  # set up params so the bodyteleop stack starts
  params.put_bool("JoystickDebugMode", True)
  write_car_params(params)
  cloudlog.info("smolcard: CarParams written (notCar=True)")

  board = Board()
  board.set_buzzer(1900, 0.1, 0.05, 2)
  cloudlog.info("smolcard: board connected on %s", board.port.port)

  sm = messaging.SubMaster(['testJoystick'], frequency=1. / DT_CTRL)
  pm = messaging.PubMaster(['carState', 'controlsState', 'carParams', 'carOutput'])
  rk = Ratekeeper(100, print_delay_threshold=None)

  SERIAL_RATE = 5  # send serial commands every N loops (100Hz / 5 = 20Hz)
  # Reconnect every 10s to prevent silent USB CDC stalls.
  # The kernel CDC ACM driver buffers writes and returns success even when
  # the STM32 stops consuming data, so writes never throw — we must
  # proactively cycle the connection.
  RECONNECT_EVERY = 1000  # loops (~10s at 100Hz)

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

      # Proactive reconnect: stop motors first, then cycle the serial port
      if sm.frame > 0 and sm.frame % RECONNECT_EVERY == 0:
        try:
          board.stop()  # zero motors + center steering before disconnect
          board.reconnect()
          cloudlog.info("smolcard: proactive reconnect to %s", board.port.port)
        except Exception as e:
          cloudlog.error("smolcard: proactive reconnect failed: %s", e)

      # throttle serial commands to 20Hz to avoid overwhelming the STM32
      if sm.frame % SERIAL_RATE == 0:
        # motors: positive throttle = forward
        # motors 1,2 on one side, 3,4 on the other (reversed)
        speed = -throttle * MAX_SPEED
        try:
          board.set_motor_speed([(1, speed), (2, speed), (3, -speed), (4, -speed)])
        except Exception as e:
          cloudlog.error("smolcard: serial write failed, reconnecting: %s", e)
          try:
            board.reconnect()
            cloudlog.info("smolcard: reconnected to %s", board.port.port)
          except Exception as e2:
            cloudlog.error("smolcard: reconnect failed: %s", e2)

        # steering: negative steer = right, positive = left
        pulse = SERVO_CENTER + int(steer * SERVO_RANGE)
        try:
          board.set_steering(pulse, servo_id=STEERING_SERVO_ID)
        except Exception:
          pass  # already reconnecting on motor write failure

      # publish carState for webrtc telemetry
      publish_car_state(pm, sm.frame)

      # publish carOutput
      co_msg = messaging.new_message('carOutput')
      co_msg.valid = True
      pm.send('carOutput', co_msg)

      rk.keep_time()

  except KeyboardInterrupt:
    pass
  finally:
    cloudlog.info("smolcard: stopping")
    board.close()


if __name__ == "__main__":
  main()
