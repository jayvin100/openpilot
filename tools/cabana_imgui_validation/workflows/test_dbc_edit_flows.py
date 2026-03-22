"""
Workflow tests: DBC message/signal editing from the live ImGui detail pane.

These cover real edit flows and verify the resulting DBC output, while keeping
all validation outside the production app.
"""

import re
import tempfile
import time
from pathlib import Path

import pytest
from PIL import Image, ImageChops

from ..helpers import (  # noqa: TID251
  DEMO_ROUTE,
  ROOT,
  XvfbCabana,
  open_add_signal_via_menu,
  open_edit_message_via_menu,
  open_dbc_path,
  reset_layout,
  save_current_dbc,
  save_dbc_as_path,
  select_first_message,
  undo_via_menu,
  wait_for_demo_route,
)

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90
SOURCE_DBC = ROOT / "opendbc" / "dbc" / "ford_lincoln_base_pt.dbc"
SELECTION_SUMMARY_BOX = (235, 120, 560, 142)
BINARY_SELECT_POINT = (500, 170)


class TestDbcEditFlows:
  @staticmethod
  def _setup_with_message(env_extra=None, clean_config=True):
    cabana = XvfbCabana(
      args=[DEMO_ROUTE, "--no-vipc"],
      timeout=WORKFLOW_TIMEOUT,
      env_extra=env_extra,
      clean_config=clean_config,
    )
    cabana.start()
    wait_for_demo_route(cabana)
    reset_layout(cabana)
    open_dbc_path(cabana, str(SOURCE_DBC))
    select_first_message(cabana)
    return cabana

  def test_edit_message_definition_and_save_as(self):
    with tempfile.TemporaryDirectory(prefix="cabana_edit_message_") as config_home:
      output_path = Path(config_home) / "edited_message.dbc"
      cabana = self._setup_with_message(
        env_extra={"XDG_CONFIG_HOME": config_home},
        clean_config=False,
      )
      try:
        open_edit_message_via_menu(cabana)
        cabana.send_key("ctrl+a")
        time.sleep(0.1)
        cabana.type_text("CABANA_EDITED_MSG")
        time.sleep(0.2)
        cabana.send_key("Return")
        time.sleep(1.0)

        save_dbc_as_path(cabana, str(output_path))
        assert output_path.exists()
        text = output_path.read_text()
        assert re.search(r"BO_ \d+ CABANA_EDITED_MSG:", text)

        path = cabana.screenshot("workflow_dbc_message_edit.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()

  def test_edit_message_definition_undo_redo(self):
    with tempfile.TemporaryDirectory(prefix="cabana_edit_message_undo_") as config_home:
      output_path = Path(config_home) / "edited_message_undo_redo.dbc"
      cabana = self._setup_with_message(
        env_extra={"XDG_CONFIG_HOME": config_home},
        clean_config=False,
      )
      try:
        open_edit_message_via_menu(cabana)
        cabana.send_key("ctrl+a")
        time.sleep(0.1)
        cabana.type_text("CABANA_UNDO_MSG")
        time.sleep(0.2)
        cabana.send_key("Return")
        time.sleep(1.0)

        undo_via_menu(cabana)
        save_dbc_as_path(cabana, str(output_path))
        undo_text = output_path.read_text()
        assert "CABANA_UNDO_MSG" not in undo_text

        cabana.focus()
        cabana.send_key("ctrl+y")
        time.sleep(1.0)
        save_current_dbc(cabana)
        redo_text = output_path.read_text()
        assert re.search(r"BO_ \d+ CABANA_UNDO_MSG:", redo_text)

        path = cabana.screenshot("workflow_dbc_message_undo_redo.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()

  def test_binary_selection_adds_signal_definition_and_saves(self):
    with tempfile.TemporaryDirectory(prefix="cabana_add_signal_") as config_home:
      output_path = Path(config_home) / "edited_signal.dbc"
      cabana = self._setup_with_message(
        env_extra={"XDG_CONFIG_HOME": config_home},
        clean_config=False,
      )
      try:
        # Freeze replay updates so screenshot diffs isolate the selection UI change.
        cabana.send_key("space")
        time.sleep(0.8)

        before = cabana.screenshot("workflow_binary_before_select.png")
        cabana.click(*BINARY_SELECT_POINT)
        time.sleep(0.6)
        after = cabana.screenshot("workflow_binary_after_select.png")

        before_img = Image.open(before).crop(SELECTION_SUMMARY_BOX)
        after_img = Image.open(after).crop(SELECTION_SUMMARY_BOX)
        diff = ImageChops.difference(before_img, after_img)
        assert diff.getbbox() is not None

        open_add_signal_via_menu(cabana)
        cabana.send_key("ctrl+a")
        time.sleep(0.1)
        cabana.type_text("CABANA_NEW_SIGNAL")
        time.sleep(0.2)
        cabana.send_key("Return")
        time.sleep(1.0)

        save_dbc_as_path(cabana, str(output_path))
        assert output_path.exists()
        text = output_path.read_text()
        assert 'SG_ CABANA_NEW_SIGNAL : 7|1@1+ (1,0) [0|1] "" XXX' in text

        path = cabana.screenshot("workflow_binary_signal_added.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()

  def test_binary_selection_signal_add_undo(self):
    with tempfile.TemporaryDirectory(prefix="cabana_add_signal_undo_") as config_home:
      output_path = Path(config_home) / "edited_signal_undo_redo.dbc"
      cabana = self._setup_with_message(
        env_extra={"XDG_CONFIG_HOME": config_home},
        clean_config=False,
      )
      try:
        cabana.send_key("space")
        time.sleep(0.8)
        cabana.click(*BINARY_SELECT_POINT)
        time.sleep(0.6)

        open_add_signal_via_menu(cabana)
        cabana.send_key("ctrl+a")
        time.sleep(0.1)
        cabana.type_text("CABANA_UNDO_SIGNAL")
        time.sleep(0.2)
        cabana.send_key("Return")
        time.sleep(1.0)

        undo_via_menu(cabana)
        save_dbc_as_path(cabana, str(output_path))
        undo_text = output_path.read_text()
        assert "CABANA_UNDO_SIGNAL" not in undo_text

        path = cabana.screenshot("workflow_binary_signal_undo_redo.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()
