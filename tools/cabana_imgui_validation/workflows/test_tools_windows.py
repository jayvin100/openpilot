"""
Workflow tests: tool windows.

Drive the real Tools menu and verify the new dialogs open as black-box UI.
"""

import pytest

from ..helpers import (  # noqa: TID251
  DEMO_ROUTE,
  XvfbCabana,
  open_find_similar_bits_window,
  open_route_info_window,
  wait_for_demo_route,
)

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90


class TestToolWindows:
  def test_find_similar_bits_window_opens(self):
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=WORKFLOW_TIMEOUT) as cabana:
      wait_for_demo_route(cabana)
      open_find_similar_bits_window(cabana)

      path = cabana.screenshot("workflow_find_similar_bits_window.png")
      assert cabana.is_alive()
      assert path.exists()

  def test_route_info_window_opens(self):
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=WORKFLOW_TIMEOUT) as cabana:
      wait_for_demo_route(cabana)
      open_route_info_window(cabana)

      path = cabana.screenshot("workflow_route_info_window.png")
      assert cabana.is_alive()
      assert path.exists()
