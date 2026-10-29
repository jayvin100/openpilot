"""Settings for jay's customizations, and the trimming of the stock settings screen.

JaySettingsLayout is the "Jay" panel. Its values live in openpilot.jay.config, not Params.
arrange_panels() is called once from SettingsLayout.__init__: it adds the Jay panel, drops the panels jay doesn't use
and puts the rest in a useful order.
"""
from openpilot.jay import config
from openpilot.jay.guard import guarded
from openpilot.jay.road_mode import road_mode_button
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.widgets.list_view import toggle_item
from openpilot.system.ui.widgets.scroller_tici import Scroller


def config_toggle(key: str, title: str, description: str):
  return toggle_item(title=title, description=description, initial_state=bool(config.get(key)),
                     callback=lambda state: config.put(key, bool(state)))


class JaySettingsLayout(Widget):
  def __init__(self):
    super().__init__()
    items = [
      road_mode_button(),
      config_toggle("onroad_minimal", "Minimal Driving Screen",
                    "Hide the path and lane lines while driving. Speed, alerts and driver monitoring always stay on."),
      config_toggle("screensaver", "Clock Screensaver",
                    "When the car is off, show a clock for 5 minutes before the screen turns off. Tap to dismiss."),
      config_toggle("screensaver_dim_night", "Dim Screensaver at Night",
                    "Dim the clock screensaver between 9 pm and 7 am."),
      config_toggle("storage_auto_clean", "Auto-clean Storage",
                    "When free space drops below 15%, delete video comma has already uploaded, then video older than 7 days, "
                    "then video older than a day, until 18% is free. Drive logs are kept."),
    ]
    self._scroller = Scroller(items, line_separator=True, spacing=0)

  def _render(self, rect):
    self._scroller.render(rect)

  def show_event(self):
    self._scroller.show_event()


# Sidebar order, by PanelType name. Panels not listed here (new upstream ones) go at the end.
PANEL_ORDER = ("JAY", "TOGGLES", "NETWORK", "DEVICE", "SOFTWARE", "DEVELOPER")
# The home screen already shows Firehose uploads.
REMOVED_PANELS = ("FIREHOSE",)

# Removed panel widgets are kept alive here. Firehose's background thread is what refreshes ApiCache_FirehoseStats,
# the count on the home screen and screensaver, so it must keep running with its panel gone.
_removed: list = []


@guarded()
def arrange_panels(panels: dict) -> dict:
  """Returns the panels to show, in order. Called once with SettingsLayout's panel dict; None (on error) keeps stock."""
  from openpilot.selfdrive.ui.layouts.settings.settings import PanelInfo, PanelType  # imported lazily: settings imports this module
  from openpilot.jay.software_panel import JaySoftwareLayout

  panels = {PanelType.JAY: PanelInfo("Jay", JaySettingsLayout()), **panels}
  panels[PanelType.SOFTWARE] = PanelInfo("Software", JaySoftwareLayout())
  kept = {}
  for panel_type, info in panels.items():
    if panel_type.name in REMOVED_PANELS:
      _removed.append(info.instance)
    else:
      kept[panel_type] = info

  rank = {name: i for i, name in enumerate(PANEL_ORDER)}
  return dict(sorted(kept.items(), key=lambda kv: rank.get(kv[0].name, len(PANEL_ORDER))))
