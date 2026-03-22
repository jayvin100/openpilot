"""
Smoke tests: cabana with a loaded route.

These tests verify that cabana can load a route, populate messages,
and remain stable. Uses the demo route (requires cache or network).
"""

import time

import pytest

from ..helpers import DEMO_ROUTE, XvfbCabana  # noqa: TID251

pytestmark = pytest.mark.xdist_group("cabana_demo_route")


class TestDemoRoute:
  def test_demo_boots(self):
    """Cabana launches with a route and shows a window."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      assert c.wid is not None
      assert c.is_alive()

  def test_demo_stays_alive(self):
    """Cabana with a loaded route stays alive for 10 seconds."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      time.sleep(10)
      assert c.is_alive()

  def test_demo_screenshot(self):
    """Screenshot of cabana with route loaded and messages populated."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      # Give it time to load route data and populate the UI
      time.sleep(10)
      assert c.is_alive()
      path = c.screenshot_window("smoke_demo_loaded.png")
      assert path.exists()
      assert path.stat().st_size > 1000

  def test_demo_window_title(self):
    """Window title should contain 'Cabana'."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      name = c.get_window_name()
      assert "Cabana" in name, f"Window title: {name}"
