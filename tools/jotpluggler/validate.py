#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import hashlib
import json
import os
import select
import shlex
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from collections.abc import Iterable
from collections import defaultdict
from dataclasses import asdict, dataclass
from functools import partial
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageOps

from openpilot.common.basedir import BASEDIR
from openpilot.selfdrive.test.process_replay.migration import migrate_all
from openpilot.tools.lib.logreader import LogReader, ReadMode, save_log
from openpilot.tools.plotjuggler import juggle as plotjuggler_juggle

ROOT = Path(BASEDIR) / "tools" / "jotpluggler"
LAYOUT_DIR = Path(BASEDIR) / "tools" / "plotjuggler" / "layouts"
VALIDATION_DIR = ROOT / "validation"
FIXTURE_DIR = VALIDATION_DIR / "fixtures"
SANITIZED_LAYOUT_DIR = VALIDATION_DIR / "sanitized_layouts"
BASELINE_DIR = VALIDATION_DIR / "baselines"
REPORT_DIR = VALIDATION_DIR / "reports"

DEFAULT_WIDTH = 1600
DEFAULT_HEIGHT = 900
DEFAULT_DPI = 96
DEFAULT_COMPARE_SIZE = (320, 180)
DEFAULT_SCORE_THRESHOLD = 0.78
DEFAULT_SETTLE_TIME = 0.75
DEFAULT_READY_TIMEOUT = 10.0
READY_MARKER = "Done reading Rlog data"
ALLOWED_PLUGIN_IDS = {"DataLoad Rlog", "Cereal Subscriber"}
FIXTURE_SCHEMA_VERSION = 3


@dataclass(frozen=True)
class LayoutSpec:
  name: str
  layout_path: str
  tab_index: int
  tab_name: str
  services: tuple[str, ...]
  source_hash: str


@dataclass(frozen=True)
class ComparisonMetrics:
  overall_similarity: float
  edge_similarity: float
  color_similarity: float
  score: float
  passed: bool


@dataclass(frozen=True)
class FixtureRecord:
  layout: str
  tab_index: int
  tab_name: str
  fixture_path: str
  sanitized_layout_path: str
  services: tuple[str, ...]
  route: str
  layout_hash: str
  message_count: int
  fixture_hash: str
  schema_version: int


def sha256_bytes(data: bytes) -> str:
  return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
  return sha256_bytes(path.read_bytes())


def ensure_dir(path: Path) -> Path:
  path.mkdir(parents=True, exist_ok=True)
  return path


def write_json(path: Path, payload: Any) -> None:
  path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")


def service_from_series_path(series_name: str) -> str | None:
  if not series_name.startswith("/"):
    return None
  parts = series_name.split("/")
  return parts[1] if len(parts) > 1 else None


def load_layout_specs(layout_names: list[str] | None = None, tab_index: int = 0) -> list[LayoutSpec]:
  requested = set(layout_names or [])
  paths = sorted(LAYOUT_DIR.glob("*.xml"))
  specs = []
  for path in paths:
    if requested and path.name not in requested and path.stem not in requested:
      continue
    specs.append(parse_layout_spec(path, tab_index=tab_index))
  if requested:
    found = {spec.name for spec in specs} | {Path(spec.layout_path).stem for spec in specs}
    missing = sorted(name for name in requested if name not in found)
    if missing:
      raise FileNotFoundError(f"Unknown layouts: {', '.join(missing)}")
  return specs


def parse_layout_spec(path: Path, tab_index: int = 0) -> LayoutSpec:
  xml_bytes = path.read_bytes()
  root = ET.fromstring(xml_bytes)
  tabs = root.findall(".//tabbed_widget/Tab")
  if not tabs:
    raise ValueError(f"{path} does not contain any tabs")
  if tab_index < 0 or tab_index >= len(tabs):
    raise IndexError(f"{path} has {len(tabs)} tabs, requested {tab_index}")

  tab = tabs[tab_index]
  tab_name = tab.attrib.get("tab_name", f"tab{tab_index}")
  curve_names = [curve.attrib.get("name", "") for curve in tab.findall(".//curve")]

  services = {service_from_series_path(name) for name in curve_names}
  services.discard(None)

  needed_snippets = {name for name in curve_names if name and not name.startswith("/")}
  for snippet in root.findall(".//customMathEquations/snippet"):
    if snippet.attrib.get("name") not in needed_snippets:
      continue
    linked = snippet.findtext("linked_source", default="")
    linked_service = service_from_series_path(linked)
    if linked_service is not None:
      services.add(linked_service)
    for child in snippet.findall("./additional_sources/*"):
      svc = service_from_series_path((child.text or "").strip())
      if svc is not None:
        services.add(svc)

  return LayoutSpec(
    name=path.stem,
    layout_path=str(path),
    tab_index=tab_index,
    tab_name=tab_name,
    services=tuple(sorted(services)),
    source_hash=sha256_bytes(xml_bytes),
  )


def message_series_paths(msg: Any) -> set[str]:
  typ = msg.which()
  paths = {f"/{typ}/__valid", f"/{typ}/__logMonoTime"}

  def walk(prefix: str, value: Any) -> None:
    if hasattr(value, "to_dict"):
      try:
        walk(prefix, value.to_dict(verbose=True))
        return
      except Exception:
        pass
    if isinstance(value, dict):
      for key, child in value.items():
        walk(f"{prefix}/{key}", child)
      return
    if isinstance(value, tuple):
      for index, child in enumerate(value):
        walk(f"{prefix}/{index}", child)
      if not value:
        paths.add(prefix)
      return
    if hasattr(value, "__len__") and hasattr(value, "__getitem__") and not isinstance(value, (str, bytes, bytearray, np.ndarray)):
      values = [value[index] for index in range(len(value))]
      for index, child in enumerate(values):
        walk(f"{prefix}/{index}", child)
      if not values:
        paths.add(prefix)
      return
    if isinstance(value, Iterable) and not isinstance(value, (str, bytes, bytearray, np.ndarray)):
      values = list(value)
      for index, child in enumerate(values):
        walk(f"{prefix}/{index}", child)
      if not values:
        paths.add(prefix)
      return
    if isinstance(value, list):
      for index, child in enumerate(value):
        walk(f"{prefix}/{index}", child)
      if not value:
        paths.add(prefix)
      return
    if isinstance(value, np.ndarray):
      if value.ndim == 0:
        paths.add(prefix)
      else:
        for index, child in enumerate(value.tolist()):
          walk(f"{prefix}/{index}", child)
      return
    paths.add(prefix)

  walk(f"/{typ}", getattr(msg, typ))
  return paths


def select_single_tab(root: ET.Element, tab_index: int) -> ET.Element:
  tabbed_widget = root.find(".//tabbed_widget")
  if tabbed_widget is None:
    raise ValueError("Layout does not contain a tabbed_widget")

  tabs = tabbed_widget.findall("./Tab")
  if not tabs:
    raise ValueError("Layout does not contain any tabs")
  if tab_index < 0 or tab_index >= len(tabs):
    raise IndexError(f"Layout has {len(tabs)} tabs, requested {tab_index}")

  selected_tab = tabs[tab_index]
  for index, tab in enumerate(tabs):
    if index != tab_index:
      tabbed_widget.remove(tab)

  current_index = tabbed_widget.find("./currentTabIndex")
  if current_index is None:
    current_index = ET.SubElement(tabbed_widget, "currentTabIndex")
  current_index.set("index", "0")
  return selected_tab


def snippet_sources(snippet: ET.Element) -> list[str]:
  sources = []
  linked = snippet.findtext("linked_source", default="").strip()
  if linked:
    sources.append(linked)
  for child in snippet.findall("./additional_sources/*"):
    text = (child.text or "").strip()
    if text:
      sources.append(text)
  return sources


def prune_invalid_curves(root: ET.Element, tab: ET.Element, available_paths: set[str]) -> None:
  curves = list(tab.findall(".//curve"))
  snippets = {
    snippet.attrib.get("name", ""): snippet
    for snippet in root.findall(".//customMathEquations/snippet")
  }

  invalid_snippets = set()
  used_snippets = {
    curve.attrib.get("name", "")
    for curve in curves
    if curve.attrib.get("name", "") and not curve.attrib.get("name", "").startswith("/")
  }
  for name in used_snippets:
    snippet = snippets.get(name)
    if snippet is None:
      invalid_snippets.add(name)
      continue
    if any(source.startswith("/") and source not in available_paths for source in snippet_sources(snippet)):
      invalid_snippets.add(name)

  for curve in curves:
    name = curve.attrib.get("name", "")
    should_remove = (
      name.startswith("/") and name not in available_paths
    ) or (
      name and not name.startswith("/") and name in invalid_snippets
    )
    if not should_remove:
      continue
    parent = _find_parent(tab, curve)
    if parent is not None:
      parent.remove(curve)

  remaining_curves = {
    curve.attrib.get("name", "")
    for curve in tab.findall(".//curve")
    if curve.attrib.get("name", "") and not curve.attrib.get("name", "").startswith("/")
  }
  custom_math = root.find(".//customMathEquations")
  if custom_math is not None:
    for snippet in list(custom_math.findall("./snippet")):
      if snippet.attrib.get("name", "") not in remaining_curves:
        custom_math.remove(snippet)


def sanitized_layout_bytes(path: Path, tab_index: int, available_paths: set[str] | None = None) -> bytes:
  root = ET.fromstring(path.read_bytes())

  selected_tab = select_single_tab(root, tab_index)

  plugins = root.find(".//Plugins")
  if plugins is not None:
    for plugin in list(plugins):
      if plugin.attrib.get("ID") not in ALLOWED_PLUGIN_IDS:
        plugins.remove(plugin)

  for tag in ("fileInfo", "previouslyLoaded_Datafiles"):
    for element in root.findall(f".//{tag}"):
      parent = _find_parent(root, element)
      if parent is not None:
        parent.remove(element)

  if available_paths is not None:
    prune_invalid_curves(root, selected_tab, available_paths)

  tree = ET.ElementTree(root)
  with tempfile.NamedTemporaryFile() as tmp:
    tree.write(tmp.name, encoding="utf-8", xml_declaration=True)
    return Path(tmp.name).read_bytes()


def _find_parent(root: ET.Element, child: ET.Element) -> ET.Element | None:
  for parent in root.iter():
    for candidate in parent:
      if candidate is child:
        return parent
  return None


def write_sanitized_layout(spec: LayoutSpec, output_dir: Path, available_paths: set[str]) -> Path:
  ensure_dir(output_dir)
  output_path = output_dir / f"{spec.name}.xml"
  output_path.write_bytes(sanitized_layout_bytes(Path(spec.layout_path), spec.tab_index, available_paths))
  return output_path


def build_fixtures(
  specs: list[LayoutSpec],
  route: str,
  fixture_dir: Path,
  sanitized_layout_dir: Path,
  force: bool,
  should_migrate: bool,
) -> dict[str, FixtureRecord]:
  ensure_dir(fixture_dir)
  ensure_dir(sanitized_layout_dir)

  existing = load_manifest(fixture_dir / "manifest.json")
  if not force and existing:
    cached = {}
    for spec in specs:
      record = existing.get(spec.name)
      if (
        record
        and record.get("layout_hash") == spec.source_hash
        and record.get("schema_version") == FIXTURE_SCHEMA_VERSION
        and Path(record["fixture_path"]).exists()
        and Path(record["sanitized_layout_path"]).exists()
      ):
        cached[spec.name] = FixtureRecord(
          layout=record["layout"],
          tab_index=record["tab_index"],
          tab_name=record["tab_name"],
          fixture_path=record["fixture_path"],
          sanitized_layout_path=record["sanitized_layout_path"],
          services=tuple(record["services"]),
          route=record["route"],
          layout_hash=record["layout_hash"],
          message_count=record["message_count"],
          fixture_hash=record["fixture_hash"],
          schema_version=record.get("schema_version", 1),
        )
    if len(cached) == len(specs):
      return cached

  lr = LogReader(route, default_mode=ReadMode.AUTO_INTERACTIVE)
  all_data = lr.run_across_segments(24, partial(plotjuggler_juggle.process, False))
  if should_migrate:
    all_data = migrate_all(all_data)

  service_to_layouts: dict[str, list[str]] = defaultdict(list)
  layout_buckets: dict[str, list[Any]] = {spec.name: [] for spec in specs}
  layout_available_paths: dict[str, set[str]] = {spec.name: set() for spec in specs}
  for spec in specs:
    for service in spec.services:
      service_to_layouts[service].append(spec.name)

  for msg in all_data:
    layout_names = service_to_layouts.get(msg.which(), ())
    if not layout_names:
      continue
    paths = message_series_paths(msg)
    for layout_name in layout_names:
      layout_buckets[layout_name].append(msg)
      layout_available_paths[layout_name].update(paths)

  records: dict[str, FixtureRecord] = {}
  for spec in specs:
    sanitized_path = write_sanitized_layout(spec, sanitized_layout_dir, layout_available_paths[spec.name])
    fixture_path = fixture_dir / f"{spec.name}.rlog"
    msgs = layout_buckets[spec.name]
    save_log(str(fixture_path), msgs, compress=False)
    record = FixtureRecord(
      layout=spec.name,
      tab_index=spec.tab_index,
      tab_name=spec.tab_name,
      fixture_path=str(fixture_path),
      sanitized_layout_path=str(sanitized_path),
      services=spec.services,
      route=route,
      layout_hash=spec.source_hash,
      message_count=len(msgs),
      fixture_hash=sha256_file(fixture_path),
      schema_version=FIXTURE_SCHEMA_VERSION,
    )
    records[spec.name] = record

  write_json(fixture_dir / "manifest.json", {name: asdict(record) for name, record in sorted(records.items())})
  return records


def load_manifest(path: Path) -> dict[str, dict[str, Any]]:
  if not path.exists():
    return {}
  return json.loads(path.read_text())


def ensure_plotjuggler_ready() -> None:
  if not Path(plotjuggler_juggle.PLOTJUGGLER_BIN).exists():
    plotjuggler_juggle.install()
    return
  if plotjuggler_juggle.get_plotjuggler_version() < plotjuggler_juggle.MINIMUM_PLOTJUGGLER_VERSION:
    plotjuggler_juggle.install()


@contextlib.contextmanager
def xvfb_session(width: int, height: int, dpi: int):
  cmd = [
    "Xvfb",
    "-displayfd",
    "1",
    "-screen",
    "0",
    f"{width}x{height}x24",
    "-dpi",
    str(dpi),
    "-nolisten",
    "tcp",
  ]
  proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
  try:
    assert proc.stdout is not None
    display_num = proc.stdout.readline().strip()
    if not display_num:
      stderr = proc.stderr.read() if proc.stderr is not None else ""
      raise RuntimeError(f"Failed to start Xvfb: {stderr.strip()}")
    env = os.environ.copy()
    env["DISPLAY"] = f":{display_num}"
    subprocess.run(["xdpyinfo"], env=env, check=True, capture_output=True, text=True, timeout=5)
    yield env
  finally:
    proc.terminate()
    try:
      proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
      proc.kill()
      proc.wait()


def wait_for_window(title: str, env: dict[str, str], timeout: float) -> str:
  proc = subprocess.run(
    ["xdotool", "search", "--sync", "--name", title],
    env=env,
    capture_output=True,
    text=True,
    timeout=timeout,
    check=True,
  )
  winid = proc.stdout.strip().splitlines()
  if not winid:
    raise RuntimeError(f"Window not found for title {title!r}")
  return winid[0]


def resize_window(winid: str, width: int, height: int, env: dict[str, str]) -> None:
  subprocess.run(["xdotool", "windowmove", winid, "0", "0"], env=env, check=True, timeout=5)
  subprocess.run(["xdotool", "windowsize", "--sync", winid, str(width), str(height)], env=env, check=True, timeout=5)


def capture_window(winid: str, output_path: Path, env: dict[str, str], retries: int = 3) -> None:
  last_error: Exception | None = None
  for attempt in range(retries):
    try:
      with tempfile.NamedTemporaryFile(suffix=".xwd", delete=False) as tmp:
        tmp_path = Path(tmp.name)
      try:
        subprocess.run(["xwd", "-silent", "-id", winid, "-out", str(tmp_path)], env=env, check=True, timeout=10)
        subprocess.run(["convert", str(tmp_path), str(output_path)], env=env, check=True, timeout=10)
      finally:
        tmp_path.unlink(missing_ok=True)
      return
    except subprocess.CalledProcessError as exc:
      last_error = exc
      if attempt == retries - 1:
        break
      time.sleep(0.5 * (attempt + 1))
  assert last_error is not None
  raise last_error


def wait_for_plotjuggler_ready(proc: subprocess.Popen[str], timeout: float) -> tuple[bool, list[str]]:
  if proc.stderr is None:
    return True, []

  lines = []
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    if proc.poll() is not None:
      break
    ready, _, _ = select.select([proc.stderr], [], [], 0.2)
    if not ready:
      continue
    line = proc.stderr.readline()
    if not line:
      continue
    lines.append(line.rstrip())
    if READY_MARKER in line:
      return True, lines

  if proc.poll() is not None:
    raise RuntimeError("\n".join(lines[-20:]) or "PlotJuggler exited before becoming ready")
  return False, lines


def plotjuggler_env(config_home: Path) -> dict[str, str]:
  env = os.environ.copy()
  env["BASEDIR"] = BASEDIR
  env["PATH"] = f"{plotjuggler_juggle.INSTALL_DIR}:{os.getenv('PATH', '')}"
  env["LD_LIBRARY_PATH"] = os.environ.get("LD_LIBRARY_PATH", "") + f":{plotjuggler_juggle.INSTALL_DIR}"
  env["XDG_CONFIG_HOME"] = str(config_home)
  env["QT_SCALE_FACTOR"] = "1"
  env["QT_AUTO_SCREEN_SCALE_FACTOR"] = "0"
  env["QT_QPA_PLATFORM"] = "xcb"
  return env


def capture_plotjuggler_baseline(
  record: FixtureRecord,
  width: int,
  height: int,
  dpi: int,
  settle_time: float,
  ready_timeout: float,
  output_dir: Path,
) -> dict[str, Any]:
  ensure_dir(output_dir)
  output_path = output_dir / f"{record.layout}.png"
  title = f"jotplugger-baseline-{record.layout}"

  with tempfile.TemporaryDirectory(prefix="jotplugger-pj-config-") as config_dir, xvfb_session(width, height, dpi) as display_env:
    env = plotjuggler_env(Path(config_dir))
    env.update(display_env)
    cmd = [
      plotjuggler_juggle.PLOTJUGGLER_BIN,
      "--buffer_size",
      str(plotjuggler_juggle.MAX_STREAMING_BUFFER_SIZE),
      "--plugin_folders",
      plotjuggler_juggle.INSTALL_DIR,
      "--disable_opengl",
      "--nosplash",
      "-d",
      record.fixture_path,
      "-l",
      record.sanitized_layout_path,
      "--window_title",
      title,
    ]

    with subprocess.Popen(cmd, env=env, cwd=plotjuggler_juggle.juggle_dir, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True) as proc:
      try:
        winid = wait_for_window(title, env, timeout=20)
        resize_window(winid, width, height, env)
        saw_ready_marker, _ = wait_for_plotjuggler_ready(proc, timeout=ready_timeout)
        time.sleep(settle_time if saw_ready_marker else max(settle_time, 2.0))
        capture_window(winid, output_path, env)
      finally:
        proc.terminate()
        try:
          proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
          proc.kill()
          proc.wait()

  return {
    "layout": record.layout,
    "image_path": str(output_path),
    "fixture_path": record.fixture_path,
    "fixture_hash": record.fixture_hash,
    "layout_hash": record.layout_hash,
    "width": width,
    "height": height,
    "dpi": dpi,
    "plotjuggler_version": ".".join(map(str, plotjuggler_juggle.get_plotjuggler_version())),
    "tab_index": record.tab_index,
    "tab_name": record.tab_name,
  }


def baseline_records_for_specs(specs: list[LayoutSpec], fixture_dir: Path) -> dict[str, FixtureRecord]:
  manifest = load_manifest(fixture_dir / "manifest.json")
  missing = [spec.name for spec in specs if spec.name not in manifest]
  if missing:
    raise FileNotFoundError(
      "Missing fixtures for: "
      + ", ".join(missing)
      + ". Run `prepare-fixtures` first."
    )
  records = {}
  for spec in specs:
    entry = manifest[spec.name]
    records[spec.name] = FixtureRecord(
      layout=entry["layout"],
      tab_index=entry["tab_index"],
      tab_name=entry["tab_name"],
      fixture_path=entry["fixture_path"],
      sanitized_layout_path=entry["sanitized_layout_path"],
      services=tuple(entry["services"]),
      route=entry["route"],
      layout_hash=entry["layout_hash"],
      message_count=entry["message_count"],
      fixture_hash=entry["fixture_hash"],
      schema_version=entry.get("schema_version", 1),
    )
  return records


def generate_baselines(
  specs: list[LayoutSpec],
  fixture_dir: Path,
  output_dir: Path,
  width: int,
  height: int,
  dpi: int,
  workers: int,
  settle_time: float,
  ready_timeout: float,
  force: bool,
) -> dict[str, dict[str, Any]]:
  ensure_plotjuggler_ready()
  ensure_dir(output_dir)

  fixture_records = baseline_records_for_specs(specs, fixture_dir)
  existing = load_manifest(output_dir / "manifest.json")
  if not force:
    cached_names = []
    for spec in specs:
      entry = existing.get(spec.name)
      if not entry:
        continue
      if (
        entry.get("layout_hash") == spec.source_hash
        and entry.get("width") == width
        and entry.get("height") == height
        and entry.get("dpi") == dpi
        and Path(entry["image_path"]).exists()
      ):
        cached_names.append(spec.name)
    if len(cached_names) == len(specs):
      return existing

  records: dict[str, dict[str, Any]] = {}
  todo = []
  for spec in specs:
    entry = existing.get(spec.name)
    needs_refresh = (
      force
      or entry is None
      or entry.get("layout_hash") != spec.source_hash
      or entry.get("width") != width
      or entry.get("height") != height
      or entry.get("dpi") != dpi
      or not Path(entry["image_path"]).exists()
    )
    if needs_refresh:
      todo.append(fixture_records[spec.name])

  if workers <= 1:
    for record in todo:
      records[record.layout] = capture_plotjuggler_baseline(record, width, height, dpi, settle_time, ready_timeout, output_dir)
  else:
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
      futures = {
        executor.submit(capture_plotjuggler_baseline, record, width, height, dpi, settle_time, ready_timeout, output_dir): record.layout
        for record in todo
      }
      for future in concurrent.futures.as_completed(futures):
        layout = futures[future]
        records[layout] = future.result()

  for name, entry in existing.items():
    if name not in records and Path(entry["image_path"]).exists():
      records[name] = entry

  write_json(output_dir / "manifest.json", dict(sorted(records.items())))
  return records


def normalize_image_pair(reference: Image.Image, candidate: Image.Image) -> tuple[Image.Image, Image.Image]:
  width = max(reference.width, candidate.width)
  height = max(reference.height, candidate.height)
  ref_canvas = Image.new("RGB", (width, height), "black")
  cand_canvas = Image.new("RGB", (width, height), "black")
  ref_canvas.paste(reference, (0, 0))
  cand_canvas.paste(candidate, (0, 0))
  return ref_canvas, cand_canvas


def downsample_for_compare(image: Image.Image, compare_size: tuple[int, int]) -> Image.Image:
  return image.filter(ImageFilter.GaussianBlur(radius=3.0)).resize(compare_size, Image.Resampling.BILINEAR)


def to_gray_array(image: Image.Image) -> np.ndarray:
  return np.asarray(image.convert("L"), dtype=np.float32)


def edge_array(image: Image.Image) -> np.ndarray:
  edges = image.convert("L").filter(ImageFilter.FIND_EDGES)
  return np.asarray(ImageOps.autocontrast(edges), dtype=np.float32)


def saturation_array(image: Image.Image) -> np.ndarray:
  rgb = np.asarray(image, dtype=np.float32) / 255.0
  return np.max(rgb, axis=2) - np.min(rgb, axis=2)


def compare_image_pair(
  reference_path: Path,
  candidate_path: Path,
  diff_path: Path,
  score_threshold: float,
  compare_size: tuple[int, int],
) -> ComparisonMetrics:
  reference = Image.open(reference_path).convert("RGB")
  candidate = Image.open(candidate_path).convert("RGB")
  reference, candidate = normalize_image_pair(reference, candidate)

  ref_small = downsample_for_compare(reference, compare_size)
  cand_small = downsample_for_compare(candidate, compare_size)

  gray_diff = np.abs(to_gray_array(ref_small) - to_gray_array(cand_small))
  overall_similarity = 1.0 - float(np.mean(gray_diff) / 255.0)

  edge_diff = np.abs(edge_array(ref_small) - edge_array(cand_small))
  edge_similarity = 1.0 - float(np.mean(edge_diff) / 255.0)

  sat_ref = saturation_array(ref_small)
  sat_cand = saturation_array(cand_small)
  if np.max(sat_ref) < 1e-3 and np.max(sat_cand) < 1e-3:
    color_similarity = 1.0
  else:
    color_similarity = 1.0 - float(np.mean(np.abs(sat_ref - sat_cand)))

  score = 0.55 * overall_similarity + 0.25 * edge_similarity + 0.20 * color_similarity
  passed = score >= score_threshold and overall_similarity >= max(0.65, score_threshold - 0.08)

  create_diff_artifact(reference, candidate, diff_path, overall_similarity, edge_similarity, color_similarity, score, passed)
  return ComparisonMetrics(
    overall_similarity=overall_similarity,
    edge_similarity=edge_similarity,
    color_similarity=color_similarity,
    score=score,
    passed=passed,
  )


def create_diff_artifact(
  reference: Image.Image,
  candidate: Image.Image,
  output_path: Path,
  overall_similarity: float,
  edge_similarity: float,
  color_similarity: float,
  score: float,
  passed: bool,
) -> None:
  reference, candidate = normalize_image_pair(reference, candidate)
  diff = ImageChops.difference(reference, candidate).convert("L")
  diff = ImageOps.autocontrast(diff)
  diff = ImageEnhance.Brightness(diff).enhance(3.5)
  heatmap = ImageOps.colorize(diff, black="#000000", mid="#ff8800", white="#ffffff")
  overlay = Image.blend(reference, candidate, 0.5)

  width, height = reference.size
  header_h = 72
  canvas = Image.new("RGB", (width * 2, height * 2 + header_h), "#101010")
  canvas.paste(reference, (0, header_h))
  canvas.paste(candidate, (width, header_h))
  canvas.paste(heatmap, (0, header_h + height))
  canvas.paste(overlay, (width, header_h + height))

  draw = ImageDraw.Draw(canvas)
  status = "PASS" if passed else "FAIL"
  draw.text((16, 12), f"{status}  score={score:.3f}  overall={overall_similarity:.3f}  edges={edge_similarity:.3f}  color={color_similarity:.3f}", fill="white")
  draw.text((16, header_h + 12), "reference", fill="white")
  draw.text((width + 16, header_h + 12), "candidate", fill="white")
  draw.text((16, header_h + height + 12), "heatmap", fill="white")
  draw.text((width + 16, header_h + height + 12), "overlay", fill="white")
  canvas.save(output_path)


def compare_candidate_dir(
  specs: list[LayoutSpec],
  baseline_dir: Path,
  candidate_dir: Path,
  output_dir: Path,
  score_threshold: float,
  compare_size: tuple[int, int],
) -> dict[str, Any]:
  ensure_dir(output_dir)
  baseline_manifest = load_manifest(baseline_dir / "manifest.json")
  if not baseline_manifest:
    raise FileNotFoundError("Missing baseline manifest. Run `capture-baselines` first.")

  results = {}
  for spec in specs:
    baseline_entry = baseline_manifest.get(spec.name)
    if baseline_entry is None:
      raise FileNotFoundError(f"Missing baseline for {spec.name}")

    reference_path = Path(baseline_entry["image_path"])
    candidate_path = candidate_dir / f"{spec.name}.png"
    layout_dir = ensure_dir(output_dir / spec.name)
    diff_path = layout_dir / "diff.png"
    metrics_path = layout_dir / "metrics.json"

    result = {
      "layout": spec.name,
      "reference_path": str(reference_path),
      "candidate_path": str(candidate_path),
      "tab_name": spec.tab_name,
    }
    if not candidate_path.exists():
      result.update({
        "passed": False,
        "missing_candidate": True,
      })
      write_json(metrics_path, result)
      results[spec.name] = result
      continue

    reference_link = layout_dir / "reference.png"
    candidate_link = layout_dir / "candidate.png"
    reference_link.write_bytes(reference_path.read_bytes())
    candidate_link.write_bytes(candidate_path.read_bytes())

    metrics = compare_image_pair(reference_path, candidate_path, diff_path, score_threshold, compare_size)
    result.update(asdict(metrics))
    write_json(metrics_path, result)
    results[spec.name] = result

  passed = sum(1 for result in results.values() if result.get("passed"))
  summary = {
    "layouts": len(specs),
    "passed": passed,
    "failed": len(specs) - passed,
    "score_threshold": score_threshold,
    "results": dict(sorted(results.items())),
  }
  write_json(output_dir / "summary.json", summary)
  return summary


def render_candidate_command(
  specs: list[LayoutSpec],
  fixture_dir: Path,
  command_template: str,
  candidate_dir: Path,
  width: int,
  height: int,
  dpi: int,
  workers: int,
  timeout: float,
) -> None:
  fixture_manifest = load_manifest(fixture_dir / "manifest.json")
  if not fixture_manifest:
    raise FileNotFoundError("Missing fixture manifest. Run `prepare-fixtures` first.")

  ensure_dir(candidate_dir)

  def run_one(spec: LayoutSpec) -> None:
    fixture_record = fixture_manifest[spec.name]
    output_path = candidate_dir / f"{spec.name}.png"
    sanitized_layout = Path(fixture_record["sanitized_layout_path"])
    format_args = {
      "layout": shlex.quote(spec.layout_path),
      "sanitized_layout": shlex.quote(str(sanitized_layout)),
      "fixture": shlex.quote(fixture_record["fixture_path"]),
      "output": shlex.quote(str(output_path)),
      "width": width,
      "height": height,
      "tab_index": spec.tab_index,
      "tab_name": shlex.quote(spec.tab_name),
    }
    shell_cmd = command_template.format(**format_args)
    with xvfb_session(width, height, dpi) as env:
      completed = subprocess.run(
        ["/bin/bash", "-lc", shell_cmd],
        env=env,
        cwd=BASEDIR,
        timeout=timeout,
        capture_output=True,
        text=True,
      )
    if completed.returncode != 0:
      raise RuntimeError(
        f"Candidate command failed for {spec.name}\nSTDOUT:\n{completed.stdout}\nSTDERR:\n{completed.stderr}"
      )
    if not output_path.exists():
      raise FileNotFoundError(f"Candidate command did not create {output_path}")

  if workers <= 1:
    for spec in specs:
      run_one(spec)
    return

  with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
    futures = {executor.submit(run_one, spec): spec.name for spec in specs}
    for future in concurrent.futures.as_completed(futures):
      future.result()


def print_summary(summary: dict[str, Any]) -> None:
  print(f"layouts: {summary['layouts']}  passed: {summary['passed']}  failed: {summary['failed']}")
  for layout, result in sorted(summary["results"].items(), key=lambda item: item[1].get("score", -1.0)):
    if result.get("missing_candidate"):
      print(f"{layout:28} missing candidate")
      continue
    summary_line = (
      f"{layout:28} {'PASS' if result['passed'] else 'FAIL'} "
      + f"score={result['score']:.3f} overall={result['overall_similarity']:.3f} "
      + f"edges={result['edge_similarity']:.3f} color={result['color_similarity']:.3f}"
    )
    print(
      summary_line
    )


def parse_compare_size(value: str) -> tuple[int, int]:
  width, height = value.lower().split("x", 1)
  return int(width), int(height)


def default_workers(layout_count: int) -> int:
  return max(1, min(6, os.cpu_count() or 1, layout_count))


def cmd_prepare_fixtures(args: argparse.Namespace) -> int:
  specs = load_layout_specs(args.layouts, tab_index=0)
  records = build_fixtures(
    specs,
    route=args.route,
    fixture_dir=Path(args.fixture_dir),
    sanitized_layout_dir=Path(args.sanitized_layout_dir),
    force=args.force,
    should_migrate=not args.no_migration,
  )
  print(f"prepared fixtures: {len(records)}")
  return 0


def cmd_capture_baselines(args: argparse.Namespace) -> int:
  specs = load_layout_specs(args.layouts, tab_index=0)
  baselines = generate_baselines(
    specs=specs,
    fixture_dir=Path(args.fixture_dir),
    output_dir=Path(args.baseline_dir),
    width=args.width,
    height=args.height,
    dpi=args.dpi,
    workers=args.workers or default_workers(len(specs)),
    settle_time=args.settle_time,
    ready_timeout=args.ready_timeout,
    force=args.force,
  )
  print(f"captured baselines: {len(baselines)}")
  return 0


def cmd_compare_images(args: argparse.Namespace) -> int:
  specs = load_layout_specs(args.layouts, tab_index=0)
  summary = compare_candidate_dir(
    specs=specs,
    baseline_dir=Path(args.baseline_dir),
    candidate_dir=Path(args.candidate_dir),
    output_dir=Path(args.output_dir),
    score_threshold=args.score_threshold,
    compare_size=parse_compare_size(args.compare_size),
  )
  print_summary(summary)
  return 0 if summary["failed"] == 0 else 1


def cmd_validate_command(args: argparse.Namespace) -> int:
  specs = load_layout_specs(args.layouts, tab_index=0)
  workers = args.workers or default_workers(len(specs))
  candidate_dir = Path(args.candidate_dir)
  render_candidate_command(
    specs=specs,
    fixture_dir=Path(args.fixture_dir),
    command_template=args.command_template,
    candidate_dir=candidate_dir,
    width=args.width,
    height=args.height,
    dpi=args.dpi,
    workers=workers,
    timeout=args.timeout,
  )
  summary = compare_candidate_dir(
    specs=specs,
    baseline_dir=Path(args.baseline_dir),
    candidate_dir=candidate_dir,
    output_dir=Path(args.output_dir),
    score_threshold=args.score_threshold,
    compare_size=parse_compare_size(args.compare_size),
  )
  print_summary(summary)
  return 0 if summary["failed"] == 0 else 1


def build_parser() -> argparse.ArgumentParser:
  parser = argparse.ArgumentParser(description="Validation tooling for JotPlugger")
  subparsers = parser.add_subparsers(dest="command", required=True)

  prepare = subparsers.add_parser("prepare-fixtures", help="Create cached first-tab .rlog fixtures")
  prepare.add_argument("--route", default=plotjuggler_juggle.DEMO_ROUTE)
  prepare.add_argument("--fixture-dir", default=str(FIXTURE_DIR))
  prepare.add_argument("--sanitized-layout-dir", default=str(SANITIZED_LAYOUT_DIR))
  prepare.add_argument("--layouts", nargs="*")
  prepare.add_argument("--force", action="store_true")
  prepare.add_argument("--no-migration", action="store_true")
  prepare.set_defaults(func=cmd_prepare_fixtures)

  baselines = subparsers.add_parser("capture-baselines", help="Generate cached PlotJuggler baselines")
  baselines.add_argument("--fixture-dir", default=str(FIXTURE_DIR))
  baselines.add_argument("--baseline-dir", default=str(BASELINE_DIR))
  baselines.add_argument("--width", type=int, default=DEFAULT_WIDTH)
  baselines.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
  baselines.add_argument("--dpi", type=int, default=DEFAULT_DPI)
  baselines.add_argument("--workers", type=int)
  baselines.add_argument("--settle-time", type=float, default=DEFAULT_SETTLE_TIME)
  baselines.add_argument("--ready-timeout", type=float, default=DEFAULT_READY_TIMEOUT)
  baselines.add_argument("--force", action="store_true")
  baselines.add_argument("--layouts", nargs="*")
  baselines.set_defaults(func=cmd_capture_baselines)

  compare = subparsers.add_parser("compare-images", help="Compare candidate PNGs against cached baselines")
  compare.add_argument("candidate_dir")
  compare.add_argument("--baseline-dir", default=str(BASELINE_DIR))
  compare.add_argument("--output-dir", default=str(REPORT_DIR / "compare-images"))
  compare.add_argument("--score-threshold", type=float, default=DEFAULT_SCORE_THRESHOLD)
  compare.add_argument("--compare-size", default=f"{DEFAULT_COMPARE_SIZE[0]}x{DEFAULT_COMPARE_SIZE[1]}")
  compare.add_argument("--layouts", nargs="*")
  compare.set_defaults(func=cmd_compare_images)

  validate = subparsers.add_parser("validate-command", help="Render candidate PNGs with a command template, then compare")
  validate.add_argument("--command-template", required=True)
  validate.add_argument("--fixture-dir", default=str(FIXTURE_DIR))
  validate.add_argument("--baseline-dir", default=str(BASELINE_DIR))
  validate.add_argument("--candidate-dir", default=str(REPORT_DIR / "validate-command" / "candidate"))
  validate.add_argument("--output-dir", default=str(REPORT_DIR / "validate-command"))
  validate.add_argument("--width", type=int, default=DEFAULT_WIDTH)
  validate.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
  validate.add_argument("--dpi", type=int, default=DEFAULT_DPI)
  validate.add_argument("--workers", type=int)
  validate.add_argument("--timeout", type=float, default=60.0)
  validate.add_argument("--score-threshold", type=float, default=DEFAULT_SCORE_THRESHOLD)
  validate.add_argument("--compare-size", default=f"{DEFAULT_COMPARE_SIZE[0]}x{DEFAULT_COMPARE_SIZE[1]}")
  validate.add_argument("--layouts", nargs="*")
  validate.set_defaults(func=cmd_validate_command)

  return parser


def main() -> int:
  parser = build_parser()
  args = parser.parse_args()
  return args.func(args)


if __name__ == "__main__":
  raise SystemExit(main())
