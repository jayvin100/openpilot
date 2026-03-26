#!/usr/bin/env python3
from dataclasses import dataclass

import numpy as np

from openpilot.common.pid import PIDController

TARGET_TEMP = 75.0  # °C setpoint


@dataclass
class ThermalModel:
  fan_breakpoints: list[float]  # fan speed %
  r_thermal: list[float]        # °C/W at each fan breakpoint, decreasing with fan speed


# Cooling curves measured per device
THERMAL_MODELS: dict[str, ThermalModel] = {
  "mici": ThermalModel(
    fan_breakpoints=[0, 30, 50, 70, 100],
    r_thermal=[12.0, 8.2, 5.6, 3.8, 1.8],
  ),
  # "tizi": ThermalModel(...),  # TODO: measure
}


class FanController:
  def __init__(self, rate: int, thermal_model: ThermalModel | None = None) -> None:
    self.last_ignition = False
    self.controller = PIDController(k_p=0, k_i=4e-3, rate=rate)
    self.thermal_model = thermal_model

  def update(self, cur_temp: float, ignition: bool, power_draw: float = 0., intake_temp: float = 0.) -> int:
    self.controller.pos_limit = 100 if ignition else 30
    self.controller.neg_limit = 30 if ignition else 0

    if ignition != self.last_ignition:
      self.controller.reset()
    self.last_ignition = ignition

    # Model-based feedforward when thermal model and sensor data are available
    if self.thermal_model is not None and power_draw > 0 and intake_temp > 0:
      r_required = (TARGET_TEMP - intake_temp) / power_draw
      feedforward = np.interp(r_required, self.thermal_model.r_thermal[::-1], self.thermal_model.fan_breakpoints[::-1])
    else:
      feedforward = np.interp(cur_temp, [60.0, 100.0], [0, 100])

    return int(self.controller.update(
      error=(cur_temp - TARGET_TEMP),
      feedforward=feedforward,
    ))
