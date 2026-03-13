import time

import pyray as rl
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.widgets import Widget
from .animations import Animation, AnimationMode, NORMAL, SLEEPY, ASLEEP, INQUISITIVE

GRID_COLS = 16
GRID_ROWS = 8
RADIUS = 50

IDLE_TIMEOUT = 30.0        # seconds of no joystick input before playing INQUISITIVE
IDLE_STEER_THRESH = 0.5    # degrees — below this counts as no input
IDLE_SPEED_THRESH = 0.01   # m/s — below this counts as no input


def _get_frame_index(animation: Animation, elapsed: float, gap_first: bool = False) -> int:
  """Get the current frame index given elapsed time and animation mode."""
  num_frames = len(animation.frames)
  if num_frames == 1:
    return 0

  forward_duration = num_frames * animation.frame_duration
  has_backward = animation.mode in (AnimationMode.ONCE_FORWARD_BACKWARD, AnimationMode.REPEAT_FORWARD_BACKWARD)
  repeats = animation.mode in (AnimationMode.REPEAT_FORWARD, AnimationMode.REPEAT_FORWARD_BACKWARD)

  if has_backward:
    backward_frames = max(num_frames - 2, 0)
    backward_duration = backward_frames * animation.frame_duration
    cycle_duration = forward_duration + animation.hold_end + backward_duration
  else:
    backward_frames = 0
    backward_duration = 0
    cycle_duration = forward_duration

  if not repeats:
    # Play once — clamp elapsed to one cycle
    t = min(elapsed, cycle_duration)
  else:
    adj_elapsed = elapsed + cycle_duration if gap_first else elapsed
    t = adj_elapsed % animation.repeat_interval

  if t < forward_duration:
    return min(int(t / animation.frame_duration), num_frames - 1)
  elif not has_backward:
    return num_frames - 1
  elif t < forward_duration + animation.hold_end:
    return num_frames - 1
  elif t < forward_duration + animation.hold_end + backward_duration:
    backward_elapsed = t - forward_duration - animation.hold_end
    backward_index = min(int(backward_elapsed / animation.frame_duration), backward_frames - 1)
    return num_frames - 2 - backward_index
  else:
    return 0


class FaceAnimator:
  def __init__(self, animation: Animation):
    self._animation = animation
    self._next: Animation | None = None
    self._start_time = time.monotonic()
    self._rewinding = False
    self._rewind_start: float = 0.0
    self._rewind_from: int = 0
    self._seen_nonzero = False

  def set_animation(self, animation: Animation):
    if animation is not self._animation:
      self._next = animation

  def get_dots(self) -> list[tuple[int, int]]:
    now = time.monotonic()
    elapsed = now - self._start_time

    # Handle rewind for forward-only animations
    if self._rewinding:
      rewind_elapsed = now - self._rewind_start
      frames_back = round(rewind_elapsed / self._animation.frame_duration)
      frame_index = self._rewind_from - frames_back
      if frame_index <= 0:
        return self._switch_to_next(now)
      return self._animation.frames[frame_index]

    # Play starting frames first (once)
    starting = self._animation.starting_frames or []
    starting_duration = len(starting) * self._animation.frame_duration
    if starting and elapsed < starting_duration:
      frame_index = min(int(elapsed / self._animation.frame_duration), len(starting) - 1)
      return starting[frame_index]

    # Main loop
    loop_elapsed = elapsed - starting_duration if starting else elapsed
    frame_index = _get_frame_index(self._animation, loop_elapsed, gap_first=bool(starting))

    if frame_index != 0:
      self._seen_nonzero = True

    if self._next is not None:
      if frame_index == 0 and (len(self._animation.frames) == 1 or self._seen_nonzero):
        return self._switch_to_next(now)
      # No natural return to frame 0 — start rewinding
      if self._animation.mode in (AnimationMode.ONCE_FORWARD, AnimationMode.REPEAT_FORWARD):
        self._rewinding = True
        self._rewind_start = now
        self._rewind_from = frame_index

    return self._animation.frames[frame_index]

  def _switch_to_next(self, now: float) -> list[tuple[int, int]]:
    self._animation = self._next
    self._next = None
    self._rewinding = False
    self._seen_nonzero = False
    self._start_time = now
    return self._animation.frames[0]


def draw_dot_grid(rect: rl.Rectangle, dots: list[tuple[int, int]], color: rl.Color = None):
  if color is None:
    color = rl.WHITE

  spacing = (rect.height) / (GRID_ROWS)

  grid_w = (GRID_COLS - 1) * spacing
  grid_h = (GRID_ROWS - 1) * spacing

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
    self._animator = FaceAnimator(ASLEEP)
    self._turning_left = False
    self._turning_right = False
    self._last_input_time = time.monotonic()
    self._was_active = False

  def set_settings_callback(self, callback):
    pass

  def _update_state(self):
    sm = ui_state.sm

    joystick_mode = ui_state.params.get_bool("JoystickDebugMode")
    active = joystick_mode and sm['selfdriveState'].enabled
    if active:
      if not self._was_active:
        self._last_input_time = time.monotonic()
        self._was_active = True

      cs = sm['carState']
      has_input = abs(cs.steeringAngleDeg) > IDLE_STEER_THRESH or abs(cs.vEgo) > IDLE_SPEED_THRESH
      if has_input:
        self._last_input_time = time.monotonic()

      if time.monotonic() - self._last_input_time > IDLE_TIMEOUT:
        self._animator.set_animation(INQUISITIVE)
      else:
        self._animator.set_animation(NORMAL)
    else:
      self._was_active = False
      self._animator.set_animation(ASLEEP)

    if not sm.updated['carState']:
      return

    steer = sm['testJoystick'].axes[1] if len(sm['testJoystick'].axes) > 1 else 0
    self._turning_left = steer <= -0.05
    self._turning_right = steer >= 0.05

  def _handle_mouse_release(self, mouse_pos):
    super()._handle_mouse_release(mouse_pos)
    if not self._was_active:
      self._animator.set_animation(SLEEPY)

  def _render(self, rect: rl.Rectangle):
    rl.clear_background(rl.BLACK)
    dots = self._animator.get_dots()
    animation = self._animator._animation
    if self._turning_left and animation.left_turn_remove:
      remove_set = set(animation.left_turn_remove)
      dots = [d for d in dots if d not in remove_set]
    elif self._turning_right and animation.right_turn_remove:
      remove_set = set(animation.right_turn_remove)
      dots = [d for d in dots if d not in remove_set]
    draw_dot_grid(rect, dots)
