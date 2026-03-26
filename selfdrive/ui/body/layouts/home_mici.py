import pyray as rl
from openpilot.system.ui.lib.application import gui_app, FontWeight, MousePos
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets.button import Button, ButtonStyle
from openpilot.selfdrive.ui.body.widgets.pairing_dialog import OneTimeConnectPanel
from openpilot.selfdrive.ui.mici.layouts.home import MiciHomeLayout

PAIR_BTN_FONT_SIZE = 36
PAIR_BTN_MARGIN = 8


class MiciBodyHomeLayout(MiciHomeLayout):
  def __init__(self):
    super().__init__()
    self._branch_label.set_visible(False)
    self._pair_button = Button("CONNECT", font_size=PAIR_BTN_FONT_SIZE, font_weight=FontWeight.BOLD,
                              click_callback=lambda: gui_app.push_widget(OneTimeConnectPanel()),
                              button_style=ButtonStyle.ACTION)

  def _render(self, rect: rl.Rectangle):
    super()._render(rect)
    font_bold = gui_app.font(FontWeight.BOLD)
    text_size = measure_text_cached(font_bold, "CONNECT", PAIR_BTN_FONT_SIZE)
    btn_w = int(text_size.x + 100)
    btn_h = 124
    btn_x = int(self._rect.x + self._rect.width - btn_w - PAIR_BTN_MARGIN)
    btn_y = int(self._rect.y + self._rect.height - btn_h - 5 - PAIR_BTN_MARGIN)
    self._pair_button.render(rl.Rectangle(btn_x, btn_y, btn_w, btn_h))
