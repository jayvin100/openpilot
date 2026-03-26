import pytest

from openpilot.system.hardware.fan_controller import FanController, THERMAL_MODELS

ALL_CONTROLLERS = [FanController]

def patched_controller(mocker, controller_class, thermal_model=None):
  mocker.patch("os.system", new=mocker.Mock())
  return controller_class(2, thermal_model=thermal_model)

class TestFanController:
  def wind_up(self, controller, ignition=True):
    for _ in range(1000):
      controller.update(100, ignition)

  def wind_down(self, controller, ignition=False):
    for _ in range(1000):
      controller.update(10, ignition)

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_hot_onroad(self, mocker, controller_class):
    controller = patched_controller(mocker, controller_class)
    self.wind_up(controller)
    assert controller.update(100, True) >= 70

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_offroad_limits(self, mocker, controller_class):
    controller = patched_controller(mocker, controller_class)
    self.wind_up(controller)
    assert controller.update(100, False) <= 30

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_no_fan_wear(self, mocker, controller_class):
    controller = patched_controller(mocker, controller_class)
    self.wind_down(controller)
    assert controller.update(10, False) == 0

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_limited(self, mocker, controller_class):
    controller = patched_controller(mocker, controller_class)
    self.wind_up(controller, True)
    assert controller.update(100, True) == 100

  @pytest.mark.parametrize("controller_class", ALL_CONTROLLERS)
  def test_windup_speed(self, mocker, controller_class):
    controller = patched_controller(mocker, controller_class)
    self.wind_down(controller, True)
    for _ in range(10):
      controller.update(90, True)
    assert controller.update(90, True) >= 60

  # Model-based feedforward tests (comma four / mici)
  def test_model_high_power_high_fan(self, mocker):
    """High power draw at warm intake should demand high fan speed."""
    controller = patched_controller(mocker, FanController, thermal_model=THERMAL_MODELS["mici"])
    # 7W at 50°C intake: r_required = (75-50)/7 = 3.57, between 70% (3.8) and 100% (1.8)
    for _ in range(100):
      fan = controller.update(80, True, power_draw=7.0, intake_temp=50.0)
    assert fan >= 70

  def test_model_low_power_low_fan(self, mocker):
    """Low power draw at cool intake shouldn't need much fan."""
    controller = patched_controller(mocker, FanController, thermal_model=THERMAL_MODELS["mici"])
    # 3W at 30°C intake: r_required = (75-30)/3 = 15, above max R (12.0) -> fan ~0%
    for _ in range(100):
      fan = controller.update(60, True, power_draw=3.0, intake_temp=30.0)
    assert fan <= 40

  def test_model_hot_intake_max_fan(self, mocker):
    """When intake is already above target, feedforward should push to max."""
    controller = patched_controller(mocker, FanController, thermal_model=THERMAL_MODELS["mici"])
    # intake above target: r_required < 0 -> maps to 100%
    for _ in range(100):
      fan = controller.update(90, True, power_draw=5.0, intake_temp=80.0)
    assert fan == 100

  def test_model_fallback_no_intake(self, mocker):
    """Without intake data, should fall back to temperature-only feedforward."""
    controller = patched_controller(mocker, FanController, thermal_model=THERMAL_MODELS["mici"])
    # intake_temp=0 triggers fallback even with thermal model
    for _ in range(100):
      fan_model = controller.update(80, True, power_draw=5.0, intake_temp=0.)
    controller2 = patched_controller(mocker, FanController)
    for _ in range(100):
      fan_legacy = controller2.update(80, True)
    assert fan_model == fan_legacy

  def test_no_model_uses_legacy(self, mocker):
    """Without a thermal model (e.g. tizi), uses legacy temperature-only feedforward."""
    controller = patched_controller(mocker, FanController, thermal_model=None)
    for _ in range(100):
      fan = controller.update(80, True, power_draw=5.0, intake_temp=40.0)
    controller2 = patched_controller(mocker, FanController, thermal_model=None)
    for _ in range(100):
      fan_legacy = controller2.update(80, True)
    assert fan == fan_legacy
