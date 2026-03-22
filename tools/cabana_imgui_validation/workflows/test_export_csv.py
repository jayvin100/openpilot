"""
Workflow tests: CSV export.

Drive the real File -> Export to CSV path and validate the written file.
"""

import tempfile
from pathlib import Path

import pytest

from ..helpers import DEMO_ROUTE, XvfbCabana, export_csv_path, wait_for_demo_route  # noqa: TID251

pytestmark = pytest.mark.xdist_group("cabana_demo_route")
WORKFLOW_TIMEOUT = 90


class TestExportCSV:
  def test_export_csv_produces_can_dump(self):
    with tempfile.TemporaryDirectory(prefix="cabana_export_csv_") as tmpdir:
      output_path = Path(tmpdir) / "route_export.csv"

      cabana = XvfbCabana(
        args=[DEMO_ROUTE, "--no-vipc"],
        timeout=WORKFLOW_TIMEOUT,
      )
      cabana.start()
      try:
        wait_for_demo_route(cabana)
        export_csv_path(cabana, str(output_path))

        assert output_path.exists()
        lines = output_path.read_text().splitlines()
        assert lines
        assert lines[0] == "time,addr,bus,data"
        assert len(lines) > 10
        assert any(line.startswith(("0.", "1.")) for line in lines[1:20])

        path = cabana.screenshot("workflow_export_csv.png")
        assert cabana.is_alive()
        assert path.exists()
      finally:
        cabana.kill()
