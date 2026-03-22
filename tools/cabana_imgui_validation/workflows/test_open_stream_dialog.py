"""
Workflow tests: Open Stream dialog.

Keep validation black-box by driving the real File -> Open Stream modal from
the app shell and verifying both dialog open and stream switching behavior.
"""

from ..helpers import (  # noqa: TID251
  DEMO_ROUTE,
  XvfbCabana,
  export_csv_path,
  open_stream_window,
  wait_for_demo_route,
  wait_for_window_name_contains,
)

WORKFLOW_TIMEOUT = 60


class TestOpenStreamDialog:
  def test_open_stream_dialog_opens(self):
    with XvfbCabana(args=[], timeout=WORKFLOW_TIMEOUT) as cabana:
      cabana.maximize()
      open_stream_window(cabana)

      path = cabana.screenshot("workflow_open_stream_dialog.png")
      assert cabana.is_alive()
      assert path.exists()

  def test_switch_from_msgq_to_replay_via_dialog(self, tmp_path):
    csv_path = tmp_path / "switched_route.csv"

    with XvfbCabana(args=["--msgq"], timeout=WORKFLOW_TIMEOUT) as cabana:
      cabana.maximize()
      assert "Live Streaming" in cabana.get_window_name()

      open_stream_window(cabana)
      cabana.type_text(DEMO_ROUTE)
      cabana.send_key("Return")
      wait_for_window_name_contains(cabana, DEMO_ROUTE, timeout=15)
      wait_for_demo_route(cabana)

      export_csv_path(cabana, str(csv_path))

      path = cabana.screenshot("workflow_open_stream_switch_to_replay.png")
      assert cabana.is_alive()
      assert path.exists()

    assert csv_path.exists()
    csv_text = csv_path.read_text()
    assert csv_text.startswith("time,addr,bus,data\n")
    assert len(csv_text.splitlines()) > 1
