#!/usr/bin/env python3
import numpy as np

from openpilot.common.pid import PIDController

# Thermal model parameters from fleet data
R_TH = 3.3  # K/W - thermal resistance at 30%+ fan (84k onroad steady-state samples)


class FanController:
  def __init__(self, rate: int) -> None:
    self.last_ignition = False
    self.controller = PIDController(k_p=2, k_i=4e-3, rate=rate)
    self.dt = 1.0 / rate
    self._power_filt = 0.0
    self._t_amb = 55.0  # default until first soaked ignition-on

  def update(self, cur_temp: float, ignition: bool, power_draw_w: float = 0.0, soaked: bool = False) -> int:
    self.controller.pos_limit = 100 if ignition else 30
    self.controller.neg_limit = 30 if ignition else 0

    if ignition and not self.last_ignition:
      if soaked:
        self._t_amb = cur_temp  # device cooled to ambient — core ≈ ambient
      self._power_filt = power_draw_w
      self.controller.reset()
    elif not ignition and self.last_ignition:
      self.controller.reset()
    self.last_ignition = ignition

    ff_temp = np.interp(cur_temp, [60.0, 100.0], [0, 100])

    # LP filter on power (tau=5s) to suppress governor cycling noise (±1W),
    # prevents audible fan oscillation while preserving step response
    if power_draw_w > 0:
      self._power_filt += (self.dt / (5.0 + self.dt)) * (power_draw_w - self._power_filt)
      ff_power = float(np.interp(self._t_amb + self._power_filt * R_TH, [60.0, 100.0], [0, 100]))
    else:
      ff_power = 0.0
      self._power_filt = 0.0

    return int(self.controller.update(
                 error=(cur_temp - 75),
                 feedforward=max(ff_temp, ff_power)
              ))
