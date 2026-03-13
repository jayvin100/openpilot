import os
import time
import pyray as rl
from enum import Enum
from openpilot.common.basedir import BASEDIR
from openpilot.system.ui.widgets import Widget

BODY_ASSETS = os.path.join(BASEDIR, "selfdrive/assets/body")
FRAME_DELAY = 0.1  # seconds between frames
CYCLE_INTERVAL = 10.0  # seconds between animation cycles

class BodyAnim(Enum):
  AWAKE = "awake.gif"
  SLEEP = "sleep.gif"

class BodyLayout(Widget):
  def __init__(self, anim: BodyAnim):
    super().__init__()
    self._setup_widget = type('', (), {'set_open_settings_callback': lambda self, cb: None})()
    self._frame_count = 0
    self._current_frame = 0
    self._last_frame_time = 0.0
    self._next_cycle_time = 0.0
    self._animating = True
    self._texture = None
    self._image = None
    self._frame_size = 0

    self._load_gif(os.path.join(BODY_ASSETS, anim.value))

  def set_settings_callback(self, callback):
    pass


  def _load_gif(self, gif_path: str):
    frames_ptr = rl.ffi.new('int *')
    self._image = rl.load_image_anim(gif_path, frames_ptr)
    self._frame_count = frames_ptr[0]

    # Each frame: width * height * 4 bytes (RGBA)
    self._frame_size = self._image.width * self._image.height * 4

    self._texture = rl.load_texture_from_image(self._image)
    rl.set_texture_filter(self._texture, rl.TextureFilter.TEXTURE_FILTER_BILINEAR)
    self._last_frame_time = time.monotonic()

  def _render(self, rect: rl.Rectangle):
    rl.clear_background(rl.BLACK)

    now = time.monotonic()

    # Start a new animation cycle every CYCLE_INTERVAL
    if not self._animating and now >= self._next_cycle_time:
      self._animating = True
      self._current_frame = 0
      self._last_frame_time = now
      # Update texture to first frame
      rl.update_texture(self._texture, self._image.data)

    # Advance frames while animating
    if self._animating and now - self._last_frame_time >= FRAME_DELAY:
      self._current_frame += 1
      self._last_frame_time = now

      if self._current_frame >= self._frame_count:
        # Animation complete, wait for next cycle
        self._current_frame = 0
        self._animating = False
        self._next_cycle_time = now + CYCLE_INTERVAL
        rl.update_texture(self._texture, self._image.data)
      else:
        offset = self._current_frame * self._frame_size
        frame_data = rl.ffi.cast("unsigned char *", self._image.data) + offset
        rl.update_texture(self._texture, frame_data)

    # Draw centered and scaled to fit
    scale = min(rect.width / self._texture.width, rect.height / self._texture.height)
    draw_w = self._texture.width * scale
    draw_h = self._texture.height * scale
    x = rect.x + (rect.width - draw_w) / 2
    y = rect.y + (rect.height - draw_h) / 2

    source = rl.Rectangle(0, 0, self._texture.width, self._texture.height)
    dest = rl.Rectangle(x, y, draw_w, draw_h)
    rl.draw_texture_pro(self._texture, source, dest, rl.Vector2(0, 0), 0, rl.WHITE)
