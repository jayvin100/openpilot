"""
Workflow tests: DBC new/clipboard flows.

These drive the real ImGui file/menu surfaces and validate only normal app
outputs: clipboard contents, saved DBC files, settings.json, and screenshots.
"""

import json
import re
import tempfile
import time
from pathlib import Path

import pytest

from ..helpers import (  # noqa: TID251
  DEMO_ROUTE,
  ROOT,
  XvfbCabana,
  copy_dbc_to_clipboard,
  load_dbc_from_clipboard,
  load_manage_dbc_from_clipboard,
  new_dbc_via_menu,
  new_manage_dbc_for_bus,
  open_dbc_path,
  save_dbc_as_path,
  save_manage_dbc_as_path,
  wait_for_demo_route,
)

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90
SOURCE_DBC = ROOT / "opendbc" / "dbc" / "ford_lincoln_base_pt.dbc"
GLOBAL_CLIPBOARD_DBC = """VERSION ""

BO_ 256 CABANA_CLIPBOARD_GLOBAL_MSG: 8 XXX
 SG_ CABANA_CLIPBOARD_GLOBAL_SIGNAL : 0|8@1+ (1,0) [0|255] "" XXX
"""
BUS_CLIPBOARD_DBC = """VERSION ""

BO_ 512 CABANA_CLIPBOARD_BUS_MSG: 8 XXX
 SG_ CABANA_CLIPBOARD_BUS_SIGNAL : 8|8@1+ (1,0) [0|255] "" XXX
"""


class TestDbcClipboardFlows:
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

  def _wait_for_global_assignment(self, config_home, expected_path, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      state = self._load_state(config_home, timeout=timeout)
      if state.get("active_dbc_files", {}).get("-1") == expected_path:
        return state
      time.sleep(0.1)
    raise AssertionError(f"global DBC assignment did not become {expected_path}")

  def _wait_for_global_assignment_cleared(self, config_home, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      state = self._load_state(config_home, timeout=timeout)
      if not state.get("active_dbc_files") and not state.get("active_dbc_file"):
        return state
      time.sleep(0.1)
    raise AssertionError("global DBC assignment did not clear")

  def _wait_for_bus_assignment(self, config_home, expected_path, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
      state = self._load_state(config_home, timeout=timeout)
      assignments = state.get("active_dbc_files", {})
      positives = sorted(int(source) for source, path in assignments.items()
                         if int(source) >= 0 and path == expected_path)
      if positives:
        return state, positives
      time.sleep(0.1)
    raise AssertionError(f"bus DBC assignment did not become {expected_path}")

  def test_new_file_clears_global_assignment_and_clipboard_loads(self):
    with tempfile.TemporaryDirectory(prefix="cabana_dbc_clipboard_global_") as config_home:
      output_path = Path(config_home) / "clipboard_global.dbc"
      cabana = XvfbCabana(
        args=[DEMO_ROUTE, "--no-vipc"],
        timeout=WORKFLOW_TIMEOUT,
        env_extra={"XDG_CONFIG_HOME": config_home},
        clean_config=False,
      )
      cabana.start()
      try:
        wait_for_demo_route(cabana)
        open_dbc_path(cabana, str(SOURCE_DBC))
        state = self._wait_for_global_assignment(config_home, str(SOURCE_DBC))
        assert state["active_dbc_file"] == str(SOURCE_DBC)

        new_dbc_via_menu(cabana)
        cleared_state = self._wait_for_global_assignment_cleared(config_home)
        assert cleared_state.get("active_dbc_files", {}) == {}
        assert cleared_state.get("active_dbc_file", "") == ""

        cabana.set_clipboard_text(GLOBAL_CLIPBOARD_DBC)
        load_dbc_from_clipboard(cabana)
        state_after_clipboard = self._load_state(config_home)
        assert state_after_clipboard.get("active_dbc_files", {}) == {}

        save_dbc_as_path(cabana, str(output_path))
        saved_state = self._wait_for_global_assignment(config_home, str(output_path))
        assert saved_state["active_dbc_file"] == str(output_path)
        assert output_path.exists()
        text = output_path.read_text()
        assert "BO_ 256 CABANA_CLIPBOARD_GLOBAL_MSG: 8 XXX" in text
        assert "SG_ CABANA_CLIPBOARD_GLOBAL_SIGNAL : 0|8@1+ (1,0) [0|255] \"\" XXX" in text

        path = cabana.screenshot("workflow_dbc_clipboard_global.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()

  def test_copy_to_clipboard_and_per_bus_new_and_clipboard(self):
    with tempfile.TemporaryDirectory(prefix="cabana_dbc_clipboard_bus_") as config_home:
      env_extra = {"XDG_CONFIG_HOME": config_home}
      bus_new_path = Path(config_home) / "bus_new_empty.dbc"
      bus_clipboard_path = Path(config_home) / "bus_clipboard.dbc"

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
        self._wait_for_global_assignment(config_home, str(SOURCE_DBC))

        copy_dbc_to_clipboard(cabana)
        clipboard_text = cabana.get_clipboard_text()
        first_message = re.search(r"^BO_ .*$", SOURCE_DBC.read_text(), re.MULTILINE)
        assert first_message is not None
        assert first_message.group(0) in clipboard_text

        new_manage_dbc_for_bus(cabana, bus_index=0)
        save_manage_dbc_as_path(cabana, str(bus_new_path), bus_index=0)
        new_state, new_sources = self._wait_for_bus_assignment(config_home, str(bus_new_path))
        assert new_state["active_dbc_files"]["-1"] == str(SOURCE_DBC)
        assert bus_new_path.read_text() == ""
        assert any(source < 64 for source in new_sources)

        cabana.set_clipboard_text(BUS_CLIPBOARD_DBC)
        load_manage_dbc_from_clipboard(cabana, bus_index=0)
        mid_state = self._load_state(config_home)
        assert mid_state["active_dbc_files"]["-1"] == str(SOURCE_DBC)

        save_manage_dbc_as_path(cabana, str(bus_clipboard_path), bus_index=0)
        saved_state, saved_sources = self._wait_for_bus_assignment(config_home, str(bus_clipboard_path))
        assert saved_state["active_dbc_files"]["-1"] == str(SOURCE_DBC)
        assert saved_sources == new_sources
        text = bus_clipboard_path.read_text()
        assert "BO_ 512 CABANA_CLIPBOARD_BUS_MSG: 8 XXX" in text
        assert "SG_ CABANA_CLIPBOARD_BUS_SIGNAL : 8|8@1+ (1,0) [0|255] \"\" XXX" in text

        path = cabana.screenshot("workflow_dbc_clipboard_bus.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()
