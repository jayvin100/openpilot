"""Keeps a bug in jay's UI code from taking down the UI. A guarded function that raises is logged once and then
switched off for the rest of the process, so stock drawing (alerts, driver monitoring) carries on."""
import functools

from openpilot.common.swaglog import cloudlog

_failed: set[str] = set()


def guarded(default=None):
  def wrap(fn):
    name = f"{fn.__module__}.{fn.__qualname__}"

    @functools.wraps(fn)
    def inner(*args, **kwargs):
      if name in _failed:
        return default
      try:
        return fn(*args, **kwargs)
      except Exception:
        _failed.add(name)
        cloudlog.exception(f"jay: {name} failed, disabled until restart")
        return default
    return inner
  return wrap
