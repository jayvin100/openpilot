"""jay's design kit: colors, type scale and drawing helpers shared by all custom screens.

Sizes are for the comma 3X UI (2160x1080). The panel is OLED: keep backgrounds pure black and large areas dark.
UI fonts only contain ASCII plus EXTRA_FONT_CHARS (system/ui/lib/application.py): use "•", "–", "°", never "·" or "…".
"""
import pyray as rl

from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached


def _c(r: int, g: int, b: int, a: int = 255) -> rl.Color:
  return rl.Color(r, g, b, a)


class C:
  BG = _c(0, 0, 0)
  CARD = _c(22, 22, 26)
  CARD_RAISED = _c(34, 34, 40)
  TRACK = _c(52, 52, 60)          # empty part of bars and rings
  LINE = _c(255, 255, 255, 22)    # hairlines and card borders

  TEXT = _c(242, 242, 245)
  TEXT_DIM = _c(152, 152, 162)
  TEXT_FAINT = _c(98, 98, 108)

  ACCENT = _c(255, 184, 64)       # sunny amber: brand, highlights
  BLUE = _c(92, 128, 255)         # progress, uploads
  GREEN = _c(64, 200, 128)        # good, engaged
  YELLOW = _c(255, 200, 70)       # attention
  RED = _c(240, 84, 84)           # problem


class T:
  """Font sizes in px."""
  HERO = 150
  DISPLAY = 110
  H1 = 72
  H2 = 56
  BODY = 46
  SMALL = 38
  LABEL = 32  # uppercase section labels


class S:
  """Spacing in px."""
  XS = 8
  SM = 16
  MD = 24
  LG = 40
  XL = 56
  CARD_PAD = 48
  RADIUS = 36


def font(weight: FontWeight = FontWeight.NORMAL) -> rl.Font:
  return gui_app.font(weight)


def with_alpha(color: rl.Color, alpha: float) -> rl.Color:
  return rl.Color(color.r, color.g, color.b, int(max(0.0, min(1.0, alpha)) * color.a))


def measure(text: str, size: int, weight: FontWeight = FontWeight.NORMAL) -> rl.Vector2:
  return measure_text_cached(font(weight), text, size)


def fit(text: str, size: int, max_width: float, weight: FontWeight = FontWeight.NORMAL) -> str:
  """Shorten text with "..." so it fits in max_width."""
  if measure(text, size, weight).x <= max_width:
    return text
  while text and measure(text + "...", size, weight).x > max_width:
    text = text[:-1]
  return text.rstrip() + "..."


def text(s: str, x: float, y: float, size: int, color: rl.Color = C.TEXT, weight: FontWeight = FontWeight.NORMAL,
         max_width: float | None = None) -> float:
  """Draw text with its top-left at (x, y). Returns the drawn width."""
  if max_width is not None:
    s = fit(s, size, max_width, weight)
  rl.draw_text_ex(font(weight), s, rl.Vector2(round(x), round(y)), size, 0, color)
  return measure(s, size, weight).x


def text_right(s: str, right: float, y: float, size: int, color: rl.Color = C.TEXT, weight: FontWeight = FontWeight.NORMAL) -> float:
  w = measure(s, size, weight).x
  rl.draw_text_ex(font(weight), s, rl.Vector2(round(right - w), round(y)), size, 0, color)
  return w


def text_center(s: str, cx: float, y: float, size: int, color: rl.Color = C.TEXT, weight: FontWeight = FontWeight.NORMAL) -> float:
  w = measure(s, size, weight).x
  rl.draw_text_ex(font(weight), s, rl.Vector2(round(cx - w / 2), round(y)), size, 0, color)
  return w


def label(s: str, x: float, y: float, color: rl.Color = C.TEXT_FAINT) -> float:
  """Small uppercase section label."""
  return text(s.upper(), x, y, T.LABEL, color, FontWeight.SEMI_BOLD)


def rounded(rect: rl.Rectangle, radius: float, color: rl.Color) -> None:
  """Rounded rectangle with a corner radius in px (raylib takes a 0-1 roundness)."""
  short = min(rect.width, rect.height)
  if short <= 0:
    return
  rl.draw_rectangle_rounded(rect, min(1.0, 2 * radius / short), 16, color)


def card(rect: rl.Rectangle, color: rl.Color = C.CARD, radius: float = S.RADIUS) -> rl.Rectangle:
  """Draw a card and return its padded content rect."""
  rounded(rect, radius, color)
  p = S.CARD_PAD
  return rl.Rectangle(rect.x + p, rect.y + p, rect.width - 2 * p, rect.height - 2 * p)


def bar(rect: rl.Rectangle, frac: float, color: rl.Color = C.BLUE, track: rl.Color = C.TRACK) -> None:
  """Horizontal progress bar with rounded ends."""
  r = rect.height / 2
  rounded(rect, r, track)
  frac = max(0.0, min(1.0, frac))
  if frac > 0:
    rounded(rl.Rectangle(rect.x, rect.y, max(rect.height, rect.width * frac), rect.height), r, color)


def ring(cx: float, cy: float, radius: float, thickness: float, frac: float, color: rl.Color = C.BLUE, track: rl.Color = C.TRACK) -> None:
  """Circular progress ring, filling clockwise from the top."""
  inner, outer = radius - thickness, radius
  rl.draw_ring(rl.Vector2(cx, cy), inner, outer, 0, 360, 96, track)
  frac = max(0.0, min(1.0, frac))
  if frac > 0:
    rl.draw_ring(rl.Vector2(cx, cy), inner, outer, -90, -90 + 360 * frac, 96, color)


def dot(cx: float, cy: float, radius: float, color: rl.Color) -> None:
  rl.draw_circle(round(cx), round(cy), radius, color)


def pill(s: str, x: float, y: float, color: rl.Color, size: int = T.SMALL, right_align: bool = False) -> rl.Rectangle:
  """Status chip: colored dot + text on a dark rounded background. Returns its rect."""
  pad_x, dot_r = size * 0.6, size * 0.2
  w = pad_x * 2 + dot_r * 2 + size * 0.4 + measure(s, size, FontWeight.SEMI_BOLD).x
  h = size * 1.7
  if right_align:
    x -= w
  r = rl.Rectangle(x, y, w, h)
  rounded(r, h / 2, with_alpha(color, 0.16))
  dot(x + pad_x + dot_r, y + h / 2, dot_r, color)
  text(s, x + pad_x + dot_r * 2 + size * 0.4, y + (h - size) / 2 - size * 0.08, size, color, FontWeight.SEMI_BOLD)
  return r


def stat(value: str, caption: str, x: float, y: float, size: int = T.H1, color: rl.Color = C.TEXT) -> float:
  """Big number with a small caption under it. Returns the width used."""
  w = text(value, x, y, size, color, FontWeight.BOLD)
  w2 = text(caption, x, y + size * 1.12, T.SMALL, C.TEXT_DIM)
  return max(w, w2)


def hline(x: float, y: float, w: float, color: rl.Color = C.LINE) -> None:
  rl.draw_rectangle(round(x), round(y), round(w), 2, color)
