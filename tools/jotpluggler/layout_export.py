#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import xml.etree.ElementTree as ET
from pathlib import Path


def _split_children(node: ET.Element) -> list[ET.Element]:
  return [child for child in node if child.tag in ("DockSplitter", "DockArea")]


def _curve_leaf(series_name: str) -> str:
  if not series_name:
    return "plot"
  parts = [part for part in series_name.split("/") if part]
  if not parts:
    return series_name
  return parts[-1]


def pane_title(node: ET.Element) -> str:
  raw = (node.attrib.get("name") or "").strip()
  if raw and raw != "...":
    return raw

  curve_names = [curve.attrib.get("name", "") for curve in node.findall(".//curve")]
  leaves = []
  for name in curve_names:
    leaf = _curve_leaf(name)
    if leaf and leaf not in leaves:
      leaves.append(leaf)
    if len(leaves) == 3:
      break
  return " / ".join(leaves) if leaves else "plot"


def parse_color(color: str) -> list[int]:
  value = color.lstrip("#")
  if len(value) != 6:
    return [160, 170, 180]
  return [int(value[i:i+2], 16) for i in (0, 2, 4)]


def normalize_sizes(node: ET.Element, child_count: int) -> list[float]:
  raw_sizes = [part.strip() for part in node.attrib.get("sizes", "").split(";") if part.strip()]
  if len(raw_sizes) != child_count:
    return [1.0 / child_count] * child_count
  parsed = [max(float(value), 0.0) for value in raw_sizes]
  total = sum(parsed)
  if total <= 0:
    return [1.0 / child_count] * child_count
  return [value / total for value in parsed]


def flatten(node: ET.Element, x: float, y: float, w: float, h: float) -> list[dict]:
  if node.tag == "DockArea":
    return [{
      "x": x,
      "y": y,
      "w": w,
      "h": h,
      "title": pane_title(node),
      "curve_colors": [parse_color(curve.attrib.get("color", "")) for curve in node.findall(".//curve")],
    }]

  if node.tag != "DockSplitter":
    panes = []
    for child in _split_children(node):
      panes.extend(flatten(child, x, y, w, h))
    return panes

  children = _split_children(node)
  if not children:
    return []

  sizes = normalize_sizes(node, len(children))
  panes = []
  offset = 0.0
  horizontal = node.attrib.get("orientation", "-") == "-"
  for index, (child, size) in enumerate(zip(children, sizes, strict=True)):
    extent = 1.0 - offset if index == len(children) - 1 else size
    if horizontal:
      panes.extend(flatten(child, x + w * offset, y, w * extent, h))
    else:
      panes.extend(flatten(child, x, y + h * offset, w, h * extent))
    offset += size
  return panes


def export_layout(layout_path: Path) -> dict:
  root = ET.parse(layout_path).getroot()
  tab = root.find(".//tabbed_widget/Tab")
  if tab is None:
    raise ValueError(f"{layout_path} does not contain any tabs")

  container = tab.find("./Container")
  if container is None:
    raise ValueError(f"{layout_path} does not contain a Container")

  child = next((node for node in container if node.tag in ("DockSplitter", "DockArea")), None)
  if child is None:
    raise ValueError(f"{layout_path} does not contain a dock layout")

  return {
    "layout": layout_path.stem,
    "tab_name": tab.attrib.get("tab_name", "tab0"),
    "panes": flatten(child, 0.0, 0.0, 1.0, 1.0),
  }


def main() -> int:
  parser = argparse.ArgumentParser(description="Export first-tab PlotJuggler layout geometry")
  parser.add_argument("--layout", required=True)
  parser.add_argument("--output")
  args = parser.parse_args()

  payload = export_layout(Path(args.layout))
  if args.output:
    Path(args.output).write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
  else:
    print(json.dumps(payload, indent=2, sort_keys=True))
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
