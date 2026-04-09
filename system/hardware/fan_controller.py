#!/usr/bin/env python3
import numpy as np

from openpilot.common.pid import PIDController

# Thermal model parameters
R_TH = 3.3       # K/W - thermal resistance at 30%+ fan (84k onroad steady-state samples)
T_AMB_REF = 55.0  # C - design ambient for power feedforward (fleet p75 intake temp)


class FanController:
  def __init__(self, rate: int) -> None:
    self.last_ignition = False
    self.controller = PIDController(k_p=0, k_i=4e-3, rate=rate)

  def update(self, cur_temp: float, ignition: bool, power_draw_w: float = 0.0) -> int:
    self.controller.pos_limit = 100 if ignition else 30
    self.controller.neg_limit = 30 if ignition else 0

    if ignition != self.last_ignition:
      self.controller.reset()
    self.last_ignition = ignition

    ff_temp = np.interp(cur_temp, [60.0, 100.0], [0, 100])

    # Thermal model feedforward: predict equilibrium temperature from power,
    # map through the same curve. Responds to power changes instantly rather
    # than waiting for temperature to rise through the thermal mass (tau=27s).
    #   T_eq = T_amb + P * R_th   (lumped capacitance, fleet-fitted)
    ff_power = np.interp(T_AMB_REF + power_draw_w * R_TH, [60.0, 100.0], [0, 100]) if power_draw_w > 0 else 0

    return int(self.controller.update(
                 error=(cur_temp - 75),
                 feedforward=max(ff_temp, ff_power)
              ))
