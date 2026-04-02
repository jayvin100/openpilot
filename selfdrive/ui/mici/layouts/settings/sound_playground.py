import json
import math
import os
import pyray as rl

from openpilot.selfdrive.ui.ui_state import device, ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight, MouseEvent
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.widgets.label import gui_label
from openpilot.system.ui.widgets.nav_widget import NavWidget
from openpilot.selfdrive.ui.mici.widgets.dialog import BigInputDialog

STATE_PATH = "/tmp/sound_playground_state.json"
CONTENT_SIDE_PADDING = 8.0

FREQ_MIN = 20.0
FREQ_MAX = 12000.0
VOLUME_MIN = 0.0
VOLUME_MAX = 1.0
VOLUME_STEP = 0.05

WAVEFORMS = ["sine", "square", "saw", "triangle"]
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
SLIDER_GRADIENT_LEFT = rl.Color(30, 53, 255, 255)
SLIDER_GRADIENT_RIGHT = rl.Color(23, 193, 255, 255)

# Keep UI readout aligned with soundd bankstown defaults.
BASS_FLOOR_HZ = 33
BASS_CEIL_HZ = 200
BASS_FINAL_HP_HZ = 85
BASS_AMT = 1.50
BASS_BLEND = 0.37


def _safe_float(value, default: float) -> float:
  if value is None:
    return default
  try:
    if isinstance(value, bytes):
      value = value.decode("utf-8")
    return float(value)
  except (TypeError, ValueError):
    return default


def _safe_int(value, default: int) -> int:
  if value is None:
    return default
  try:
    if isinstance(value, bytes):
      value = value.decode("utf-8")
    return int(value)
  except (TypeError, ValueError):
    return default


def _clamp(value: float, min_value: float, max_value: float) -> float:
  if not math.isfinite(value):
    return min_value
  return max(min_value, min(max_value, value))


def _fit_font_size(text: str, max_width: float, preferred: int, min_size: int = 8, weight: FontWeight = FontWeight.NORMAL) -> int:
  font = gui_app.font(weight)
  if max_width <= 0:
    return min_size
  size = preferred
  while size > min_size and measure_text_cached(font, text, size).x > max_width:
    size -= 1
  return max(min_size, size)


class Slider(Widget):
  def __init__(self, title: str, min_value: float, max_value: float, value: float, on_change, log_scale: bool = False):
    super().__init__()
    self._title = title
    self._min = min_value
    self._max = max_value
    self._value = _clamp(value, min_value, max_value)
    self._on_change = on_change
    self._log_scale = log_scale
    self._dragging = False

  @property
  def value(self) -> float:
    return self._value

  def set_value(self, value: float):
    self._value = _clamp(value, self._min, self._max)

  def _to_normalized(self, value: float) -> float:
    if self._log_scale:
      lv = math.log10(max(value, self._min))
      lmin = math.log10(self._min)
      lmax = math.log10(self._max)
      return (lv - lmin) / (lmax - lmin)
    return (value - self._min) / (self._max - self._min)

  def _from_normalized(self, normalized: float) -> float:
    n = _clamp(normalized, 0.0, 1.0)
    if self._log_scale:
      lmin = math.log10(self._min)
      lmax = math.log10(self._max)
      return 10 ** (lmin + n * (lmax - lmin))
    return self._min + n * (self._max - self._min)

  def _set_from_mouse(self, x: float):
    track_left = self._rect.x + 10
    track_right = self._rect.x + self._rect.width - 10
    if track_right <= track_left:
      return
    normalized = (x - track_left) / (track_right - track_left)
    old = self._value
    self._value = self._from_normalized(normalized)
    if abs(self._value - old) > 1e-6:
      self._on_change(self._value)

  def _handle_mouse_event(self, mouse_event: MouseEvent):
    super()._handle_mouse_event(mouse_event)
    # Always end drag on release, even if release happens outside this slider's hit rect.
    if mouse_event.left_released:
      self._dragging = False
      return

    if self._dragging and mouse_event.left_down:
      self._set_from_mouse(mouse_event.pos.x)

  def _handle_mouse_press(self, mouse_pos):
    super()._handle_mouse_press(mouse_pos)
    self._dragging = True
    self._set_from_mouse(mouse_pos.x)

  def _handle_mouse_release(self, mouse_pos):
    super()._handle_mouse_release(mouse_pos)
    self._dragging = False
    self._set_from_mouse(mouse_pos.x)

  def _render(self, _):
    title_h = max(12, int(self._rect.height * 0.42))
    title_text = self._title
    value_text = f"{self._value:.1f} Hz" if "frequency" in self._title else f"{self._value * 100:.0f}%"
    title_font = _fit_font_size(title_text, self._rect.width * 0.45, max(10, int(title_h * 0.78)), min_size=8, weight=FontWeight.BOLD)
    value_font = _fit_font_size(value_text, self._rect.width * 0.50, title_font, min_size=8, weight=FontWeight.MEDIUM)
    title_rect = rl.Rectangle(self._rect.x, self._rect.y, self._rect.width, title_h)
    gui_label(title_rect, title_text, font_size=title_font, font_weight=FontWeight.BOLD,
              alignment=rl.GuiTextAlignment.TEXT_ALIGN_LEFT)

    gui_label(title_rect, value_text, font_size=value_font, font_weight=FontWeight.MEDIUM, alignment=rl.GuiTextAlignment.TEXT_ALIGN_RIGHT,
              color=rl.Color(220, 220, 220, 255))

    track_h = max(4, int(self._rect.height * 0.14))
    track_y = self._rect.y + title_h + max(4, int(self._rect.height * 0.08))
    track_rect = rl.Rectangle(self._rect.x + 10, track_y, self._rect.width - 20, track_h)
    rl.draw_rectangle_rounded(track_rect, 1.0, 8, rl.Color(70, 70, 70, 255))

    normalized = self._to_normalized(self._value)
    fill_rect = rl.Rectangle(track_rect.x, track_rect.y, track_rect.width * normalized, track_rect.height)
    if fill_rect.width > 0:
      rl.draw_rectangle_gradient_h(int(fill_rect.x), int(fill_rect.y), int(fill_rect.width), int(fill_rect.height),
                                   SLIDER_GRADIENT_LEFT, SLIDER_GRADIENT_RIGHT)

    knob_x = track_rect.x + track_rect.width * normalized
    knob_y = track_rect.y + track_rect.height / 2
    knob_radius = max(6, int(track_h * 1.3))
    rl.draw_circle(int(knob_x), int(knob_y), knob_radius, rl.Color(245, 245, 245, 255))
    rl.draw_circle_lines(int(knob_x), int(knob_y), knob_radius, rl.Color(30, 30, 30, 255))


class CompactButton(Widget):
  def __init__(self, label: str, value: str = "", click_callback=None):
    super().__init__()
    self._label = label
    self._value = value
    self._click_callback = click_callback

  def set_value(self, value: str):
    self._value = value

  def _handle_mouse_release(self, mouse_pos):
    super()._handle_mouse_release(mouse_pos)
    if self._click_callback is not None:
      self._click_callback()

  def _render(self, _):
    texture = gui_app.texture("icons_mici/buttons/button_rectangle_pressed.png" if self.is_pressed else "icons_mici/buttons/button_rectangle.png",
                              int(self._rect.width), int(self._rect.height), keep_aspect_ratio=False)
    rl.draw_texture_ex(texture, rl.Vector2(self._rect.x, self._rect.y), 0.0, 1.0, rl.WHITE)

    title_font = max(9, int(self._rect.height * 0.24))
    value_font = max(10, int(self._rect.height * 0.32))
    if self._label:
      title_font = _fit_font_size(self._label, self._rect.width - 10, title_font, min_size=8, weight=FontWeight.NORMAL)
      value_font = _fit_font_size(self._value, self._rect.width - 10, value_font, min_size=8, weight=FontWeight.BOLD)
      gui_label(self._rect, self._label, font_size=title_font, color=rl.Color(190, 190, 190, 255),
                alignment=rl.GuiTextAlignment.TEXT_ALIGN_CENTER, alignment_vertical=rl.GuiTextAlignmentVertical.TEXT_ALIGN_TOP)
      gui_label(self._rect, self._value, font_size=value_font, font_weight=FontWeight.BOLD,
                alignment=rl.GuiTextAlignment.TEXT_ALIGN_CENTER, alignment_vertical=rl.GuiTextAlignmentVertical.TEXT_ALIGN_BOTTOM)
    else:
      value_font = _fit_font_size(self._value, self._rect.width - 10, value_font, min_size=8, weight=FontWeight.BOLD)
      gui_label(self._rect, self._value, font_size=value_font, font_weight=FontWeight.BOLD,
                alignment=rl.GuiTextAlignment.TEXT_ALIGN_CENTER, alignment_vertical=rl.GuiTextAlignmentVertical.TEXT_ALIGN_MIDDLE)


class CircleTextButton(Widget):
  def __init__(self, value: str, click_callback=None):
    super().__init__()
    self._value = value
    self._click_callback = click_callback

  def set_value(self, value: str):
    self._value = value

  def _handle_mouse_release(self, mouse_pos):
    super()._handle_mouse_release(mouse_pos)
    if self._click_callback is not None:
      self._click_callback()

  def _render(self, _):
    texture = gui_app.texture("icons_mici/buttons/button_circle_pressed.png" if self.is_pressed else "icons_mici/buttons/button_circle.png",
                              int(self._rect.width), int(self._rect.height), keep_aspect_ratio=False)
    rl.draw_texture_ex(texture, rl.Vector2(self._rect.x, self._rect.y), 0.0, 1.0, rl.WHITE)

    value_font = _fit_font_size(self._value, self._rect.width - 16, max(12, int(self._rect.height * 0.26)),
                                min_size=8, weight=FontWeight.BOLD)
    gui_label(self._rect, self._value, font_size=value_font, font_weight=FontWeight.BOLD,
              alignment=rl.GuiTextAlignment.TEXT_ALIGN_CENTER, alignment_vertical=rl.GuiTextAlignmentVertical.TEXT_ALIGN_MIDDLE)


class ToggleCircleTextButton(CircleTextButton):
  def __init__(self, value: str, toggle_callback=None):
    super().__init__(value, click_callback=None)
    self._toggled = False
    self._toggle_callback = toggle_callback

  def set_toggled(self, toggled: bool):
    self._toggled = toggled

  def _handle_mouse_release(self, mouse_pos):
    super()._handle_mouse_release(mouse_pos)
    self._toggled = not self._toggled
    if self._toggle_callback is not None:
      self._toggle_callback(self._toggled)

  def _render(self, _):
    texture = gui_app.texture("icons_mici/buttons/button_circle_pressed.png" if self._toggled else "icons_mici/buttons/button_circle.png",
                              int(self._rect.width), int(self._rect.height), keep_aspect_ratio=False)
    rl.draw_texture_ex(texture, rl.Vector2(self._rect.x, self._rect.y), 0.0, 1.0, rl.WHITE)
    value_font = _fit_font_size(self._value, self._rect.width - 16, max(12, int(self._rect.height * 0.26)),
                                min_size=8, weight=FontWeight.BOLD)
    gui_label(self._rect, self._value, font_size=value_font, font_weight=FontWeight.BOLD,
              alignment=rl.GuiTextAlignment.TEXT_ALIGN_CENTER, alignment_vertical=rl.GuiTextAlignmentVertical.TEXT_ALIGN_MIDDLE)


class HoldButton(Widget):
  def __init__(self, toggle_callback=None):
    super().__init__()
    self._playing = False
    self._toggle_callback = toggle_callback

  def set_playing(self, playing: bool):
    self._playing = playing

  def _handle_mouse_release(self, mouse_pos):
    super()._handle_mouse_release(mouse_pos)
    self._playing = not self._playing
    if self._toggle_callback is not None:
      self._toggle_callback(self._playing)

  def _render(self, _):
    texture = gui_app.texture("icons_mici/buttons/button_rectangle_pressed.png" if self._playing else "icons_mici/buttons/button_rectangle.png",
                              int(self._rect.width), int(self._rect.height), keep_aspect_ratio=False)
    tint = rl.Color(190, 220, 255, 255) if self._playing else rl.WHITE
    rl.draw_texture_ex(texture, rl.Vector2(self._rect.x, self._rect.y), 0.0, 1.0, tint)

    icon_color = rl.Color(250, 250, 250, 255)
    if self._playing:
      # Stop icon
      size = min(self._rect.width, self._rect.height) * 0.32
      rx = self._rect.x + (self._rect.width - size) / 2
      ry = self._rect.y + (self._rect.height - size) / 2
      rl.draw_rectangle_rounded(rl.Rectangle(rx, ry, size, size), 0.1, 4, icon_color)
    else:
      # Play icon
      cx = self._rect.x + self._rect.width / 2
      cy = self._rect.y + self._rect.height / 2
      size = min(self._rect.width, self._rect.height) * 0.28
      p1 = rl.Vector2(cx - size * 0.45, cy - size * 0.55)
      p2 = rl.Vector2(cx - size * 0.45, cy + size * 0.55)
      p3 = rl.Vector2(cx + size * 0.60, cy)
      rl.draw_triangle(p1, p2, p3, icon_color)


class SoundPlaygroundLayout(NavWidget):
  def __init__(self):
    super().__init__()
    state = self._load_state()
    self._freq_hz = _clamp(_safe_float(state.get("frequency_hz"), 440.0), FREQ_MIN, FREQ_MAX)
    self._volume = _clamp(_safe_float(state.get("volume"), 1.0), VOLUME_MIN, VOLUME_MAX)
    self._waveform_idx = int(max(0, min(len(WAVEFORMS) - 1, _safe_int(state.get("waveform"), 0))))
    self._bass_enabled = bool(state.get("bass", False))
    self._is_playing = False
    self._was_driver_view_enabled = False

    self._freq_slider = self._child(Slider("frequency", FREQ_MIN, FREQ_MAX, self._freq_hz, self._on_freq_changed, log_scale=True))
    self._volume_slider = self._child(Slider("volume", VOLUME_MIN, VOLUME_MAX, self._volume, self._on_volume_changed))

    self._waveform_btn = self._child(CircleTextButton(WAVEFORMS[self._waveform_idx], self._cycle_waveform))
    self._fine_freq_btn = self._child(CircleTextButton("Hz", self._show_frequency_input))
    self._bass_btn = self._child(ToggleCircleTextButton("bass", toggle_callback=self._on_bass_toggled))
    self._bass_btn.set_toggled(self._bass_enabled)

    self._play_btn = self._child(HoldButton(toggle_callback=self._put_play))
    self._freq_input_dialog: BigInputDialog | None = None
    self._controls_enabled = lambda: (not self.is_dismissing) and (self._freq_input_dialog is None or not gui_app.widget_in_stack(self._freq_input_dialog))
    self._freq_slider.set_touch_valid_callback(self._controls_enabled)
    self._volume_slider.set_touch_valid_callback(self._controls_enabled)
    self._waveform_btn.set_touch_valid_callback(self._controls_enabled)
    self._fine_freq_btn.set_touch_valid_callback(self._controls_enabled)
    self._bass_btn.set_touch_valid_callback(self._controls_enabled)
    self._play_btn.set_touch_valid_callback(self._controls_enabled)

  def _load_state(self) -> dict:
    try:
      with open(STATE_PATH) as f:
        data = json.load(f)
      return data if isinstance(data, dict) else {}
    except (FileNotFoundError, OSError, ValueError, TypeError):
      return {}

  def _write_state(self):
    state = {
      "frequency_hz": float(self._freq_hz),
      "gain": 1.0,
      "volume": float(self._volume),
      "waveform": int(self._waveform_idx),
      "bass": bool(self._bass_enabled),
      "play": bool(self._is_playing),
    }
    tmp_path = f"{STATE_PATH}.tmp"
    try:
      with open(tmp_path, "w") as f:
        json.dump(state, f)
      os.replace(tmp_path, STATE_PATH)
    except OSError:
      pass

  def _put_play(self, playing: bool):
    if self._is_playing == playing:
      return
    self._is_playing = playing
    self._play_btn.set_playing(playing)
    self._write_state()

  def _on_freq_changed(self, value: float):
    self._freq_hz = _clamp(value, FREQ_MIN, FREQ_MAX)
    self._write_state()

  def _on_volume_changed(self, value: float):
    snapped = round(value / VOLUME_STEP) * VOLUME_STEP
    self._volume = _clamp(snapped, VOLUME_MIN, VOLUME_MAX)
    self._volume_slider.set_value(self._volume)
    self._write_state()

  def _cycle_waveform(self):
    self._waveform_idx = (self._waveform_idx + 1) % len(WAVEFORMS)
    self._waveform_btn.set_value(WAVEFORMS[self._waveform_idx])
    self._write_state()

  def _on_bass_toggled(self, toggled: bool):
    self._bass_enabled = toggled
    self._write_state()

  def _show_frequency_input(self):
    if self._freq_input_dialog is not None and gui_app.widget_in_stack(self._freq_input_dialog):
      return

    def apply_frequency(text: str):
      self._freq_input_dialog = None
      cleaned = text.strip()
      if not cleaned:
        return

      try:
        parsed = float(cleaned)
      except ValueError:
        return
      if not math.isfinite(parsed):
        return

      self._freq_hz = _clamp(parsed, FREQ_MIN, FREQ_MAX)
      self._freq_slider.set_value(self._freq_hz)
      self._write_state()

    default_text = f"{self._freq_hz:.1f}".rstrip("0").rstrip(".")
    dlg = BigInputDialog("enter exact frequency (Hz)...", default_text=default_text,
                         minimum_length=1, confirm_callback=apply_frequency, start_on_numbers=True)
    dlg.set_back_callback(lambda: setattr(self, "_freq_input_dialog", None))
    self._freq_input_dialog = dlg
    gui_app.push_widget(dlg)

  def _note_info(self) -> tuple[str, float]:
    midi = 69.0 + 12.0 * math.log2(max(self._freq_hz, 1e-6) / 440.0)
    nearest = int(round(midi))
    cents = (midi - nearest) * 100.0
    note_name = NOTE_NAMES[nearest % 12]
    octave = nearest // 12 - 1
    return f"{note_name}{octave}", cents

  def _harmonic_info(self) -> str:
    if not self._bass_enabled:
      return ""
    return f"bankstown: bp {BASS_FLOOR_HZ}-{BASS_CEIL_HZ}Hz, hp {BASS_FINAL_HP_HZ}Hz, amt {BASS_AMT:.2f}, blend {BASS_BLEND:.2f}"

  def show_event(self):
    super().show_event()
    self._was_driver_view_enabled = ui_state.params.get_bool("IsDriverViewEnabled")
    ui_state.params.put_bool("IsDriverViewEnabled", True)
    device.set_override_interactive_timeout(300)
    self._write_state()
    self._put_play(False)

  def hide_event(self):
    super().hide_event()
    self._put_play(False)
    device.set_override_interactive_timeout(None)
    ui_state.params.put_bool("IsDriverViewEnabled", self._was_driver_view_enabled)

  def _render(self, rect: rl.Rectangle):
    viewport = rl.Rectangle(rect.x + CONTENT_SIDE_PADDING, rect.y, max(0.0, rect.width - CONTENT_SIDE_PADDING * 2), rect.height)
    bottom_h = min(80.0, viewport.height)
    top_h = max(0.0, viewport.height - bottom_h)
    top = rl.Rectangle(viewport.x, viewport.y, viewport.width, top_h)
    bottom = rl.Rectangle(viewport.x, viewport.y + top_h, viewport.width, bottom_h)

    info_h = 20.0
    top_margin = 0.0
    row_gap = 0.0
    usable_h = max(40.0, top.height - info_h - top_margin * 2 - row_gap)
    slider_h = usable_h / 2.0
    row_y = top.y + top_margin
    self._freq_slider.render(rl.Rectangle(top.x, row_y, top.width, slider_h))
    row_y += slider_h + row_gap
    self._volume_slider.render(rl.Rectangle(top.x, row_y, top.width, slider_h))

    note, cents = self._note_info()
    cents_text = f"{cents:+.1f} cents"
    info_color = rl.Color(200, 200, 200, 255)
    info_text = f"piano: {note} ({cents_text})"
    harmonic_text = self._harmonic_info()
    title_h = max(12, int(slider_h * 0.42))
    title_font = max(10, int(title_h * 0.78))
    info_font = _fit_font_size(info_text, top.width - 6, title_font, min_size=8, weight=FontWeight.NORMAL)
    gui_label(rl.Rectangle(top.x, top.y + top.height - info_h - 4, top.width, info_h),
              info_text, font_size=info_font, color=info_color,
              alignment=rl.GuiTextAlignment.TEXT_ALIGN_LEFT)
    if harmonic_text:
      harmonic_font = _fit_font_size(harmonic_text, top.width - 6, max(8, title_font - 2), min_size=7, weight=FontWeight.NORMAL)
      gui_label(rl.Rectangle(top.x, top.y + top.height - info_h - 20, top.width, info_h),
                harmonic_text, font_size=harmonic_font, color=rl.Color(170, 170, 170, 255),
                alignment=rl.GuiTextAlignment.TEXT_ALIGN_LEFT)

    # Bottom row: play | waveform | bass | Hz
    button_gap = 10.0
    circle_size = 80.0
    total_gaps = button_gap * 3.0
    circle_block_w = circle_size * 3.0
    play_w = max(0.0, bottom.width - circle_block_w - total_gaps)

    play_rect = rl.Rectangle(bottom.x, bottom.y + (bottom.height - 80.0) / 2.0, play_w, 80.0)
    start_x = bottom.x + play_w + button_gap
    circle_y = bottom.y + (bottom.height - 80.0) / 2.0
    wave_rect = rl.Rectangle(start_x, circle_y, circle_size, circle_size)
    bass_rect = rl.Rectangle(start_x + circle_size + button_gap, circle_y, circle_size, circle_size)
    fine_rect = rl.Rectangle(start_x + (circle_size + button_gap) * 2.0, circle_y, circle_size, circle_size)
    self._play_btn.render(play_rect)
    self._waveform_btn.render(wave_rect)
    self._bass_btn.render(bass_rect)
    self._fine_freq_btn.render(fine_rect)
