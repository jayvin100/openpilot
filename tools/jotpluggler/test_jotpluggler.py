#!/usr/bin/env python3
"""Fuzz and layout screenshot tests for jotpluggler.
Each test gets its own Xvfb + process, all run in parallel via xdist."""
from __future__ import annotations

import os
import random
import re
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

import pytest

BINARY = Path(__file__).resolve().parent / "jotpluggler"
LAYOUTS_DIR = Path(__file__).resolve().parent / "layouts"
WIDTH, HEIGHT = 1920, 1080
DEMO_ROUTE = "5beb9b58bd12b691/0000010a--a51155e496"
DEMO_ROUTE_QUICK = f"{DEMO_ROUTE}/0/q"
FUZZ_ACTIONS = 300
SCREENSHOT_EVERY = 30
FREEZE_TIMEOUT_S = 8.0
ROUNDS_PER_SCENARIO = 10

KEYS = [
  "Escape", "Return", "Tab", "space", "Delete", "BackSpace",
  "Up", "Down", "Left", "Right",
  "ctrl+z", "ctrl+y", "ctrl+s", "ctrl+a", "ctrl+c", "ctrl+v",
  "Home", "End", "Page_Up", "Page_Down", "F1", "F5", "F11", "plus", "minus",
  "alt+F4", "ctrl+w", "ctrl+n", "ctrl+t",
]

MODIFIERS = ["shift", "ctrl", "alt", "super"]


# -- report directory (shared across xdist workers) -------------------------

def _report_root() -> Path:
  ts = time.strftime("%Y%m%d_%H%M")
  try:
    sha = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], text=True, cwd=BINARY.parent).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], text=True, cwd=BINARY.parent).strip())
    tag = f"{sha}_dirty" if dirty else sha
  except subprocess.CalledProcessError:
    tag = "unknown"
  d = BINARY.parent / "reports" / f"{ts}_{tag}"
  d.mkdir(parents=True, exist_ok=True)
  return d

REPORT_DIR = _report_root()


# -- scenarios ---------------------------------------------------------------

@dataclass(frozen=True)
class Scenario:
  name: str
  cabana: bool = False

SCENARIOS = [
  Scenario("plot"),
  Scenario("cabana", cabana=True),
]


# -- session -----------------------------------------------------------------

class Session:
  def __init__(self, workdir: Path | None = None):
    self.xvfb = self.jotp = None
    self.display = self.winid = ""
    self.env: dict[str, str] = {}
    self.workdir = workdir or Path(tempfile.mkdtemp(prefix="jotp_"))
    self.workdir.mkdir(parents=True, exist_ok=True)
    self._fhs = []
    self._cur_w, self._cur_h = WIDTH, HEIGHT

  def start(self, *, route: str, cabana: bool = False, layout: str | None = None, sync_load: bool = False):
    self.xvfb = subprocess.Popen(
      ["Xvfb", "-displayfd", "1", "-screen", "0", f"{WIDTH}x{HEIGHT}x24", "-dpi", "96", "-nolisten", "tcp"],
      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    self.display = f":{self.xvfb.stdout.readline().strip()}"
    assert self.display != ":"

    self.env = os.environ.copy()
    self.env["DISPLAY"] = self.display
    if cabana:
      self.env["JOTP_START_CABANA"] = "1"

    cmd = [str(BINARY), "--show", "--width", str(WIDTH), "--height", str(HEIGHT)]
    if layout:
      cmd.extend(["--layout", layout])
    if sync_load:
      cmd.append("--sync-load")
    cmd.append(route)

    stdout_fh = open(self.workdir / "stdout.log", "w")
    stderr_fh = open(self.workdir / "stderr.log", "w")
    self._fhs = [stdout_fh, stderr_fh]
    self.jotp = subprocess.Popen(cmd, env=self.env, stdout=stdout_fh, stderr=stderr_fh)

    proc = subprocess.run(
      ["xdotool", "search", "--sync", "--name", "jotpluggler"],
      env=self.env, capture_output=True, text=True, timeout=30,
    )
    wids = proc.stdout.strip().splitlines()
    assert wids, "jotpluggler window never appeared"
    self.winid = wids[0]

    self._xdo("windowmove", self.winid, "0", "0")
    self._xdo("windowsize", "--sync", self.winid, str(WIDTH), str(HEIGHT))
    self._xdo("windowfocus", "--sync", self.winid)
    time.sleep(2.0)

  def stop(self):
    for p in (self.jotp, self.xvfb):
      if p and p.poll() is None:
        p.terminate()
        try:
          p.wait(timeout=5)
        except subprocess.TimeoutExpired:
          p.kill()
    for fh in self._fhs:
      fh.close()
    if self.xvfb:
      for s in (self.xvfb.stdout, self.xvfb.stderr):
        if s:
          s.close()

  @property
  def alive(self):
    return self.jotp is not None and self.jotp.poll() is None

  def crash_reason(self) -> str:
    p = self.workdir / "stderr.log"
    text = p.read_text(errors="replace") if p.exists() else ""
    for pat in [r"Assertion.*failed", r"SIGSEGV|SIGABRT|SIGFPE|SIGBUS", r"AddressSanitizer"]:
      m = re.search(pat, text)
      if m:
        start = text.rfind("\n", 0, m.start()) + 1
        end = text.find("\n", m.end())
        return text[start:end if end != -1 else len(text)].strip()
    return f"exit code {self.jotp.poll() if self.jotp else '?'}"

  def stderr_tail(self, n: int = 20) -> str:
    p = self.workdir / "stderr.log"
    return "\n".join(p.read_text(errors="replace").splitlines()[-n:]) if p.exists() else ""

  def _xdo(self, *args: str, timeout: float = 5.0):
    subprocess.run(["xdotool", *args], env=self.env, check=True, timeout=timeout)

  def click(self, x, y, button=1):
    self._xdo("mousemove", str(x), str(y))
    time.sleep(0.01)
    self._xdo("click", str(button))
    time.sleep(0.03)
    return f"click ({x},{y}) btn={button}"

  def modified_click(self, x, y, modifier, button=1):
    self._xdo("mousemove", str(x), str(y))
    time.sleep(0.01)
    self._xdo("keydown", modifier)
    self._xdo("click", str(button))
    self._xdo("keyup", modifier)
    time.sleep(0.03)
    return f"{modifier}+click ({x},{y}) btn={button}"

  def doubleclick(self, x, y):
    self._xdo("mousemove", str(x), str(y))
    time.sleep(0.01)
    self._xdo("click", "--repeat", "2", "--delay", "80", "1")
    time.sleep(0.03)
    return f"dblclick ({x},{y})"

  def tripleclick(self, x, y):
    self._xdo("mousemove", str(x), str(y))
    time.sleep(0.01)
    self._xdo("click", "--repeat", "3", "--delay", "80", "1")
    time.sleep(0.03)
    return f"tripleclick ({x},{y})"

  def drag(self, x1, y1, x2, y2):
    self._xdo("mousemove", str(x1), str(y1))
    time.sleep(0.01)
    self._xdo("mousedown", "1")
    for i in range(1, 7):
      a = i / 6
      self._xdo("mousemove", str(round(x1 + (x2 - x1) * a)), str(round(y1 + (y2 - y1) * a)))
      time.sleep(0.01)
    self._xdo("mouseup", "1")
    time.sleep(0.03)
    return f"drag ({x1},{y1})->({x2},{y2})"

  def scroll(self, x, y, clicks):
    self._xdo("mousemove", str(x), str(y))
    self._xdo("click", "--repeat", str(abs(clicks)), "4" if clicks > 0 else "5")
    time.sleep(0.03)
    return f"scroll ({x},{y}) n={clicks}"

  def key(self, keys):
    self._xdo("key", keys)
    time.sleep(0.03)
    return f"key {keys}"

  def type_text(self, text):
    self._xdo("type", "--clearmodifiers", text)
    time.sleep(0.03)
    return f"type {text!r}"

  def mousemove(self, x, y):
    self._xdo("mousemove", str(x), str(y))
    return f"move ({x},{y})"

  def resize(self, w, h):
    self._xdo("windowsize", "--sync", self.winid, str(w), str(h))
    self._cur_w, self._cur_h = w, h
    time.sleep(0.05)
    return f"resize {w}x{h}"

  def minimize_restore(self):
    self._xdo("windowminimize", self.winid)
    time.sleep(0.1)
    self._xdo("windowactivate", "--sync", self.winid)
    time.sleep(0.05)
    return "minimize+restore"

  def screenshot(self, path: Path) -> float:
    t0 = time.monotonic()
    xwd = Path(tempfile.mktemp(suffix=".xwd"))
    try:
      subprocess.run(["xwd", "-silent", "-id", self.winid, "-out", str(xwd)],
                     env=self.env, check=True, timeout=FREEZE_TIMEOUT_S)
      subprocess.run(["convert", str(xwd), str(path)],
                     env=self.env, check=True, timeout=FREEZE_TIMEOUT_S)
    finally:
      xwd.unlink(missing_ok=True)
    return time.monotonic() - t0


# -- fuzz helpers ------------------------------------------------------------

def _rand_xy(rng: random.Random, w: int = WIDTH, h: int = HEIGHT) -> tuple[int, int]:
  return rng.randint(0, w - 1), rng.randint(0, h - 1)


def _edge_xy(rng: random.Random, w: int = WIDTH, h: int = HEIGHT) -> tuple[int, int]:
  """Bias toward edges and corners where off-by-one bugs live."""
  edge = rng.choice(["top", "bottom", "left", "right", "corner"])
  if edge == "top":
    return rng.randint(0, w - 1), rng.randint(0, 3)
  if edge == "bottom":
    return rng.randint(0, w - 1), rng.randint(h - 4, h - 1)
  if edge == "left":
    return rng.randint(0, 3), rng.randint(0, h - 1)
  if edge == "right":
    return rng.randint(w - 4, w - 1), rng.randint(0, h - 1)
  # corner
  cx = rng.choice([0, 1, w - 2, w - 1])
  cy = rng.choice([0, 1, h - 2, h - 1])
  return cx, cy


def _rand_action(session: Session, rng: random.Random) -> str:
  w, h = session._cur_w, session._cur_h
  xy = lambda: _rand_xy(rng, w, h)
  kind = rng.choices(
    ["click", "mod_click", "dblclick", "tripleclick", "drag", "scroll",
     "key", "type", "move", "resize", "minimize", "edge_click", "burst"],
    weights=[25, 6, 5, 3, 12, 12, 12, 4, 5, 5, 2, 5, 4], k=1,
  )[0]
  if kind == "click":
    return session.click(*xy(), rng.choice([1, 1, 1, 1, 3]))
  if kind == "mod_click":
    return session.modified_click(*xy(), rng.choice(MODIFIERS), rng.choice([1, 1, 3]))
  if kind == "dblclick":
    return session.doubleclick(*xy())
  if kind == "tripleclick":
    return session.tripleclick(*xy())
  if kind == "drag":
    x1, y1 = xy()
    x2 = max(0, min(w - 1, x1 + rng.randint(-400, 400)))
    y2 = max(0, min(h - 1, y1 + rng.randint(-400, 400)))
    return session.drag(x1, y1, x2, y2)
  if kind == "scroll":
    n = rng.choice([-20, -10, -5, -3, -1, 1, 3, 5, 10, 20])
    return session.scroll(*xy(), n)
  if kind == "key":
    return session.key(rng.choice(KEYS))
  if kind == "type":
    return session.type_text("".join(rng.choices("abcdefghijklmnopqrstuvwxyz0123456789", k=rng.randint(1, 8))))
  if kind == "move":
    return session.mousemove(*xy())
  if kind == "resize":
    nw = rng.choice([640, 800, 1024, 1280, 1600, 1920, 400, 2560])
    nh = rng.choice([480, 600, 768, 720, 900, 1080, 300, 1440])
    return session.resize(nw, nh)
  if kind == "minimize":
    return session.minimize_restore()
  if kind == "edge_click":
    return session.click(*_edge_xy(rng, w, h), rng.choice([1, 1, 3]))
  if kind == "burst":
    # rapid-fire same action with no sleep
    bx, by = xy()
    n = rng.randint(5, 15)
    for _ in range(n):
      session._xdo("mousemove", str(bx), str(by))
      session._xdo("click", "1")
    return f"burst {n}x click ({bx},{by})"
  raise ValueError(kind)


def run_fuzz(scenario: Scenario, round_id: int):
  rng = random.Random(round_id * 31 + hash(scenario.name))
  route = DEMO_ROUTE if rng.random() < 0.10 else DEMO_ROUTE_QUICK
  route_tag = "full" if route == DEMO_ROUTE else "quick"
  label = f"{scenario.name}-r{round_id:02d}-{route_tag}"

  session = Session(REPORT_DIR / f"fuzz_{label}")
  try:
    session.start(route=route, cabana=scenario.cabana)
    print(f"\n[{label}] pid={session.jotp.pid} display={session.display} route={route_tag}")

    shots_dir = session.workdir / "screenshots"
    shots_dir.mkdir()
    log: list[str] = []
    crashes, freezes = [], []

    for i in range(1, FUZZ_ACTIONS + 1):
      if i % SCREENSHOT_EVERY == 0:
        try:
          elapsed = session.screenshot(shots_dir / f"{i:04d}.png")
          log.append(f"[{i:03d}] screenshot ({elapsed:.2f}s)")
          if elapsed > FREEZE_TIMEOUT_S * 0.8:
            freezes.append(f"[{i:03d}] slow screenshot ({elapsed:.2f}s)")
        except subprocess.TimeoutExpired:
          freezes.append(f"[{i:03d}] screenshot timed out ({FREEZE_TIMEOUT_S}s)")
        except subprocess.CalledProcessError:
          pass

      try:
        desc = _rand_action(session, rng)
      except subprocess.TimeoutExpired:
        freezes.append(f"[{i:03d}] xdotool timed out")
        continue
      except subprocess.CalledProcessError:
        continue
      log.append(f"[{i:03d}] {desc}")
      if i <= 5 or i % 50 == 0:
        print(f"  [{i:03d}/{FUZZ_ACTIONS}] {desc}")

      if not session.alive:
        reason = session.crash_reason()
        crashes.append(f"[{i:03d}] {reason}")
        print(f"  !! CRASH at action {i}: {reason}")
        print(f"  !! last: {desc}")
        print(f"  !! stderr:\n{session.stderr_tail()}")
        break

    log_path = session.workdir / "fuzz.log"
    log_path.write_text("\n".join(log) + "\n")
    print(f"  [{label}] done: {len(log)} actions, {len(crashes)} crashes, {len(freezes)} freezes — {log_path}")

    parts = []
    if crashes:
      parts.append(f"CRASH: {crashes[0]}")
    if freezes:
      parts.append(f"FREEZE: {freezes[0]}")
    if parts:
      pytest.fail(f"[{label}] {' | '.join(parts)} (log: {log_path})")
  finally:
    session.stop()


# -- layout screenshot -------------------------------------------------------

def run_layout_screenshot(layout: str):
  session = Session(REPORT_DIR / f"layout_{layout}")
  try:
    session.start(route=DEMO_ROUTE_QUICK, layout=layout, sync_load=True)
    assert session.alive, f"jotpluggler crashed on startup: {session.crash_reason()}"
    shot = session.workdir / "screenshot.png"
    session.screenshot(shot)
    print(f"  [layout_{layout}] {shot} ({shot.stat().st_size // 1024}KB)")
    assert shot.stat().st_size > 1000, f"screenshot too small ({shot.stat().st_size}B)"
  finally:
    session.stop()


# -- test parametrization ----------------------------------------------------

def _fuzz_params():
  return [pytest.param(s, r, id=f"{s.name}-r{r:02d}")
          for s in SCENARIOS for r in range(ROUNDS_PER_SCENARIO)]

def _layout_names():
  return sorted(p.stem for p in LAYOUTS_DIR.glob("*.json"))


@pytest.mark.skipif(not BINARY.exists(), reason="jotpluggler binary not built")
class TestJotplugglerFuzz:
  @pytest.mark.parametrize("scenario,round_id", _fuzz_params())
  def test_fuzz(self, scenario: Scenario, round_id: int):
    run_fuzz(scenario, round_id)


@pytest.mark.skipif(not BINARY.exists(), reason="jotpluggler binary not built")
class TestJotplugglerLayouts:
  @pytest.mark.parametrize("layout", _layout_names())
  def test_layout_screenshot(self, layout: str):
    run_layout_screenshot(layout)
