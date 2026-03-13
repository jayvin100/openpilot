from __future__ import annotations

import socket
import time

import numpy as np
import pyray as rl
import qrcode

from openpilot.common.api import Api
from openpilot.common.params import Params
from openpilot.common.swaglog import cloudlog
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import FontWeight, gui_app
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.lib.wrap_text import wrap_text
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.widgets.button import IconButton

WEBRTC_PORT = 5001
CARD_BG = rl.Color(40, 40, 40, 255)
CARD_RADIUS = 0.05
TEXT_COLOR = rl.WHITE
TEXT_DIM = rl.Color(255, 255, 255, 150)
SCREEN_BG = rl.Color(20, 20, 20, 255)


def _get_local_ip() -> str:
  try:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
      s.connect(("8.8.8.8", 80))
      return s.getsockname()[0]
  except Exception:
    return ""


class BodyPairingScreen(Widget):
  """Two-panel pairing screen for comma body: account pairing (left) and one-time connection (right)."""

  QR_REFRESH_INTERVAL = 300  # seconds

  def __init__(self):
    super().__init__()
    self._params = Params()
    self._close_btn = self._child(IconButton(gui_app.texture("icons/close.png", 80, 80)))
    self._close_btn.set_click_callback(gui_app.pop_widget)

    # QR code state for account pairing (left card)
    self._pair_qr_texture: rl.Texture | None = None
    self._pair_qr_last_gen = float('-inf')

    # QR code state for one-time connection (right card)
    self._connect_qr_texture: rl.Texture | None = None
    self._connect_qr_last_gen = float('-inf')

    # Cached IP
    self._ip_address = ""
    self._last_ip_check = float('-inf')

    self._font = gui_app.font(FontWeight.NORMAL)
    self._font_bold = gui_app.font(FontWeight.BOLD)
    self._font_semi = gui_app.font(FontWeight.SEMI_BOLD)

  def _update_state(self):
    # Refresh IP periodically
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
    return f"http://{ip}:{WEBRTC_PORT}"

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

  def _render(self, rect: rl.Rectangle):
    rl.clear_background(SCREEN_BG)

    margin = 40
    gap = 30
    close_size = 60

    # Close button top-left
    close_rect = rl.Rectangle(rect.x + margin, rect.y + margin, close_size, close_size)
    self._close_btn.render(close_rect)

    # Cards area below close button
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

    # Title
    title = "PAIR with COMMA BODY"
    rl.draw_text_ex(self._font_bold, title, rl.Vector2(x, y), 48, 0, TEXT_COLOR)
    y += 70

    if is_paired:
      self._render_paired_state(x, y, w)
    else:
      self._render_unpaired_state(x, y, w, rect)

  def _render_paired_state(self, x: float, y: float, w: float):
    # Checkmark circle
    circle_x = x + w / 2
    circle_y = y + 80
    rl.draw_circle(int(circle_x), int(circle_y), 40, rl.Color(75, 180, 75, 255))
    check_font = gui_app.font(FontWeight.BOLD)
    check_size = measure_text_cached(check_font, "✓", 50)
    rl.draw_text_ex(check_font, "✓", rl.Vector2(circle_x - check_size.x / 2, circle_y - check_size.y / 2), 50, 0, rl.WHITE)

    y = circle_y + 70

    paired_text = "This comma body is paired"
    text_size = measure_text_cached(self._font_semi, paired_text, 38)
    rl.draw_text_ex(self._font_semi, paired_text, rl.Vector2(x + (w - text_size.x) / 2, y), 38, 0, TEXT_COLOR)
    y += 50

    sub_text = "Manage your device at connect.comma.ai"
    sub_size = measure_text_cached(self._font, sub_text, 30)
    rl.draw_text_ex(self._font, sub_text, rl.Vector2(x + (w - sub_size.x) / 2, y), 30, 0, TEXT_DIM)

  def _render_unpaired_state(self, x: float, y: float, w: float, card_rect: rl.Rectangle):
    # Description
    desc = "With connect prime, you can control your comma body from anywhere in the world!"
    wrapped = wrap_text(self._font, desc, 34, int(w))
    for line in wrapped:
      rl.draw_text_ex(self._font, line, rl.Vector2(x, y), 34, 0, TEXT_DIM)
      y += 42

    y += 20

    # QR code centered in remaining space
    self._refresh_pair_qr()
    remaining_h = card_rect.y + card_rect.height - y - 40
    qr_size = min(int(w * 0.6), int(remaining_h))
    if qr_size > 0 and self._pair_qr_texture:
      qr_x = x + (w - qr_size) / 2
      source = rl.Rectangle(0, 0, self._pair_qr_texture.width, self._pair_qr_texture.height)
      dest = rl.Rectangle(qr_x, y, qr_size, qr_size)
      rl.draw_texture_pro(self._pair_qr_texture, source, dest, rl.Vector2(0, 0), 0, rl.WHITE)

  def _render_connect_card(self, rect: rl.Rectangle):
    rl.draw_rectangle_rounded(rect, CARD_RADIUS, 20, CARD_BG)

    pad = 40
    x = rect.x + pad
    y = rect.y + pad
    w = rect.width - 2 * pad

    # Title
    title = "Connect to COMMA BODY"
    rl.draw_text_ex(self._font_bold, title, rl.Vector2(x, y), 48, 0, TEXT_COLOR)
    y += 70

    # Description
    desc = "You can connect to this comma one time by scanning the QR code on your connect app"
    wrapped = wrap_text(self._font, desc, 34, int(w))
    for line in wrapped:
      rl.draw_text_ex(self._font, line, rl.Vector2(x, y), 34, 0, TEXT_DIM)
      y += 42

    y += 20

    # QR code
    self._refresh_connect_qr()
    qr_avail_h = rect.y + rect.height - y - 180  # leave room for manual info
    qr_size = min(int(w * 0.5), int(qr_avail_h))
    if qr_size > 0 and self._connect_qr_texture:
      qr_x = x + (w - qr_size) / 2
      source = rl.Rectangle(0, 0, self._connect_qr_texture.width, self._connect_qr_texture.height)
      dest = rl.Rectangle(qr_x, y, qr_size, qr_size)
      rl.draw_texture_pro(self._connect_qr_texture, source, dest, rl.Vector2(0, 0), 0, rl.WHITE)
      y += qr_size + 20

    # Manual connection info at bottom
    bottom_y = rect.y + rect.height - pad - 120
    y = max(y, bottom_y)

    rl.draw_text_ex(self._font, "Can't use the QR code? Input the following manually:", rl.Vector2(x, y), 30, 0, TEXT_DIM)
    y += 40

    ip_text = f"IP: {self._ip_address}" if self._ip_address else "IP: not connected"
    rl.draw_text_ex(self._font_semi, ip_text, rl.Vector2(x, y), 34, 0, TEXT_COLOR)
    y += 42
    rl.draw_text_ex(self._font_semi, f"Port: {WEBRTC_PORT}", rl.Vector2(x, y), 34, 0, TEXT_COLOR)

  def __del__(self):
    for tex in (self._pair_qr_texture, self._connect_qr_texture):
      if tex and tex.id != 0:
        rl.unload_texture(tex)


if __name__ == "__main__":
  gui_app.init_window("Body Pairing")
  screen = BodyPairingScreen()
  gui_app.push_widget(screen)
  try:
    for _ in gui_app.render():
      pass
  finally:
    del screen
