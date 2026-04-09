import pytest

from openpilot.system.hardware.fan_controller import FanController

ALL_CONTROLLERS = [FanController]

def make_controller(controller_class):
  return controller_class(2)

class TestFanController:
  def wind_up(self, controller, ignition=True):
    for _ in range(1000):
      controller.update(100, ignition)

  def wind_down(self, controller, ignition=False):
    for _ in range(1000):
      controller.update(10, ignition)

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_hot_onroad(self, controller_class):
    controller = make_controller(controller_class)
    self.wind_up(controller)
    assert controller.update(100, True) >= 70

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_offroad_limits(self, controller_class):
    controller = make_controller(controller_class)
    self.wind_up(controller)
    assert controller.update(100, False) <= 30

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_no_fan_wear(self, controller_class):
    controller = make_controller(controller_class)
    self.wind_down(controller)
    assert controller.update(10, False) == 0

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_limited(self, controller_class):
    controller = make_controller(controller_class)
    self.wind_up(controller, True)
    assert controller.update(100, True) == 100

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_windup_speed(self, controller_class):
    controller = make_controller(controller_class)
    self.wind_down(controller, True)
    for _ in range(200):  # enough for 30s LP filter to settle
      controller.update(90, True)
    assert controller.update(90, True) >= 60

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_prespin_on_ignition(self, controller_class):
    """Fan should start immediately when going onroad, not from 0%."""
    controller = make_controller(controller_class)
    for _ in range(100):
      controller.update(50, False)
    fan_pct = controller.update(50, True)
    assert fan_pct >= 30

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_power_feedforward(self, controller_class):
    """High power draw should drive fan even at moderate temperature."""
    controller = make_controller(controller_class)
    for _ in range(5):
      controller.update(65, True, power_draw_w=8.0)
    fan_pct = controller.update(65, True, power_draw_w=8.0)
    assert fan_pct >= 60

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_no_hard_reset_offroad(self, controller_class):
    """Going offroad at high temp shouldn't drop fan to 0% instantly."""
    controller = make_controller(controller_class)
    for _ in range(100):
      controller.update(80, True)
    fan_pct = controller.update(80, False)
    assert fan_pct > 0

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_backward_compatible(self, controller_class):
    """update() should work without power_draw_w argument."""
    controller = make_controller(controller_class)
    fan_pct = controller.update(70, True)
    assert 0 <= fan_pct <= 100

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_ambient_hot(self, controller_class):
    """Hot ambient should produce more fan than moderate ambient at same power."""
    ctrl_mod = make_controller(controller_class)
    ctrl_hot = make_controller(controller_class)
    for _ in range(100):  # enough time for slew rate limit to settle
      fan_mod = ctrl_mod.update(70, True, power_draw_w=5.0, t_amb=45.0)
      fan_hot = ctrl_hot.update(70, True, power_draw_w=5.0, t_amb=55.0)
    assert fan_hot > fan_mod

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_ambient_cool(self, controller_class):
    """Cool ambient should produce less fan than hot ambient at same power."""
    ctrl_hot = make_controller(controller_class)
    ctrl_cool = make_controller(controller_class)
    for _ in range(100):  # enough time for slew rate limit to settle
      fan_hot = ctrl_hot.update(70, True, power_draw_w=5.0, t_amb=55.0)
      fan_cool = ctrl_cool.update(70, True, power_draw_w=5.0, t_amb=35.0)
    assert fan_cool < fan_hot

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_ambient_missing(self, controller_class):
    """t_amb=0 should behave identically to not passing t_amb."""
    ctrl_default = make_controller(controller_class)
    ctrl_zero = make_controller(controller_class)
    for _ in range(10):
      fan_default = ctrl_default.update(65, True, power_draw_w=6.0)
      fan_zero = ctrl_zero.update(65, True, power_draw_w=6.0, t_amb=0.0)
    assert fan_default == fan_zero
