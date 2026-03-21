#!/usr/bin/env python3
"""
Pinball teleop daemon. Reads testJoystick messages from teleoprtc
and sends CAN commands to the RP2350.

CAN protocol (ID 0x100, 1 byte):
  bit 0: left flipper  (A key, axes[1] > 0)
  bit 1: right flipper  (D key, axes[1] < 0)
  bit 2: plunger/launch  (W key, axes[0] < 0)

Run on the comma four. Requires webrtcd + bodyteleop web server
(both start automatically in notcar mode).
"""
import time

import cereal.messaging as messaging
from panda import Panda
from opendbc.car.structs import CarParams

CAN_MSG_ID = 0x100
CAN_BUS = 0

def main():
  p = Panda()
  p.set_safety_mode(CarParams.SafetyModel.allOutput)

  sm = messaging.SubMaster(['testJoystick'])

  print("Pinball daemon running, waiting for teleop input...")
  last_bits = -1
  while True:
    sm.update(100)  # 100ms timeout

    if sm.updated['testJoystick']:
      axes = sm['testJoystick'].axes
      if len(axes) >= 2:
        bits = 0
        if axes[1] > 0:   # A key
          bits |= 0x01    # left flipper
        if axes[1] < 0:   # D key
          bits |= 0x02    # right flipper
        if axes[0] < 0:   # W key
          bits |= 0x04    # plunger

        p.can_send(CAN_MSG_ID, bytes([bits]), CAN_BUS)

        if bits != last_bits:
          print(f"bits=0b{bits:03b} (L={'X' if bits & 1 else '.'} R={'X' if bits & 2 else '.'} P={'X' if bits & 4 else '.'})")
          last_bits = bits

if __name__ == "__main__":
  main()
