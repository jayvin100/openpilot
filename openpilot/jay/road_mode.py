"""Temporary offroad override. Never bypass normal onroad startup conditions."""
from pathlib import Path

from opendbc.car.structs import car

REQUEST = Path("/tmp/jay_offroad_override")


def can_enter(sm) -> bool:
  return (sm.all_checks(["carState", "selfdriveState"]) and sm["carState"].standstill
          and sm["carState"].gearShifter == car.CarState.GearShifter.park
          and not sm["selfdriveState"].enabled)


class OffroadOverride:
  def __init__(self):
    self.active = False
    REQUEST.unlink(missing_ok=True)

  def allow_onroad(self, sm, ignition: bool) -> bool:
    requested = REQUEST.exists()
    if not ignition or (requested and not self.active and not can_enter(sm)):
      REQUEST.unlink(missing_ok=True)
      requested = False
    self.active = requested
    return not self.active


def road_mode_button():
  from openpilot.selfdrive.ui.ui_state import ui_state
  from openpilot.system.ui.lib.application import gui_app
  from openpilot.system.ui.widgets import DialogResult
  from openpilot.system.ui.widgets.confirm_dialog import ConfirmDialog
  from openpilot.system.ui.widgets.list_view import button_item

  def clicked():
    if REQUEST.exists():
      REQUEST.unlink(missing_ok=True)
    elif ui_state.is_onroad() and can_enter(ui_state.sm):
      def confirmed(result):
        if result == DialogResult.CONFIRM and ui_state.is_onroad() and can_enter(ui_state.sm):
          REQUEST.touch()
      gui_app.push_widget(ConfirmDialog("Enter offroad mode? Driving assistance and drive recording will stop. "
                                      "Stay parked until you select GO ONROAD.", "Go offroad", callback=confirmed))

  item = button_item("Road Mode", lambda: "GO ONROAD" if REQUEST.exists() else "GO OFFROAD", callback=clicked,
                     description="Temporary offroad mode while the ignition is on. Requires Park and openpilot disengaged.")
  item.action_item.set_enabled(lambda: REQUEST.exists() or (ui_state.is_onroad() and can_enter(ui_state.sm)))
  return item
