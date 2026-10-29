"""Settings for jay's customizations.

Params keys are compiled into the C++ params library, so new keys can't be added from Python.
These settings live in a small JSON file instead. It is outside /data/openpilot, so updates keep it.
"""
import json
import os
import threading
import time

CONFIG_PATH = "/data/jay/settings.json"

DEFAULTS: dict[str, bool | int | str] = {
  "onroad_minimal": False,       # driving screen: hide the path and lane lines
  "screensaver": True,           # offroad: show a clock before the screen turns off
  "screensaver_timeout": 300,    # seconds the clock shows before the screen turns off
  "screensaver_dim_night": True,  # screensaver: dimmer at night
  "storage_auto_clean": True,    # delete video before logs when the disk fills up
  "timezone": "Asia/Kuala_Lumpur",  # the device clock is UTC; used to show local times
}

_lock = threading.Lock()
_cache: dict = {}
_cache_mtime = -1.0
_cache_checked = 0.0
RECHECK_INTERVAL = 1.0  # seconds between stat() calls, so get() is cheap enough to call every frame


def _load() -> dict:
  global _cache, _cache_mtime, _cache_checked
  now = time.monotonic()
  if now - _cache_checked < RECHECK_INTERVAL:
    return _cache
  _cache_checked = now
  try:
    mtime = os.path.getmtime(CONFIG_PATH)
  except OSError:
    _cache, _cache_mtime = {}, -1.0
    return _cache
  if mtime != _cache_mtime:
    try:
      with open(CONFIG_PATH) as f:
        data = json.load(f)
      _cache = data if isinstance(data, dict) else {}
    except (OSError, ValueError):
      _cache = {}
    _cache_mtime = mtime
  return _cache


def get(key: str):
  with _lock:
    return _load().get(key, DEFAULTS[key])


def put(key: str, value) -> None:
  global _cache_checked
  assert key in DEFAULTS, key
  with _lock:
    _cache_checked = 0.0
    data = dict(_load())
    data[key] = value
    os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
    tmp = CONFIG_PATH + ".tmp"
    with open(tmp, "w") as f:
      json.dump(data, f, indent=2, sort_keys=True)
    os.replace(tmp, CONFIG_PATH)
    _cache_checked = 0.0
