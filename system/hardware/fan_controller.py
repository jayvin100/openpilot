#!/usr/bin/env python3
import numpy as np

# Fan curve fitted from 98k onroad samples across 824 seg-0 fleet transitions.
# LMH-DCVS trips at 95°C with 30°C hysteresis (release at 65°C), so fan must
# hit 100% before 90°C to avoid the throttle cliff.
TEMP_BP = [65.0, 72.0, 78.0, 85.0, 90.0]
FAN_BP  = [  0.0, 30.0, 75.0, 90.0, 100.0]


class FanController:
  def __init__(self, rate: int) -> None:
    pass

  def update(self, cur_temp: float, ignition: bool) -> int:
    if not ignition:
      return int(np.clip(np.interp(cur_temp, TEMP_BP, FAN_BP), 0, 30))
    return int(np.clip(np.interp(cur_temp, TEMP_BP, FAN_BP), 30, 100))
