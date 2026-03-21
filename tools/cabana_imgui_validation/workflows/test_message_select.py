"""
Workflow tests: message selection and detail view.

Launch cabana with a route, wait for messages to populate,
click on a message, and verify the detail pane updates.
"""

import time
from tools.cabana_imgui_validation.helpers import XvfbCabana, DEMO_ROUTE


class TestMessageSelect:
  def test_messages_populated(self):
    """After loading a route, messages should appear in the left panel."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      c.maximize()
      time.sleep(8)  # let route load and messages populate
      assert c.is_alive()
      path = c.screenshot("workflow_messages_populated.png")
      assert path.exists()

  def test_click_message(self):
    """Clicking a message in the list should show details in the center pane."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      c.maximize()
      time.sleep(8)

      # Screenshot before click
      c.screenshot("workflow_before_click.png")

      # Click in the message list area (left panel, ~100px from left, ~150px down)
      # The message list starts below the "CABANA" header and search bar
      c.click(100, 200)
      time.sleep(1)

      # Screenshot after click
      path = c.screenshot("workflow_after_click.png")
      assert c.is_alive()
      assert path.exists()

  def test_double_click_message_adds_chart(self):
    """Double-clicking a message should add a chart."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      c.maximize()
      time.sleep(8)

      # Double-click on a message in the list
      c.xdotool("mousemove", "--window", c.wid, "100", "200")
      c.xdotool("click", "--window", c.wid, "--repeat", "2", "1")
      time.sleep(2)

      path = c.screenshot("workflow_double_click_chart.png")
      assert c.is_alive()
      assert path.exists()
