"""
Workflow tests: manual DBC open/save flows.

Drive the real Ctrl+O / Ctrl+Shift+S paths and verify only normal app outputs:
settings.json, screenshots, and written DBC files.
"""

import json
import tempfile
import time
from pathlib import Path

import pytest

from ..helpers import DEMO_ROUTE, ROOT, XvfbCabana, open_dbc_path, save_dbc_as_path, wait_for_demo_route  # noqa: TID251

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90
SOURCE_DBC = ROOT / "opendbc" / "dbc" / "ford_lincoln_base_pt.dbc"


class TestDbcFileFlows:
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

  def _wait_for_active_dbc(self, config_home, expected_path, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      state = self._load_state(config_home, timeout=timeout)
      if state.get("active_dbc_file") == expected_path:
        return state
      time.sleep(0.1)
    raise AssertionError(f"active_dbc_file did not become {expected_path}")

  def test_open_and_save_dbc_via_shortcuts(self):
    with tempfile.TemporaryDirectory(prefix="cabana_dbc_flow_") as config_home:
      env_extra = {"XDG_CONFIG_HOME": config_home}
      output_path = Path(config_home) / "saved_copy.dbc"

      cabana = XvfbCabana(
        args=[DEMO_ROUTE, "--no-vipc"],
        timeout=WORKFLOW_TIMEOUT,
        env_extra=env_extra,
        clean_config=False,
      )
      cabana.start()
      try:
        wait_for_demo_route(cabana)
        open_dbc_path(cabana, str(SOURCE_DBC))
        state = self._load_state(config_home)
        assert state["active_dbc_file"] == str(SOURCE_DBC)
        assert state["recent_dbc_files"][0] == str(SOURCE_DBC)

        save_dbc_as_path(cabana, str(output_path))
        saved_state = self._wait_for_active_dbc(config_home, str(output_path))
        assert saved_state["active_dbc_file"] == str(output_path)
        assert saved_state["recent_dbc_files"][0] == str(output_path)
        assert output_path.exists()
        assert output_path.read_text() == SOURCE_DBC.read_text()

        saved = cabana.screenshot("workflow_dbc_saved_as.png")
        assert cabana.is_alive()
        assert saved.exists()
      finally:
        cabana.kill()

      restored = XvfbCabana(
        args=[DEMO_ROUTE, "--no-vipc"],
        timeout=WORKFLOW_TIMEOUT,
        env_extra=env_extra,
        clean_config=False,
      )
      restored.start()
      try:
        wait_for_demo_route(restored)
        restored_state = self._load_state(config_home)
        assert restored_state["active_dbc_file"] == str(output_path)
        assert restored_state["recent_dbc_files"][0] == str(output_path)
        path = restored.screenshot("workflow_dbc_restored.png")
        assert restored.is_alive()
        assert path.exists()
      finally:
        restored.kill()
