"""Driving screen: minimal mode (no path or lane lines).

Never hides alerts or driver monitoring: those are drawn by AlertRenderer and DriverStateRenderer, which this doesn't touch.
Lead markers, speed and set speed stay.
"""
from openpilot.jay import config
from openpilot.jay.guard import guarded


@guarded(default=False)
def minimal_mode() -> bool:
  return bool(config.get("onroad_minimal"))
