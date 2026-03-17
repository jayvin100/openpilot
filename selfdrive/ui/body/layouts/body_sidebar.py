import pyray as rl
import time
from dataclasses import dataclass
from collections.abc import Callable
from cereal import log
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight, MousePos, FONT_SCALE
from openpilot.system.ui.lib.multilang import tr, tr_noop
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget

BODY_SIDEBAR_HEIGHT = 200
METRIC_HEIGHT = 117
METRIC_WIDTH = 220
METRIC_MARGIN = 20
FONT_SIZE = 30
BATTERY_FONT_SIZE = 26

ThermalStatus = log.DeviceState.ThermalStatus
NetworkType = log.DeviceState.NetworkType


class Colors:
  WHITE = rl.WHITE
  WHITE_DIM = rl.Color(255, 255, 255, 85)
  GRAY = rl.Color(84, 84, 84, 255)
  GOOD = rl.WHITE
  WARNING = rl.Color(218, 202, 37, 255)
  DANGER = rl.Color(201, 34, 49, 255)
  METRIC_BORDER = rl.Color(255, 255, 255, 85)
  BUTTON_NORMAL = rl.WHITE
  BUTTON_PRESSED = rl.Color(255, 255, 255, 166)
  BATTERY_GREEN = rl.Color(0, 200, 0, 255)
  BATTERY_LOW = rl.Color(201, 34, 49, 255)


NETWORK_TYPES = {
  NetworkType.none: tr_noop("--"),
  NetworkType.wifi: tr_noop("Wi-Fi"),
  NetworkType.ethernet: tr_noop("ETH"),
  NetworkType.cell2G: tr_noop("2G"),
  NetworkType.cell3G: tr_noop("3G"),
  NetworkType.cell4G: tr_noop("LTE"),
  NetworkType.cell5G: tr_noop("5G"),
}


@dataclass(slots=True)
class MetricData:
  label: str
  value: str
  color: rl.Color

  def update(self, label: str, value: str, color: rl.Color):
    self.label = label
    self.value = value
    self.color = color


class BodySidebar(Widget):
  """A top-dropping sidebar for the comma body, containing the same info as the regular sidebar."""

  def __init__(self):
    super().__init__()
    self._net_type = NETWORK_TYPES.get(NetworkType.none)
    self._net_strength = 0

    self._temp_status = MetricData(tr_noop("TEMP"), tr_noop("GOOD"), Colors.GOOD)
    self._panda_status = MetricData(tr_noop("VEHICLE"), tr_noop("ONLINE"), Colors.GOOD)
    self._connect_status = MetricData(tr_noop("CONNECT"), tr_noop("OFFLINE"), Colors.WARNING)
    self._battery_percent = 0.0
    self._battery_charging = False
    self._recording_audio = False

    self._settings_img = gui_app.texture("images/button_settings.png", 200, 117)
    self._flag_img = gui_app.texture("images/button_flag.png", 180, 180)
    self._mic_img = gui_app.texture("icons/microphone.png", 30, 30)
    self._mic_indicator_rect = rl.Rectangle(0, 0, 0, 0)
    self._font_regular = gui_app.font(FontWeight.NORMAL)
    self._font_bold = gui_app.font(FontWeight.SEMI_BOLD)
    self._font_extra_bold = gui_app.font(FontWeight.BOLD)

    # Callbacks
    self._on_settings_click: Callable | None = None
    self._on_flag_click: Callable | None = None
    self._open_settings_callback: Callable | None = None

  def set_callbacks(self, on_settings: Callable | None = None, on_flag: Callable | None = None,
                    open_settings: Callable | None = None):
    self._on_settings_click = on_settings
    self._on_flag_click = on_flag
    self._open_settings_callback = open_settings

  def _render(self, rect: rl.Rectangle):
    rl.draw_rectangle_rec(rect, rl.BLACK)

    self._draw_settings_button(rect)
    self._draw_network_indicator(rect)
    self._draw_metrics(rect)
    self._draw_battery_indicator(rect)
    self._draw_mic_indicator(rect)

  def _update_state(self):
    sm = ui_state.sm
    self._update_battery_status()

    if not sm.updated['deviceState']:
      return

    device_state = sm['deviceState']
    self._recording_audio = ui_state.recording_audio
    self._update_network_status(device_state)
    self._update_temperature_status(device_state)
    self._update_connection_status(device_state)
    self._update_panda_status()

  def _update_network_status(self, device_state):
    self._net_type = NETWORK_TYPES.get(device_state.networkType.raw, tr_noop("Unknown"))
    strength = device_state.networkStrength
    self._net_strength = max(0, min(5, strength.raw + 1)) if strength.raw > 0 else 0

  def _update_temperature_status(self, device_state):
    thermal_status = device_state.thermalStatus
    if thermal_status == ThermalStatus.green:
      self._temp_status.update(tr_noop("TEMP"), tr_noop("GOOD"), Colors.GOOD)
    elif thermal_status == ThermalStatus.yellow:
      self._temp_status.update(tr_noop("TEMP"), tr_noop("OK"), Colors.WARNING)
    else:
      self._temp_status.update(tr_noop("TEMP"), tr_noop("HIGH"), Colors.DANGER)

  def _update_connection_status(self, device_state):
    last_ping = device_state.lastAthenaPingTime
    if last_ping == 0:
      self._connect_status.update(tr_noop("CONNECT"), tr_noop("OFFLINE"), Colors.WARNING)
    elif time.monotonic_ns() - last_ping < 80_000_000_000:
      self._connect_status.update(tr_noop("CONNECT"), tr_noop("ONLINE"), Colors.GOOD)
    else:
      self._connect_status.update(tr_noop("CONNECT"), tr_noop("ERROR"), Colors.DANGER)

  def _update_panda_status(self):
    if ui_state.panda_type == log.PandaState.PandaType.unknown:
      self._panda_status.update(tr_noop("NO"), tr_noop("PANDA"), Colors.DANGER)
    else:
      self._panda_status.update(tr_noop("VEHICLE"), tr_noop("ONLINE"), Colors.GOOD)

  def _update_battery_status(self):
    sm = ui_state.sm
    if sm.updated['carState']:
      car_state = sm['carState']
      self._battery_percent = max(0.0, min(1.0, car_state.fuelGauge))
      self._battery_charging = car_state.charging

  def _handle_mouse_release(self, mouse_pos: MousePos):
    # Settings button (top-left)
    settings_rect = rl.Rectangle(self._rect.x + 30, self._rect.y + 30, 200, 117)
    if rl.check_collision_point_rec(mouse_pos, settings_rect):
      if self._on_settings_click:
        self._on_settings_click()
      return

    # Mic indicator
    if self._recording_audio and rl.check_collision_point_rec(mouse_pos, self._mic_indicator_rect):
      if self._open_settings_callback:
        self._open_settings_callback()

  def _draw_settings_button(self, rect: rl.Rectangle):
    mouse_pos = rl.get_mouse_position()
    mouse_down = self.is_pressed and rl.is_mouse_button_down(rl.MouseButton.MOUSE_BUTTON_LEFT)

    btn_x = int(rect.x + 30)
    btn_y = int(rect.y + 30)
    settings_rect = rl.Rectangle(btn_x, btn_y, 200, 117)
    settings_down = mouse_down and rl.check_collision_point_rec(mouse_pos, settings_rect)
    tint = Colors.BUTTON_PRESSED if settings_down else Colors.BUTTON_NORMAL
    rl.draw_texture(self._settings_img, btn_x, btn_y, tint)

  def _draw_battery_indicator(self, rect: rl.Rectangle):
    # Battery icon dimensions
    batt_w = 50
    batt_h = 28
    tip_w = 5
    tip_h = 12
    batt_x = int(rect.x + rect.width - batt_w - tip_w - 30)
    batt_y = int(rect.y + 30 + (METRIC_HEIGHT - batt_h) / 2)

    # Choose fill color based on level
    pct = self._battery_percent
    if pct <= 0.2:
      fill_color = Colors.BATTERY_LOW
    elif self._battery_charging:
      fill_color = Colors.BATTERY_GREEN
    else:
      fill_color = Colors.WHITE

    # Battery outline
    rl.draw_rectangle_rounded_lines_ex(rl.Rectangle(batt_x, batt_y, batt_w, batt_h), 0.2, 6, 2, Colors.WHITE)

    # Battery tip (positive terminal)
    tip_x = batt_x + batt_w
    tip_y = batt_y + (batt_h - tip_h) / 2
    rl.draw_rectangle_rounded(rl.Rectangle(tip_x, tip_y, tip_w, tip_h), 0.3, 4, Colors.WHITE)

    # Fill level
    fill_margin = 4
    fill_max_w = batt_w - 2 * fill_margin
    fill_w = max(0, int(fill_max_w * pct))
    if fill_w > 0:
      rl.draw_rectangle_rounded(
        rl.Rectangle(batt_x + fill_margin, batt_y + fill_margin, fill_w, batt_h - 2 * fill_margin),
        0.15, 4, fill_color
      )

    # Percentage text
    pct_text = f"{int(pct * 100)}%"
    if self._battery_charging:
      pct_text = pct_text
    pct_size = measure_text_cached(self._font_bold, pct_text, BATTERY_FONT_SIZE)
    pct_pos = rl.Vector2(batt_x + (batt_w - pct_size.x) / 2, batt_y + batt_h + 6)
    rl.draw_text_ex(self._font_bold, pct_text, pct_pos, BATTERY_FONT_SIZE, 0, Colors.WHITE)

  # def _draw_flag_button(self, rect: rl.Rectangle):
  #   if not ui_state.started:
  #     return

  #   mouse_pos = rl.get_mouse_position()
  #   mouse_down = self.is_pressed and rl.is_mouse_button_down(rl.MouseButton.MOUSE_BUTTON_LEFT)

  #   btn_x = int(rect.x + rect.width - 100)
  #   btn_y = int(rect.y + 30)
  #   flag_rect = rl.Rectangle(btn_x, btn_y, 60, 60)
  #   flag_pressed = mouse_down and rl.check_collision_point_rec(mouse_pos, flag_rect)
  #   tint = Colors.BUTTON_PRESSED if flag_pressed else Colors.BUTTON_NORMAL
  #   rl.draw_texture(self._flag_img, btn_x, btn_y, tint)

  def _draw_network_indicator(self, rect: rl.Rectangle):
    # Draw network dots horizontally, positioned after the settings button
    x_start = rect.x + 260
    y_pos = rect.y + 40
    dot_size = 20
    dot_spacing = 28

    for i in range(5):
      color = Colors.WHITE if i < self._net_strength else Colors.GRAY
      x = int(x_start + i * dot_spacing + dot_size // 2)
      y = int(y_pos + dot_size // 2)
      rl.draw_circle(x, y, dot_size // 2, color)

    # Network type text below dots
    text_pos = rl.Vector2(x_start, y_pos + dot_size + 8)
    rl.draw_text_ex(self._font_regular, tr(self._net_type), text_pos, FONT_SIZE, 0, Colors.WHITE)

  def _draw_metrics(self, rect: rl.Rectangle):
    metrics = [self._temp_status, self._panda_status, self._connect_status]
    # Center the 3 metrics in the middle of the bar
    total_width = len(metrics) * METRIC_WIDTH + (len(metrics) - 1) * METRIC_MARGIN
    start_x = rect.x + (rect.width - total_width) / 2

    y = rect.y + 30

    for i, metric in enumerate(metrics):
      x = start_x + i * (METRIC_WIDTH + METRIC_MARGIN)
      self._draw_metric(metric, x, y)

  def _draw_metric(self, metric: MetricData, x: float, y: float):
    r = rl.Rectangle(x, y, METRIC_WIDTH, METRIC_HEIGHT)

    # Colored top edge (clipped rounded rect)
    rl.begin_scissor_mode(int(x), int(y + 4), int(METRIC_WIDTH), 18)
    rl.draw_rectangle_rounded(rl.Rectangle(x + 4, y + 4, METRIC_WIDTH - 8, 100), 0.3, 10, metric.color)
    rl.end_scissor_mode()

    rl.draw_rectangle_rounded_lines_ex(r, 0.3, 10, 2, Colors.METRIC_BORDER)

    # Center label and value below the top edge
    text_y = y + 22 + ((METRIC_HEIGHT - 22) / 2 - 2 * FONT_SIZE * FONT_SCALE)
    for label in (metric.label, metric.value):
      text = tr(label)
      size = measure_text_cached(self._font_bold, text, FONT_SIZE)
      text_y += size.y
      text_pos = rl.Vector2(
        x + (METRIC_WIDTH - size.x) / 2,
        text_y
      )
      rl.draw_text_ex(self._font_bold, text, text_pos, FONT_SIZE, 0, Colors.WHITE)

  def _draw_mic_indicator(self, rect: rl.Rectangle):
    if not self._recording_audio:
      return

    mouse_pos = rl.get_mouse_position()
    mouse_down = self.is_pressed and rl.is_mouse_button_down(rl.MouseButton.MOUSE_BUTTON_LEFT)

    self._mic_indicator_rect = rl.Rectangle(rect.x + rect.width - 180, rect.y + rect.height - 50, 75, 40)
    mic_pressed = mouse_down and rl.check_collision_point_rec(mouse_pos, self._mic_indicator_rect)
    bg_color = rl.Color(Colors.DANGER.r, Colors.DANGER.g, Colors.DANGER.b, int(255 * 0.65)) if mic_pressed else Colors.DANGER

    rl.draw_rectangle_rounded(self._mic_indicator_rect, 1, 10, bg_color)
    rl.draw_texture(self._mic_img, int(self._mic_indicator_rect.x + (self._mic_indicator_rect.width - self._mic_img.width) / 2),
                    int(self._mic_indicator_rect.y + (self._mic_indicator_rect.height - self._mic_img.height) / 2), Colors.WHITE)
