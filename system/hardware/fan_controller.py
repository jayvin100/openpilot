#!/usr/bin/env python3
import numpy as np

from openpilot.common.pid import PIDController


class FanController:
  def __init__(self, rate: int) -> None:
    self.last_ignition = False
    self.controller = PIDController(k_p=2.0, k_i=4e-3, rate=rate)
    # Power feedforward breakpoints (to be refined from fleet thermal model fit)
    self.power_bp = [2.0, 4.0, 6.0, 8.0]
    self.power_ff = [0, 30, 60, 90]

  def update(self, cur_temp: float, ignition: bool, power_draw_w: float = 0.0, t_amb: float = 0.0) -> int:
    self.controller.pos_limit = 100 if ignition else 30
    self.controller.neg_limit = 30 if ignition else 0

    if ignition and not self.last_ignition:
      # Pre-spin: seed integrator so fan starts immediately on ignition.
      # Without this, the integrator resets to 0 and the fan takes 60-90s
      # to catch up to the thermal load from onroad processes starting.
      ff_at_temp = np.interp(cur_temp, [60.0, 100.0], [0, 100])
      prespin = np.interp(cur_temp, [40.0, 55.0, 70.0], [30.0, 50.0, 80.0])
      self.controller.i = max(0, prespin - ff_at_temp)
    # No reset on offroad transition — output limits (0-30%) handle clamping,
    # and the fan winds down naturally instead of stopping abruptly.

    self.last_ignition = ignition

    # Temperature-based feedforward (original)
    ff_temp = np.interp(cur_temp, [60.0, 100.0], [0, 100])
    # Power-based feedforward: power draw spikes 30-60s before temperature rises,
    # giving the fan a head start on cooling.
    ff_power = np.interp(power_draw_w, self.power_bp, self.power_ff) if ignition else 0
    # Ambient-aware scaling: breakpoints assume ~35C ambient. At hotter ambient,
    # less thermal headroom means we need more fan for the same power draw.
    if t_amb > 0:
      ff_power *= (75.0 - 35.0) / max(75.0 - t_amb, 5.0)
    feedforward = max(ff_temp, ff_power)

    return int(self.controller.update(
                 error=(cur_temp - 75),  # temperature setpoint in C
                 feedforward=feedforward
              ))
