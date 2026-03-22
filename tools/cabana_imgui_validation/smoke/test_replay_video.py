"""
Smoke tests: replay video rendering.

These tests keep validation black-box by launching a normal replay route with
VIPC enabled and verifying that the Video pane renders real image data and that
the image changes while playback advances.
"""

import time

import pytest
from PIL import Image, ImageChops, ImageStat

from ..helpers import DEMO_ROUTE, XvfbCabana  # noqa: TID251

VIDEO_BOX = (1480, 85, 1915, 332)
VIDEO_TIMEOUT = 90
pytestmark = pytest.mark.xdist_group("cabana_demo_route")


def crop_video(path):
  return Image.open(path).crop(VIDEO_BOX)


class TestReplayVideo:
  def test_video_frame_renders(self):
    with XvfbCabana(args=[DEMO_ROUTE], timeout=VIDEO_TIMEOUT) as cabana:
      cabana.maximize()
      time.sleep(12)

      path = cabana.screenshot("smoke_replay_video_frame.png")
      crop = crop_video(path)
      stat = ImageStat.Stat(crop)

      assert cabana.is_alive()
      assert path.exists()
      assert max(stat.stddev) > 10.0

  def test_video_frame_changes_during_playback(self):
    with XvfbCabana(args=[DEMO_ROUTE], timeout=VIDEO_TIMEOUT) as cabana:
      cabana.maximize()
      time.sleep(10)
      before = crop_video(cabana.screenshot("smoke_replay_video_before.png"))
      time.sleep(2)
      after = crop_video(cabana.screenshot("smoke_replay_video_after.png"))

      diff = ImageChops.difference(before, after)
      assert cabana.is_alive()
      assert diff.getbbox() is not None
