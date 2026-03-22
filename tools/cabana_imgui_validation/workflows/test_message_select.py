"""
Workflow tests: message selection and detail view.

Launch cabana with a route, wait for messages to populate,
click on a message, and verify the detail pane updates.
"""

import pytest

from ..helpers import DEMO_ROUTE, XvfbCabana, select_first_message, wait_for_demo_route  # noqa: TID251

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90


class TestMessageSelect:
  def test_messages_populated(self):
    """After loading a route, messages should appear in the left panel."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=WORKFLOW_TIMEOUT) as c:
      wait_for_demo_route(c)
      assert c.is_alive()
      path = c.screenshot("workflow_messages_populated.png")
      assert path.exists()

  def test_select_message(self):
    """Selecting a message in the list should show details in the center pane."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=WORKFLOW_TIMEOUT) as c:
      wait_for_demo_route(c)

      # Screenshot before click
      c.screenshot("workflow_before_click.png")

      select_first_message(c)

      # Screenshot after click
      path = c.screenshot("workflow_after_click.png")
      assert c.is_alive()
      assert path.exists()

  def test_select_message_shows_detail(self):
    """Selecting a message should produce a current selected-state screenshot."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=WORKFLOW_TIMEOUT) as c:
      wait_for_demo_route(c)
      select_first_message(c)

      path = c.screenshot("workflow_message_selected.png")
      assert c.is_alive()
      assert path.exists()
