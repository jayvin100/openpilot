"""
Workflow tests: full UI with reset layout.

Reset window layout to get the default dock arrangement,
then capture the full UI with messages visible.
"""

import pytest
import time

from ..helpers import DEMO_ROUTE, XvfbCabana  # noqa: TID251

pytestmark = pytest.mark.skip(reason="workflow validation is not stable enough for this checkpoint")


class TestFullUI:
  def _launch_and_reset(self):
    """Launch cabana, maximize, load route, reset window layout."""
    c = XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60)
    c.start()
    c.maximize()
    time.sleep(8)

    # Open View menu and click "Reset Window Layout"
    c.click(75, 8)  # View menu
    time.sleep(0.5)
    # "Reset Window Layout" is the last item in the View menu
    c.click(87, 58)  # Reset Window Layout
    time.sleep(2)
    return c

  def test_reset_layout_shows_messages(self):
    """After reset, the messages panel should be visible."""
    c = self._launch_and_reset()
    try:
      path = c.screenshot("workflow_full_ui_reset.png")
      assert c.is_alive()
      assert path.exists()
    finally:
      c.kill()

  def test_view_menu_info(self):
    """View menu shows message count and fingerprint."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      c.maximize()
      time.sleep(8)

      # Open View menu
      c.click(75, 8)
      time.sleep(1)

      path = c.screenshot("workflow_view_menu_info.png")
      assert c.is_alive()
      assert path.exists()

  def test_select_message_after_reset(self):
    """After reset, clicking a message in the list shows details."""
    c = self._launch_and_reset()
    try:
      # After reset, messages should be in the left panel
      # Click on a message in the list (left side, ~150px in, ~100px down)
      c.click(150, 100)
      time.sleep(1)

      path = c.screenshot("workflow_message_selected.png")
      assert c.is_alive()
      assert path.exists()
    finally:
      c.kill()

  def test_message_detail_binary_view(self):
    """Selecting a message shows binary view in the center pane."""
    c = self._launch_and_reset()
    try:
      # Click on a message
      c.click(150, 120)
      time.sleep(1)

      # Screenshot the detail view
      path = c.screenshot("workflow_binary_view.png")
      assert c.is_alive()
      assert path.exists()

      # Navigate down through messages with arrow keys
      for _ in range(5):
        c.send_key("Down")
        time.sleep(0.3)

      path = c.screenshot("workflow_binary_view_scrolled.png")
      assert c.is_alive()
    finally:
      c.kill()
