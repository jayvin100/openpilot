"""
Smoke tests: live-source boot paths.

These keep validation external by launching the real app in live-source modes
and asserting the window comes up cleanly.
"""

import time

from ..helpers import XvfbCabana  # noqa: TID251

BOOT_TIMEOUT = 60


class TestLiveBoot:
  def test_msgq_boots_cleanly(self):
    with XvfbCabana(args=["--msgq"], timeout=BOOT_TIMEOUT) as cabana:
      cabana.maximize()
      time.sleep(2.0)

      assert "Live Streaming From 127.0.0.1" in cabana.get_window_name()
      path = cabana.screenshot("smoke_msgq_boot.png")
      assert cabana.is_alive()
      assert path.exists()
