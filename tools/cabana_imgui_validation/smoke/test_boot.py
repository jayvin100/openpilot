"""
Smoke tests: cabana boot and lifecycle.

These tests verify that cabana can launch, show a window, stay alive, and exit cleanly.
No route data is required — cabana shows the "Open stream" dialog.
"""

import time
from tools.cabana_imgui_validation.helpers import XvfbCabana


class TestBoot:
  def test_window_appears(self):
    """Cabana launches and a visible window appears."""
    with XvfbCabana(args=[], timeout=15) as c:
      assert c.wid is not None
      assert c.is_alive()

  def test_stays_alive(self):
    """Cabana stays alive for 5 seconds without crashing."""
    with XvfbCabana(args=[], timeout=15) as c:
      time.sleep(5)
      assert c.is_alive()

  def test_has_geometry(self):
    """The main cabana window has a reasonable size."""
    with XvfbCabana(args=[], timeout=15) as c:
      geo = c.get_window_geometry()
      assert geo is not None
      # When launched with no route, the "Open stream" dialog or main window should be visible
      assert geo.get("WIDTH", 0) >= 50
      assert geo.get("HEIGHT", 0) >= 50

  def test_stream_selector_visible(self):
    """The 'Open stream' dialog is visible when launched with no route."""
    with XvfbCabana(args=[], timeout=15) as c:
      windows = c.find_windows("Open stream")
      assert len(windows) > 0, "Expected 'Open stream' dialog"
      wid, name, geo = windows[0]
      assert "Open stream" in name
      assert geo["WIDTH"] > 100
      assert geo["HEIGHT"] > 100

  def test_screenshot_captures(self):
    """A screenshot can be taken of the full virtual screen."""
    with XvfbCabana(args=[], timeout=15) as c:
      path = c.screenshot("smoke_boot.png")
      assert path.exists()
      assert path.stat().st_size > 1000  # not an empty/trivial image

  def test_clean_exit(self):
    """Cabana exits when sent alt+F4."""
    with XvfbCabana(args=[], timeout=15) as c:
      exit_code = c.close(timeout=10)
      # The key check is that it doesn't hang forever
      assert exit_code is not None
