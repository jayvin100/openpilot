import pytest

from openpilot.system.hardware.fan_controller import FanController

ALL_CONTROLLERS = [FanController]

class TestFanController:
  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_hot_onroad(self, controller_class):
    controller = controller_class(2)
    assert controller.update(100, True) == 100

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_offroad_limits(self, controller_class):
    controller = controller_class(2)
    assert controller.update(100, False) <= 30

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_no_fan_wear(self, controller_class):
    controller = controller_class(2)
    assert controller.update(10, False) == 0

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_warm_onroad(self, controller_class):
    controller = controller_class(2)
    assert controller.update(80, True) >= 75

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_cool_onroad(self, controller_class):
    controller = controller_class(2)
    assert controller.update(60, True) == 30

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_deterministic(self, controller_class):
    """Same input always produces same output."""
    c1 = controller_class(2)
    c2 = controller_class(2)
    assert c1.update(85, True) == c2.update(85, True)
