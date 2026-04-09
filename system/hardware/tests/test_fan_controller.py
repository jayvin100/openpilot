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
