"""
Workflow tests: route/session restore.

Validate recent-route restore plus persisted selection/detail state using only
normal config files and black-box relaunch behavior.
"""

import json
import tempfile
from pathlib import Path

import pytest

from ..helpers import DEMO_ROUTE, XvfbCabana, quit_via_menu, reset_layout, select_first_message, wait_for_demo_route  # noqa: TID251

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90


class TestSessionRestore:
  @staticmethod
  def _settings_path(config_home):
    return Path(config_home) / "cabana_imgui" / "settings.json"

  def _load_state(self, config_home):
    return json.loads(self._settings_path(config_home).read_text())

  def test_route_selection_and_detail_restore(self):
    with tempfile.TemporaryDirectory(prefix="cabana_session_restore_") as config_home:
      env_extra = {"XDG_CONFIG_HOME": config_home}
      settings_path = self._settings_path(config_home)

      cabana = XvfbCabana(
        args=[DEMO_ROUTE, "--no-vipc"],
        timeout=WORKFLOW_TIMEOUT,
        env_extra=env_extra,
        clean_config=False,
      )
      cabana.start()
      try:
        wait_for_demo_route(cabana)
        reset_layout(cabana)
        select_first_message(cabana)

        saved_before_exit = self._load_state(config_home)
        assert saved_before_exit["recent_routes"][0] == DEMO_ROUTE
        assert saved_before_exit["selected_message"]

        quit_via_menu(cabana)
      finally:
        if cabana.is_alive():
          cabana.kill()

      saved = json.loads(settings_path.read_text())
      saved["current_detail_tab"] = "history"
      settings_path.write_text(json.dumps(saved) + "\n")

      restored = XvfbCabana(
        args=["--no-vipc"],
        timeout=WORKFLOW_TIMEOUT,
        env_extra=env_extra,
        clean_config=False,
      )
      restored.start()
      try:
        wait_for_demo_route(restored)
        restored_state = self._load_state(config_home)
        assert restored_state["recent_routes"][0] == DEMO_ROUTE
        assert restored_state["selected_message"] == saved_before_exit["selected_message"]
        assert restored_state["current_detail_tab"] == "history"
        assert DEMO_ROUTE in restored.get_window_name()

        path = restored.screenshot("workflow_session_restored.png")
        assert restored.is_alive()
        assert path.exists()
      finally:
        restored.kill()
