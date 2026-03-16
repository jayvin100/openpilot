#!/usr/bin/env python3

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from smoke_test import (
  CABANA_DIR,
  ROOT,
  capture_steady_window,
  demo_segment_route,
  ensure_cabana_built,
  find_free_display,
  find_window_id,
  load_stats,
  make_diff_image,
  terminate_process,
  wait_for_socket,
)

if str(ROOT) not in sys.path:
  sys.path.insert(0, str(ROOT))

from tools.cabana.dbc.generate_dbc_json import generate_dbc_dict


DEFAULT_OUTPUT_DIR = CABANA_DIR / "validation_artifacts" / "latest"
DEFAULT_BASELINE_DIR = CABANA_DIR / "validation_baseline"
DEFAULT_SIZE = "1600x900"
DEFAULT_REQUIRED_WIDGETS = [
  "CabanaMainWindow",
  "MainMenuBar",
  "MessagesWidget",
  "MessageTable",
  "MessageHeader",
  "MessagesFilterName",
  "VideoWidget",
  "PlaybackToolbar",
  "PlaybackPlayToggleButton",
  "PlaybackSeekForwardButton",
  "PlaybackSeekBackwardButton",
  "ChartsWidget",
  "ChartsToolbar",
  "ChartsRemoveAllButton",
]
ACTION_IGNORE_PREFIXES = (
  "File>Load DBC from commaai/opendbc>",
  "File>Open Recent>",
  "File>Manage DBC Files>",
)
METRIC_LIMITS = {
  "startup_visual": {
    "steady_state_ms": (1.35, 150.0),
    "route_load_ms": (1.35, 100.0),
    "max_ui_gap_ms": (1.50, 10.0),
  },
  "core_workflow": {
    "selection_latency_ms": (1.50, 40.0),
    "filter_latency_ms": (1.50, 50.0),
    "chart_add_latency_ms": (1.50, 60.0),
    "zoom_latency_ms": (1.60, 75.0),
    "seek_latency_ms": (1.50, 75.0),
    "max_playback_stall_ms": (1.25, 40.0),
  },
  "edit_save_reload": {
    "edit_latency_ms": (1.60, 75.0),
    "save_latency_ms": (1.60, 100.0),
    "reload_ready_ms": (1.35, 150.0),
  },
  "session_restore": {
    "restore_ready_ms": (1.35, 150.0),
    "restore_latency_ms": (1.50, 75.0),
  },
  "video_sanity": {
    "steady_state_ms": (1.40, 200.0),
    "max_playback_stall_ms": (1.25, 40.0),
  },
  "invalid_dbc": {
    "dialog_latency_ms": (1.40, 100.0),
  },
  "input_fuzz": {
    "max_playback_stall_ms": (1.25, 40.0),
    "max_state_stall_ms": (1.25, 100.0),
  },
}
ABSOLUTE_LIMITS = {
  "max_playback_stall_ms": 500.0,
  "max_state_stall_ms": 1000.0,
}


class ValidationFailure(RuntimeError):
  pass


def wait_for_x_ready(display, timeout=5.0):
  deadline = time.monotonic() + timeout
  env = {"DISPLAY": display}
  while time.monotonic() < deadline:
    result = subprocess.run(
      ["xdpyinfo"],
      env=env,
      stdout=subprocess.DEVNULL,
      stderr=subprocess.DEVNULL,
      text=True,
    )
    if result.returncode == 0:
      return
    time.sleep(0.05)
  raise ValidationFailure(f"X display {display} did not become ready")


def write_json(path, data):
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(json.dumps(data, indent=2))


def load_json(path):
  with path.open() as f:
    return json.load(f)


def remove_tree(path, *, retries=10, delay=0.05):
  path = Path(path)
  last_error = None
  for _ in range(retries):
    if not path.exists():
      return
    try:
      shutil.rmtree(path)
      return
    except FileNotFoundError:
      return
    except OSError as exc:
      last_error = exc
      time.sleep(delay)
  if last_error is not None:
    raise last_error


def rect_center(rect, x_ratio=0.5, y_ratio=0.5):
  return int(rect[0] + rect[2] * x_ratio), int(rect[1] + rect[3] * y_ratio)


def first_dbc_file(state):
  dbc_files = state.get("dbc_files", [])
  return dbc_files[0]["filename"] if dbc_files else None


def resolve_dbc_from_fingerprint(state):
  fingerprint = state.get("car_fingerprint", "")
  if not fingerprint:
    return None
  dbc_name = generate_dbc_dict().get(fingerprint)
  if not dbc_name:
    return None
  candidates = [
    ROOT / "opendbc_repo" / "opendbc" / "dbc" / f"{dbc_name}.dbc",
    ROOT / "opendbc" / "dbc" / f"{dbc_name}.dbc",
  ]
  for candidate in candidates:
    if candidate.exists():
      return str(candidate)
  return None


def action_manifest(actions):
  manifest = {}
  for action in actions:
    path = action["path"]
    if any(path.startswith(prefix) for prefix in ACTION_IGNORE_PREFIXES):
      continue
    manifest[path] = {
      "enabled": bool(action.get("enabled", False)),
      "visible": bool(action.get("visible", False)),
      "checkable": bool(action.get("checkable", False)),
    }
  return manifest


def text_prefix(text, width=5):
  match = re.search(r"[A-Za-z0-9_]{3,}", text)
  if not match:
    raise ValidationFailure(f"Could not derive a stable filter prefix from {text!r}")
  return match.group(0)[:width]


class CabanaSession:
  def __init__(self, suite, name, *, no_vipc=True, route=None, dbc_path=None,
               allow_session_restore=False, profile_dir=None, cleanup_profile=True,
               cleanup_existing=True):
    self.suite = suite
    self.name = name
    self.no_vipc = no_vipc
    self.route = route or suite.route
    self.dbc_path = dbc_path
    self.allow_session_restore = allow_session_restore
    self.profile_dir = Path(profile_dir) if profile_dir is not None else None
    self.cleanup_profile = cleanup_profile
    self.session_dir = suite.output_dir / name
    if cleanup_existing and self.session_dir.exists():
      remove_tree(self.session_dir)
    self.session_dir.mkdir(parents=True, exist_ok=True)
    self.log_path = self.session_dir / "cabana.log"
    self.stats_path = self.session_dir / "stats.json"
    self.ready_png = self.session_dir / "ready.png"
    self.state_path = self.session_dir / "state.json"
    self.summary_path = self.session_dir / "summary.json"
    for stale in [self.log_path, self.stats_path, self.ready_png, self.state_path, self.summary_path]:
      stale.unlink(missing_ok=True)
    self.display_num = None
    self.display = None
    self.proc = None
    self.xvfb_proc = None
    self.window_id = None
    self._created_profile = False

  def launch(self, *, wait_ready=True):
    if self.profile_dir is None:
      self.profile_dir = Path(tempfile.mkdtemp(prefix=f"{self.name}_profile_", dir=self.session_dir))
      self._created_profile = True
    self.display_num = find_free_display()
    self.display = f":{self.display_num}"
    self.xvfb_proc = subprocess.Popen(
      ["Xvfb", self.display, "-screen", "0", f"{self.suite.args.size}x24"],
      stdout=subprocess.DEVNULL,
      stderr=subprocess.DEVNULL,
      start_new_session=True,
    )
    wait_for_socket(self.display_num)
    wait_for_x_ready(self.display)

    env = os.environ.copy()
    env.update({
      "DISPLAY": self.display,
      "HOME": str(self.profile_dir),
      "XDG_CONFIG_HOME": str(self.profile_dir / ".config"),
      "LIBGL_ALWAYS_SOFTWARE": "1",
      "CABANA_SMOKETEST": "1",
      "CABANA_SMOKETEST_SIZE": self.suite.args.size,
      "CABANA_SMOKETEST_STATS": str(self.stats_path),
      "CABANA_SMOKETEST_SCREENSHOT": str(self.ready_png),
      "CABANA_VALIDATION_STATE": str(self.state_path),
    })
    if self.allow_session_restore:
      env["CABANA_VALIDATION_ALLOW_SESSION_RESTORE"] = "1"

    cmd = [str(self.suite.binary)]
    if self.no_vipc:
      cmd.append("--no-vipc")
    if self.dbc_path:
      cmd.extend(["--dbc", str(self.dbc_path)])
    route = self.route or demo_segment_route() or "--demo"
    if route == "--demo":
      cmd.append("--demo")
    else:
      cmd.append(route)
    if self.suite.args.data_dir:
      cmd.extend(["--data_dir", self.suite.args.data_dir])

    with self.log_path.open("w") as log_file:
      self.proc = subprocess.Popen(
        cmd,
        cwd=ROOT,
        env=env,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        start_new_session=True,
      )
    self.window_id = find_window_id(self.proc.pid, self.display, timeout=5.0)
    self.focus_window()
    if wait_ready:
      try:
        self.stats = load_stats(self.stats_path, self.log_path, self.suite.args.timeout)
      except TimeoutError as exc:
        self.ensure_alive()
        state = self.read_state()
        if state is not None:
          dialogs = state.get("dialogs", [])
          if dialogs:
            dialog = dialogs[0]
            title = dialog.get("window_title") or dialog.get("object_name") or "unknown dialog"
            message = dialog.get("text") or dialog.get("informative_text")
            if message:
              raise ValidationFailure(f"{self.name}: startup blocked by dialog {title!r}: {message}") from exc
            raise ValidationFailure(f"{self.name}: startup blocked by dialog {title!r}") from exc
        raise ValidationFailure(f"{self.name}: timed out waiting for ready stats") from exc
      self.wait_for_state(lambda state: state.get("ready"), timeout=5.0, desc="ready validation state")
    return self

  def xrun(self, args, *, capture_output=False, check=True):
    env = {"DISPLAY": self.display}
    return subprocess.run(
      args,
      env=env,
      check=check,
      text=True,
      capture_output=capture_output,
    )

  def ensure_alive(self):
    if self.proc is not None and self.proc.poll() is not None:
      raise ValidationFailure(
        f"{self.name}: Cabana exited with code {self.proc.returncode}\n{self.log_excerpt()}"
      )

  def read_state(self):
    if not self.state_path.exists() or self.state_path.stat().st_size == 0:
      return None
    try:
      return load_json(self.state_path)
    except json.JSONDecodeError:
      return None

  def wait_for_state(self, predicate, *, timeout, desc, poll=0.05):
    deadline = time.monotonic() + timeout
    last_state = None
    while time.monotonic() < deadline:
      self.ensure_alive()
      state = self.read_state()
      if state is not None:
        last_state = state
        if predicate(state):
          return state
      time.sleep(poll)
    raise ValidationFailure(f"{self.name}: timed out waiting for {desc}")

  def current_state(self):
    state = self.read_state()
    if state is None:
      raise ValidationFailure(f"{self.name}: validation state file is not readable")
    return state

  def widget_rect(self, widget_name, *, state=None):
    state = state or self.current_state()
    widget = state.get("widgets", {}).get(widget_name)
    if not widget:
      raise ValidationFailure(f"{self.name}: missing widget {widget_name}")
    rect = widget.get("rect")
    if not rect or rect[2] <= 0 or rect[3] <= 0:
      raise ValidationFailure(f"{self.name}: widget {widget_name} has invalid rect {rect}")
    return rect

  def click_rect(self, rect, *, button=1, x_ratio=0.5, y_ratio=0.5):
    x, y = rect_center(rect, x_ratio, y_ratio)
    self.xrun(["xdotool", "mousemove", "--sync", str(x), str(y)])
    self.xrun(["xdotool", "click", str(button)])

  def focus_window(self):
    self.xrun(["xdotool", "windowfocus", "--sync", str(self.window_id)], check=False)

  def click_widget(self, widget_name, *, state=None, button=1):
    self.click_rect(self.widget_rect(widget_name, state=state), button=button)

  def double_click_widget(self, widget_name, *, state=None):
    rect = self.widget_rect(widget_name, state=state)
    x, y = rect_center(rect)
    self.xrun(["xdotool", "mousemove", "--sync", str(x), str(y)])
    self.xrun(["xdotool", "click", "--repeat", "2", "--delay", "50", "1"])

  def key(self, sequence):
    if "+" in sequence:
      self.xrun(["xdotool", "key", "--clearmodifiers", sequence])
      return
    self.key_down(sequence)
    time.sleep(0.10)
    self.key_up(sequence)

  def key_down(self, sequence):
    self.xrun(["xdotool", "keydown", sequence])

  def key_up(self, sequence):
    self.xrun(["xdotool", "keyup", sequence])

  def type_text(self, text):
    for char in text:
      self.xrun(["xdotool", "type", "--clearmodifiers", "--delay", "0", char])
      time.sleep(0.03)

  def main_window_key(self, sequence):
    self.key(sequence)

  def clear_and_type_widget(self, widget_name, text, *, state=None):
    self.click_widget(widget_name, state=state)
    self.key("ctrl+a")
    self.key("BackSpace")
    if text:
      self.type_text(text)

  def drag_between(self, start, end, *, button=1, duration_ms=120):
    self.xrun(["xdotool", "mousemove", "--sync", str(start[0]), str(start[1])])
    self.xrun(["xdotool", "mousedown", str(button)])
    self.xrun(["xdotool", "mousemove", "--sync", str(end[0]), str(end[1])])
    time.sleep(duration_ms / 1000.0)
    self.xrun(["xdotool", "mouseup", str(button)])

  def drag_rect(self, rect, *, start_ratio=(0.25, 0.5), end_ratio=(0.75, 0.5), button=1):
    start = rect_center(rect, *start_ratio)
    end = rect_center(rect, *end_ratio)
    self.drag_between(start, end, button=button)

  def shift_click_rect(self, rect):
    self.key_down("Shift_L")
    try:
      self.click_rect(rect)
    finally:
      self.key_up("Shift_L")

  def scroll_rect(self, rect, amount):
    self.click_rect(rect)
    button = "4" if amount > 0 else "5"
    for _ in range(abs(amount)):
      self.xrun(["xdotool", "click", button])

  def wait_until(self, predicate, *, timeout, desc, poll=0.05):
    return self.wait_for_state(predicate, timeout=timeout, desc=desc, poll=poll)

  def wait_for_dialog(self, pattern, *, timeout=5.0):
    regex = re.compile(pattern)
    return self.wait_until(
      lambda state: any(regex.search(dialog.get("window_title", "") or dialog.get("object_name", "")) for dialog in state.get("dialogs", [])),
      timeout=timeout,
      desc=f"dialog {pattern!r}",
    )

  def capture(self, name, *, threshold=12, max_diff_pct=0.75, use_ready=False):
    actual_path = self.session_dir / f"{name}.png"
    diff_path = self.session_dir / f"{name}.diff.png"
    baseline_dir = self.suite.baseline_dir / self.name
    baseline_path = baseline_dir / f"{name}.png"
    if use_ready and self.ready_png.exists():
      shutil.copy2(self.ready_png, actual_path)
    else:
      capture_steady_window(self.window_id, actual_path, self.display, timeout=1.5, interval=0.1)
    baseline_dir.mkdir(parents=True, exist_ok=True)
    if self.suite.args.update_baseline or not baseline_path.exists():
      shutil.copy2(actual_path, baseline_path)
    diff = make_diff_image(actual_path, baseline_path, diff_path, threshold)
    if diff["changed_pixels_pct"] > max_diff_pct:
      raise ValidationFailure(
        f"{self.name}: screenshot {name} diff {diff['changed_pixels_pct']:.4f}% exceeded {max_diff_pct:.4f}%"
      )
    return {
      "actual": str(actual_path),
      "baseline": str(baseline_path),
      "diff": str(diff_path),
      "metrics": diff,
    }

  def graceful_close(self):
    if self.proc is None or self.proc.poll() is not None:
      return
    try:
      self.main_window_key("ctrl+q")
      self.proc.wait(timeout=3.0)
    except Exception:
      try:
        self.xrun(["xdotool", "windowclose", str(self.window_id)], check=False)
        self.proc.wait(timeout=2.0)
      except Exception:
        terminate_process(self.proc)

  def close(self, *, graceful=False):
    if graceful:
      self.graceful_close()
    if self.proc is not None:
      terminate_process(self.proc)
      self.proc = None
    if self.xvfb_proc is not None:
      terminate_process(self.xvfb_proc)
      self.xvfb_proc = None
    if self._created_profile and self.cleanup_profile:
      remove_tree(self.profile_dir)

  def log_excerpt(self, lines=40):
    if not self.log_path.exists():
      return ""
    content = self.log_path.read_text(errors="replace").splitlines()
    excerpt = "\n".join(content[-lines:])
    return f"Last log lines:\n{excerpt}" if excerpt else ""


class ValidationSuite:
  def __init__(self, args):
    self.args = args
    self.binary = Path(args.cabana_bin).resolve() if args.cabana_bin else ensure_cabana_built(args.build)
    self.output_dir = Path(args.output_dir).resolve()
    self.baseline_dir = Path(args.baseline_dir).resolve()
    self.output_dir.mkdir(parents=True, exist_ok=True)
    self.baseline_dir.mkdir(parents=True, exist_ok=True)
    self.route = args.route or demo_segment_route() or "--demo"
    self.summary = {"route": self.route, "results": []}
    self.matching_dbc = None

  def record_result(self, name, result):
    result["name"] = name
    self.summary["results"].append(result)
    write_json(self.output_dir / "summary.json", self.summary)

  def compare_metrics(self, scenario_name, metrics):
    baseline_metrics_path = self.baseline_dir / scenario_name / "metrics.json"
    if self.args.update_baseline or not baseline_metrics_path.exists():
      write_json(baseline_metrics_path, metrics)
      return
    baseline = load_json(baseline_metrics_path)
    for metric_name, value in metrics.items():
      absolute_limit = ABSOLUTE_LIMITS.get(metric_name)
      if absolute_limit is not None and value > absolute_limit:
        raise ValidationFailure(
          f"{scenario_name}: metric {metric_name}={value:.2f} exceeded absolute limit {absolute_limit:.2f}"
        )
      if metric_name not in METRIC_LIMITS.get(scenario_name, {}) or metric_name not in baseline:
        continue
      factor, slack = METRIC_LIMITS[scenario_name][metric_name]
      limit = baseline[metric_name] * factor + slack
      if value > limit:
        raise ValidationFailure(
          f"{scenario_name}: metric {metric_name}={value:.2f} regressed past baseline {baseline[metric_name]:.2f} (limit {limit:.2f})"
        )

  def require_matching_dbc(self):
    if self.matching_dbc:
      return self.matching_dbc
    session = CabanaSession(self, "_discover_matching_dbc").launch()
    try:
      state = session.current_state()
      self.matching_dbc = first_dbc_file(state)
      if not self.matching_dbc:
        self.matching_dbc = resolve_dbc_from_fingerprint(state)
      if not self.matching_dbc:
        raise ValidationFailure("Could not discover the current route's loaded DBC file")
      return self.matching_dbc
    finally:
      session.close()


def select_message_with_signals(session):
  state = session.current_state()
  if state.get("detail", {}).get("signal_rows") and state.get("messages", {}).get("current_row", -1) >= 0:
    return state
  for row in candidate_message_rows(state):
    state = select_message(session, row)
    if state.get("detail", {}).get("signal_rows"):
      return state
  raise ValidationFailure(f"{session.name}: could not find a visible message with signals")


def find_alternate_message_with_signals(session, current_message_id):
  state = session.current_state()
  for row in candidate_message_rows(state):
    if row["message_id"] == current_message_id:
      continue
    next_state = select_message(session, row)
    if next_state.get("detail", {}).get("signal_rows"):
      return row, next_state
  raise ValidationFailure(f"{session.name}: could not find a second visible message with signals")


def wait_for_message_filter(session, prefix):
  clear_and_type(session, "MessagesFilterName", prefix)
  state = session.wait_until(
    lambda current: current.get("messages", {}).get("row_count", 0) > 0 and current.get("messages", {}).get("rows", [{}])[0].get("name", "").startswith(prefix),
    timeout=1.0,
    desc=f"message filter prefix {prefix}",
  )
  return state


def candidate_message_rows(state):
  return list(state.get("messages", {}).get("rows", []))


def is_imgui_backend(state):
  return state.get("app_backend") == "imgui_cabana"


def select_message(session, row):
  state = session.current_state()
  if is_imgui_backend(state):
    target_message_id = row["message_id"]
    for _ in range(max(3, len(candidate_message_rows(state)) + 2)):
      state = session.current_state()
      current_id = state.get("messages", {}).get("current_message_id")
      if current_id == target_message_id:
        return state
      rows = candidate_message_rows(state)
      row_ids = [candidate["message_id"] for candidate in rows]
      if target_message_id not in row_ids:
        raise ValidationFailure(f"{session.name}: target message {target_message_id} is not visible")
      current_pos = row_ids.index(current_id) if current_id in row_ids else 0
      target_pos = row_ids.index(target_message_id)
      session.main_window_key("F12" if target_pos > current_pos else "F11")
      time.sleep(0.12)
    return session.wait_until(
      lambda current: current.get("messages", {}).get("current_message_id") == target_message_id,
      timeout=1.0,
      desc=f"select message {target_message_id}",
    )
  session.click_rect(row["rect"])
  return session.wait_until(
    lambda current: current.get("messages", {}).get("current_message_id") == row["message_id"],
    timeout=1.0,
    desc=f"select message {row['message_id']}",
  )


def focus_widget_for_typing(session, widget_name, *, state=None):
  state = state or session.current_state()
  if not is_imgui_backend(state):
    session.click_widget(widget_name, state=state)
    return
  keymap = {
    "MessagesFilterName": "F3",
    "SignalFilterEdit": "F4",
    "EditMessageNameEdit": None,
  }
  hotkey = keymap.get(widget_name)
  if hotkey:
    session.main_window_key(hotkey)


def clear_and_type(session, widget_name, text, *, state=None):
  state = state or session.current_state()
  focus_widget_for_typing(session, widget_name, state=state)
  if is_imgui_backend(state):
    time.sleep(0.05)
    current_text = state.get("widgets", {}).get(widget_name, {}).get("text", "") or ""
    for _ in range(max(4, len(current_text) + 2)):
      session.key("BackSpace")
  else:
    session.key("ctrl+a")
    session.key("BackSpace")
  if text:
    session.type_text(text)


def toggle_signal_plot(session, signal_row):
  state = session.current_state()
  if is_imgui_backend(state):
    session.main_window_key("F5")
    return
  session.click_rect(signal_row["plot_rect"])


def open_edit_dialog(session):
  state = session.current_state()
  if is_imgui_backend(state):
    session.main_window_key("F2")
    return
  session.click_widget("EditMessageButton")


def remove_all_charts(session):
  state = session.current_state()
  if is_imgui_backend(state):
    session.main_window_key("F6")
    return
  session.click_widget("ChartsRemoveAllButton")


def zoom_chart(session, chart_rect):
  state = session.current_state()
  if is_imgui_backend(state):
    session.main_window_key("F7")
    return
  session.drag_rect(chart_rect, start_ratio=(0.20, 0.40), end_ratio=(0.80, 0.40))


def reset_chart_zoom(session):
  state = session.current_state()
  if is_imgui_backend(state):
    session.main_window_key("F8")
    return
  session.click_widget("ChartsResetZoomButton")


def toggle_play_pause(session):
  state = session.current_state()
  if is_imgui_backend(state):
    session.main_window_key("space")
    return
  session.click_widget("PlaybackPlayToggleButton")


def seek_forward(session):
  state = session.current_state()
  if is_imgui_backend(state):
    session.main_window_key("Right")
    return
  session.click_widget("PlaybackSeekForwardButton")


def synthetic_slider_seek(session):
  state = session.current_state()
  if is_imgui_backend(state):
    session.main_window_key("F10")
    return True
  return False


def monitor_playback(session, *, duration, stall_timeout=0.5):
  deadline = time.monotonic() + duration
  state = session.current_state()
  last_snapshot_ms = state.get("snapshot_time_ms")
  last_snapshot_change = time.monotonic()
  last_sec = float(state.get("current_sec", 0.0))
  last_progress = time.monotonic()
  max_playback_stall = 0.0
  max_state_stall = 0.0

  while time.monotonic() < deadline:
    session.ensure_alive()
    state = session.current_state()
    now = time.monotonic()
    snapshot_ms = state.get("snapshot_time_ms")
    if snapshot_ms != last_snapshot_ms:
      last_snapshot_ms = snapshot_ms
      last_snapshot_change = now
    max_state_stall = max(max_state_stall, (now - last_snapshot_change) * 1000.0)
    if max_state_stall > ABSOLUTE_LIMITS["max_state_stall_ms"]:
      raise ValidationFailure(f"{session.name}: validation state stalled for {max_state_stall:.1f}ms")

    paused = bool(state.get("paused", True))
    current_sec = float(state.get("current_sec", last_sec))
    if not paused:
      if current_sec > last_sec + 0.01:
        last_sec = current_sec
        last_progress = now
      max_playback_stall = max(max_playback_stall, (now - last_progress) * 1000.0)
      if max_playback_stall > stall_timeout * 1000.0:
        raise ValidationFailure(f"{session.name}: playback stalled for {max_playback_stall:.1f}ms")
    else:
      last_sec = current_sec
      last_progress = now
    time.sleep(0.05)

  return {
    "max_playback_stall_ms": max_playback_stall,
    "max_state_stall_ms": max_state_stall,
    "max_ui_gap_ms": float(state.get("max_ui_gap_ms", 0.0)),
  }


def ensure_no_unexpected_dialogs(session, *, allow_patterns=()):
  patterns = [re.compile(pattern) for pattern in allow_patterns]
  dialogs = session.current_state().get("dialogs", [])
  unexpected = []
  for dialog in dialogs:
    title = dialog.get("window_title", "") or dialog.get("object_name", "")
    if any(pattern.search(title) for pattern in patterns):
      continue
    unexpected.append(title)
  if unexpected:
    raise ValidationFailure(f"{session.name}: unexpected dialogs visible: {unexpected}")


def scenario_startup_visual(suite):
  session = CabanaSession(suite, "startup_visual").launch()
  try:
    state = session.current_state()
    missing_widgets = [name for name in DEFAULT_REQUIRED_WIDGETS if name not in state.get("widgets", {})]
    if missing_widgets:
      raise ValidationFailure(f"startup_visual: missing required widgets {missing_widgets}")
    if state.get("messages", {}).get("row_count", 0) <= 0:
      raise ValidationFailure("startup_visual: message list is empty")

    actions = action_manifest(state.get("actions", []))
    actions_path = suite.baseline_dir / "startup_visual" / "actions.json"
    if suite.args.update_baseline or not actions_path.exists():
      write_json(actions_path, actions)
    else:
      baseline_actions = load_json(actions_path)
      missing_paths = sorted(path for path in baseline_actions if path not in actions)
      regressed_actions = sorted(
        path for path, info in baseline_actions.items()
        if path in actions and info.get("enabled") and not actions[path].get("enabled")
      )
      if missing_paths:
        raise ValidationFailure(f"startup_visual: missing menu actions {missing_paths}")
      if regressed_actions:
        raise ValidationFailure(f"startup_visual: menu actions unexpectedly disabled {regressed_actions}")

    screenshot = session.capture("startup_ready", threshold=suite.args.threshold, max_diff_pct=0.50, use_ready=True)
    metrics = {
      "route_load_ms": float(session.stats.get("route_load_ms", 0.0)),
      "steady_state_ms": float(session.stats.get("steady_state_ms", 0.0)),
      "max_ui_gap_ms": float(state.get("max_ui_gap_ms", 0.0)),
    }
    suite.compare_metrics("startup_visual", metrics)
    suite.matching_dbc = first_dbc_file(state)
    result = {
      "pass": True,
      "metrics": metrics,
      "dbc_file": suite.matching_dbc,
      "artifacts": screenshot,
    }
    write_json(session.summary_path, result)
    return result
  finally:
    session.close()


def scenario_core_workflow(suite):
  session = CabanaSession(suite, "core_workflow", dbc_path=suite.require_matching_dbc()).launch()
  try:
    state = select_message_with_signals(session)
    for widget_name in ["DetailToolbar", "DetailMessageLabel", "SignalView", "SignalTree", "SignalFilterEdit"]:
      if widget_name not in state.get("widgets", {}):
        raise ValidationFailure(f"core_workflow: missing widget {widget_name}")
    current_message_id = state["messages"]["current_message_id"]
    selection_latency_ms = None
    row = None
    for candidate in candidate_message_rows(state):
      if candidate["message_id"] == current_message_id:
        continue
      select_start = time.monotonic()
      next_state = select_message(session, candidate)
      if next_state.get("detail", {}).get("signal_rows"):
        row = candidate
        state = next_state
        selection_latency_ms = (time.monotonic() - select_start) * 1000.0
        break
    if row is None or selection_latency_ms is None:
      raise ValidationFailure("core_workflow: could not find an alternate visible message with signals")
    prefix = text_prefix(row["name"])

    filter_start = time.monotonic()
    state = wait_for_message_filter(session, prefix)
    filter_latency_ms = (time.monotonic() - filter_start) * 1000.0
    clear_and_type(session, "MessagesFilterName", "")
    session.wait_until(
      lambda current: current.get("widgets", {}).get("MessagesFilterName", {}).get("text", "") == "",
      timeout=1.0,
      desc="clear message filter",
    )

    state = select_message_with_signals(session)
    signal_rows = state.get("detail", {}).get("signal_rows", [])
    if not signal_rows:
      raise ValidationFailure("core_workflow: selected message has no signal rows")

    chart_start = time.monotonic()
    toggle_signal_plot(session, signal_rows[0])
    state = session.wait_until(
      lambda current: current.get("charts", {}).get("count", 0) >= 1,
      timeout=1.5,
      desc="chart to open",
    )
    chart_add_latency_ms = (time.monotonic() - chart_start) * 1000.0
    chart_screenshot = session.capture("chart_open", threshold=suite.args.threshold, max_diff_pct=1.25)

    chart_rects = state.get("charts", {}).get("rects", [])
    if chart_rects:
      zoom_start = time.monotonic()
      zoom_chart(session, chart_rects[0])
      state = session.wait_until(
        lambda current: "time_range_start" in current and "time_range_end" in current,
        timeout=2.0,
        desc="chart zoom",
      )
      zoom_latency_ms = (time.monotonic() - zoom_start) * 1000.0
      reset_chart_zoom(session)
      session.wait_until(
        lambda current: "time_range_start" not in current and "time_range_end" not in current,
        timeout=1.5,
        desc="chart zoom reset",
      )
    else:
      raise ValidationFailure("core_workflow: chart rects missing after opening chart")

    remove_all_charts(session)
    session.wait_until(
      lambda current: current.get("charts", {}).get("count", 0) == 0,
      timeout=1.0,
      desc="remove all charts",
    )

    toggle_play_pause(session)
    session.wait_until(lambda current: not current.get("paused", True), timeout=1.0, desc="playback resume")
    playback_metrics = monitor_playback(session, duration=5.0)

    seek_before = session.current_state()["current_sec"]
    seek_start = time.monotonic()
    seek_forward(session)
    session.wait_until(
      lambda current: current.get("current_sec", 0.0) > seek_before + 0.5,
      timeout=1.5,
      desc="seek forward",
    )
    seek_latency_ms = (time.monotonic() - seek_start) * 1000.0

    slider_rect = session.current_state().get("video", {}).get("slider_rect")
    if slider_rect:
      pre_drag_sec = float(session.current_state().get("current_sec", 0.0))
      if not synthetic_slider_seek(session):
        session.drag_rect(slider_rect, start_ratio=(0.30, 0.50), end_ratio=(0.60, 0.50))
      session.wait_until(
        lambda current: abs(float(current.get("current_sec", 0.0)) - pre_drag_sec) > 0.25,
        timeout=1.5,
        desc="playback slider drag",
      )
    toggle_play_pause(session)
    session.wait_until(lambda current: current.get("paused", True), timeout=1.0, desc="playback pause")

    state = session.current_state()
    metrics = {
      "selection_latency_ms": selection_latency_ms,
      "filter_latency_ms": filter_latency_ms,
      "chart_add_latency_ms": chart_add_latency_ms,
      "zoom_latency_ms": zoom_latency_ms,
      "seek_latency_ms": seek_latency_ms,
      "max_playback_stall_ms": playback_metrics["max_playback_stall_ms"],
      "max_ui_gap_ms": float(state.get("max_ui_gap_ms", 0.0)),
    }
    suite.compare_metrics("core_workflow", metrics)
    result = {
      "pass": True,
      "metrics": metrics,
      "artifacts": {"chart_open": chart_screenshot},
    }
    write_json(session.summary_path, result)
    return result
  finally:
    session.close()


def scenario_edit_save_reload(suite):
  source_dbc = suite.require_matching_dbc()
  scenario_dir = suite.output_dir / "edit_save_reload"
  scenario_dir.mkdir(parents=True, exist_ok=True)
  temp_dbc = scenario_dir / "editable.dbc"
  shutil.copy2(source_dbc, temp_dbc)

  session = CabanaSession(suite, "edit_save_reload", dbc_path=temp_dbc, cleanup_existing=False).launch()
  try:
    state = select_message_with_signals(session)
    message_id = state["messages"]["current_message_id"]
    new_name = f"VALID_{message_id.replace(':', '_')}"
    original_text = temp_dbc.read_text()

    edit_start = time.monotonic()
    open_edit_dialog(session)
    session.wait_for_dialog("Edit message")
    clear_and_type(session, "EditMessageNameEdit", new_name)
    session.key("Return")
    state = session.wait_until(
      lambda current: new_name in current.get("detail", {}).get("message_label", ""),
      timeout=2.0,
      desc="edited message label",
    )
    edit_latency_ms = (time.monotonic() - edit_start) * 1000.0

    save_start = time.monotonic()
    session.main_window_key("ctrl+s")
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
      if new_name in temp_dbc.read_text():
        break
      time.sleep(0.05)
    else:
      raise ValidationFailure("edit_save_reload: edited DBC did not persist to disk")
    save_latency_ms = (time.monotonic() - save_start) * 1000.0
    if original_text == temp_dbc.read_text():
      raise ValidationFailure("edit_save_reload: DBC file contents did not change")
  finally:
    session.close(graceful=True)

  reload_session = CabanaSession(suite, "edit_save_reload_reload", dbc_path=temp_dbc).launch()
  try:
    reload_start = time.monotonic()
    state = reload_session.current_state()
    row = next((row for row in state.get("messages", {}).get("rows", []) if row["message_id"] == message_id), None)
    if row is None:
      raise ValidationFailure(f"edit_save_reload: edited message {message_id} is no longer visible")
    select_message(reload_session, row)
    reload_session.wait_until(
      lambda current: new_name in current.get("detail", {}).get("message_label", ""),
      timeout=2.0,
      desc="reloaded edited message label",
    )
    reload_ready_ms = (time.monotonic() - reload_start) * 1000.0
    metrics = {
      "edit_latency_ms": edit_latency_ms,
      "save_latency_ms": save_latency_ms,
      "reload_ready_ms": reload_ready_ms,
    }
    suite.compare_metrics("edit_save_reload", metrics)
    result = {
      "pass": True,
      "metrics": metrics,
      "dbc_file": str(temp_dbc),
      "edited_name": new_name,
    }
    write_json(reload_session.summary_path, result)
    return result
  finally:
    reload_session.close()


def scenario_session_restore(suite):
  source_dbc = suite.require_matching_dbc()
  scenario_dir = suite.output_dir / "session_restore"
  scenario_dir.mkdir(parents=True, exist_ok=True)
  temp_dbc = scenario_dir / "restored.dbc"
  shutil.copy2(source_dbc, temp_dbc)
  profile_dir = scenario_dir / "profile"
  profile_dir.mkdir(parents=True, exist_ok=True)

  session = CabanaSession(
    suite,
    "session_restore_seed",
    dbc_path=temp_dbc,
    allow_session_restore=True,
    profile_dir=profile_dir,
    cleanup_profile=False,
  ).launch()
  try:
    state = select_message_with_signals(session)
    row, state = find_alternate_message_with_signals(session, state["messages"]["current_message_id"])
    selected_message_id = row["message_id"]
    signal_rows = state.get("detail", {}).get("signal_rows", [])
    if not signal_rows:
      raise ValidationFailure("session_restore: selected message has no signal rows")
    toggle_signal_plot(session, signal_rows[0])
    session.wait_until(
      lambda current: current.get("charts", {}).get("count", 0) >= 1,
      timeout=1.5,
      desc="chart before restart",
    )
  finally:
    session.close(graceful=True)

  restore_session = CabanaSession(
    suite,
    "session_restore_check",
    dbc_path=temp_dbc,
    allow_session_restore=True,
    profile_dir=profile_dir,
    cleanup_profile=False,
  ).launch()
  try:
    restore_start = time.monotonic()
    state = restore_session.wait_until(
      lambda current: current.get("detail", {}).get("current_message_id") == selected_message_id and current.get("charts", {}).get("count", 0) >= 1,
      timeout=3.0,
      desc="session restore state",
    )
    metrics = {
      "restore_ready_ms": float(restore_session.stats.get("steady_state_ms", 0.0)),
      "restore_latency_ms": (time.monotonic() - restore_start) * 1000.0,
    }
    suite.compare_metrics("session_restore", metrics)
    result = {
      "pass": True,
      "metrics": metrics,
      "restored_message_id": selected_message_id,
      "chart_count": state.get("charts", {}).get("count", 0),
    }
    write_json(restore_session.summary_path, result)
    return result
  finally:
    restore_session.close(graceful=True)
    remove_tree(profile_dir)


def scenario_video_sanity(suite):
  session = CabanaSession(suite, "video_sanity", no_vipc=False, dbc_path=suite.require_matching_dbc()).launch()
  try:
    state = session.current_state()
    slider = state.get("video", {})
    if not slider.get("slider_visible"):
      raise ValidationFailure("video_sanity: playback slider is not visible")
    screenshot = session.capture("video_ready", threshold=suite.args.threshold, max_diff_pct=1.50, use_ready=True)
    toggle_play_pause(session)
    session.wait_until(lambda current: not current.get("paused", True), timeout=1.0, desc="video playback resume")
    playback_metrics = monitor_playback(session, duration=4.0)
    toggle_play_pause(session)
    session.wait_until(lambda current: current.get("paused", True), timeout=1.0, desc="video playback pause")
    state = session.current_state()
    metrics = {
      "steady_state_ms": float(session.stats.get("steady_state_ms", 0.0)),
      "max_playback_stall_ms": playback_metrics["max_playback_stall_ms"],
      "max_ui_gap_ms": float(state.get("max_ui_gap_ms", 0.0)),
    }
    suite.compare_metrics("video_sanity", metrics)
    result = {
      "pass": True,
      "metrics": metrics,
      "artifacts": screenshot,
    }
    write_json(session.summary_path, result)
    return result
  finally:
    session.close()


def scenario_invalid_dbc(suite):
  scenario_dir = suite.output_dir / "invalid_dbc"
  scenario_dir.mkdir(parents=True, exist_ok=True)
  invalid_dbc = scenario_dir / "invalid.dbc"
  invalid_dbc.write_text("BO_ totally invalid content\nSG_ broken signal")

  session = CabanaSession(suite, "invalid_dbc", dbc_path=invalid_dbc, cleanup_existing=False).launch(wait_ready=False)
  try:
    dialog_start = time.monotonic()
    state = session.wait_until(
      lambda current: any("Failed to load DBC file" in (dialog.get("window_title", "") or "") for dialog in current.get("dialogs", [])),
      timeout=2.0,
      desc="invalid DBC dialog",
    )
    screenshot = session.capture("invalid_dbc_dialog", threshold=suite.args.threshold, max_diff_pct=1.50)
    dialog_latency_ms = (time.monotonic() - dialog_start) * 1000.0
    session.main_window_key("Escape")
    session.wait_until(lambda current: not current.get("dialogs", []), timeout=2.0, desc="dialog dismissal")
    session.stats = load_stats(session.stats_path, session.log_path, suite.args.timeout)
    session.wait_for_state(lambda current: current.get("ready"), timeout=5.0, desc="ready after invalid DBC dialog")
    metrics = {"dialog_latency_ms": dialog_latency_ms}
    suite.compare_metrics("invalid_dbc", metrics)
    result = {
      "pass": True,
      "metrics": metrics,
      "artifacts": screenshot,
      "dialogs": state.get("dialogs", []),
    }
    write_json(session.summary_path, result)
    return result
  finally:
    session.close()


def scenario_input_fuzz(suite):
  source_dbc = suite.require_matching_dbc()
  scenario_dir = suite.output_dir / "input_fuzz"
  scenario_dir.mkdir(parents=True, exist_ok=True)
  temp_dbc = scenario_dir / "fuzz.dbc"
  shutil.copy2(source_dbc, temp_dbc)
  session = CabanaSession(suite, "input_fuzz", dbc_path=temp_dbc, cleanup_existing=False).launch()
  rng = random.Random(suite.args.seed)

  def random_message_click(state):
    rows = state.get("messages", {}).get("rows", [])
    if rows:
      select_message(session, rng.choice(rows))

  def random_signal_click(state):
    rows = state.get("detail", {}).get("signal_rows", [])
    if rows:
      toggle_signal_plot(session, rng.choice(rows))

  def random_plot_toggle(state):
    rows = [row for row in state.get("detail", {}).get("signal_rows", []) if row.get("plot_rect")]
    if rows:
      toggle_signal_plot(session, rng.choice(rows))

  def message_scroll(state):
    if is_imgui_backend(state):
      session.main_window_key(rng.choice(["Page_Up", "Page_Down", "Up", "Down"]))
    else:
      session.scroll_rect(session.widget_rect("MessageTable", state=state), rng.choice([-3, -2, -1, 1, 2, 3]))

  def signal_scroll(state):
    if is_imgui_backend(state):
      session.main_window_key("F4")
      if rng.random() < 0.5:
        session.type_text(rng.choice(["lane", "brak", "stee", ""]))
      else:
        session.key("ctrl+a")
        session.key("BackSpace")
    else:
      session.scroll_rect(session.widget_rect("SignalTree", state=state), rng.choice([-3, -2, -1, 1, 2, 3]))

  def filter_text(state):
    rows = state.get("messages", {}).get("rows", [])
    if not rows:
      return
    prefix = text_prefix(rng.choice(rows)["name"], width=4)
    wait_for_message_filter(session, prefix)
    clear_and_type(session, "MessagesFilterName", "")
    session.wait_until(lambda current: current.get("messages", {}).get("row_count", 0) > 0, timeout=1.0, desc="clear fuzz filter")

  def play_pause(_state):
    toggle_play_pause(session)

  def seek(state):
    if is_imgui_backend(state):
      session.main_window_key(rng.choice(["Left", "Right"]))
    else:
      session.click_widget(rng.choice(["PlaybackSeekBackwardButton", "PlaybackSeekForwardButton"]))

  def slider_drag(state):
    rect = state.get("video", {}).get("slider_rect")
    if synthetic_slider_seek(session):
      return
    if rect:
      start = (0.20, 0.50) if rng.random() < 0.5 else (0.60, 0.50)
      end = (0.60, 0.50) if start[0] < 0.5 else (0.20, 0.50)
      session.drag_rect(rect, start_ratio=start, end_ratio=end)

  def keyboard_nav(_state):
    session.main_window_key(rng.choice(["Up", "Down", "Page_Up", "Page_Down", "space", "ctrl+z", "ctrl+shift+z", "Tab", "F5", "F6", "F7", "F8"]))

  def remove_all_chart_action(state):
    if state.get("charts", {}).get("count", 0) > 0:
      remove_all_charts(session)

  actions = [
    ("random_message_click", random_message_click),
    ("random_message_click", random_message_click),
    ("random_message_click", random_message_click),
    ("random_signal_click", random_signal_click),
    ("random_signal_click", random_signal_click),
    ("random_plot_toggle", random_plot_toggle),
    ("message_scroll", message_scroll),
    ("message_scroll", message_scroll),
    ("signal_scroll", signal_scroll),
    ("signal_scroll", signal_scroll),
    ("play_pause", play_pause),
    ("seek", seek),
    ("seek", seek),
    ("slider_drag", slider_drag),
    ("keyboard_nav", keyboard_nav),
    ("keyboard_nav", keyboard_nav),
    ("keyboard_nav", keyboard_nav),
    ("remove_all_charts", remove_all_chart_action),
    ("filter_text", filter_text),
  ]

  try:
    select_message_with_signals(session)
    if session.current_state().get("paused", True):
      toggle_play_pause(session)
      session.wait_until(lambda current: not current.get("paused", True), timeout=1.0, desc="fuzz playback resume")

    start = time.monotonic()
    last_snapshot_change = start
    last_snapshot_ms = session.current_state().get("snapshot_time_ms")
    last_progress = start
    last_sec = float(session.current_state().get("current_sec", 0.0))
    max_state_stall_ms = 0.0
    max_playback_stall_ms = 0.0
    actions_executed = 0
    action_history = []

    while time.monotonic() - start < suite.args.fuzz_duration:
      state = session.current_state()
      action_name, action = rng.choice(actions)
      action(state)
      actions_executed += 1
      action_history.append(action_name)
      if len(action_history) > 20:
        action_history.pop(0)
      time.sleep(0.02)
      session.ensure_alive()
      state = session.current_state()

      snapshot_ms = state.get("snapshot_time_ms")
      now = time.monotonic()
      if snapshot_ms != last_snapshot_ms:
        last_snapshot_ms = snapshot_ms
        last_snapshot_change = now
      max_state_stall_ms = max(max_state_stall_ms, (now - last_snapshot_change) * 1000.0)
      if max_state_stall_ms > ABSOLUTE_LIMITS["max_state_stall_ms"]:
        raise ValidationFailure(f"input_fuzz: validation state stalled for {max_state_stall_ms:.1f}ms")

      if not state.get("paused", True):
        current_sec = float(state.get("current_sec", last_sec))
        if current_sec > last_sec + 0.01:
          last_sec = current_sec
          last_progress = now
        max_playback_stall_ms = max(max_playback_stall_ms, (now - last_progress) * 1000.0)
        if max_playback_stall_ms > ABSOLUTE_LIMITS["max_playback_stall_ms"]:
          raise ValidationFailure(f"input_fuzz: playback stalled for {max_playback_stall_ms:.1f}ms")
      else:
        last_progress = now
        last_sec = float(state.get("current_sec", last_sec))

      dialogs = [dialog.get("window_title", "") or dialog.get("object_name", "") for dialog in state.get("dialogs", [])]
      if dialogs:
        raise ValidationFailure(
          f"input_fuzz: unexpected dialogs visible after actions {action_history}: {dialogs}"
        )

    state = session.current_state()
    if actions_executed < 25:
      raise ValidationFailure(f"input_fuzz: only executed {actions_executed} actions")
    metrics = {
      "actions_executed": actions_executed,
      "max_state_stall_ms": max_state_stall_ms,
      "max_playback_stall_ms": max_playback_stall_ms,
      "max_ui_gap_ms": float(state.get("max_ui_gap_ms", 0.0)),
    }
    suite.compare_metrics("input_fuzz", metrics)
    result = {
      "pass": True,
      "metrics": metrics,
      "seed": suite.args.seed,
    }
    write_json(session.summary_path, result)
    return result
  finally:
    session.close()


SCENARIOS = {
  "startup_visual": scenario_startup_visual,
  "core_workflow": scenario_core_workflow,
  "edit_save_reload": scenario_edit_save_reload,
  "session_restore": scenario_session_restore,
  "video_sanity": scenario_video_sanity,
  "invalid_dbc": scenario_invalid_dbc,
  "input_fuzz": scenario_input_fuzz,
}


def retryable_failure(message):
  return any(token in message for token in [
    "Timed out waiting for Cabana window",
    "X display",
    "could not connect to display",
  ])


def parse_args():
  parser = argparse.ArgumentParser(description="Headless Cabana validation suite")
  parser.add_argument("--build", action="store_true", help="Build Cabana before running")
  parser.add_argument("--cabana-bin", help="Explicit Cabana binary path")
  parser.add_argument("--timeout", type=float, default=30.0, help="Seconds to wait for startup readiness")
  parser.add_argument("--size", default=DEFAULT_SIZE, help="Window size for deterministic validation")
  parser.add_argument("--threshold", type=int, default=12, help="Per-pixel diff threshold")
  parser.add_argument("--output-dir", default=str(DEFAULT_OUTPUT_DIR), help="Artifact directory")
  parser.add_argument("--baseline-dir", default=str(DEFAULT_BASELINE_DIR), help="Baseline directory")
  parser.add_argument("--update-baseline", action="store_true", help="Refresh screenshots and metric baselines")
  parser.add_argument("--route", help="Explicit route instead of the demo segment")
  parser.add_argument("--data-dir", help="Local route cache")
  parser.add_argument("--fuzz-duration", type=float, default=15.0, help="Seconds to spend in the input fuzzing scenario")
  parser.add_argument("--seed", type=int, default=1337, help="Random seed for input fuzzing")
  parser.add_argument("--scenario", action="append", choices=sorted(SCENARIOS), help="Run only the named scenario")
  parser.add_argument("--continue-on-failure", action="store_true", help="Run remaining scenarios after a failure")
  return parser.parse_args()


def main():
  args = parse_args()
  suite = ValidationSuite(args)
  selected = args.scenario or list(SCENARIOS)
  overall_pass = True

  for name in selected:
    final_exception = None
    for attempt in range(2):
      started = time.monotonic()
      scenario_dir = suite.output_dir / name
      if scenario_dir.exists():
        remove_tree(scenario_dir)
      scenario_dir.mkdir(parents=True, exist_ok=True)
      try:
        result = SCENARIOS[name](suite)
        result["duration_s"] = time.monotonic() - started
        if attempt:
          result["retry_attempt"] = attempt
        suite.record_result(name, result)
        print(f"[PASS] {name}: {result['duration_s']:.2f}s")
        final_exception = None
        break
      except Exception as exc:
        final_exception = exc
        if attempt == 0 and retryable_failure(str(exc)):
          continue
        overall_pass = False
        result = {
          "pass": False,
          "error": str(exc),
          "duration_s": time.monotonic() - started,
        }
        suite.record_result(name, result)
        print(f"[FAIL] {name}: {exc}")
        break
    if final_exception is not None and not args.continue_on_failure:
      break

  suite.summary["pass"] = overall_pass
  write_json(suite.output_dir / "summary.json", suite.summary)
  if not overall_pass:
    raise SystemExit(1)


if __name__ == "__main__":
  main()
