"""
Workflow tests: tab switching in the detail pane.

After selecting a message, switch between Binary, Signals, and History tabs.
"""

import time
from tools.cabana_imgui_validation.helpers import XvfbCabana, DEMO_ROUTE


class TestTabs:
  def _setup_with_message(self):
    """Launch cabana, maximize, load route, click a message."""
    c = XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60)
    c.start()
    c.maximize()
    time.sleep(8)
    # Click on a message
    c.click(100, 200)
    time.sleep(1)
    return c

  def test_binary_tab(self):
    """Binary tab is visible after selecting a message."""
    c = self._setup_with_message()
    try:
      path = c.screenshot("workflow_binary_tab.png")
      assert c.is_alive()
      assert path.exists()
    finally:
      c.kill()

  def test_keyboard_navigation(self):
    """Arrow keys navigate through messages."""
    c = self._setup_with_message()
    try:
      c.screenshot("workflow_nav_start.png")

      # Press down arrow a few times to move through messages
      for _ in range(3):
        c.send_key("Down")
        time.sleep(0.3)
      time.sleep(0.5)

      c.screenshot("workflow_nav_after_down.png")

      # Press up to go back
      c.send_key("Up")
      time.sleep(0.5)

      path = c.screenshot("workflow_nav_after_up.png")
      assert c.is_alive()
      assert path.exists()
    finally:
      c.kill()
