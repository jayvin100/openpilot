"""
Workflow tests: per-bus DBC management.

These drive the Manage DBC Files menu and validate that bus-specific DBC
assignments live entirely in normal app state on disk.
"""

import json
import tempfile
import time
from pathlib import Path

import pytest

from ..helpers import (  # noqa: TID251
  DEMO_ROUTE,
  ROOT,
  XvfbCabana,
  open_manage_dbc_for_bus_path,
  remove_manage_dbc_from_bus,
  wait_for_demo_route,
)

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90
SOURCE_DBC = ROOT / "opendbc" / "dbc" / "ford_lincoln_base_pt.dbc"


class TestManageDbcFiles:
  @staticmethod
  def _settings_path(config_home):
    return Path(config_home) / "cabana_imgui" / "settings.json"

  def _load_state(self, config_home, timeout=5):
    path = self._settings_path(config_home)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      if path.exists():
        return json.loads(path.read_text())
      time.sleep(0.1)
    raise FileNotFoundError(path)

  def _wait_for_bus_assignment(self, config_home, expected_path, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      state = self._load_state(config_home, timeout=timeout)
      assignments = state.get("active_dbc_files", {})
      positive = [int(source) for source, path in assignments.items() if int(source) >= 0 and path == expected_path]
      if positive:
        return state, positive
      time.sleep(0.1)
    raise AssertionError(f"bus-specific DBC assignment did not become {expected_path}")

  def _wait_for_assignment_removed(self, config_home, removed_sources, timeout=5):
    deadline = time.monotonic() + timeout
    removed_sources = {str(source) for source in removed_sources}
    while time.monotonic() < deadline:
      state = self._load_state(config_home, timeout=timeout)
      assignments = state.get("active_dbc_files", {})
      if removed_sources.isdisjoint(assignments.keys()):
        return state
      time.sleep(0.1)
    raise AssertionError(f"bus-specific DBC assignment did not clear for {sorted(removed_sources)}")

  def test_manage_dbc_files_assigns_and_removes_first_bus(self):
    with tempfile.TemporaryDirectory(prefix="cabana_manage_dbc_") as config_home:
      cabana = XvfbCabana(
        args=[DEMO_ROUTE, "--no-vipc"],
        timeout=WORKFLOW_TIMEOUT,
        env_extra={"XDG_CONFIG_HOME": config_home},
        clean_config=False,
      )
      cabana.start()
      try:
        wait_for_demo_route(cabana)

        open_manage_dbc_for_bus_path(cabana, str(SOURCE_DBC), bus_index=0)
        state, assigned_sources = self._wait_for_bus_assignment(config_home, str(SOURCE_DBC))
        assert any(source < 64 for source in assigned_sources)
        assert state["active_dbc_file"] == str(SOURCE_DBC)

        remove_manage_dbc_from_bus(cabana, bus_index=0)
        cleared_state = self._wait_for_assignment_removed(config_home, assigned_sources)
        assert all(str(source) not in cleared_state.get("active_dbc_files", {}) for source in assigned_sources)

        path = cabana.screenshot("workflow_manage_dbc_bus_menu.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()
