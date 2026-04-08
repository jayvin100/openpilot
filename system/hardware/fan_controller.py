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

# Thermal model parameters, fitted from fleet data (SoM power basis):
#
# R_passive: 815k ES offroad STATUS_PACKETs at 0% fan → R = (maxTempC - intake) / somPowerW = 5.9 K/W
# k_fan:     ES comparison: 0% fan (R=5.9) vs 90-100% fan (R=3.3) → k_fan = R_nat/R_max - 1 = 0.78
# tau_heat:  29 heating ramps from 2101 rlogs at 8.7Hz, 63% method → median 16.5s
# tau_cool:  7 cooling ramps from rlogs, 63% method → median 85.0s (5x slower, thermal mass retains heat)
#
# Fan effect: huge from 0→30% (R drops 5.9→3.3), diminishing above 30% (3.3→3.0).
# Confirmed by 3.4k rlog steady-state points: R_th ≈ 3.3 K/W for fan 30-100%.
# The biggest win is preventing 0% fan offroad in hot ambient (the offroad fan floor fix).
MODEL_PARAMS = {
  'tau_heat': 16.5,    # s — heats fast
  'tau_cool': 85.0,    # s — cools 5x slower
  'R_passive': 5.9,    # K/W at 0% fan (offroad, from ES)
  'k_fan': 0.78,       # R_total = R_passive / (1 + k_fan * fan%/100)
}


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
    self.dt = 1.0 / rate
    self.R_passive = MODEL_PARAMS['R_passive']
    self.k_fan = MODEL_PARAMS['k_fan']
    self.tau_heat = MODEL_PARAMS['tau_heat']
    # Lookup table fallback when no ambient sensor available
    self.power_bp = list(POWER_FF_BP)
    self.power_ff = list(POWER_FF_FAN)

  def _model_feedforward(self, power_draw_w: float, t_amb: float) -> float:
    """Steady-state inversion: compute fan% to hold T_SETPOINT given power and ambient.

    From: T_setpoint = T_amb + P * R_passive / (1 + k_fan * fan/100)
    Solve: fan = 100/k_fan * (P * R_passive / (T_setpoint - T_amb) - 1)
    """
    delta_t = max(T_SETPOINT - t_amb, 5.0)
    fan = 100.0 / self.k_fan * (power_draw_w * self.R_passive / delta_t - 1.0)
    return max(0.0, fan)

  def _predict_overshoot(self, cur_temp: float, power_draw_w: float, t_amb: float, fan_pct: float) -> float:
    """Forward-simulate 10s to detect impending temperature overshoot."""
    T = cur_temp
    R_total = self.R_passive / (1.0 + self.k_fan * fan_pct / 100.0)
    T_eq = t_amb + power_draw_w * R_total
    for _ in range(int(10.0 / self.dt)):
      T += (self.dt / max(self.tau_heat, 1.0)) * (T_eq - T)
    return max(0.0, T - T_SETPOINT)

  def update(self, cur_temp: float, ignition: bool, power_draw_w: float = 0.0, t_amb: float = 0.0) -> int:
    self.controller.pos_limit = 100 if ignition else 30
    self.controller.neg_limit = 30 if ignition else 0

    if ignition and not self.last_ignition:
      ff_at_temp = np.interp(cur_temp, TEMP_FF_BP, TEMP_FF_FAN)
      prespin = np.interp(cur_temp, PRESPIN_TEMP_BP, PRESPIN_FAN_BP)
      self.controller.i = max(0, prespin - ff_at_temp)

    self.last_ignition = ignition

    # Temperature feedforward (safety floor — always active)
    ff_temp = np.interp(cur_temp, TEMP_FF_BP, TEMP_FF_FAN)

    if ignition and power_draw_w > 0:
      if t_amb > 0:
        # Model-based: compute exact fan% from physics
        ff_power = self._model_feedforward(power_draw_w, t_amb)
        # Predictive bump: if forward sim shows overshoot, add more fan now
        overshoot = self._predict_overshoot(cur_temp, power_draw_w, t_amb, ff_power)
        ff_power += overshoot * 2.0  # 2% per degree of predicted overshoot
      else:
        # No ambient sensor: fall back to lookup table
        ff_power = np.interp(power_draw_w, self.power_bp, self.power_ff)
    else:
      ff_power = 0

    feedforward = max(ff_temp, ff_power)

    return int(self.controller.update(
                 error=(cur_temp - T_SETPOINT),
                 feedforward=feedforward
              ))
