import math
import pyray as rl
from cereal import log
from openpilot.selfdrive.ui.mici.onroad import SIDE_PANEL_WIDTH
from openpilot.selfdrive.ui.ui_state import ui_state, UIStatus
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.lib.application import gui_app
from openpilot.common.filter_simple import FirstOrderFilter

MORPH_DURATION = 0.2  # seconds
MORPH_RC = MORPH_DURATION / 3.0
BLINK_GROW_DELAY = 0.3
BLINK_OFF_1 = 0.12
BLINK_ON_1 = 0.12
BLINK_OFF_2 = 0.12
BLINK_ON_2 = 0.6
BLINK_CYCLE = BLINK_OFF_1 + BLINK_ON_1 + BLINK_OFF_2 + BLINK_ON_2
RED_BLINK_ON = 0.28
RED_BLINK_OFF = 0.28
RED_BLINK_CYCLE = RED_BLINK_ON + RED_BLINK_OFF
SMOOTH_WARNING_START_SPEED = 0.5  # relative to red cadence
SMOOTH_WARNING_ACCEL_DURATION = 6.0  # seconds to reach max cadence
BLINK_DIM_ALPHA = 0.15
BLINK_ALPHA_RC = 0.035
# Test override to match alert icon test mode in alert_renderer.py.
# When True, lane-change events also trigger the orange confidence bar.
TEST_ORANGE_BAR_FOR_LANE_CHANGE_EVENTS = False
LEFT_BLINDSPOT_SHIFT = 60
RIGHT_DOT_EXIT_RC = 0.06
RIGHT_DOT_EXIT_PIXELS = 14
ORANGE_BAR_WIDTH = 48  # Match normal confidence ball diameter (2 * 24)
ALERT_COLOR_MIX_RC = 0.08
BALL_ANIM_RC = 0.04


def draw_circle_gradient(center_x: float, center_y: float, radius: int,
                         top: rl.Color, bottom: rl.Color) -> None:
  # Draw a square with the gradient
  rl.draw_rectangle_gradient_v(int(center_x - radius), int(center_y - radius),
                               radius * 2, radius * 2,
                               top, bottom)

  # Paint over square with a ring
  outer_radius = math.ceil(radius * math.sqrt(2)) + 1
  rl.draw_ring(rl.Vector2(int(center_x), int(center_y)), radius, outer_radius,
               0.0, 360.0,
               20, rl.BLACK)


class ConfidenceBall(Widget):
  def __init__(self, demo: bool = False):
    super().__init__()
    self._demo = demo
    self._confidence_filter = FirstOrderFilter(-0.5, 0.5, 1 / gui_app.target_fps)
    self._alert_morph_filter = FirstOrderFilter(0.0, MORPH_RC, 1 / gui_app.target_fps)
    self._blink_alpha_filter = FirstOrderFilter(1.0, BLINK_ALPHA_RC, 1 / gui_app.target_fps)
    self._ball_alpha_filter = FirstOrderFilter(1.0, BALL_ANIM_RC, 1 / gui_app.target_fps)
    self._alert_color_mix_filter = FirstOrderFilter(0.0, ALERT_COLOR_MIX_RC, 1 / gui_app.target_fps)
    self._right_dot_presence_filter = FirstOrderFilter(1.0, RIGHT_DOT_EXIT_RC, 1 / gui_app.target_fps)
    self._orange_alert_started_at = -1.0
    self._orange_alert_prev_active = False
    self._orange_breath_phase = 0.0
    self._ball_anim_started_at = rl.get_time()
    self._ball_breath_phase = 0.0
    self._ball_anim_prev_active = False

  def update_filter(self, value: float):
    self._confidence_filter.update(value)

  def _is_orange_alert_active(self) -> bool:
    ss = ui_state.sm['selfdriveState']
    if ss.alertSize == log.SelfdriveState.AlertSize.none:
      return False

    # Keep default behavior for orange/userPrompt alerts.
    if ss.alertStatus == log.SelfdriveState.AlertStatus.userPrompt:
      return True

    # Blindspot warning should also trigger the orange grow bar.
    event_name = ss.alertType.split('/')[0] if ss.alertType else ''
    if event_name == 'laneChangeBlocked':
      return True

    # Keep this test behavior aligned with the icon override experiment.
    if TEST_ORANGE_BAR_FOR_LANE_CHANGE_EVENTS:
      return event_name in ('preLaneChangeLeft', 'preLaneChangeRight', 'laneChange')

    return False

  def _is_red_alert_active(self) -> bool:
    ss = ui_state.sm['selfdriveState']
    return ss.alertSize != log.SelfdriveState.AlertSize.none and ss.alertStatus == log.SelfdriveState.AlertStatus.critical

  def _is_alert_bar_active(self) -> bool:
    return self._is_orange_alert_active() or self._is_red_alert_active()

  def _is_first_pay_attention_warning_active(self) -> bool:
    """First level DM warning: 'Pay Attention' pre-warning state."""
    ss = ui_state.sm['selfdriveState']
    if ss.alertSize == log.SelfdriveState.AlertSize.none:
      return False
    event_name = ss.alertType.split('/')[0] if ss.alertType else ''
    return event_name == 'preDriverDistracted'

  def _is_left_blindspot_alert_active(self) -> bool:
    ss = ui_state.sm['selfdriveState']
    if ss.alertSize == log.SelfdriveState.AlertSize.none:
      return False

    event_name = ss.alertType.split('/')[0] if ss.alertType else ''
    if event_name == 'laneChangeBlocked':
      return ui_state.sm['carState'].leftBlinker
    if TEST_ORANGE_BAR_FOR_LANE_CHANGE_EVENTS:
      return event_name == 'preLaneChangeLeft'
    return False

  def _orange_blink_target_alpha(self) -> float:
    """Blink schedule with dim phases after initial grow delay."""
    if self._orange_alert_started_at < 0:
      return 1.0

    elapsed = rl.get_time() - self._orange_alert_started_at
    if elapsed < BLINK_GROW_DELAY:
      return 1.0

    # Alternate animation mode: breathing pulse that accelerates over time.
    if ui_state.params.get_bool("SmoothWarning"):
      post_delay_elapsed = elapsed - BLINK_GROW_DELAY
      progress = min(max(post_delay_elapsed / SMOOTH_WARNING_ACCEL_DURATION, 0.0), 1.0)
      start_cycle = RED_BLINK_CYCLE / SMOOTH_WARNING_START_SPEED
      current_cycle = self._lerp(start_cycle, RED_BLINK_CYCLE, progress)
      dt = 1 / gui_app.target_fps
      self._orange_breath_phase = (self._orange_breath_phase + dt / max(current_cycle, 1e-3)) % 1.0
      wave = 0.5 * (1.0 + math.cos(self._orange_breath_phase * 2.0 * math.pi))
      return BLINK_DIM_ALPHA + (1.0 - BLINK_DIM_ALPHA) * wave

    phase = (elapsed - BLINK_GROW_DELAY) % BLINK_CYCLE
    if phase < BLINK_OFF_1:
      return BLINK_DIM_ALPHA
    if phase < BLINK_OFF_1 + BLINK_ON_1:
      return 1.0
    if phase < BLINK_OFF_1 + BLINK_ON_1 + BLINK_OFF_2:
      return BLINK_DIM_ALPHA
    return 1.0

  def _red_blink_target_alpha(self) -> float:
    """Default (non-smooth) red cadence for the side bar."""
    if self._orange_alert_started_at < 0:
      return 1.0

    elapsed = rl.get_time() - self._orange_alert_started_at
    if elapsed < BLINK_GROW_DELAY:
      return 1.0

    phase = (elapsed - BLINK_GROW_DELAY) % RED_BLINK_CYCLE
    return 1.0 if phase < RED_BLINK_ON else BLINK_DIM_ALPHA

  def _ball_blink_target_alpha(self) -> float:
    """Default-mode confidence ball cadence: same pattern as orange alert."""
    elapsed = rl.get_time() - self._ball_anim_started_at
    phase = elapsed % BLINK_CYCLE
    if phase < BLINK_OFF_1:
      return BLINK_DIM_ALPHA
    if phase < BLINK_OFF_1 + BLINK_ON_1:
      return 1.0
    if phase < BLINK_OFF_1 + BLINK_ON_1 + BLINK_OFF_2:
      return BLINK_DIM_ALPHA
    return 1.0

  def _ball_breath_target_alpha(self) -> float:
    """Smooth-mode confidence ball cadence: breathe at orange starting speed."""
    start_cycle = RED_BLINK_CYCLE / SMOOTH_WARNING_START_SPEED
    dt = 1 / gui_app.target_fps
    self._ball_breath_phase = (self._ball_breath_phase + dt / max(start_cycle, 1e-3)) % 1.0
    wave = 0.5 * (1.0 + math.cos(self._ball_breath_phase * 2.0 * math.pi))
    return BLINK_DIM_ALPHA + (1.0 - BLINK_DIM_ALPHA) * wave

  @staticmethod
  def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t

  @staticmethod
  def _lerp_color(a: rl.Color, b: rl.Color, t: float) -> rl.Color:
    return rl.Color(
      int(a.r + (b.r - a.r) * t),
      int(a.g + (b.g - a.g) * t),
      int(a.b + (b.b - a.b) * t),
      int(a.a + (b.a - a.a) * t),
    )

  @staticmethod
  def _with_alpha(c: rl.Color, alpha_scale: float) -> rl.Color:
    return rl.Color(c.r, c.g, c.b, int(c.a * alpha_scale))

  def _draw_gradient_capsule(self, x: float, y: float, width: float, height: float, top_color: rl.Color, bottom_color: rl.Color) -> None:
    width_i = max(1, int(round(width)))
    height_i = max(1, int(round(height)))
    x_i = int(round(x))
    y_i = int(round(y))
    radius = width_i / 2.0
    cx = x_i + radius - 0.5

    for i in range(height_i):
      t = i / max(height_i - 1, 1)
      r = int(top_color.r + (bottom_color.r - top_color.r) * t)
      g = int(top_color.g + (bottom_color.g - top_color.g) * t)
      b = int(top_color.b + (bottom_color.b - top_color.b) * t)
      a = int(top_color.a + (bottom_color.a - top_color.a) * t)
      row_color = rl.Color(r, g, b, a)

      # Signed distance from the top/bottom arc centers.
      if i < radius:
        dy = (radius - 0.5) - i
        half_w = math.sqrt(max(radius * radius - dy * dy, 0.0))
      elif i >= height_i - radius:
        dy = i - (height_i - radius) + 0.5
        half_w = math.sqrt(max(radius * radius - dy * dy, 0.0))
      else:
        half_w = radius

      left = cx - half_w
      right = cx + half_w
      lp = math.floor(left)
      rp = math.floor(right)

      # One-pixel span near tips
      if lp == rp:
        cov = max(0.0, min(1.0, right - left))
        if cov > 0:
          rl.draw_pixel(lp, y_i + i, rl.Color(row_color.r, row_color.g, row_color.b, int(row_color.a * cov)))
        continue

      # Anti-aliased left edge pixel
      left_cov = 1.0 - (left - lp)
      if left_cov > 0:
        rl.draw_pixel(lp, y_i + i, rl.Color(row_color.r, row_color.g, row_color.b, int(row_color.a * min(left_cov, 1.0))))

      # Solid interior
      inner_x = lp + 1
      inner_w = max(0, rp - lp - 1)
      if inner_w > 0:
        rl.draw_rectangle(inner_x, y_i + i, inner_w, 1, row_color)

      # Anti-aliased right edge pixel
      right_cov = right - rp
      if right_cov > 0:
        rl.draw_pixel(rp, y_i + i, rl.Color(row_color.r, row_color.g, row_color.b, int(row_color.a * min(right_cov, 1.0))))

  def _get_regular_dot_colors(self) -> tuple[rl.Color, rl.Color]:
    # confidence zones
    if ui_state.status == UIStatus.ENGAGED or self._demo:
      if self._confidence_filter.x > 0.5:
        return rl.Color(0, 255, 204, 255), rl.Color(0, 255, 38, 255)
      if self._confidence_filter.x > 0.2:
        return rl.Color(255, 200, 0, 255), rl.Color(255, 115, 0, 255)
      return rl.Color(255, 0, 21, 255), rl.Color(255, 0, 89, 255)

    if ui_state.status == UIStatus.OVERRIDE:
      return rl.Color(255, 255, 255, 255), rl.Color(82, 82, 82, 255)

    return rl.Color(50, 50, 50, 255), rl.Color(13, 13, 13, 255)

  def _update_state(self):
    if self._demo:
      return

    # animate status dot in from bottom
    if ui_state.status == UIStatus.DISENGAGED:
      self._confidence_filter.update(-0.5)
    else:
      self._confidence_filter.update((1 - max(ui_state.sm['modelV2'].meta.disengagePredictions.brakeDisengageProbs or [1])) *
                                                        (1 - max(ui_state.sm['modelV2'].meta.disengagePredictions.steerOverrideProbs or [1])))

    orange_active = self._is_orange_alert_active()
    red_active = self._is_red_alert_active()
    alert_active = orange_active or red_active
    left_blindspot_active = self._is_left_blindspot_alert_active()
    if alert_active and not self._orange_alert_prev_active:
      self._orange_alert_started_at = rl.get_time()
      self._orange_breath_phase = 0.0
    elif not alert_active:
      self._orange_alert_started_at = -1.0
      self._orange_breath_phase = 0.0
    self._orange_alert_prev_active = alert_active
    self._alert_morph_filter.update(1.0 if alert_active else 0.0)
    if red_active:
      if ui_state.params.get_bool("SmoothWarning"):
        # Smooth mode: keep red side bar solid; cadence is on background.
        target_alpha = 1.0
      else:
        # Non-smooth mode: red cadence stays on side bar.
        target_alpha = self._red_blink_target_alpha()
    elif orange_active:
      target_alpha = self._orange_blink_target_alpha()
    else:
      target_alpha = 1.0
    self._blink_alpha_filter.update(target_alpha)
    if alert_active:
      self._alert_color_mix_filter.update(1.0 if red_active else 0.0)
    else:
      # Reset immediately when no alert bar is active to prevent red tint carryover.
      self._alert_color_mix_filter.x = 0.0

    # Animate the regular confidence ball ONLY during first-level "Pay Attention" warning.
    ball_anim_active = (not alert_active) and self._is_first_pay_attention_warning_active()
    if ball_anim_active and not self._ball_anim_prev_active:
      self._ball_anim_started_at = rl.get_time()
      self._ball_breath_phase = 0.0
    self._ball_anim_prev_active = ball_anim_active

    if ball_anim_active:
      if ui_state.params.get_bool("SmoothWarning"):
        self._ball_alpha_filter.update(self._ball_breath_target_alpha())
      else:
        self._ball_alpha_filter.update(self._ball_blink_target_alpha())
    else:
      self._ball_alpha_filter.update(1.0)
    # Hide the base confidence dot whenever the orange alert bar is active,
    # so blink dim phases never reveal the dot underneath.
    self._right_dot_presence_filter.update(0.0 if alert_active else 1.0)

  def _render(self, _):
    content_rect = rl.Rectangle(
      self.rect.x + self.rect.width - SIDE_PANEL_WIDTH,
      self.rect.y,
      SIDE_PANEL_WIDTH,
      self.rect.height,
    )

    status_dot_radius = 24
    dot_height = (1 - self._confidence_filter.x) * (content_rect.height - 2 * status_dot_radius) + status_dot_radius
    dot_height = self._rect.y + dot_height
    top_dot_color, bottom_dot_color = self._get_regular_dot_colors()
    alert_active = self._is_alert_bar_active()
    left_blindspot_active = self._is_left_blindspot_alert_active()

    # Right confidence dot with a small out-animation during left-side blindspot alerts.
    right_presence = self._right_dot_presence_filter.x
    right_exit_offset = (1.0 - right_presence) * RIGHT_DOT_EXIT_PIXELS
    # Keep motion relative to the shifted onroad content: when left blindspot mode is active,
    # move the dot right by the same content shift while it animates out.
    if left_blindspot_active:
      right_exit_offset += (1.0 - right_presence) * LEFT_BLINDSPOT_SHIFT
    right_dot_center_x = content_rect.x + content_rect.width - status_dot_radius + right_exit_offset
    if right_presence > 0.01:
      ball_alpha = self._ball_alpha_filter.x if not alert_active else 1.0
      draw_circle_gradient(right_dot_center_x, dot_height, status_dot_radius,
                           self._with_alpha(top_dot_color, right_presence * ball_alpha),
                           self._with_alpha(bottom_dot_color, right_presence * ball_alpha))

    morph = self._alert_morph_filter.x
    if morph < 0.01:
      return

    orange_top = rl.Color(255, 200, 0, 255)
    orange_bottom = rl.Color(255, 115, 0, 255)
    red_top = rl.Color(255, 0, 21, 255)
    red_bottom = rl.Color(255, 0, 89, 255)
    alert_color_mix = self._alert_color_mix_filter.x
    alert_top = self._lerp_color(orange_top, red_top, alert_color_mix)
    alert_bottom = self._lerp_color(orange_bottom, red_bottom, alert_color_mix)

    dot_d = status_dot_radius * 2
    if left_blindspot_active:
      dot_x = self.rect.x
      bar_x = self.rect.x
    else:
      dot_x = content_rect.x + content_rect.width - dot_d
      bar_x = content_rect.x + content_rect.width - ORANGE_BAR_WIDTH
    dot_y = dot_height - status_dot_radius
    bar_w = ORANGE_BAR_WIDTH
    bar_h = content_rect.height
    bar_y = content_rect.y

    x = self._lerp(dot_x, bar_x, morph)
    y = self._lerp(dot_y, bar_y, morph)
    w = self._lerp(dot_d, bar_w, morph)
    h = self._lerp(dot_d, bar_h, morph)
    top_color = self._lerp_color(top_dot_color, alert_top, morph)
    bottom_color = self._lerp_color(bottom_dot_color, alert_bottom, morph)
    blink_alpha = self._blink_alpha_filter.x if alert_active else 1.0
    top_color = rl.Color(top_color.r, top_color.g, top_color.b, int(top_color.a * blink_alpha))
    bottom_color = rl.Color(bottom_color.r, bottom_color.g, bottom_color.b, int(bottom_color.a * blink_alpha))
    self._draw_gradient_capsule(x, y, w, h, top_color, bottom_color)
