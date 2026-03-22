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

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CABANA_BIN = ROOT / "tools" / "cabana_imgui" / "_cabana_imgui"
CABANA_BIN = os.environ.get("CABANA_BIN", str(DEFAULT_CABANA_BIN))
DEMO_ROUTE = os.environ.get("CABANA_DEMO_ROUTE", "5beb9b58bd12b691/0000010a--a51155e496")
GOLDENS_DIR = Path(__file__).parent / "goldens"
GOLDENS_DIR.mkdir(exist_ok=True)

XVFB_RESOLUTION = "1920x1080x24"

# Minimum size to consider a window "real" (not a Qt internal widget)
MIN_WINDOW_SIZE = 50

SIGNAL_PLOT_X = 1280
SIGNAL_PLOT_Y0 = 440
SIGNAL_PLOT_ROW_STEP = 22
DETAIL_TAB_BINARY = (272, 40)
DETAIL_TAB_SIGNALS = (309, 40)
DETAIL_TAB_HISTORY = (345, 40)
CHART_NEW_TAB = (1550, 660)
CHART_SPLIT = (1480, 690)
CHART_REMOVE = (1540, 690)
CHART_TAB_X0 = 1485
CHART_TAB_Y = 710
CHART_TAB_X_STEP = 60
FILE_MENU = (15, 12)
FILE_MENU_EXPORT_CSV = (60, 60)
FILE_MENU_NEW_DBC = (60, 92)
FILE_MENU_SAVE = (60, 199)
FILE_MENU_SAVE_AS = (60, 215)
FILE_MENU_LOAD_CLIPBOARD = (60, 176)
FILE_MENU_COPY_DBC = (60, 231)
FILE_MENU_EXIT = (30, 295)
FILE_MENU_MANAGE_DBCS = (60, 124)
FILE_MENU_OPENDBC_SUBMENU = (132, 160)
FILE_MENU_OPENDBC_SUBMENU_X = 290
TOOLS_MENU = (102, 12)
TOOLS_MENU_FIND_SIGNAL = (100, 28)
TOOLS_MENU_FIND_SIMILAR_BITS = (100, 44)
TOOLS_MENU_ROUTE_INFO = (100, 68)
EDIT_MENU = (38, 12)
EDIT_MENU_UNDO = (78, 28)
EDIT_MENU_REDO = (78, 44)
EDIT_MENU_EDIT_MESSAGE = (80, 60)
EDIT_MENU_ADD_SIGNAL = (80, 76)
MANAGE_DBC_MODAL_NEW_X = 605
MANAGE_DBC_MODAL_OPEN_X = 703
MANAGE_DBC_MODAL_LOAD_CLIPBOARD_X = 815
MANAGE_DBC_MODAL_SAVE_AS_X = 1001
MANAGE_DBC_MODAL_COPY_X = 1095
MANAGE_DBC_MODAL_REMOVE_BUS_X = 1197
MANAGE_DBC_MODAL_ROW_Y0 = 496
MANAGE_DBC_MODAL_ROW_STEP = 40
FIND_SIGNAL_FIND_BUTTON = (550, 394)
FIND_SIGNAL_CREATE_BUTTON = (1075, 394)


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
    last_error = None
    for attempt in range(3):
      self._cached_env = None
      self.display = None
      self.wid = None

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

      try:
        # Wait for a real window to appear
        self.wid = self._wait_for_window()
        return self
      except Exception as err:
        last_error = err
        self.kill()
        if attempt < 2:
          time.sleep(1.0)
          continue
        raise
    if last_error is not None:
      raise last_error
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
        raise RuntimeError(f"Cabana exited early with code {self.proc.returncode}\nstdout: {stdout}\nstderr: {stderr}")

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
      filename = f"screenshot_{time.monotonic_ns()}.png"
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
      filename = f"window_{time.monotonic_ns()}.png"
    path = GOLDENS_DIR / filename
    env = self._xdotool_env()
    if env is None:
      raise RuntimeError("Cannot screenshot — display not discovered yet")
    subprocess.run(
      ["import", "-window", wid, str(path)],
      env=env, timeout=10, check=True,
    )
    return path

  def set_clipboard_text(self, text):
    """Write text into the X11 clipboard for this display."""
    env = self._xdotool_env()
    if env is None:
      raise RuntimeError("Cannot access clipboard — display not discovered yet")
    subprocess.run(
      ["xclip", "-selection", "clipboard", "-in"],
      input=text,
      text=True,
      env=env,
      timeout=10,
      check=True,
    )

  def get_clipboard_text(self):
    """Read text from the X11 clipboard for this display."""
    env = self._xdotool_env()
    if env is None:
      raise RuntimeError("Cannot access clipboard — display not discovered yet")
    result = subprocess.run(
      ["xclip", "-selection", "clipboard", "-out"],
      capture_output=True,
      text=True,
      env=env,
      timeout=10,
      check=True,
    )
    return result.stdout

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

  def type_text(self, text, delay_ms=1):
    """Type text into the focused window."""
    return self.xdotool("type", "--delay", str(delay_ms), "--clearmodifiers", text)

  def key_down(self, key):
    """Press and hold a key against the focused window."""
    return self.xdotool("keydown", "--window", self.wid, key)

  def key_up(self, key):
    """Release a previously held key."""
    return self.xdotool("keyup", "--window", self.wid, key)

  def click(self, x, y, button=1):
    """Click at (x, y) relative to the cabana window."""
    geo = self.get_window_geometry()
    if geo is not None:
      abs_x = geo["X"] + x
      abs_y = geo["Y"] + y
      self.xdotool("mousemove", str(abs_x), str(abs_y))
    else:
      self.xdotool("mousemove", "--window", self.wid, str(x), str(y))
    self.xdotool("click", str(button))

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

    self.focus()
    self.xdotool("windowclose", self.wid)

    try:
      self.proc.wait(timeout=timeout)
      self._close_process_streams()
      self._cleanup_tmpdir()
      return self.proc.returncode
    except subprocess.TimeoutExpired:
      pass

    self.send_key("ctrl+q")

    try:
      self.proc.wait(timeout=timeout)
      self._close_process_streams()
      self._cleanup_tmpdir()
      return self.proc.returncode
    except subprocess.TimeoutExpired:
      pass

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


def wait_for_demo_route(cabana, settle_seconds=8):
  """Maximize and allow the demo route UI to populate."""
  cabana.maximize()
  time.sleep(settle_seconds)


def reset_layout(cabana):
  """Open View -> Reset Window Layout using screen coordinates."""
  cabana.focus()
  cabana.click(110, 12)
  time.sleep(0.4)
  cabana.click(120, 80)
  time.sleep(1.0)


def select_first_message(cabana):
  """Focus the message list and select the first visible row via keyboard."""
  cabana.focus()
  cabana.click(120, 86)
  time.sleep(0.3)
  cabana.send_key("Down")
  time.sleep(0.5)


def click_signal_plot(cabana, signal_index=0, merge=False):
  """Click a signal plot checkbox in the selected message detail view."""
  cabana.focus()
  y = SIGNAL_PLOT_Y0 + signal_index * SIGNAL_PLOT_ROW_STEP
  if merge:
    cabana.key_down("Shift_L")
    time.sleep(0.1)
  cabana.click(SIGNAL_PLOT_X, y)
  if merge:
    time.sleep(0.1)
    cabana.key_up("Shift_L")
  time.sleep(0.8)


def activate_detail_tab(cabana, tab_name):
  """Activate one of the Detail tabs by name."""
  tab_map = {
    "binary": DETAIL_TAB_BINARY,
    "signals": DETAIL_TAB_SIGNALS,
    "history": DETAIL_TAB_HISTORY,
  }
  cabana.focus()
  cabana.click(*tab_map[tab_name])
  time.sleep(0.6)


def open_dbc_path(cabana, path):
  """Use the real Ctrl+O flow to open a DBC path."""
  cabana.focus()
  cabana.send_key("ctrl+o")
  time.sleep(0.6)
  cabana.send_key("ctrl+a")
  time.sleep(0.1)
  cabana.type_text(path)
  time.sleep(0.3)
  cabana.send_key("Return")
  time.sleep(1.2)


def new_dbc_via_shortcut(cabana):
  """Use the real Ctrl+N flow to create a new in-memory DBC."""
  cabana.focus()
  cabana.click(400, 400)
  time.sleep(0.2)
  cabana.send_key("ctrl+n")
  time.sleep(1.0)


def new_dbc_via_menu(cabana):
  """Use File -> New DBC File."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_NEW_DBC)
  time.sleep(1.0)


def load_dbc_from_clipboard(cabana):
  """Use File -> Load DBC From Clipboard."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_LOAD_CLIPBOARD)
  time.sleep(1.0)


def copy_dbc_to_clipboard(cabana):
  """Use File -> Copy DBC To Clipboard."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_COPY_DBC)
  time.sleep(0.8)


def export_csv_path(cabana, path):
  """Use File -> Export to CSV and write the chosen output path."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_EXPORT_CSV)
  time.sleep(1.0)
  cabana.send_key("ctrl+a")
  time.sleep(0.2)
  cabana.type_text(path)
  time.sleep(0.4)
  cabana.send_key("Return")
  time.sleep(2.0)


def load_first_opendbc_via_menu(cabana):
  """Use File -> Load DBC from comma/opendbc -> first menu item."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_OPENDBC_SUBMENU)
  time.sleep(0.8)
  cabana.click(FILE_MENU_OPENDBC_SUBMENU_X, FILE_MENU_OPENDBC_SUBMENU[1])
  time.sleep(1.2)


def save_dbc_as_path(cabana, path):
  """Use the real File -> Save DBC As flow to save the current DBC."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_SAVE_AS)
  time.sleep(1.0)
  cabana.send_key("ctrl+a")
  time.sleep(0.2)
  cabana.type_text(path)
  time.sleep(0.5)
  cabana.send_key("Return")
  time.sleep(1.5)


def save_current_dbc(cabana):
  """Use the real File -> Save DBC flow to save to the current active path."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_SAVE)
  time.sleep(1.2)


def new_manage_dbc_for_bus(cabana, bus_index=0):
  """Use File -> Manage DBC Files -> Bus N -> New DBC."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_MANAGE_DBCS)
  time.sleep(0.8)
  cabana.click(MANAGE_DBC_MODAL_NEW_X, MANAGE_DBC_MODAL_ROW_Y0 + bus_index * MANAGE_DBC_MODAL_ROW_STEP)
  time.sleep(0.8)


def open_manage_dbc_for_bus_path(cabana, path, bus_index=0):
  """Use File -> Manage DBC Files -> Bus N -> Open DBC."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_MANAGE_DBCS)
  time.sleep(0.8)
  cabana.click(MANAGE_DBC_MODAL_OPEN_X, MANAGE_DBC_MODAL_ROW_Y0 + bus_index * MANAGE_DBC_MODAL_ROW_STEP)
  time.sleep(0.8)
  cabana.send_key("ctrl+a")
  time.sleep(0.1)
  cabana.type_text(path)
  time.sleep(0.3)
  cabana.send_key("Return")
  time.sleep(1.2)


def load_manage_dbc_from_clipboard(cabana, bus_index=0):
  """Use File -> Manage DBC Files -> Bus N -> Load Clipboard."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_MANAGE_DBCS)
  time.sleep(0.8)
  cabana.click(MANAGE_DBC_MODAL_LOAD_CLIPBOARD_X, MANAGE_DBC_MODAL_ROW_Y0 + bus_index * MANAGE_DBC_MODAL_ROW_STEP)
  time.sleep(1.0)


def save_manage_dbc_as_path(cabana, path, bus_index=0):
  """Use File -> Manage DBC Files -> Bus N -> Save As."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_MANAGE_DBCS)
  time.sleep(0.8)
  cabana.click(MANAGE_DBC_MODAL_SAVE_AS_X, MANAGE_DBC_MODAL_ROW_Y0 + bus_index * MANAGE_DBC_MODAL_ROW_STEP)
  time.sleep(1.0)
  cabana.send_key("ctrl+a")
  time.sleep(0.2)
  cabana.type_text(path)
  time.sleep(0.4)
  cabana.send_key("Return")
  time.sleep(1.5)


def remove_manage_dbc_from_bus(cabana, bus_index=0):
  """Use File -> Manage DBC Files -> Bus N -> Remove Bus."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_MANAGE_DBCS)
  time.sleep(0.8)
  cabana.click(MANAGE_DBC_MODAL_REMOVE_BUS_X, MANAGE_DBC_MODAL_ROW_Y0 + bus_index * MANAGE_DBC_MODAL_ROW_STEP)
  time.sleep(0.8)


def open_add_signal_via_menu(cabana):
  """Use the real Edit -> Add Signal flow to open the signal editor."""
  cabana.focus()
  cabana.click(*EDIT_MENU)
  time.sleep(0.4)
  cabana.click(*EDIT_MENU_ADD_SIGNAL)
  time.sleep(0.8)


def open_edit_message_via_menu(cabana):
  """Use the real Edit -> Edit Message flow to open the message editor."""
  cabana.focus()
  cabana.click(*EDIT_MENU)
  time.sleep(0.4)
  cabana.click(*EDIT_MENU_EDIT_MESSAGE)
  time.sleep(0.8)


def undo_via_menu(cabana):
  """Use the real Edit -> Undo flow."""
  cabana.focus()
  cabana.click(*EDIT_MENU)
  time.sleep(0.4)
  cabana.click(*EDIT_MENU_UNDO)
  time.sleep(0.8)


def redo_via_menu(cabana):
  """Use the real Edit -> Redo flow."""
  cabana.focus()
  cabana.click(*EDIT_MENU)
  time.sleep(0.4)
  cabana.click(*EDIT_MENU_REDO)
  time.sleep(0.8)


def create_chart_tab(cabana):
  """Create a new chart tab from the chart toolbar."""
  cabana.focus()
  cabana.click(*CHART_NEW_TAB)
  time.sleep(1.0)


def activate_chart_tab(cabana, tab_index):
  """Activate a chart tab by index."""
  cabana.focus()
  cabana.click(CHART_TAB_X0 + tab_index * CHART_TAB_X_STEP, CHART_TAB_Y)
  time.sleep(0.5)


def split_selected_chart(cabana):
  """Trigger split on the selected chart."""
  cabana.focus()
  cabana.click(*CHART_SPLIT)
  time.sleep(0.8)


def remove_selected_chart(cabana):
  """Remove the selected chart via the chart toolbar."""
  cabana.focus()
  cabana.click(*CHART_REMOVE)
  time.sleep(0.8)


def quit_via_menu(cabana, timeout=5):
  """Quit via File -> Exit so the app runs its normal shutdown path."""
  cabana.focus()
  cabana.click(*FILE_MENU)
  time.sleep(0.4)
  cabana.click(*FILE_MENU_EXIT)
  try:
    cabana.proc.wait(timeout=timeout)
    cabana._close_process_streams()
    cabana._cleanup_tmpdir()
    return cabana.proc.returncode
  except subprocess.TimeoutExpired:
    return cabana.close(timeout=timeout)


def open_find_similar_bits_window(cabana):
  """Use Tools -> Find Similar Bits."""
  cabana.focus()
  cabana.click(*TOOLS_MENU)
  time.sleep(0.4)
  cabana.click(*TOOLS_MENU_FIND_SIMILAR_BITS)
  time.sleep(1.0)


def open_find_signal_window(cabana):
  """Use Tools -> Find Signal."""
  cabana.focus()
  cabana.click(*TOOLS_MENU)
  time.sleep(0.4)
  cabana.click(*TOOLS_MENU_FIND_SIGNAL)
  time.sleep(1.0)


def run_find_signal_search(cabana):
  """Click the primary Find button in the Find Signal tool."""
  cabana.focus()
  cabana.click(*FIND_SIGNAL_FIND_BUTTON)
  time.sleep(1.5)


def create_signal_from_find_signal(cabana):
  """Click Create Signal for the selected Find Signal result."""
  cabana.focus()
  cabana.click(*FIND_SIGNAL_CREATE_BUTTON)
  time.sleep(1.5)


def open_route_info_window(cabana):
  """Use Tools -> Route Info."""
  cabana.focus()
  cabana.click(*TOOLS_MENU)
  time.sleep(0.4)
  cabana.click(*TOOLS_MENU_ROUTE_INFO)
  time.sleep(1.0)
