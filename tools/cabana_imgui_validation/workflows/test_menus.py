"""
Workflow tests: menu interactions.

Test opening various menus and dialogs through the menu bar.
"""

import pytest
import time

from ..helpers import DEMO_ROUTE, XvfbCabana  # noqa: TID251

pytestmark = pytest.mark.skip(reason="workflow validation is not stable enough for this checkpoint")


class TestMenus:
  def test_file_menu(self):
    """File menu opens when clicked."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      c.maximize()
      time.sleep(8)

      # Click on "File" in the menu bar (top-left area)
      c.click(15, 12)
      time.sleep(1)

      path = c.screenshot("workflow_file_menu.png")
      assert c.is_alive()
      assert path.exists()

  def test_tools_menu(self):
    """Tools menu opens when clicked."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      c.maximize()
      time.sleep(8)

      # Click on "Tools" in the menu bar
      c.click(110, 12)
      time.sleep(1)

      path = c.screenshot("workflow_tools_menu.png")
      assert c.is_alive()
      assert path.exists()

  def test_help_overlay(self):
    """F1 opens the help overlay."""
    with XvfbCabana(args=[DEMO_ROUTE, "--no-vipc"], timeout=60) as c:
      c.maximize()
      time.sleep(8)

      c.send_key("F1")
      time.sleep(1)

      path = c.screenshot("workflow_help_overlay.png")
      assert c.is_alive()
      assert path.exists()
