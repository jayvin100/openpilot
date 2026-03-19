from __future__ import annotations

import socket
import time

import numpy as np
import pyray as rl
import qrcode

from openpilot.common.api import CONNECT_HOST, CONNECT_HOST_DISPLAY
from openpilot.common.swaglog import cloudlog
from openpilot.selfdrive.ui.ui_state import device
from openpilot.system.ui.lib.application import FontWeight, MousePos, gui_app
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.lib.wifi_manager import WifiManager
from openpilot.system.ui.lib.wrap_text import wrap_text
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.widgets.nav_widget import NavWidget
from openpilot.system.ui.widgets.scroller import NavScroller
from openpilot.selfdrive.ui.mici.widgets.button import BigButton

WEBRTC_PORT = 5001


def _get_local_ip() -> str:
  try:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
      s.connect(("8.8.8.8", 80))
      return s.getsockname()[0]
  except Exception:
    return ""


class _BodyPairingBase:
  """Shared QR generation, URL building, and IP refresh logic for body pairing screens."""

  QR_REFRESH_INTERVAL = 300  # seconds

  def _init_pairing_state(self):
    self._connect_qr_texture: rl.Texture | None = None
    self._connect_qr_last_gen = float('-inf')

    self._ip_address = ""
    self._last_ip_check = float('-inf')

    self._wifi_manager = WifiManager()
    self._wifi_manager.set_active(False)

  def _refresh_ip(self):
    now = time.monotonic()
    if now - self._last_ip_check > 10:
      self._ip_address = _get_local_ip()
      self._last_ip_check = now

  def _get_connect_url(self) -> str:
    ip = self._ip_address or "unknown"
    return f"{CONNECT_HOST}/?body={ip}:{WEBRTC_PORT}"

  def _generate_qr(self, data: str, invert: bool = False) -> rl.Texture | None:
    try:
      qr = qrcode.QRCode(version=1, error_correction=qrcode.constants.ERROR_CORRECT_L, box_size=10, border=4)
      qr.add_data(data)
      qr.make(fit=True)

      fill = "white" if invert else "black"
      bg = "black" if invert else "white"
      pil_img = qr.make_image(fill_color=fill, back_color=bg).convert('RGBA')
      img_array = np.array(pil_img, dtype=np.uint8)

      rl_image = rl.Image()
      rl_image.data = rl.ffi.cast("void *", img_array.ctypes.data)
      rl_image.width = pil_img.width
      rl_image.height = pil_img.height
      rl_image.mipmaps = 1
      rl_image.format = rl.PixelFormat.PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
      return rl.load_texture_from_image(rl_image)
    except Exception:
      cloudlog.exception("QR code generation failed")
      return None

  def _refresh_connect_qr(self):
    now = time.monotonic()
    if now - self._connect_qr_last_gen >= self.QR_REFRESH_INTERVAL:
      if self._connect_qr_texture and self._connect_qr_texture.id != 0:
        rl.unload_texture(self._connect_qr_texture)
      self._connect_qr_texture = self._generate_qr(self._get_connect_url(), invert=True)
      self._connect_qr_last_gen = now

  def _draw_qr_texture(self, texture: rl.Texture, x: float, y: float, size: int):
    source = rl.Rectangle(0, 0, texture.width, texture.height)
    dest = rl.Rectangle(x, y, size, size)
    rl.draw_texture_pro(texture, source, dest, rl.Vector2(0, 0), 0, rl.WHITE)

  def _cleanup_textures(self):
    if self._connect_qr_texture and self._connect_qr_texture.id != 0:
      rl.unload_texture(self._connect_qr_texture)


# -- Big (normal) layout: two side-by-side cards --

CARD_BG = rl.Color(40, 40, 40, 255)
CARD_RADIUS = 0.05
TEXT_COLOR = rl.WHITE
TEXT_DIM = rl.Color(255, 255, 255, 150)
SCREEN_BG = rl.Color(20, 20, 20, 255)

CLOSE_BTN_SIZE = 125
CLOSE_ICON_SIZE = 50
CLOSE_BTN_COLOR = rl.Color(41, 41, 41, 255)
CLOSE_BTN_PRESSED = rl.Color(59, 59, 59, 255)


class BodyPairingScreen(_BodyPairingBase, Widget):
  """Connection screen for comma body: shows one-time connection QR and manual connect info."""

  def __init__(self):
    Widget.__init__(self)
    self._init_pairing_state()
    self._close_icon = gui_app.texture("icons/close2.png", CLOSE_ICON_SIZE, CLOSE_ICON_SIZE)
    self._close_btn_rect = rl.Rectangle(0, 0, 0, 0)

    self._font = gui_app.font(FontWeight.NORMAL)
    self._font_bold = gui_app.font(FontWeight.BOLD)
    self._font_semi = gui_app.font(FontWeight.SEMI_BOLD)

  def _update_state(self):
    self._refresh_ip()

  def _handle_mouse_release(self, mouse_pos: MousePos) -> None:
    if rl.check_collision_point_rec(mouse_pos, self._close_btn_rect):
      gui_app.pop_widget()

  def _render(self, rect: rl.Rectangle):
    rl.clear_background(SCREEN_BG)

    margin = 40

    # Close button (settings-style)
    close_btn_rect = rl.Rectangle(rect.x + margin, rect.y + 40, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE)
    self._close_btn_rect = close_btn_rect

    pressed = (rl.is_mouse_button_down(rl.MouseButton.MOUSE_BUTTON_LEFT) and
               rl.check_collision_point_rec(rl.get_mouse_position(), close_btn_rect))
    close_color = CLOSE_BTN_PRESSED if pressed else CLOSE_BTN_COLOR
    rl.draw_rectangle_rounded(close_btn_rect, 1.0, 20, close_color)

    icon_color = rl.Color(220, 220, 220, 255) if pressed else rl.Color(255, 255, 255, 255)
    icon_dest = rl.Rectangle(
      close_btn_rect.x + (close_btn_rect.width - self._close_icon.width) / 2,
      close_btn_rect.y + (close_btn_rect.height - self._close_icon.height) / 2,
      self._close_icon.width,
      self._close_icon.height,
    )
    rl.draw_texture_pro(
      self._close_icon,
      rl.Rectangle(0, 0, self._close_icon.width, self._close_icon.height),
      icon_dest,
      rl.Vector2(0, 0),
      0,
      icon_color,
    )

    ssid = self._wifi_manager.connected_ssid
    wifi_text = f"WiFi: {ssid}" if ssid else "WiFi: not connected"
    wifi_size = measure_text_cached(self._font_semi, wifi_text, 38)
    wifi_x = rect.x + (rect.width - wifi_size.x) / 2
    wifi_y = rect.y + 60 + (CLOSE_BTN_SIZE - wifi_size.y) / 2
    rl.draw_text_ex(self._font_semi, wifi_text, rl.Vector2(wifi_x, wifi_y), 38, 0, TEXT_COLOR)

    cards_y = rect.y + 60 + CLOSE_BTN_SIZE + 20
    cards_h = rect.height - (cards_y - rect.y) - margin
    card_w = rect.width - 2 * margin

    connect_rect = rl.Rectangle(rect.x + margin, cards_y, card_w, cards_h)
    self._render_connect_card(connect_rect)

  def _render_connect_card(self, rect: rl.Rectangle):
    rl.draw_rectangle_rounded(rect, CARD_RADIUS, 20, CARD_BG)

    pad = 40
    x = rect.x + pad
    w = rect.width - 2 * pad
    h = rect.height - 2 * pad

    # QR on the right side
    self._refresh_connect_qr()
    qr_size = min(int(h), int(w * 0.4))
    qr_x = x + w - qr_size
    qr_y = rect.y + (rect.height - qr_size) / 2
    if qr_size > 0 and self._connect_qr_texture:
      self._draw_qr_texture(self._connect_qr_texture, qr_x, qr_y, qr_size)

    # Text on the left side, vertically centered
    text_w = int(w - qr_size - pad)

    title = "Connect to this COMMA Body"
    desc = "connection options:"
    wrapped = wrap_text(self._font, desc, 34, text_w)

    # Layout constants
    circle_r = 18
    text_offset = circle_r * 2 + 14
    line_h = 42
    option_line_h = 48  # taller for circle rows

    # Calculate total text block height
    # title + gap + desc lines + gap + option1 + or + option2 + 3 detail lines
    total_text_h = (48 + 22 + len(wrapped) * line_h + 20 +
                    option_line_h + line_h + option_line_h + line_h * 3)
    text_y = rect.y + (rect.height - total_text_h) / 2

    rl.draw_text_ex(self._font_bold, title, rl.Vector2(x, text_y), 48, 0, TEXT_COLOR)
    text_y += 70

    for line in wrapped:
      rl.draw_text_ex(self._font, line, rl.Vector2(x, text_y), 34, 0, TEXT_DIM)
      text_y += line_h

    text_y += 20

    # Option 1
    circle_cy = text_y + option_line_h / 2
    circle_cx = x + circle_r
    rl.draw_circle_lines(int(circle_cx), int(circle_cy), circle_r, TEXT_COLOR)
    num_size = measure_text_cached(self._font_semi, "1", 28)
    rl.draw_text_ex(self._font_semi, "1", rl.Vector2(circle_cx - num_size.x / 2, circle_cy - num_size.y / 2), 28, 0, TEXT_COLOR)
    opt1_text = "scan qr code"
    opt1_size = measure_text_cached(self._font_semi, opt1_text, 34)
    rl.draw_text_ex(self._font_semi, opt1_text, rl.Vector2(x + text_offset, circle_cy - opt1_size.y / 2), 34, 0, TEXT_COLOR)
    text_y += option_line_h

    # OR
    or_size = measure_text_cached(self._font, "OR", 30)
    rl.draw_text_ex(self._font, "OR", rl.Vector2(x + text_offset + (opt1_size.x - or_size.x) / 2, text_y + (line_h - or_size.y) / 2), 30, 0, TEXT_DIM)
    text_y += line_h

    # Option 2
    circle_cy = text_y + option_line_h / 2
    rl.draw_circle_lines(int(circle_cx), int(circle_cy), circle_r, TEXT_COLOR)
    num_size = measure_text_cached(self._font_semi, "2", 28)
    rl.draw_text_ex(self._font_semi, "2", rl.Vector2(circle_cx - num_size.x / 2, circle_cy - num_size.y / 2), 28, 0, TEXT_COLOR)
    opt2_text = "connect manually"
    opt2_size = measure_text_cached(self._font_semi, opt2_text, 34)
    rl.draw_text_ex(self._font_semi, opt2_text, rl.Vector2(x + text_offset, circle_cy - opt2_size.y / 2), 34, 0, TEXT_COLOR)
    text_y += option_line_h

    rl.draw_text_ex(self._font, f"visit {CONNECT_HOST_DISPLAY}", rl.Vector2(x + text_offset, text_y), 34, 0, TEXT_COLOR)
    text_y += line_h

    rl.draw_text_ex(self._font, "click add new device", rl.Vector2(x + text_offset, text_y), 34, 0, TEXT_COLOR)
    text_y += line_h

    ip_text = f"enter IP: {self._ip_address}" if self._ip_address else "IP: not connected"
    rl.draw_text_ex(self._font, ip_text, rl.Vector2(x + text_offset, text_y), 34, 0, TEXT_COLOR)

  def __del__(self):
    self._cleanup_textures()


# -- MICI (small screen) layout: NavScroller with BigButtons leading to detail panels --

MICI_TEXT_COLOR = rl.Color(255, 255, 255, int(255 * 0.9))
MICI_TEXT_DIM = rl.Color(0xAA, 0xAA, 0xAA, 255)
MICI_LABEL_SIZE = 24
MICI_DESC_SIZE = 26
MICI_LINE_HEIGHT = 32


class OneTimeConnectPanel(_BodyPairingBase, NavWidget):
  """Detail panel showing one-time connection QR code with manual IP/port info. Swipe down to go back."""

  def __init__(self):
    NavWidget.__init__(self)
    self._init_pairing_state()

    self._font = gui_app.font(FontWeight.ROMAN)
    self._font_bold = gui_app.font(FontWeight.BOLD)
    self._font_semi = gui_app.font(FontWeight.SEMI_BOLD)
    self._font_medium = gui_app.font(FontWeight.MEDIUM)

  def show_event(self):
    super().show_event()
    device.set_override_interactive_timeout(300)

  def hide_event(self):
    super().hide_event()
    device.set_override_interactive_timeout(None)

  def _update_state(self):
    super()._update_state()
    self._refresh_ip()

  def _render(self, rect: rl.Rectangle):
    self._refresh_connect_qr()

    # QR code scaled to fit, left-aligned
    qr_size = int(rect.height)
    if self._connect_qr_texture and qr_size > 0:
      self._draw_qr_texture(self._connect_qr_texture, rect.x + 8, rect.y, qr_size)

    # Label and manual info to the right
    label_x = rect.x + 8 + qr_size
    y = rect.y + 18

    prefix = "scan qr code "
    rl.draw_text_ex(self._font, prefix, rl.Vector2(label_x, y), MICI_LABEL_SIZE, 0, MICI_TEXT_COLOR)
    or_x = label_x + rl.measure_text_ex(self._font, prefix, MICI_LABEL_SIZE, 0).x
    rl.draw_text_ex(self._font_semi, "OR", rl.Vector2(or_x + 20, y), MICI_LABEL_SIZE, 0, MICI_TEXT_COLOR)
    y += MICI_LABEL_SIZE + 6

    rl.draw_text_ex(self._font, "connect manually:", rl.Vector2(label_x, y), MICI_LABEL_SIZE, 0, MICI_TEXT_COLOR)
    y += MICI_LABEL_SIZE + 12

    step_font_size = MICI_LABEL_SIZE - 3
    circle_r = step_font_size // 2 + 1
    circle_d = circle_r * 2
    gap = 8
    num_font_size = step_font_size - 4
    row_h = circle_d
    for i, step in enumerate((CONNECT_HOST_DISPLAY, "add new device", "connect to comma body"), 1):
      cx = int(label_x + circle_r)
      cy = int(y + row_h // 2)
      rl.draw_circle_lines(cx, cy, circle_r, MICI_TEXT_COLOR)
      num = str(i)
      num_size = rl.measure_text_ex(self._font_semi, num, num_font_size, 0)
      rl.draw_text_ex(self._font_semi, num, rl.Vector2(int(cx - num_size.x / 2), int(cy - num_size.y / 2 - 1)), num_font_size, 0, MICI_TEXT_COLOR)
      step_size = rl.measure_text_ex(self._font, step, step_font_size, 0)
      rl.draw_text_ex(self._font, step, rl.Vector2(label_x + circle_d + gap, int(cy - step_size.y / 2 - 3)), step_font_size, 0, MICI_TEXT_COLOR)
      y += row_h + 6

    y += 10

    ip_text = f"IP: {self._ip_address}" if self._ip_address else "IP: not connected"
    rl.draw_text_ex(self._font, ip_text, rl.Vector2(label_x, y), 22, 0, MICI_TEXT_COLOR)
    y += 24

    ssid = self._wifi_manager.connected_ssid
    if ssid and len(ssid) > 20:
      ssid = ssid[:17] + "..."
    wifi_text = f"WiFi: {ssid}" if ssid else "wifi: not connected"
    rl.draw_text_ex(self._font, wifi_text, rl.Vector2(label_x, y), 22, 0, MICI_TEXT_COLOR)

  def __del__(self):
    self._cleanup_textures()


class _PairingBigButton(BigButton):
  def _get_label_font_size(self):
    return 64


class MiciBodyPairingScreen(NavScroller):
  """MICI pairing screen: NavScroller with BigButtons for each pairing method."""

  def __init__(self):
    super().__init__()

    self._wifi_manager = WifiManager()
    self._wifi_manager.set_active(False)

    self._connect_panel = OneTimeConnectPanel()
    connect_btn = _PairingBigButton("connect", "",
                                    gui_app.texture("icons_mici/settings/network/wifi_strength_full.png", 76, 56))
    connect_btn.set_click_callback(lambda: gui_app.push_widget(self._connect_panel))

    self._connect_btn = connect_btn
    self._scroller.add_widgets([connect_btn])

  def show_event(self):
    super().show_event()
    device.set_override_interactive_timeout(60)

  def hide_event(self):
    super().hide_event()
    device.set_override_interactive_timeout(None)

  def _update_state(self):
    super()._update_state()
    ssid = self._wifi_manager.connected_ssid
    self._connect_btn.set_value(ssid or "")


if __name__ == "__main__":
  gui_app.init_window("Body Pairing")
  if gui_app.big_ui():
    screen = BodyPairingScreen()
  else:
    screen = MiciBodyPairingScreen()
  gui_app.push_widget(screen)
  try:
    for _ in gui_app.render():
      pass
  finally:
    del screen
