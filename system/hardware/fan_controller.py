#!/usr/bin/env python3
import numpy as np

# Lumped-parameter thermal model: T_eq = T_ambient + P * R_th
# R_th = 3.5 °C/W - flat above 30% fan, fitted from fleet data
# T_LMH = 90 °C - LMH-DCVS trips at 95°C, 5°C safety margin
# Fan is mapped from thermal headroom: how far current temp is from the LMH limit.
R_TH = 3.5   # °C/W
T_LMH = 90.0 # 5°C safety margin from 95°C LMH-DCVS trip point
HEADROOM_BP = [0, 5, 12, 18, 25]
FAN_BP      = [100, 90, 75, 30, 0]


class FanController:
  def __init__(self, rate: int) -> None:
    pass

  def update(self, cur_temp: float, ignition: bool) -> int:
    if not ignition:
      return int(np.clip(np.interp(T_LMH - cur_temp, HEADROOM_BP, FAN_BP), 0, 30))
    return int(np.clip(np.interp(T_LMH - cur_temp, HEADROOM_BP, FAN_BP), 30, 100))
