import time
import math
from enum import StrEnum
from typing import NamedTuple
import pyray as rl
import random
import string
from dataclasses import dataclass
from cereal import messaging, log, car
from openpilot.selfdrive.ui.ui_state import ui_state, UIStatus
from openpilot.common.filter_simple import BounceFilter, FirstOrderFilter
from openpilot.system.hardware import TICI
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.widgets.label import UnifiedLabel

AlertSize = log.SelfdriveState.AlertSize
AlertStatus = log.SelfdriveState.AlertStatus

ALERT_MARGIN = 18

ALERT_FONT_SMALL = 66 - 50
ALERT_FONT_BIG = 88 - 40

SELFDRIVE_STATE_TIMEOUT = 5  # Seconds
SELFDRIVE_UNRESPONSIVE_TIMEOUT = 10  # Seconds

TURN_SIGNAL_BLINK_PERIOD = 1 / (80 / 60)  # Mazda heartbeat turn signal BPM
CRITICAL_BG_TRANSITION_RC = 0.16
CRITICAL_PULSE_RC = 0.04
CRITICAL_PULSE_START_DELAY = 0.19
RED_BLINK_ON = 0.30
RED_BLINK_OFF = 0.30
RED_BLINK_CYCLE = RED_BLINK_ON + RED_BLINK_OFF
RED_BG_BREATH_CYCLE = RED_BLINK_CYCLE * 4.0  # 2x slower again
CRITICAL_ICON_LEFT_INSET = 24
CRITICAL_ICON_BOTTOM_INSET = 28
CRITICAL_ICON_BLINK_GROW_DELAY = 0.375
CRITICAL_ICON_BLINK_DIM_ALPHA = 0.15
CRITICAL_ICON_BLINK_ALPHA_RC = 0.035
ORANGE_ICON_BLINK_GROW_DELAY = 0.375
ORANGE_ICON_BLINK_OFF_1 = 0.15
ORANGE_ICON_BLINK_ON_1 = 0.15
ORANGE_ICON_BLINK_OFF_2 = 0.15
ORANGE_ICON_BLINK_ON_2 = 0.75
ORANGE_ICON_BLINK_CYCLE = ORANGE_ICON_BLINK_OFF_1 + ORANGE_ICON_BLINK_ON_1 + ORANGE_ICON_BLINK_OFF_2 + ORANGE_ICON_BLINK_ON_2
ORANGE_ICON_BLINK_DIM_ALPHA = 0.15
ORANGE_ICON_BLINK_ALPHA_RC = 0.035

DEBUG = False


class IconSide(StrEnum):
  left = 'left'
  right = 'right'


class CriticalAlertStyle(StrEnum):
  non_disengaging = 'non_disengaging'
  disengaging_or_no_entry = 'disengaging_or_no_entry'


class IconLayout(NamedTuple):
  texture: rl.Texture
  side: IconSide
  margin_x: int
  margin_y: int


class AlertLayout(NamedTuple):
  text_rect: rl.Rectangle
  icon: IconLayout | None


@dataclass
class Alert:
  text1: str = ""
  text2: str = ""
  size: int = 0
  status: int = 0
  visual_alert: int = car.CarControl.HUDControl.VisualAlert.none
  alert_type: str = ""


# Pre-defined alert instances
ALERT_STARTUP_PENDING = Alert(
  text1="openpilot Unavailable",
  text2="Waiting to start",
  size=AlertSize.mid,
  status=AlertStatus.normal,
)

ALERT_CRITICAL_TIMEOUT = Alert(
  text1="TAKE CONTROL IMMEDIATELY",
  text2="System Unresponsive",
  size=AlertSize.full,
  status=AlertStatus.critical,
)

ALERT_CRITICAL_REBOOT = Alert(
  text1="System Unresponsive",
  text2="Reboot Device",
  size=AlertSize.full,
  status=AlertStatus.critical,
)

DISENGAGING_OR_NO_ENTRY_EVENT_TYPES = frozenset((
  'noEntry',
  'userDisable',
  'softDisable',
  'immediateDisable',
))


class AlertRenderer(Widget):
  def __init__(self):
    super().__init__()

    self._alert_text1_label = UnifiedLabel(text="", font_size=ALERT_FONT_BIG, font_weight=FontWeight.DISPLAY, line_height=0.86,
                                           letter_spacing=-0.02)
    self._alert_text2_label = UnifiedLabel(text="", font_size=ALERT_FONT_SMALL, font_weight=FontWeight.ROMAN, line_height=0.86,
                                           letter_spacing=0.025)

    self._prev_alert: Alert | None = None
    self._last_visible_alert: Alert | None = None
    self._text_gen_time = 0
    self._alert_text2_gen = ''

    # animation filters
    # TODO: use 0.1 but with proper alert height calculation
    self._alert_y_filter = BounceFilter(0, 0.1, 1 / gui_app.target_fps)
    self._alpha_filter = FirstOrderFilter(0, 0.05, 1 / gui_app.target_fps)
    self._content_alpha_filter = FirstOrderFilter(0, 0.05, 1 / gui_app.target_fps)
    self._foreground_reenter_key: tuple[str, str, str, int, int, CriticalAlertStyle | None] | None = None

    self._turn_signal_timer = 0.0
    self._turn_signal_alpha_filter = FirstOrderFilter(0.0, 0.3, 1 / gui_app.target_fps)
    self._last_icon_side: IconSide | None = None
    self._critical_bg_mix_filter = FirstOrderFilter(0.0, CRITICAL_BG_TRANSITION_RC, 1 / gui_app.target_fps)
    self._critical_pulse_filter = FirstOrderFilter(1.0, CRITICAL_PULSE_RC, 1 / gui_app.target_fps)
    self._critical_alert_started_at = -1.0
    self._critical_pulse_phase = 0.0
    self._critical_prev_active = False
    self._critical_icon_alpha_filter = FirstOrderFilter(1.0, CRITICAL_ICON_BLINK_ALPHA_RC, 1 / gui_app.target_fps)
    self._critical_icon_started_at = -1.0
    self._critical_icon_prev_active = False
    self._orange_icon_alpha_filter = FirstOrderFilter(1.0, ORANGE_ICON_BLINK_ALPHA_RC, 1 / gui_app.target_fps)
    self._orange_icon_started_at = -1.0
    self._orange_icon_prev_active = False

    self._load_icons()

  def _load_icons(self):
    self._txt_turn_signal_left = gui_app.texture('icons_mici/onroad/turn_signal_left.png', 104, 96)
    self._txt_turn_signal_right = gui_app.texture('icons_mici/onroad/turn_signal_left.png', 104, 96, flip_x=True)
    self._txt_blind_spot_left = gui_app.texture('icons_mici/onroad/blind_spot_left.png', 134, 150)
    self._txt_blind_spot_right = gui_app.texture('icons_mici/onroad/blind_spot_left.png', 134, 150, flip_x=True)
    self._txt_red_warning = gui_app.texture('icons_mici/setup/red_warning.png', 106, 96)
    self._txt_orange_warning = gui_app.texture('icons_mici/setup/warning.png', 106, 96)

  def get_alert(self, sm: messaging.SubMaster) -> Alert | None:
    """Generate the current alert based on selfdrive state."""
    ss = sm['selfdriveState']

    # Check if selfdriveState messages have stopped arriving
    if not sm.updated['selfdriveState']:
      recv_frame = sm.recv_frame['selfdriveState']
      time_since_onroad = time.monotonic() - ui_state.started_time

      # 1. Never received selfdriveState since going onroad
      waiting_for_startup = recv_frame < ui_state.started_frame
      if waiting_for_startup and time_since_onroad > 5:
        return ALERT_STARTUP_PENDING

      # 2. Lost communication with selfdriveState after receiving it
      if TICI and not waiting_for_startup:
        ss_missing = time.monotonic() - sm.recv_time['selfdriveState']
        if ss_missing > SELFDRIVE_STATE_TIMEOUT:
          if ss.enabled and (ss_missing - SELFDRIVE_STATE_TIMEOUT) < SELFDRIVE_UNRESPONSIVE_TIMEOUT:
            return ALERT_CRITICAL_TIMEOUT
          return ALERT_CRITICAL_REBOOT

    # No alert if size is none
    if ss.alertSize == 0:
      return None

    # Return current alert
    ret = Alert(text1=ss.alertText1, text2=ss.alertText2, size=ss.alertSize.raw, status=ss.alertStatus.raw,
                visual_alert=ss.alertHudVisual, alert_type=ss.alertType)
    self._prev_alert = ret
    return ret

  def _critical_alert_style(self, alert: Alert) -> CriticalAlertStyle | None:
    if alert.status != AlertStatus.critical:
      return None

    event_type = self._event_type(alert)
    if event_type in DISENGAGING_OR_NO_ENTRY_EVENT_TYPES:
      return CriticalAlertStyle.disengaging_or_no_entry
    return CriticalAlertStyle.non_disengaging

  @staticmethod
  def _use_disengaged_warning_style(alert: Alert) -> bool:
    return ui_state.status == UIStatus.DISENGAGED and alert.status in (AlertStatus.userPrompt, AlertStatus.critical)

  @staticmethod
  def _event_name(alert: Alert) -> str:
    return alert.alert_type.split('/')[0] if alert.alert_type else ''

  @staticmethod
  def _event_type(alert: Alert) -> str:
    return alert.alert_type.split('/')[1] if '/' in alert.alert_type else ''

  @staticmethod
  def _icon_layout_for_texture(texture: rl.Texture, side: IconSide, margin_x: int, margin_y: int) -> IconLayout:
    return IconLayout(texture, side, margin_x, margin_y)

  @staticmethod
  def _alert_priority(alert: Alert) -> tuple[int, int]:
    return int(alert.status), int(alert.size)

  def _is_escalation(self, alert: Alert) -> bool:
    if self._last_visible_alert is None:
      return False
    return self._alert_priority(alert) > self._alert_priority(self._last_visible_alert)

  def will_render(self) -> tuple[Alert | None, bool]:
    alert = self.get_alert(ui_state.sm)
    return alert or self._prev_alert, alert is None

  def _icon_helper(self, alert: Alert) -> AlertLayout:
    if self._use_disengaged_warning_style(alert):
      self._turn_signal_timer = 0.0
      self._last_icon_side = None
      return AlertLayout(
        rl.Rectangle(
          self._rect.x + ALERT_MARGIN,
          self._alert_y_filter.x,
          self._rect.width - ALERT_MARGIN,
          self._rect.height,
        ),
        None,
      )

    icon_layout = None
    event_name = self._event_name(alert)

    if event_name == 'preLaneChangeLeft':
      icon_layout = self._icon_layout_for_texture(self._txt_turn_signal_left, IconSide.left, 2, 5)

    elif event_name == 'preLaneChangeRight':
      icon_layout = self._icon_layout_for_texture(self._txt_turn_signal_right, IconSide.right, 2, 5)

    elif event_name == 'laneChange':
      icon_side = self._last_icon_side or IconSide.left
      txt_icon = self._txt_turn_signal_left if icon_side == IconSide.left else self._txt_turn_signal_right
      icon_layout = self._icon_layout_for_texture(txt_icon, icon_side, 2, 5)

    elif event_name == 'laneChangeBlocked':
      CS = ui_state.sm['carState']
      if CS.leftBlinker:
        icon_side = IconSide.left
      elif CS.rightBlinker:
        icon_side = IconSide.right
      else:
        icon_side = self._last_icon_side or IconSide.left
      txt_icon = self._txt_blind_spot_left if icon_side == IconSide.left else self._txt_blind_spot_right
      icon_layout = self._icon_layout_for_texture(txt_icon, icon_side, 8, 0)

    else:
      self._turn_signal_timer = 0.0

    self._last_icon_side = icon_layout.side if icon_layout is not None else None

    # create text rect based on icon presence
    text_x = self._rect.x + ALERT_MARGIN
    text_width = self._rect.width - ALERT_MARGIN
    if icon_layout is not None and icon_layout.side == IconSide.left:
      text_x = self._rect.x + self._txt_turn_signal_right.width
      text_width = self._rect.width - ALERT_MARGIN - self._txt_turn_signal_right.width
    elif icon_layout is not None and icon_layout.side == IconSide.right:
      text_x = self._rect.x + ALERT_MARGIN
      text_width = self._rect.width - ALERT_MARGIN - self._txt_turn_signal_right.width

    text_rect = rl.Rectangle(
      text_x,
      self._alert_y_filter.x,
      text_width,
      self._rect.height,
    )
    return AlertLayout(text_rect, icon_layout)

  def _render(self, rect: rl.Rectangle) -> bool:
    alert = self.get_alert(ui_state.sm)

    # Foreground text/icon should animate in as a fresh notification for
    # prominent warnings while background transition behavior stays unchanged.
    foreground_reenter_key: tuple[str, str, str, int, int, CriticalAlertStyle | None] | None = None
    critical_style = self._critical_alert_style(alert) if alert is not None else None
    if alert is not None and alert.status in (AlertStatus.userPrompt, AlertStatus.critical):
      foreground_reenter_key = (alert.text1, alert.text2, alert.alert_type, alert.size, int(alert.status), critical_style)
    if foreground_reenter_key is not None and foreground_reenter_key != self._foreground_reenter_key:
      self._alert_y_filter.x = self._rect.y - 50
      self._content_alpha_filter.x = 0.0
    self._foreground_reenter_key = foreground_reenter_key

    # Animate fade and slide in/out
    self._alert_y_filter.update(self._rect.y - 50 if alert is None else self._rect.y)
    if alert is None:
      self._alpha_filter.update(0)
    elif self._is_escalation(alert):
      # Generic escalation behavior: keep background fully in, no re-entry fade.
      self._alpha_filter.x = 1.0
    else:
      self._alpha_filter.update(1)
    self._content_alpha_filter.update(0 if alert is None else 1)

    if alert is None:
      # If still animating out, keep the previous alert
      if self._alpha_filter.x > 0.01 and self._prev_alert is not None:
        alert = self._prev_alert
      else:
        self._prev_alert = None
        self._last_visible_alert = None
        return False

    self._draw_background(alert, critical_style)
    self._draw_warning_icon(alert)

    alert_layout = self._icon_helper(alert)
    self._draw_text(alert, alert_layout)
    self._draw_icons(alert_layout)
    self._last_visible_alert = alert

    return True

  def _draw_warning_icon(self, alert: Alert) -> None:
    show_critical_icon = alert.status == AlertStatus.critical
    show_disengaged_orange_icon = (ui_state.status == UIStatus.DISENGAGED and alert.status == AlertStatus.userPrompt)
    if not (show_critical_icon or show_disengaged_orange_icon):
      self._orange_icon_alpha_filter.x = 1.0
      self._orange_icon_started_at = -1.0
      self._orange_icon_prev_active = False
      return

    if show_disengaged_orange_icon:
      icon_texture = self._txt_orange_warning
      blink_alpha = self._orange_icon_blink_alpha()
    else:
      icon_texture = self._txt_red_warning
      blink_alpha = self._critical_icon_blink_alpha()

    icon_x = int(self._rect.x + CRITICAL_ICON_LEFT_INSET)
    # Match text slide-in movement while preserving icon's defined resting position.
    y_offset = self._alert_y_filter.x - self._rect.y
    icon_y = int(self._rect.y + self._rect.height - CRITICAL_ICON_BOTTOM_INSET - icon_texture.height + y_offset)
    icon_color = rl.Color(255, 255, 255, int(255 * blink_alpha * self._content_alpha_filter.x))
    rl.draw_texture_ex(icon_texture, rl.Vector2(icon_x, icon_y), 0.0, 1.0, icon_color)

  def _critical_icon_blink_alpha(self) -> float:
    # In disengaged mode, blink the red warning icon with the same cadence
    # used by the confidence ball red alert animation.
    icon_blink_active = (ui_state.status == UIStatus.DISENGAGED)

    if icon_blink_active and not self._critical_icon_prev_active:
      self._critical_icon_started_at = rl.get_time()

    if icon_blink_active and self._critical_icon_started_at >= 0:
      elapsed = rl.get_time() - self._critical_icon_started_at
      if elapsed < CRITICAL_ICON_BLINK_GROW_DELAY:
        target_alpha = 1.0
      else:
        phase = (elapsed - CRITICAL_ICON_BLINK_GROW_DELAY) % RED_BLINK_CYCLE
        target_alpha = 1.0 if phase < RED_BLINK_ON else CRITICAL_ICON_BLINK_DIM_ALPHA
      alpha = self._critical_icon_alpha_filter.update(target_alpha)
    else:
      self._critical_icon_alpha_filter.x = 1.0
      self._critical_icon_started_at = -1.0
      alpha = 1.0

    self._critical_icon_prev_active = icon_blink_active
    return alpha

  def _orange_icon_blink_alpha(self) -> float:
    icon_blink_active = (ui_state.status == UIStatus.DISENGAGED)

    if icon_blink_active and not self._orange_icon_prev_active:
      self._orange_icon_started_at = rl.get_time()

    if icon_blink_active and self._orange_icon_started_at >= 0:
      elapsed = rl.get_time() - self._orange_icon_started_at
      if elapsed < ORANGE_ICON_BLINK_GROW_DELAY:
        target_alpha = 1.0
      else:
        phase = (elapsed - ORANGE_ICON_BLINK_GROW_DELAY) % ORANGE_ICON_BLINK_CYCLE
        if phase < ORANGE_ICON_BLINK_OFF_1:
          target_alpha = ORANGE_ICON_BLINK_DIM_ALPHA
        elif phase < ORANGE_ICON_BLINK_OFF_1 + ORANGE_ICON_BLINK_ON_1:
          target_alpha = 1.0
        elif phase < ORANGE_ICON_BLINK_OFF_1 + ORANGE_ICON_BLINK_ON_1 + ORANGE_ICON_BLINK_OFF_2:
          target_alpha = ORANGE_ICON_BLINK_DIM_ALPHA
        else:
          target_alpha = 1.0
      alpha = self._orange_icon_alpha_filter.update(target_alpha)
    else:
      self._orange_icon_alpha_filter.x = 1.0
      self._orange_icon_started_at = -1.0
      alpha = 1.0

    self._orange_icon_prev_active = icon_blink_active
    return alpha

  def _draw_icons(self, alert_layout: AlertLayout) -> None:
    if alert_layout.icon is None:
      return

    if time.monotonic() - self._turn_signal_timer > TURN_SIGNAL_BLINK_PERIOD:
      self._turn_signal_timer = time.monotonic()
      self._turn_signal_alpha_filter.x = 255 * 2
    else:
      self._turn_signal_alpha_filter.update(255 * 0.2)

    if alert_layout.icon.side == IconSide.left:
      pos_x = int(self._rect.x + alert_layout.icon.margin_x)
    else:
      pos_x = int(self._rect.x + self._rect.width - alert_layout.icon.margin_x - alert_layout.icon.texture.width)

    if alert_layout.icon.texture not in (self._txt_turn_signal_left, self._txt_turn_signal_right):
      icon_alpha = 255
    else:
      icon_alpha = int(min(self._turn_signal_alpha_filter.x, 255))

    rl.draw_texture_ex(alert_layout.icon.texture, rl.Vector2(pos_x, self._rect.y + alert_layout.icon.margin_y), 0.0, 1.0,
                       rl.Color(255, 255, 255, int(icon_alpha * self._content_alpha_filter.x)))

  def _draw_background(self, alert: Alert, critical_style: CriticalAlertStyle | None) -> None:
    small_alert_height = round(self._rect.height * 0.583) # 140px at mici height
    medium_alert_height = round(self._rect.height * 0.833) # 200px at mici height

    # alert_type format is "EventName/eventType" (e.g., "preLaneChangeLeft/warning")
    event_name = self._event_name(alert)

    if event_name == 'preLaneChangeLeft':
      bg_height = small_alert_height
    elif event_name == 'preLaneChangeRight':
      bg_height = small_alert_height
    elif event_name == 'laneChange':
      bg_height = small_alert_height
    elif event_name == 'laneChangeBlocked':
      bg_height = medium_alert_height
    else:
      bg_height = int(self._rect.height)

    use_red_critical_bg = (critical_style == CriticalAlertStyle.non_disengaging and
                           not self._use_disengaged_warning_style(alert))
    if use_red_critical_bg:
      critical_mix = self._critical_bg_mix_filter.update(1.0)
    else:
      # Avoid red carryover on the next non-critical alert.
      self._critical_bg_mix_filter.x = 0.0
      critical_mix = 0.0
    alpha = int(255 * self._alpha_filter.x)

    # Base notification gradient: black top -> transparent black bottom.
    base_top = rl.Color(0, 0, 0, alpha)
    base_bottom = rl.Color(0, 0, 0, 0)
    # Critical notification gradient target: red top -> black bottom, both fully opaque.
    critical_top = rl.Color(255, 0, 21, alpha)
    critical_bottom = rl.Color(0, 0, 0, alpha)

    top = rl.Color(
      int(base_top.r + (critical_top.r - base_top.r) * critical_mix),
      int(base_top.g + (critical_top.g - base_top.g) * critical_mix),
      int(base_top.b + (critical_top.b - base_top.b) * critical_mix),
      int(base_top.a + (critical_top.a - base_top.a) * critical_mix),
    )
    bottom = rl.Color(
      int(base_bottom.r + (critical_bottom.r - base_bottom.r) * critical_mix),
      int(base_bottom.g + (critical_bottom.g - base_bottom.g) * critical_mix),
      int(base_bottom.b + (critical_bottom.b - base_bottom.b) * critical_mix),
      int(base_bottom.a + (critical_bottom.a - base_bottom.a) * critical_mix),
    )

    # Critical background breathes with a slower cadence.
    if use_red_critical_bg and critical_mix > 0.0:
      if not self._critical_prev_active:
        self._critical_alert_started_at = time.monotonic()
        self._critical_pulse_phase = 0.0

      elapsed = time.monotonic() - self._critical_alert_started_at
      if elapsed < CRITICAL_PULSE_START_DELAY:
        # Let red gradient settle in before pulse cadence starts.
        target_pulse = 1.0
      else:
        dt = 1 / gui_app.target_fps
        self._critical_pulse_phase = (self._critical_pulse_phase + dt / RED_BG_BREATH_CYCLE) % 1.0
        target_pulse = 0.5 * (1.0 + math.cos(self._critical_pulse_phase * 2.0 * math.pi))

      pulse = self._critical_pulse_filter.update(target_pulse)
      top = rl.Color(int(top.r * pulse), int(top.g * pulse), int(top.b * pulse), top.a)
    else:
      self._critical_pulse_filter.x = 1.0

    self._critical_prev_active = use_red_critical_bg

    rl.draw_rectangle_gradient_v(int(self._rect.x), int(self._rect.y), int(self._rect.width), int(bg_height), top, bottom)

  def _draw_text(self, alert: Alert, alert_layout: AlertLayout) -> None:
    icon_side = alert_layout.icon.side if alert_layout.icon is not None else None

    # TODO: hack
    alert_text1 = alert.text1.lower().replace('calibrating: ', 'calibrating:\n')
    can_draw_second_line = False
    # TODO: there should be a common way to determine font size based on text length to maximize rect
    if len(alert_text1) <= 12:
      can_draw_second_line = True
      font_size = 92 - 10
    elif len(alert_text1) <= 16:
      can_draw_second_line = True
      font_size = 70
    else:
      font_size = 64 - 10

    if icon_side is not None:
      font_size -= 10

    color = rl.Color(255, 255, 255, int(255 * 0.9 * self._content_alpha_filter.x))

    text1_y_offset = 11 if font_size >= 70 else 4
    text_rect1 = rl.Rectangle(
      alert_layout.text_rect.x,
      alert_layout.text_rect.y - text1_y_offset,
      alert_layout.text_rect.width,
      alert_layout.text_rect.height,
    )
    self._alert_text1_label.set_text(alert_text1)
    self._alert_text1_label.set_text_color(color)
    self._alert_text1_label.set_font_size(font_size)
    self._alert_text1_label.set_alignment(rl.GuiTextAlignment.TEXT_ALIGN_LEFT if icon_side != 'left' else rl.GuiTextAlignment.TEXT_ALIGN_RIGHT)
    self._alert_text1_label.render(text_rect1)

    alert_text2 = alert.text2.lower()

    # randomize chars and length for testing
    if DEBUG:
      if time.monotonic() - self._text_gen_time > 0.5:
        self._alert_text2_gen = ''.join(random.choices(string.ascii_lowercase + ' ', k=random.randint(0, 40)))
        self._text_gen_time = time.monotonic()
      alert_text2 = self._alert_text2_gen or alert_text2

    if alert.status == AlertStatus.normal and can_draw_second_line and alert_text2:
      last_line_h = self._alert_text1_label.rect.y + self._alert_text1_label.get_content_height(int(alert_layout.text_rect.width))
      last_line_h -= 4
      if len(alert_text2) > 18:
        small_font_size = 36
      elif len(alert_text2) > 24:
        small_font_size = 32
      else:
        small_font_size = 40
      text_rect2 = rl.Rectangle(
        alert_layout.text_rect.x,
        last_line_h,
        alert_layout.text_rect.width,
        alert_layout.text_rect.height - last_line_h
      )
      color = rl.Color(255, 255, 255, int(255 * 0.65 * self._content_alpha_filter.x))

      self._alert_text2_label.set_text(alert_text2)
      self._alert_text2_label.set_text_color(color)
      self._alert_text2_label.set_font_size(small_font_size)
      self._alert_text2_label.set_alignment(rl.GuiTextAlignment.TEXT_ALIGN_LEFT if icon_side != 'left' else rl.GuiTextAlignment.TEXT_ALIGN_RIGHT)
      self._alert_text2_label.render(text_rect2)
