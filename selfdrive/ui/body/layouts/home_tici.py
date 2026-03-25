import time

import pyray as rl
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.multilang import tr, tr_noop
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.selfdrive.ui.body.widgets.body_pairing import BodyPairingScreen
from openpilot.selfdrive.ui.body.animations import FaceAnimator, ASLEEP, INQUISITIVE, NORMAL, SLEEPY

GRID_COLS = 16
GRID_ROWS = 8
RADIUS = 50 if gui_app.big_ui() else 10

IDLE_TIMEOUT = 30.0        # seconds of no joystick input before playing INQUISITIVE
IDLE_STEER_THRESH = 0.5    # degrees — below this counts as no input
IDLE_SPEED_THRESH = 0.01   # m/s — below this counts as no input

PAIR_BTN_FONT_SIZE = 30
PAIR_BTN_MARGIN = 30


class BodyLayout(Widget):
  def __init__(self):
    super().__init__()
    self._setup_widget = type('', (), {})()
    self._setup_widget.set_open_settings_callback = lambda cb: None
    self._animator = FaceAnimator(ASLEEP)
    self._turning_left = False
    self._turning_right = False
    self._last_input_time = time.monotonic()
    self._was_active = False
    self._font_bold = gui_app.font(FontWeight.BOLD)

  def set_settings_callback(self, callback):
    pass

  def is_swiping_left(self) -> bool:
    return False

  def draw_dot_grid(self, rect: rl.Rectangle, dots: list[tuple[int, int]], color: rl.Color | None = None):
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

  def _draw_pair_button(self, rect: rl.Rectangle):
    mouse_pos = rl.get_mouse_position()
    mouse_down = self.is_pressed and rl.is_mouse_button_down(rl.MouseButton.MOUSE_BUTTON_LEFT)

    pair_rect = self._get_pair_btn_rect(rect)
    pair_pressed = mouse_down and rl.check_collision_point_rec(mouse_pos, pair_rect)
    bg_color = rl.Color(255, 255, 255, 166) if pair_pressed else rl.WHITE

    rl.draw_rectangle_rounded(pair_rect, 0.5, 10, bg_color)
    text = tr(tr_noop("CONNECT"))
    text_size = measure_text_cached(self._font_bold, text, PAIR_BTN_FONT_SIZE)
    text_pos = rl.Vector2(pair_rect.x + (pair_rect.width - text_size.x) / 2,
                          pair_rect.y + (pair_rect.height - text_size.y) / 2)
    rl.draw_text_ex(self._font_bold, text, text_pos, PAIR_BTN_FONT_SIZE, 0, rl.BLACK)

  def _get_pair_btn_rect(self, rect: rl.Rectangle) -> rl.Rectangle:
    text = tr(tr_noop("CONNECT"))
    text_size = measure_text_cached(self._font_bold, text, PAIR_BTN_FONT_SIZE)
    btn_w = int(text_size.x + 200)
    btn_h = 200
    btn_x = int(rect.x + rect.width - btn_w - PAIR_BTN_MARGIN)
    btn_y = int(rect.y + rect.height - btn_h - PAIR_BTN_MARGIN)
    return rl.Rectangle(btn_x, btn_y, btn_w, btn_h)

  def _update_state(self):
    sm = ui_state.sm

    active = ui_state.is_onroad()
    if active and ui_state.joystick_debug_mode:
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
    is_v2 = sm['carParams'].carFingerprint == "COMMA_BODY_V2"
    if is_v2:
      self._turning_left = steer <= -0.05
      self._turning_right = steer >= 0.05
    else:
      self._turning_left = steer >= 0.05
      self._turning_right = steer <= -0.05

  def _handle_mouse_release(self, mouse_pos):
    # Check pair button (only on big UI; on MICI it's on the home screen)
    if gui_app.big_ui():
      pair_rect = self._get_pair_btn_rect(self._rect)
      if rl.check_collision_point_rec(mouse_pos, pair_rect):
        gui_app.push_widget(BodyPairingScreen())
        return

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
    self.draw_dot_grid(rect, dots)
    if gui_app.big_ui():
      self._draw_pair_button(rect)