import time

import pyray as rl
from openpilot.system.ui.widgets import Widget
from .animations import Animation, NORMAL

GRID_COLS = 16
GRID_ROWS = 8
RADIUS = 50

ALL_DOTS = [(col, row) for row in range(GRID_ROWS) for col in range(GRID_COLS)]

def draw_dot_grid(rect: rl.Rectangle, animation: Animation, color: rl.Color = None):
  if color is None:
    color = rl.WHITE

  now = time.monotonic()
  num_frames = len(animation.frames)

  if num_frames == 1:
    frame_index = 0
  else:
    forward_duration = num_frames * animation.frame_duration
    no_backward = animation.hold_end < 0

    if no_backward:
      cycle_duration = forward_duration
    else:
      backward_frames = max(num_frames - 2, 0)
      backward_duration = backward_frames * animation.frame_duration
      cycle_duration = forward_duration + animation.hold_end + backward_duration

    if animation.animation_frequency > 0:
      elapsed = now % animation.animation_frequency
    else:
      elapsed = now % cycle_duration

    if elapsed < forward_duration:
      # Playing forward
      frame_index = min(int(elapsed / animation.frame_duration), num_frames - 1)
    elif no_backward:
      # No backward, hold last frame
      frame_index = num_frames - 1
    elif elapsed < forward_duration + animation.hold_end:
      # Holding last frame
      frame_index = num_frames - 1
    elif elapsed < forward_duration + animation.hold_end + backward_duration:
      # Playing backward (excluding first and last frame)
      backward_elapsed = elapsed - forward_duration - animation.hold_end
      backward_index = min(int(backward_elapsed / animation.frame_duration), backward_frames - 1)
      frame_index = num_frames - 2 - backward_index
    else:
      # Hold first frame for remainder
      frame_index = 0

  dots = animation.frames[frame_index]

  spacing = (rect.height) / (GRID_ROWS)

  # Total size of the grid from first to last dot center
  grid_w = (GRID_COLS - 1) * spacing
  grid_h = (GRID_ROWS - 1) * spacing

  # Center horizontally, keep vertical centering
  offset_x = rect.x + (rect.width - grid_w) / 2
  offset_y = rect.y + (rect.height - grid_h) / 2

  for row, col in dots:
    x = int(offset_x + col * spacing)
    y = int(offset_y + row * spacing)
    rl.draw_circle(x, y, RADIUS, color)

class BodyLayout(Widget):
  def __init__(self):
    super().__init__()
    self._setup_widget = type('', (), {'set_open_settings_callback': lambda self, cb: None})()

  def set_settings_callback(self, callback):
    pass

  def _render(self, rect: rl.Rectangle):
    rl.clear_background(rl.BLACK)
    draw_dot_grid(rect, NORMAL)
