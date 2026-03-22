"""
Workflow tests: Find Signal tool.

Drive the real Tools -> Find Signal path and validate create-signal behavior
strictly through external UI automation and saved DBC output.
"""

import re
import tempfile
from pathlib import Path

import pytest

from ..helpers import (  # noqa: TID251
  DEMO_ROUTE,
  XvfbCabana,
  create_signal_from_find_signal,
  open_find_signal_window,
  reset_layout,
  run_find_signal_search,
  save_dbc_as_path,
  select_first_message,
  wait_for_demo_route,
)

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90


class TestFindSignal:
  def test_find_signal_creates_signal_and_saves(self):
    with tempfile.TemporaryDirectory(prefix="cabana_find_signal_") as config_home:
      output_path = Path(config_home) / "find_signal_created.dbc"
      cabana = XvfbCabana(
        args=[DEMO_ROUTE, "--no-vipc"],
        timeout=WORKFLOW_TIMEOUT,
        env_extra={"XDG_CONFIG_HOME": config_home},
        clean_config=False,
      )
      cabana.start()
      try:
        wait_for_demo_route(cabana)
        reset_layout(cabana)
        select_first_message(cabana)

        open_find_signal_window(cabana)
        run_find_signal_search(cabana)
        create_signal_from_find_signal(cabana)

        save_dbc_as_path(cabana, str(output_path))
        assert output_path.exists()

        text = output_path.read_text()
        assert re.search(r"SG_ NEW_SIGNAL(?:_\d+)? :", text)

        path = cabana.screenshot("workflow_find_signal_created.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()
