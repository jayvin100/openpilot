#!/usr/bin/env python3
import numpy as np

from openpilot.common.pid import PIDController

# Shared constants — imported by tools/thermal_fan_sim.py and tools/thermal_model_fit.py
T_SETPOINT = 75.0
T_REF_AMB = 35.0
TEMP_FF_BP = [60.0, 100.0]
TEMP_FF_FAN = [0, 100]
POWER_FF_BP = [2.0, 4.0, 6.0, 8.0]
POWER_FF_FAN = [0, 30, 60, 90]
PRESPIN_TEMP_BP = [40.0, 55.0, 70.0]
PRESPIN_FAN_BP = [30.0, 50.0, 80.0]


def simulate_thermal(T_init, T_amb, power_series, fan_series, params, dt=0.5):
  """Lumped-capacitance thermal model. Used by fitting and simulation tools.

  params: dict with keys tau_heat, tau_cool, R_passive, k_fan
  Returns: predicted temperature array
  """
  tau_heat = params['tau_heat']
  tau_cool = params['tau_cool']
  R_passive = params['R_passive']
  k_fan = params['k_fan']

  n = len(power_series)
  T = np.zeros(n)
  T[0] = T_init
  for i in range(1, n):
    R_total = R_passive / (1.0 + k_fan * fan_series[i - 1] / 100.0)
    T_eq = T_amb + power_series[i - 1] * R_total
    tau = tau_heat if T_eq > T[i - 1] else tau_cool
    T[i] = T[i - 1] + (dt / max(tau, 1.0)) * (T_eq - T[i - 1])
  return T


class FanController:
  def __init__(self, rate: int) -> None:
    self.last_ignition = False
    self.controller = PIDController(k_p=2.0, k_i=4e-3, rate=rate)
    self.power_bp = list(POWER_FF_BP)
    self.power_ff = list(POWER_FF_FAN)

  def update(self, cur_temp: float, ignition: bool, power_draw_w: float = 0.0, t_amb: float = 0.0) -> int:
    self.controller.pos_limit = 100 if ignition else 30
    self.controller.neg_limit = 30 if ignition else 0

    if ignition and not self.last_ignition:
      # Pre-spin: seed integrator so fan starts immediately on ignition.
      ff_at_temp = np.interp(cur_temp, TEMP_FF_BP, TEMP_FF_FAN)
      prespin = np.interp(cur_temp, PRESPIN_TEMP_BP, PRESPIN_FAN_BP)
      self.controller.i = max(0, prespin - ff_at_temp)

    self.last_ignition = ignition

    ff_temp = np.interp(cur_temp, TEMP_FF_BP, TEMP_FF_FAN)
    ff_power = np.interp(power_draw_w, self.power_bp, self.power_ff) if ignition else 0
    if t_amb > 0:
      ff_power *= (T_SETPOINT - T_REF_AMB) / max(T_SETPOINT - t_amb, 5.0)
    feedforward = max(ff_temp, ff_power)

    return int(self.controller.update(
                 error=(cur_temp - T_SETPOINT),
                 feedforward=feedforward
              ))
