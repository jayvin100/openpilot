"""
Workflow tests: Open Stream dialog.

Keep validation black-box by opening the real File -> Open Stream modal from a
clean app boot and verifying the dialog appears.
"""

from ..helpers import XvfbCabana, open_stream_window  # noqa: TID251

WORKFLOW_TIMEOUT = 60


class TestOpenStreamDialog:
  def test_open_stream_dialog_opens(self):
    with XvfbCabana(args=[], timeout=WORKFLOW_TIMEOUT) as cabana:
      cabana.maximize()
      open_stream_window(cabana)

      path = cabana.screenshot("workflow_open_stream_dialog.png")
      assert cabana.is_alive()
      assert path.exists()
