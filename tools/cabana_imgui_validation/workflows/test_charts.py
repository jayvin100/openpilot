"""
Workflow tests: chart interactions and persistence.

Drive the real chart UI through signal plot toggles and toolbar actions,
then verify saved config files restore those charts across restart.
"""

import json
import tempfile
from pathlib import Path

import pytest

from ..helpers import (  # noqa: TID251
  DEMO_ROUTE,
  XvfbCabana,
  click_signal_plot,
  create_chart_tab,
  quit_via_menu,
  remove_selected_chart,
  reset_layout,
  select_first_message,
  split_selected_chart,
  wait_for_demo_route,
)

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90


class TestCharts:
  def _setup_with_message(self, env_extra=None, clean_config=True):
    cabana = XvfbCabana(
      args=[DEMO_ROUTE, "--no-vipc"],
      timeout=WORKFLOW_TIMEOUT,
      env_extra=env_extra,
      clean_config=clean_config,
    )
    cabana.start()
    wait_for_demo_route(cabana)
    reset_layout(cabana)
    select_first_message(cabana)
    return cabana

  @staticmethod
  def _settings_path(config_home):
    return Path(config_home) / "cabana_imgui" / "settings.json"

  def _load_state(self, config_home):
    return json.loads(self._settings_path(config_home).read_text())

  def test_open_signal_plot(self):
    """Selecting Plot on a decoded signal should open a chart."""
    with tempfile.TemporaryDirectory(prefix="cabana_chart_open_") as config_home:
      cabana = self._setup_with_message(env_extra={"XDG_CONFIG_HOME": config_home}, clean_config=False)
      try:
        click_signal_plot(cabana, signal_index=0)
        state = self._load_state(config_home)

        assert len(state["chart_tabs"]) == 1
        assert len(state["chart_tabs"][0]["charts"]) == 1
        assert len(state["chart_tabs"][0]["charts"][0]["signals"]) == 1
        assert state["chart_tabs"][0]["charts"][0]["signals"][0]["signal_name"] == "immoTarget1Status"

        path = cabana.screenshot("workflow_chart_open.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()

  def test_merge_split_and_remove_chart(self):
    """Charts support merge, split, and remove flows from the live UI."""
    with tempfile.TemporaryDirectory(prefix="cabana_chart_merge_") as config_home:
      cabana = self._setup_with_message(env_extra={"XDG_CONFIG_HOME": config_home}, clean_config=False)
      try:
        click_signal_plot(cabana, signal_index=0)
        click_signal_plot(cabana, signal_index=1, merge=True)
        merged_state = self._load_state(config_home)

        assert len(merged_state["chart_tabs"][0]["charts"]) == 1
        assert len(merged_state["chart_tabs"][0]["charts"][0]["signals"]) == 2
        merged = cabana.screenshot("workflow_chart_merged.png")
        assert merged.exists()

        split_selected_chart(cabana)
        split_state = self._load_state(config_home)

        assert len(split_state["chart_tabs"][0]["charts"]) == 2
        assert all(len(chart["signals"]) == 1 for chart in split_state["chart_tabs"][0]["charts"])
        split = cabana.screenshot("workflow_chart_split.png")
        assert split.exists()

        remove_selected_chart(cabana)
        removed_state = self._load_state(config_home)

        assert len(removed_state["chart_tabs"][0]["charts"]) == 1
        removed = cabana.screenshot("workflow_chart_removed.png")
        assert cabana.is_alive()
        assert removed.exists()
      finally:
        cabana.kill()

  def test_chart_state_persists_across_restart(self):
    """Chart tabs, selected tab, and dock layout restore from normal config files."""
    with tempfile.TemporaryDirectory(prefix="cabana_chart_state_") as config_home:
      env_extra = {"XDG_CONFIG_HOME": config_home}
      settings_path = self._settings_path(config_home)
      imgui_ini_path = Path(config_home) / "cabana_imgui" / "imgui.ini"

      cabana = self._setup_with_message(env_extra=env_extra, clean_config=False)
      try:
        click_signal_plot(cabana, signal_index=0)
        click_signal_plot(cabana, signal_index=1, merge=True)
        create_chart_tab(cabana)

        saved_before_exit = self._load_state(config_home)
        assert len(saved_before_exit["chart_tabs"]) == 2
        assert len(saved_before_exit["chart_tabs"][0]["charts"]) == 1
        assert len(saved_before_exit["chart_tabs"][0]["charts"][0]["signals"]) == 2

        before = cabana.screenshot("workflow_chart_before_restart.png")
        assert before.exists()
        quit_via_menu(cabana)
      finally:
        if cabana.is_alive():
          cabana.kill()

      assert settings_path.exists()
      assert imgui_ini_path.exists()

      saved = json.loads(settings_path.read_text())
      expected_current_tab = saved["current_chart_tab"]
      assert len(saved["chart_tabs"]) == 2
      assert len(saved["chart_tabs"][0]["charts"]) == 1
      assert len(saved["chart_tabs"][0]["charts"][0]["signals"]) == 2
      imgui_ini = imgui_ini_path.read_text()
      assert "[Docking][Data]" in imgui_ini
      assert "DockNode" in imgui_ini

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
        assert restored_state["current_chart_tab"] == expected_current_tab
        assert len(restored_state["chart_tabs"]) == 2
        assert len(restored_state["chart_tabs"][0]["charts"]) == 1
        assert len(restored_state["chart_tabs"][0]["charts"][0]["signals"]) == 2
        path = restored.screenshot("workflow_chart_restored.png")
        assert restored.is_alive()
        assert path.exists()
      finally:
        restored.kill()
