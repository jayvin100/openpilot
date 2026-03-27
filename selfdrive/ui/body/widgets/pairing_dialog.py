from __future__ import annotations

import socket
import time
import pyray as rl

from openpilot.common.api import CONNECT_HOST, CONNECT_HOST_DISPLAY, CONNECT_CLIENT
from openpilot.selfdrive.ui.ui_state import device
from openpilot.system.ui.lib.application import FontWeight, gui_app
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.lib.wifi_manager import WifiManager
from openpilot.system.ui.lib.wrap_text import wrap_text
from openpilot.system.ui.lib.multilang import tr
from openpilot.system.ui.widgets.label import UnifiedLabel
from openpilot.system.ui.widgets.scroller import NavScroller
from openpilot.selfdrive.ui.mici.widgets.button import BigButton
from openpilot.selfdrive.ui.widgets.pairing_dialog import PairingDialog as TiciPairingDialog
from openpilot.selfdrive.ui.mici.widgets.pairing_dialog import PairingDialog as MiciPairingDialog

def _get_local_ip() -> str:
  try:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
      s.connect(("8.8.8.8", 80))
      return s.getsockname()[0]
  except Exception:
    return ""


class _BodyConnectBase:
  """Shared QR generation overwriting"""

  def __init__(self):
    self.url = CONNECT_CLIENT
    self._wifi_manager = WifiManager()
    self._wifi_manager.set_active(False)

  def _get_pairing_url(self) -> str:
    return f"{CONNECT_HOST}/?body={CONNECT_CLIENT}"

class BodyPairingScreen(_BodyConnectBase, TiciPairingDialog):
  """Connection screen for comma body: shows one-time connection QR and manual connect info."""

  def __init__(self):
    _BodyConnectBase.__init__(self)
    TiciPairingDialog.__init__(self)

    self._title = tr("Connect to this comma body")
    self._instructions = [
      tr("Go to {url} on your phone").format(url=CONNECT_HOST_DISPLAY),
      tr("Click \"add new device\" and then \"connect to comma body\""),
      tr("Enter the URL: {url}").format(url=CONNECT_CLIENT),
      tr("Bookmark {url} to your home screen to use it like an app").format(url=CONNECT_HOST_DISPLAY),
    ]

  def _render(self, rect: rl.Rectangle) -> int:
    rl.clear_background(rl.Color(224, 224, 224, 255))

    self._check_qr_refresh()

    margin = 70
    content_rect = rl.Rectangle(rect.x + margin, rect.y + margin, rect.width - 2 * margin, rect.height - 2 * margin)
    y = content_rect.y

    # Close button
    close_size = 80
    pad = 20
    close_rect = rl.Rectangle(content_rect.x - pad, y - pad, close_size + pad * 2, close_size + pad * 2)
    self._close_btn.render(close_rect)

    # Title
    title_font = gui_app.font(FontWeight.NORMAL)
    left_width = int(content_rect.width * 0.5 - 15)

    title_wrapped = wrap_text(title_font, self._title, 60, left_width)
    rl.draw_text_ex(title_font, "\n".join(title_wrapped), rl.Vector2(content_rect.x + close_size + 60, y), 75, 0.0, rl.BLACK)
    y += close_size + 40

    # Two columns: instructions and QR code
    remaining_height = content_rect.height - (y - content_rect.y)
    right_width = content_rect.width // 2 - 20

    qr_size = min(right_width, content_rect.height) - 40
    ssid = self._wifi_manager.connected_ssid
    wifi_label = UnifiedLabel("Make sure you are connected to the same WiFi: {ssid}".format(ssid=ssid if ssid else "not connected"), font_size=38, font_weight=FontWeight.NORMAL, text_color=rl.BLACK, elide=False, wrap_text=False)
    wifi_label.render(rl.Rectangle(content_rect.x, y, qr_size, 38))
    y += 80

    # Options text
    options_font = gui_app.font(FontWeight.SEMI_BOLD)
    options_font_size = 50
    rl.draw_text_ex(options_font, "Option 1", rl.Vector2(content_rect.x, y), options_font_size, 0.0, rl.BLACK)
    rl.draw_text_ex(options_font, "Option 2", rl.Vector2(content_rect.x + content_rect.width // 2, y), options_font_size, 0.0, rl.BLACK)
    y += options_font_size + 40

    # Option 1 Instructions
    self._render_instructions(rl.Rectangle(content_rect.x, y, left_width, remaining_height))

    # Option 2 Instructions
    opt2_font = gui_app.font(FontWeight.BOLD)
    opt2_x = content_rect.x + content_rect.width // 2
    circle_radius = 25
    circle_x = opt2_x + circle_radius + 15
    text_x = opt2_x + circle_radius * 2 + 40
    text_width = right_width - (circle_radius * 2 + 40)

    wrapped = wrap_text(opt2_font, "Scan QR code with your phone camera app", 47, int(text_width))
    text_height = len(wrapped) * 47
    circle_y = y + text_height // 2

    rl.draw_circle(int(circle_x), int(circle_y), circle_radius, rl.Color(70, 70, 70, 255))
    number = str(1)
    number_size = measure_text_cached(opt2_font, number, 30)
    rl.draw_text_ex(opt2_font, number, (int(circle_x - number_size.x // 2), int(circle_y - number_size.y // 2)), 30, 0, rl.WHITE)

    rl.draw_text_ex(opt2_font, "\n".join(wrapped), rl.Vector2(text_x, y), 47, 0.0, rl.BLACK)
    y += text_height + 45

    # QR code
    qr_size = min(right_width, content_rect.height * 0.625) - 40
    qr_x = content_rect.x + left_width + 40 + (right_width - qr_size) // 2
    self._render_qr_code(rl.Rectangle(qr_x, y, qr_size, qr_size))

    return -1


MICI_TEXT_COLOR = rl.WHITE
MICI_LABEL_SIZE = 24


class OneTimeConnectPanel(_BodyConnectBase, MiciPairingDialog):
  """Detail panel showing one-time connection QR code with manual IP/port info. Swipe down to go back."""

  def __init__(self):
    _BodyConnectBase.__init__(self)
    MiciPairingDialog.__init__(self)

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

  def _render_qr_code(self) -> None:
    if not self._qr_texture:
      error_font = gui_app.font(FontWeight.BOLD)
      rl.draw_text_ex(
        error_font, "QR Code Error", rl.Vector2(self._rect.x + 20, self._rect.y + self._rect.height // 2 - 15), 30, 0.0, rl.RED
      )
      return

    qr_margin = 20
    scale = (self._rect.height - qr_margin * 2) / self._qr_texture.height
    pos = rl.Vector2(round(self._rect.x + 8 + qr_margin), round(self._rect.y + qr_margin))
    rl.draw_texture_ex(self._qr_texture, pos, 0.0, scale, rl.WHITE)

  def _render(self, rect: rl.Rectangle):
    self._check_qr_refresh()

    self._render_qr_code()

    label_x = rect.x + 8 + int(rect.height)
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

    ip_text = f"URL: {CONNECT_CLIENT}"
    rl.draw_text_ex(self._font, ip_text, rl.Vector2(label_x, y), 22, 0, MICI_TEXT_COLOR)
    y += 24

    ssid = self._wifi_manager.connected_ssid
    wifi_label = UnifiedLabel(f"WiFi: {ssid}" if ssid else "wifi: not connected", font_size=22, font_weight=FontWeight.ROMAN, text_color=MICI_TEXT_COLOR, elide=False, wrap_text=False)
    wifi_label.render(rl.Rectangle(label_x, y, rect.width - (label_x - rect.x), 22))
