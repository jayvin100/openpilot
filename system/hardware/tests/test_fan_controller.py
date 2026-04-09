import pytest

from openpilot.system.hardware.fan_controller import FanController

ALL_CONTROLLERS = [FanController]

class TestFanController:
  def wind_up(self, controller, ignition=True):
    for _ in range(1000):
      controller.update(100, ignition)

  def wind_down(self, controller, ignition=False):
    for _ in range(1000):
      controller.update(10, ignition)

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_hot_onroad(self, controller_class):
    controller = controller_class(2)
    self.wind_up(controller)
    assert controller.update(100, True) >= 70

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_offroad_limits(self, controller_class):
    controller = controller_class(2)
    self.wind_up(controller)
    assert controller.update(100, False) <= 30

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_no_fan_wear(self, controller_class):
    controller = controller_class(2)
    self.wind_down(controller)
    assert controller.update(10, False) == 0

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_limited(self, controller_class):
    controller = controller_class(2)
    self.wind_up(controller, True)
    assert controller.update(100, True) == 100

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_windup_speed(self, controller_class):
    controller = controller_class(2)
    self.wind_down(controller, True)
    for _ in range(10):
      controller.update(90, True)
    assert controller.update(90, True) >= 60

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_power_feedforward(self, controller_class):
    controller = controller_class(2)
    # high power at moderate temp should produce meaningful fan speed
    for _ in range(10):
      controller.update(65, True, power_draw_w=8.0)
    assert controller.update(65, True, power_draw_w=8.0) >= 60

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_no_power_no_change(self, controller_class):
    controller = controller_class(2)
    self.wind_down(controller)
    assert controller.update(10, False) == 0

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_fast_ramp_high_temp(self, controller_class):
    controller = controller_class(2)
    self.wind_down(controller, True)
    # 85°C should produce immediate proportional response, not wait for integrator
    fan = controller.update(85, True)
    assert fan >= 50

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_power_noise_filtered(self, controller_class):
    controller = controller_class(2)
    # feed noisy power at steady temp — fan should not swing wildly
    fans = []
    for i in range(60):
      noisy_power = 6.0 + 1.5 * (1 if i % 2 else -1)
      fans.append(controller.update(72, True, power_draw_w=noisy_power))
    # last 20 samples should vary less than 5% peak-to-peak
    assert max(fans[-20:]) - min(fans[-20:]) < 5
