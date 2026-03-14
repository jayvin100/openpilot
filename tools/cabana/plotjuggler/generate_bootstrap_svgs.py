#!/usr/bin/env python3

import sys
from pathlib import Path
import xml.etree.ElementTree as ET


APP_ICON_SPECS = {
  "add_column.svg": {"symbol": "layout-sidebar"},
  "add_row.svg": {"symbol": "layout-sidebar", "rotate": 90},
  "add_tab.svg": {"symbol": "window-plus"},
  "alarm-bell-active.svg": {"symbol": "bell-fill"},
  "alarm-bell.svg": {"symbol": "bell"},
  "clear.svg": {"symbol": "x-circle"},
  "close-button.svg": {"symbol": "x-lg"},
  "collapse.svg": {"symbol": "arrows-angle-contract"},
  "copy.svg": {"symbol": "files"},
  "datetime.svg": {"symbol": "calendar-date"},
  "expand.svg": {"symbol": "arrows-angle-expand"},
  "export.svg": {"symbol": "box-arrow-up-right"},
  "fullscreen.svg": {"symbol": "arrows-fullscreen"},
  "green_circle.svg": {"symbol": "record-circle-fill", "color": "#00a65a"},
  "grid.svg": {"symbol": "grid-3x3-gap"},
  "import.svg": {"symbol": "folder2-open"},
  "left-arrow.svg": {"symbol": "arrow-left"},
  "legend.svg": {"symbol": "card-list"},
  "link.svg": {"symbol": "link-45deg"},
  "list.svg": {"symbol": "list-ul"},
  "loop.svg": {"symbol": "arrow-repeat"},
  "move_view.svg": {"symbol": "arrows-move"},
  "paste.svg": {"symbol": "clipboard2"},
  "pause.svg": {"symbol": "pause-fill"},
  "pencil-edit.svg": {"symbol": "pencil-square"},
  "play_arrow.svg": {"symbol": "play-fill"},
  "plot_image.svg": {"symbol": "file-earmark-image"},
  "ratio.svg": {"symbol": "aspect-ratio"},
  "red_circle.svg": {"symbol": "record-circle-fill", "color": "#dc3545"},
  "remove_red.svg": {"symbol": "x-circle-fill"},
  "right-arrow.svg": {"symbol": "arrow-right"},
  "save.svg": {"symbol": "save"},
  "settings_cog.svg": {"symbol": "gear"},
  "trash.svg": {"symbol": "trash"},
  "tree.svg": {"symbol": "tree"},
  "zoom_horizontal.svg": {"symbol": "arrows-expand"},
  "zoom_in.svg": {"symbol": "zoom-in"},
  "zoom_max.svg": {"symbol": "arrows-angle-expand"},
  "zoom_vertical.svg": {"symbol": "arrows-expand", "rotate": 90},
}

STYLE_ICON_SPECS = {
  "checkbox_checked": {"symbol": "check-square"},
  "checkbox_unchecked": {"symbol": "square"},
  "radio_checked": {"symbol": "record-circle"},
  "radio_unchecked": {"symbol": "circle"},
}

STYLE_COLORS = {
  "style_light": {
    "default": "#3d3d3d",
    "disabled": "#b5b5b5",
    "focus": "#003bc9",
  },
  "style_dark": {
    "default": "#eeeeee",
    "disabled": "#7f7f7f",
    "focus": "#148cd2",
  },
}

ADS_ICON_SPECS = {
  "close-button.svg": {"symbol": "x-lg"},
  "close-button-disabled.svg": {"symbol": "x-lg", "color": "#a0a0a0"},
  "close-button-focused.svg": {"symbol": "x-lg", "color": "#ffffff"},
  "detach-button.svg": {"symbol": "box-arrow-up-right"},
  "detach-button-disabled.svg": {"symbol": "box-arrow-up-right", "color": "#a0a0a0"},
  "tabs-menu-button.svg": {"symbol": "three-dots-vertical"},
}


def load_bootstrap_symbols(bootstrap_path: Path) -> dict[str, str]:
  tree = ET.parse(bootstrap_path)
  root = tree.getroot()
  symbols = {}
  for child in root:
    if child.tag.endswith("symbol"):
      symbol_id = child.attrib.get("id")
      if symbol_id:
        symbols[symbol_id] = ET.tostring(child, encoding="unicode")
  return symbols


def symbol_to_svg(symbol_xml: str, width: int, height: int, color: str, rotate: int = 0, title: str = "") -> str:
  symbol = ET.fromstring(symbol_xml)
  attrs = {
    "xmlns": "http://www.w3.org/2000/svg",
    "viewBox": symbol.attrib["viewBox"],
    "width": str(width),
    "height": str(height),
    "style": f"color:{color};fill:{color}",
  }
  title_xml = f"<title>{title}</title>" if title else ""
  children = "".join(ET.tostring(child, encoding="unicode") for child in symbol)
  if rotate:
    children = f'<g transform="rotate({rotate} 8 8)">{children}</g>'
  attr_str = " ".join(f'{key}="{value}"' for key, value in attrs.items())
  return f"<svg {attr_str}>{title_xml}{children}</svg>\n"


def write_svg(out_path: Path, symbol_xml: str, *, width: int, height: int, color: str, rotate: int = 0):
  out_path.parent.mkdir(parents=True, exist_ok=True)
  out_path.write_text(
    symbol_to_svg(symbol_xml, width=width, height=height, color=color, rotate=rotate, title=out_path.name),
    encoding="utf-8",
  )


def main() -> int:
  if len(sys.argv) != 3:
    print(f"usage: {sys.argv[0]} <bootstrap-icons.svg> <output-dir>", file=sys.stderr)
    return 2

  bootstrap_path = Path(sys.argv[1])
  out_dir = Path(sys.argv[2])
  symbols = load_bootstrap_symbols(bootstrap_path)

  for filename, spec in APP_ICON_SPECS.items():
    write_svg(
      out_dir / "app" / filename,
      symbols[spec["symbol"]],
      width=64,
      height=64,
      color=spec.get("color", "#000000"),
      rotate=spec.get("rotate", 0),
    )

  for theme_dir, colors in STYLE_COLORS.items():
    for state_name, spec in STYLE_ICON_SPECS.items():
      for suffix, color in (("", colors["default"]), ("_disabled", colors["disabled"]), ("_focus", colors["focus"])):
        write_svg(
          out_dir / theme_dir / f"{state_name}{suffix}.svg",
          symbols[spec["symbol"]],
          width=32,
          height=32,
          color=color,
        )

  for filename, spec in ADS_ICON_SPECS.items():
    write_svg(
      out_dir / "ads" / filename,
      symbols[spec["symbol"]],
      width=16,
      height=16,
      color=spec.get("color", "#000000"),
    )

  return 0


if __name__ == "__main__":
  raise SystemExit(main())
