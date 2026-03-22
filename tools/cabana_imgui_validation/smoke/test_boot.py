"""
Smoke tests: cabana_imgui boot and lifecycle.

These tests verify that cabana_imgui can launch, show a window, stay alive,
and exit cleanly without requiring any route data.
"""

import time

from ..helpers import XvfbCabana  # noqa: TID251


class TestBoot:
  def test_window_appears(self):
    """Cabana_imgui launches and a visible window appears."""
    with XvfbCabana(args=[], timeout=15) as c:
      assert c.wid is not None
      assert c.is_alive()

  def test_stays_alive(self):
    """Cabana_imgui stays alive for 5 seconds without crashing."""
    with XvfbCabana(args=[], timeout=15) as c:
      time.sleep(5)
      assert c.is_alive()

  def test_has_geometry(self):
    """The main cabana_imgui window has a reasonable size."""
    with XvfbCabana(args=[], timeout=15) as c:
      geo = c.get_window_geometry()
      assert geo is not None
      assert geo.get("WIDTH", 0) >= 50
      assert geo.get("HEIGHT", 0) >= 50

  def test_window_title_contains_cabana(self):
    """The app window title contains Cabana."""
    with XvfbCabana(args=[], timeout=15) as c:
      name = c.get_window_name()
      assert "Cabana" in name

  def test_screenshot_captures(self):
    """A screenshot can be taken of the main cabana_imgui window."""
    with XvfbCabana(args=[], timeout=15) as c:
      path = c.screenshot_window("smoke_boot.png")
      assert path.exists()
      assert path.stat().st_size > 1000  # not an empty/trivial image

  def test_clean_exit(self):
    """Cabana_imgui exits when sent alt+F4."""
    with XvfbCabana(args=[], timeout=15) as c:
      exit_code = c.close(timeout=10)
      # The key check is that it doesn't hang forever
      assert exit_code is not None
