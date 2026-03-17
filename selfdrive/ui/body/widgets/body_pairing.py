from __future__ import annotations

import socket
import time

import numpy as np
import pyray as rl
import qrcode

from openpilot.common.api import Api
from openpilot.common.params import Params
from openpilot.common.swaglog import cloudlog
from openpilot.selfdrive.ui.ui_state import device, ui_state
from openpilot.system.ui.lib.application import FontWeight, gui_app
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.lib.wrap_text import wrap_text
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.widgets.button import IconButton
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
    self._params = Params()

    self._pair_qr_texture: rl.Texture | None = None
    self._pair_qr_last_gen = float('-inf')

    self._connect_qr_texture: rl.Texture | None = None
    self._connect_qr_last_gen = float('-inf')

    self._ip_address = ""
    self._last_ip_check = float('-inf')

  def _refresh_ip(self):
    now = time.monotonic()
    if now - self._last_ip_check > 10:
      self._ip_address = _get_local_ip()
      self._last_ip_check = now

  def _get_pairing_url(self) -> str:
    try:
      dongle_id = self._params.get("DongleId") or ""
      token = Api(dongle_id).get_token({'pair': True})
    except Exception:
      cloudlog.exception("Failed to get pairing token")
      token = ""
    return f"https://connect.comma.ai/?pair={token}"

  def _get_connect_url(self) -> str:
    ip = self._ip_address or "unknown"
    return f"https://connect.comma.ai/?body={ip}:{WEBRTC_PORT}"

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

  def _refresh_pair_qr(self):
    now = time.monotonic()
    if now - self._pair_qr_last_gen >= self.QR_REFRESH_INTERVAL:
      if self._pair_qr_texture and self._pair_qr_texture.id != 0:
        rl.unload_texture(self._pair_qr_texture)
      self._pair_qr_texture = self._generate_qr(self._get_pairing_url(), invert=True)
      self._pair_qr_last_gen = now

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
    for tex in (self._pair_qr_texture, self._connect_qr_texture):
      if tex and tex.id != 0:
        rl.unload_texture(tex)


# -- Big (normal) layout: two side-by-side cards --

CARD_BG = rl.Color(40, 40, 40, 255)
CARD_RADIUS = 0.05
TEXT_COLOR = rl.WHITE
TEXT_DIM = rl.Color(255, 255, 255, 150)
SCREEN_BG = rl.Color(20, 20, 20, 255)


class BodyPairingScreen(_BodyPairingBase, Widget):
  """Two-panel pairing screen for comma body: account pairing (left) and one-time connection (right)."""

  def __init__(self):
    Widget.__init__(self)
    self._init_pairing_state()
    self._close_btn = self._child(IconButton(gui_app.texture("icons/close.png", 80, 80)))
    self._close_btn.set_click_callback(gui_app.pop_widget)

    self._font = gui_app.font(FontWeight.NORMAL)
    self._font_bold = gui_app.font(FontWeight.BOLD)
    self._font_semi = gui_app.font(FontWeight.SEMI_BOLD)

  def _update_state(self):
    self._refresh_ip()

  def _render(self, rect: rl.Rectangle):
    rl.clear_background(SCREEN_BG)

    margin = 40
    gap = 30
    close_size = 60

    close_rect = rl.Rectangle(rect.x + margin, rect.y + margin, close_size, close_size)
    self._close_btn.render(close_rect)

    cards_y = rect.y + margin + close_size + 20
    cards_h = rect.height - margin - close_size - 20 - margin
    card_w = (rect.width - 2 * margin - gap) / 2

    left_rect = rl.Rectangle(rect.x + margin, cards_y, card_w, cards_h)
    right_rect = rl.Rectangle(rect.x + margin + card_w + gap, cards_y, card_w, cards_h)

    is_paired = ui_state.prime_state.is_paired()

    self._render_pair_card(left_rect, is_paired)
    self._render_connect_card(right_rect)

  def _render_pair_card(self, rect: rl.Rectangle, is_paired: bool):
    rl.draw_rectangle_rounded(rect, CARD_RADIUS, 20, CARD_BG)

    pad = 40
    x = rect.x + pad
    y = rect.y + pad
    w = rect.width - 2 * pad

    title = "PAIR with COMMA BODY"
    rl.draw_text_ex(self._font_bold, title, rl.Vector2(x, y), 48, 0, TEXT_COLOR)
    y += 70

    if is_paired:
      self._render_paired_state(x, y, w)
    else:
      self._render_unpaired_state(x, y, w, rect)

  def _render_paired_state(self, x: float, y: float, w: float):
    circle_x = x + w / 2
    circle_y = y + 80
    rl.draw_circle(int(circle_x), int(circle_y), 40, rl.Color(75, 180, 75, 255))
    check_font = gui_app.font(FontWeight.BOLD)
    check_size = measure_text_cached(check_font, "\u2713", 50)
    rl.draw_text_ex(check_font, "\u2713", rl.Vector2(circle_x - check_size.x / 2, circle_y - check_size.y / 2), 50, 0, rl.WHITE)

    y = circle_y + 70

    paired_text = "This comma body is paired"
    text_size = measure_text_cached(self._font_semi, paired_text, 38)
    rl.draw_text_ex(self._font_semi, paired_text, rl.Vector2(x + (w - text_size.x) / 2, y), 38, 0, TEXT_COLOR)
    y += 50

    sub_text = "Manage your device at connect.comma.ai"
    sub_size = measure_text_cached(self._font, sub_text, 30)
    rl.draw_text_ex(self._font, sub_text, rl.Vector2(x + (w - sub_size.x) / 2, y), 30, 0, TEXT_DIM)

  def _render_unpaired_state(self, x: float, y: float, w: float, card_rect: rl.Rectangle):
    desc = "With connect prime, you can control your comma body from anywhere in the world!"
    wrapped = wrap_text(self._font, desc, 34, int(w))
    for line in wrapped:
      rl.draw_text_ex(self._font, line, rl.Vector2(x, y), 34, 0, TEXT_DIM)
      y += 42

    y += 20

    self._refresh_pair_qr()
    remaining_h = card_rect.y + card_rect.height - y - 40
    qr_size = min(int(w * 0.6), int(remaining_h))
    if qr_size > 0 and self._pair_qr_texture:
      qr_x = x + (w - qr_size) / 2
      self._draw_qr_texture(self._pair_qr_texture, qr_x, y, qr_size)

  def _render_connect_card(self, rect: rl.Rectangle):
    rl.draw_rectangle_rounded(rect, CARD_RADIUS, 20, CARD_BG)

    pad = 40
    x = rect.x + pad
    y = rect.y + pad
    w = rect.width - 2 * pad

    title = "Connect to COMMA BODY"
    rl.draw_text_ex(self._font_bold, title, rl.Vector2(x, y), 48, 0, TEXT_COLOR)
    y += 70

    desc = "You can connect to this comma one time by scanning the QR code on your connect app"
    wrapped = wrap_text(self._font, desc, 34, int(w))
    for line in wrapped:
      rl.draw_text_ex(self._font, line, rl.Vector2(x, y), 34, 0, TEXT_DIM)
      y += 42

    y += 20

    self._refresh_connect_qr()
    qr_avail_h = rect.y + rect.height - y - 180
    qr_size = min(int(w * 0.5), int(qr_avail_h))
    if qr_size > 0 and self._connect_qr_texture:
      qr_x = x + (w - qr_size) / 2
      self._draw_qr_texture(self._connect_qr_texture, qr_x, y, qr_size)
      y += qr_size + 20

    bottom_y = rect.y + rect.height - pad - 120
    y = max(y, bottom_y)

    rl.draw_text_ex(self._font, "Can't use the QR code? Input the following manually:", rl.Vector2(x, y), 30, 0, TEXT_DIM)
    y += 40

    ip_text = f"IP: {self._ip_address}" if self._ip_address else "IP: not connected"
    rl.draw_text_ex(self._font_semi, ip_text, rl.Vector2(x, y), 34, 0, TEXT_COLOR)
    y += 42
    rl.draw_text_ex(self._font_semi, f"Port: {WEBRTC_PORT}", rl.Vector2(x, y), 34, 0, TEXT_COLOR)

  def __del__(self):
    self._cleanup_textures()


# -- MICI (small screen) layout: NavScroller with BigButtons leading to detail panels --

MICI_TEXT_COLOR = rl.Color(255, 255, 255, int(255 * 0.9))
MICI_TEXT_DIM = rl.Color(0xAA, 0xAA, 0xAA, 255)
MICI_LABEL_SIZE = 28
MICI_DESC_SIZE = 26
MICI_LINE_HEIGHT = 32


class _AccountPairingPanel(_BodyPairingBase, NavWidget):
  """Detail panel showing account pairing QR code. Swipe down to go back."""

  def __init__(self):
    NavWidget.__init__(self)
    self._init_pairing_state()

    self._font = gui_app.font(FontWeight.ROMAN)
    self._font_bold = gui_app.font(FontWeight.BOLD)
    self._font_semi = gui_app.font(FontWeight.SEMI_BOLD)

  def show_event(self):
    super().show_event()
    device.set_override_interactive_timeout(300)

  def hide_event(self):
    super().hide_event()
    device.set_override_interactive_timeout(None)

  def _update_state(self):
    super()._update_state()
    if ui_state.prime_state.is_paired() and not self.is_dismissing:
      self.dismiss()

  def _render(self, rect: rl.Rectangle):
    self._refresh_pair_qr()

    # QR code scaled to fit, left-aligned
    qr_size = int(rect.height)
    if self._pair_qr_texture and qr_size > 0:
      self._draw_qr_texture(self._pair_qr_texture, rect.x + 8, rect.y, qr_size)

    # Label to the right of the QR
    label_x = rect.x + 8 + qr_size + 24
    label_w = rect.width - label_x

    title = "pair with\ncomma connect"
    title_font = gui_app.font(FontWeight.BOLD)
    wrapped = wrap_text(title_font, title, 48, int(label_w))
    y = rect.y + 16
    for line in wrapped:
      rl.draw_text_ex(title_font, line, rl.Vector2(label_x, y), 48, 0, MICI_TEXT_COLOR)
      y += 52

  def __del__(self):
    self._cleanup_textures()


class _OneTimeConnectPanel(_BodyPairingBase, NavWidget):
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
    label_x = rect.x + 8 + qr_size + 24
    y = rect.y + 16

    title_font = gui_app.font(FontWeight.BOLD)
    rl.draw_text_ex(title_font, "one-time", rl.Vector2(label_x, y), 48, 0, MICI_TEXT_COLOR)
    y += 40
    rl.draw_text_ex(title_font, "connection", rl.Vector2(label_x, y), 48, 0, MICI_TEXT_COLOR)
    y += 80

    ip_text = f"IP: {self._ip_address}" if self._ip_address else "IP: not connected"
    rl.draw_text_ex(self._font_semi, ip_text, rl.Vector2(label_x, y), MICI_LABEL_SIZE, 0, MICI_TEXT_COLOR)
    y += MICI_LABEL_SIZE + 6
    rl.draw_text_ex(self._font_semi, f"Port: {WEBRTC_PORT}", rl.Vector2(label_x, y), MICI_LABEL_SIZE, 0, MICI_TEXT_COLOR)

  def __del__(self):
    self._cleanup_textures()


class _PairingBigButton(BigButton):
  def _get_label_font_size(self):
    return 64


class MiciBodyPairingScreen(NavScroller):
  """MICI pairing screen: NavScroller with BigButtons for each pairing method."""

  def __init__(self):
    super().__init__()

    self._pair_panel = _AccountPairingPanel()
    pair_btn = _PairingBigButton("pair account", "connect.comma.ai",
                                 gui_app.texture("icons_mici/settings/comma_icon.png", 33, 60))
    pair_btn.set_click_callback(lambda: gui_app.push_widget(self._pair_panel))

    self._connect_panel = _OneTimeConnectPanel()
    connect_btn = _PairingBigButton("1-time connect", "",
                                    gui_app.texture("icons_mici/settings/network/wifi_strength_full.png", 76, 56))
    connect_btn.set_click_callback(lambda: gui_app.push_widget(self._connect_panel))

    self._pair_btn = pair_btn
    self._scroller.add_widgets([connect_btn, pair_btn])

  def show_event(self):
    super().show_event()
    device.set_override_interactive_timeout(60)

  def hide_event(self):
    super().hide_event()
    device.set_override_interactive_timeout(None)

  def _update_state(self):
    super()._update_state()
    if ui_state.prime_state.is_paired():
      self._pair_btn.set_text("paired")
      self._pair_btn.set_value("connected")
    else:
      self._pair_btn.set_text("pair account")
      self._pair_btn.set_value("connect.comma.ai")


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
