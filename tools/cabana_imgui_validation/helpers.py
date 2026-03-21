"""
Shared helpers for cabana validation tests.

Provides xvfb process management, xdotool wrappers, and screenshot capture.
"""

import os
import signal
import subprocess
import tempfile
import time
from pathlib import Path

CABANA_BIN = os.environ.get("CABANA_BIN", os.path.join(os.path.dirname(__file__), "../cabana_imgui/cabana_imgui"))
DEMO_ROUTE = os.environ.get("CABANA_DEMO_ROUTE", "5beb9b58bd12b691/0000010a--a51155e496")
GOLDENS_DIR = Path(__file__).parent / "goldens"
GOLDENS_DIR.mkdir(exist_ok=True)

XVFB_RESOLUTION = "1920x1080x24"

# Minimum size to consider a window "real" (not a Qt internal widget)
MIN_WINDOW_SIZE = 50


class XvfbCabana:
  """Manages a cabana process running under xvfb-run."""

  def __init__(self, args=None, timeout=30, env_extra=None, clean_config=True):
    self.args = args or []
    self.timeout = timeout
    self.env_extra = env_extra or {}
    self.clean_config = clean_config
    self.proc = None
    self.display = None
    self.wid = None  # main visible window
    self._cached_env = None
    self._tmpdir = None

  def start(self):
    """Launch cabana under xvfb-run and wait for a visible window to appear."""
    env = os.environ.copy()
    # Use a clean config directory so saved window state doesn't interfere
    if self.clean_config:
      self._tmpdir = tempfile.mkdtemp(prefix="cabana_test_")
      env["XDG_CONFIG_HOME"] = self._tmpdir
    env.update(self.env_extra)

    cmd = [
      "xvfb-run",
      "-a",  # auto-pick display number
      "-s", f"-screen 0 {XVFB_RESOLUTION}",
      CABANA_BIN,
    ] + self.args

    self.proc = subprocess.Popen(
      cmd,
      env=env,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      start_new_session=True,  # so we can kill the whole process group
    )

    # Wait for a real window to appear
    self.wid = self._wait_for_window()
    return self

  def _close_process_streams(self):
    """Close child pipes to avoid leaking file descriptors into pytest."""
    if self.proc is None:
      return
    for stream_name in ("stdout", "stderr"):
      stream = getattr(self.proc, stream_name, None)
      if stream is None:
        continue
      try:
        stream.close()
      except OSError:
        pass

  def _cleanup_tmpdir(self):
    """Remove the temporary config directory created for this run."""
    if self._tmpdir:
      import shutil
      shutil.rmtree(self._tmpdir, ignore_errors=True)
      self._tmpdir = None

  def _wait_for_window(self):
    """Poll until a real Cabana window (>= MIN_WINDOW_SIZE) appears or timeout."""
    deadline = time.monotonic() + self.timeout
    while time.monotonic() < deadline:
      if self.proc.poll() is not None:
        stdout = self.proc.stdout.read().decode(errors="replace")
        stderr = self.proc.stderr.read().decode(errors="replace")
        self._close_process_streams()
        self._cleanup_tmpdir()
        raise RuntimeError(
          f"Cabana exited early with code {self.proc.returncode}\n"
          f"stdout: {stdout}\nstderr: {stderr}"
        )

      env = self._xdotool_env()
      if env is None:
        time.sleep(0.5)
        continue

      try:
        # Search all windows and filter manually — xdotool --name matching
        # is unreliable across different X11 name properties
        result = subprocess.run(
          ["xdotool", "search", "--name", ""],
          capture_output=True, text=True, timeout=2, env=env,
        )
        if result.returncode == 0 and result.stdout.strip():
          best_wid = None
          best_area = 0
          for wid in result.stdout.strip().split("\n"):
            name = self._get_window_name(wid, env)
            if "Cabana" not in name:
              continue
            # Skip Qt internal windows
            if "Selection Owner" in name:
              continue
            geo = self._get_geometry(wid, env)
            if geo is None:
              continue
            w, h = geo.get("WIDTH", 0), geo.get("HEIGHT", 0)
            if w >= MIN_WINDOW_SIZE and h >= MIN_WINDOW_SIZE:
              area = w * h
              if area > best_area:
                best_area = area
                best_wid = wid
          if best_wid:
            return best_wid
      except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
        pass
      time.sleep(0.5)
    raise TimeoutError(f"Cabana window did not appear within {self.timeout}s")

  @staticmethod
  def _get_window_name(wid, env):
    """Get the window title, or empty string on failure."""
    try:
      result = subprocess.run(
        ["xdotool", "getwindowname", wid],
        capture_output=True, text=True, timeout=2, env=env,
      )
      return result.stdout.strip() if result.returncode == 0 else ""
    except subprocess.TimeoutExpired:
      return ""

  @staticmethod
  def _get_geometry(wid, env):
    """Get window geometry as a dict, or None on failure."""
    try:
      result = subprocess.run(
        ["xdotool", "getwindowgeometry", "--shell", wid],
        capture_output=True, text=True, timeout=2, env=env,
      )
      if result.returncode != 0:
        return None
      geo = {}
      for line in result.stdout.strip().split("\n"):
        if "=" in line:
          k, v = line.split("=", 1)
          geo[k] = int(v)
      return geo
    except (subprocess.TimeoutExpired, ValueError):
      return None

  def _xdotool_env(self):
    """Get DISPLAY and XAUTHORITY from the cabana child process.

    xvfb-run spawns Xvfb + the app. The app child has the correct
    DISPLAY and XAUTHORITY. We skip the Xvfb process.
    Returns None if the child display can't be discovered yet.
    """
    if self._cached_env is not None:
      return self._cached_env

    try:
      children = subprocess.run(
        ["pgrep", "-P", str(self.proc.pid)],
        capture_output=True, text=True, timeout=2,
      )
      for child_pid in children.stdout.strip().split("\n"):
        if not child_pid:
          continue
        try:
          with open(f"/proc/{child_pid}/cmdline", "rb") as f:
            cmdline = f.read().decode(errors="replace")
          if "Xvfb" in cmdline:
            continue

          env_vars = self._read_proc_environ(child_pid)
          if "DISPLAY" in env_vars:
            env = os.environ.copy()
            env["DISPLAY"] = env_vars["DISPLAY"]
            self.display = env_vars["DISPLAY"]
            if "XAUTHORITY" in env_vars:
              env["XAUTHORITY"] = env_vars["XAUTHORITY"]
            self._cached_env = env
            return env
        except (FileNotFoundError, PermissionError):
          continue
    except subprocess.TimeoutExpired:
      pass
    # Don't fall back to host env — that would search the wrong display
    return None

  @staticmethod
  def _read_proc_environ(pid):
    """Read /proc/PID/environ and return as a dict."""
    result = {}
    with open(f"/proc/{pid}/environ", "rb") as f:
      for entry in f.read().split(b"\0"):
        if b"=" in entry:
          k, v = entry.split(b"=", 1)
          result[k.decode(errors="replace")] = v.decode(errors="replace")
    return result

  def find_windows(self, name_pattern="Cabana"):
    """Find all windows matching a name pattern. Returns list of (wid, name, geometry)."""
    env = self._xdotool_env()
    if env is None:
      return []
    result = subprocess.run(
      ["xdotool", "search", "--name", name_pattern],
      capture_output=True, text=True, timeout=5, env=env,
    )
    windows = []
    if result.returncode == 0:
      for wid in result.stdout.strip().split("\n"):
        if not wid:
          continue
        name_result = subprocess.run(
          ["xdotool", "getwindowname", wid],
          capture_output=True, text=True, timeout=2, env=env,
        )
        name = name_result.stdout.strip() if name_result.returncode == 0 else ""
        geo = self._get_geometry(wid, env)
        windows.append((wid, name, geo))
    return windows

  def screenshot(self, filename=None):
    """Capture a screenshot of the full virtual screen. Returns the path."""
    if filename is None:
      filename = f"screenshot_{int(time.time())}.png"
    path = GOLDENS_DIR / filename
    env = self._xdotool_env()
    if env is None:
      raise RuntimeError("Cannot screenshot — display not discovered yet")
    subprocess.run(
      ["import", "-window", "root", str(path)],
      env=env, timeout=10, check=True,
    )
    return path

  def screenshot_window(self, filename=None, wid=None):
    """Capture a screenshot of a specific window. Returns the path."""
    wid = wid or self.wid
    if not wid:
      raise RuntimeError("No window ID — is cabana running?")
    if filename is None:
      filename = f"window_{int(time.time())}.png"
    path = GOLDENS_DIR / filename
    env = self._xdotool_env()
    if env is None:
      raise RuntimeError("Cannot screenshot — display not discovered yet")
    subprocess.run(
      ["import", "-window", wid, str(path)],
      env=env, timeout=10, check=True,
    )
    return path

  def xdotool(self, *args):
    """Run an xdotool command against the cabana display."""
    env = self._xdotool_env()
    if env is None:
      return subprocess.CompletedProcess(args=[], returncode=1, stdout="", stderr="no display")
    result = subprocess.run(
      ["xdotool"] + list(args),
      env=env, capture_output=True, text=True, timeout=10,
    )
    return result

  def send_key(self, key):
    """Send a keystroke to the focused window."""
    return self.xdotool("key", "--window", self.wid, key)

  def click(self, x, y, button=1):
    """Click at (x, y) relative to the cabana window."""
    self.xdotool("mousemove", "--window", self.wid, str(x), str(y))
    self.xdotool("click", "--window", self.wid, str(button))

  def get_window_geometry(self, wid=None):
    """Return dict with WINDOW, X, Y, WIDTH, HEIGHT, SCREEN."""
    wid = wid or self.wid
    env = self._xdotool_env()
    if env is None:
      return None
    return self._get_geometry(wid, env)

  def get_window_name(self, wid=None):
    """Get the title of a window."""
    wid = wid or self.wid
    result = self.xdotool("getwindowname", wid)
    return result.stdout.strip() if result.returncode == 0 else ""

  def maximize(self):
    """Maximize the main cabana window to fill the virtual screen."""
    self.xdotool("windowactivate", "--sync", self.wid)
    self.xdotool("windowsize", self.wid, "1920", "1080")
    self.xdotool("windowmove", self.wid, "0", "0")
    time.sleep(0.5)  # let the resize settle

  def focus(self):
    """Activate and focus the main window."""
    self.xdotool("windowactivate", "--sync", self.wid)
    self.xdotool("windowfocus", "--sync", self.wid)

  def is_alive(self):
    """Check if the cabana process is still running."""
    return self.proc is not None and self.proc.poll() is None

  def close(self, timeout=5):
    """Send alt+F4 to close gracefully, then kill if needed. Returns exit code."""
    if not self.is_alive():
      self._close_process_streams()
      self._cleanup_tmpdir()
      return self.proc.returncode if self.proc else -1

    # Try graceful close via alt+F4
    self.send_key("alt+F4")

    try:
      self.proc.wait(timeout=timeout)
      self._close_process_streams()
      self._cleanup_tmpdir()
      return self.proc.returncode
    except subprocess.TimeoutExpired:
      pass

    # Kill the process group
    try:
      os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
      self.proc.wait(timeout=3)
    except (ProcessLookupError, subprocess.TimeoutExpired):
      try:
        os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
        self.proc.wait(timeout=2)
      except (ProcessLookupError, subprocess.TimeoutExpired):
        pass
    self._close_process_streams()
    self._cleanup_tmpdir()
    return self.proc.returncode

  def kill(self):
    """Force kill the process group and clean up temp dirs."""
    if self.proc:
      try:
        os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
        self.proc.wait(timeout=5)
      except (ProcessLookupError, subprocess.TimeoutExpired, ChildProcessError):
        pass
    self._close_process_streams()
    self._cleanup_tmpdir()

  def __enter__(self):
    self.start()
    return self

  def __exit__(self, *args):
    self.kill()
