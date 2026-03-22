"""
Workflow tests: loading DBCs from the bundled comma/opendbc menu.
"""

import json
import tempfile
import time
from pathlib import Path

import pytest

from ..helpers import DEMO_ROUTE, ROOT, XvfbCabana, load_first_opendbc_via_menu, wait_for_demo_route  # noqa: TID251

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90


class TestOpendbcFlows:
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

  def _wait_for_active_dbc(self, config_home, timeout=5):
    deadline = time.monotonic() + timeout
    opendbc_root = (ROOT / "opendbc" / "dbc").resolve()
    while time.monotonic() < deadline:
      state = self._load_state(config_home, timeout=timeout)
      active = state.get("active_dbc_file", "")
      if active:
        active_path = Path(active).resolve()
        if opendbc_root in active_path.parents and active_path.suffix == ".dbc":
          return state, active_path
      time.sleep(0.1)
    raise AssertionError("active_dbc_file did not become an opendbc path")

  def test_load_first_opendbc_entry(self):
    with tempfile.TemporaryDirectory(prefix="cabana_opendbc_") as config_home:
      cabana = XvfbCabana(
        args=[DEMO_ROUTE, "--no-vipc"],
        timeout=WORKFLOW_TIMEOUT,
        env_extra={"XDG_CONFIG_HOME": config_home},
        clean_config=False,
      )
      cabana.start()
      try:
        wait_for_demo_route(cabana)
        load_first_opendbc_via_menu(cabana)

        state, active_path = self._wait_for_active_dbc(config_home)
        assert active_path.exists()
        assert Path(state["recent_dbc_files"][0]).resolve() == active_path

        path = cabana.screenshot("workflow_opendbc_loaded.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()
