import pyray as rl
from openpilot.system.ui.lib.application import gui_app, FontWeight, MousePos
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.selfdrive.ui.body.widgets.body_pairing import MiciBodyPairingScreen
from openpilot.selfdrive.ui.mici.layouts.home import MiciHomeLayout

PAIR_BTN_FONT_SIZE = 36
PAIR_BTN_MARGIN = 8

class MiciBodyHomeLayout(MiciHomeLayout):
  def __init__(self):
    super().__init__()
    self._font_bold = gui_app.font(FontWeight.BOLD)

  def _get_pair_btn_rect(self) -> rl.Rectangle:
    text = "PAIR"
    text_size = measure_text_cached(self._font_bold, text, PAIR_BTN_FONT_SIZE)
    btn_w = int(text_size.x + 180)
    btn_h = 120
    btn_x = int(self._rect.x + self._rect.width - btn_w - PAIR_BTN_MARGIN)
    btn_y = int(self._rect.y + self._rect.height - btn_h - 5 - PAIR_BTN_MARGIN)
    return rl.Rectangle(btn_x, btn_y, btn_w, btn_h)

  def _draw_pair_button(self):
    pair_rect = self._get_pair_btn_rect()
    mouse_pos = rl.get_mouse_position()
    mouse_down = self.is_pressed and rl.is_mouse_button_down(rl.MouseButton.MOUSE_BUTTON_LEFT)
    pair_pressed = mouse_down and rl.check_collision_point_rec(mouse_pos, pair_rect)
    bg_color = rl.Color(255, 255, 255, 166) if pair_pressed else rl.WHITE

    rl.draw_rectangle_rounded(pair_rect, 0.5, 10, bg_color)
    text = "PAIR"
    text_size = measure_text_cached(self._font_bold, text, PAIR_BTN_FONT_SIZE)
    text_pos = rl.Vector2(pair_rect.x + (pair_rect.width - text_size.x) / 2,
                          pair_rect.y + (pair_rect.height - text_size.y) / 2)
    rl.draw_text_ex(self._font_bold, text, text_pos, PAIR_BTN_FONT_SIZE, 0, rl.BLACK)

  def _handle_mouse_release(self, mouse_pos: MousePos):
    pair_rect = self._get_pair_btn_rect()
    if rl.check_collision_point_rec(mouse_pos, pair_rect):
      gui_app.push_widget(MiciBodyPairingScreen())
      return
    super()._handle_mouse_release(mouse_pos)

  def _render(self, rect: rl.Rectangle):
    super()._render(rect)
    self._draw_pair_button()
