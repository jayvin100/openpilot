"""Per-drive stats for the home screen and drive reports.

Offroad, the jaydrives process summarizes finished routes from their qlogs into DRIVES_PATH.
qlogs keep every 10th carState, selfdriveState(SP), carControl and controlsState (10 Hz),
every onroadEvents message and gpsLocation at 1 Hz, which is enough for these stats.

  python -m openpilot.jay.drives --backfill        summarize every finished route now, then exit
  python -m openpilot.jay.drives --route <route>   print one route's summary as JSON
"""
import json
import os
import re
import sys
import time

DRIVES_PATH = "/data/jay/drives.json"
VERSION = 1
MAX_DRIVES = 200
POLL_INTERVAL = 60.0  # seconds between scans for new routes

MOVING_SPEED = 1.0       # m/s, carState.vEgo above this counts as moving
OVERRIDE_DEBOUNCE = 2.0  # s without steering input before a new steer override counts
CAUSE_BEFORE = 0.3       # s of input before a disengagement that can explain it
CAUSE_AFTER = 0.6        # s after it: qlogs are 10 Hz and pedal/cruise signals can lag by a frame or two
BLINKER_WINDOW = 2.0     # s, a steer override with the blinker on (or just off) is likely a lane change
MAX_DT = 1.0             # s, longer gaps between samples (missing segments, dropped logs) don't count as time
MAX_CURVATURE = 0.25     # 1/m, controlsState.curvature beyond full lock is the vehicle model starting up
MAX_ACCEL = 15.0         # m/s^2, faster vEgo changes are glitches (Tesla reports 79.9 m/s while powering down)
MAX_EVENTS = 500

# Drives shorter than this are kept out of the list (device woken in the garage, a few meters of parking)
MIN_MOVING_S = 30.0
MIN_DISTANCE_M = 100.0

PLAUSIBLE_WALL_TIME = 1704067200  # 2024-01-01: anything earlier is the 1970 boot clock

# Event names behind a disengagement (from selfdriveState.alertType "<event>/<type>")
STEER_EVENTS = {"steerDisengage"}
BRAKE_EVENTS = {"pedalPressed", "brakeHold", "parkBrake"}
CANCEL_EVENTS = {"buttonCancel", "pcmDisable", "manualLongitudinalRequired", "lkasDisable"}
ALERT_TYPES = {"softDisable", "immediateDisable"}

SEGMENT_RE = re.compile(r"^(?P<route>.+)--(?P<seg>\d+)$")
LOCK_STALE_AFTER = 600.0  # s, a .lock this much older than its segment's last write is left over from a power cut


def _never() -> float:
  return -1e9


class DriveAccumulator:
  """Drive counters fed one qlog sample at a time."""

  def __init__(self):
    self.reset()

  def reset(self) -> None:
    self.t0: float | None = None
    self._t: float | None = None
    self.duration_s = 0.0
    self.moving_s = 0.0
    self.distance_m = 0.0
    self.engaged_s = 0.0
    self.engaged_moving_s = 0.0
    self.lat_active_s = 0.0
    self.lat_active_moving_s = 0.0
    self.long_active_s = 0.0
    self.lat_ctl_s = 0.0    # lat active time with controlsState available
    self.lat_sat_s = 0.0    # ...of which the lateral controller reported saturation
    self.max_lat_accel: float | None = None
    self.max_lat_accel_active: float | None = None
    self.disengagements = 0
    self.steer_overrides = 0
    self.events: list[dict] = []

    self._v = 0.0
    self._good_v_t = _never()
    self._engaged = False
    self._lat = False
    self._cruise = False
    self._brake_t = self._gas_t = self._steer_t = self._cancel_t = self._cruise_off_t = _never()
    self._override_t = self._blinker_t = self._sat_t = _never()
    self._soft_t = _never()
    self._soft_alert = ("", "", "")
    self._pending: list | None = None  # [t, speed, alert, alert2, event, kind]

  @property
  def engaged_frac(self) -> float:
    return self.engaged_moving_s / self.moving_s if self.moving_s > 0 else 0.0

  def step(self, t: float, cs, sd, sdsp=None, cc=None, ctl=None) -> None:
    """t in seconds (logMonoTime), the rest are carState, selfdriveState, selfdriveStateSP, carControl, controlsState."""
    if self._t is None:
      self.t0 = t
      dt = 0.0
    else:
      dt = t - self._t
      if dt <= 0.0:
        return
      dt = min(dt, MAX_DT)
    self._t = t

    if self._pending is not None and t > self._pending[0] + CAUSE_AFTER:
      self._resolve()

    v = cs.vEgo
    if cs.gearShifter == "park":
      v = 0.0
    elif abs(v - self._v) > MAX_ACCEL * dt + 1.0 and t - self._good_v_t < 2.0 + dt:
      v = self._v  # hold the last good speed through a glitch, unless the new value persists
    else:
      self._good_v_t = t
    self._v = v
    moving = v > MOVING_SPEED
    mads = sdsp.mads if sdsp is not None else None
    engaged = sd.enabled or (mads is not None and mads.enabled)
    lat = cc is not None and cc.latActive
    cruise = cs.cruiseState.enabled

    # input timestamps for cause attribution
    if cs.brakePressed:
      self._brake_t = t
    if cs.gasPressed:
      self._gas_t = t
    if cs.steeringPressed or cs.steeringDisengage:
      self._steer_t = t
    if self._cruise and not cruise:
      self._cruise_off_t = t
    if len(cs.buttonEvents):
      for be in cs.buttonEvents:
        if be.type == "cancel":
          self._cancel_t = t
    if cs.leftBlinker or cs.rightBlinker:
      self._blinker_t = t
    if sd.state == "softDisabling" or (mads is not None and mads.state == "softDisabling"):
      self._soft_t = t
      self._soft_alert = (sd.alertText1, sd.alertText2, sd.alertType)

    # time integrals
    self.duration_s += dt
    if v > 0.0:
      self.distance_m += v * dt
    if moving:
      self.moving_s += dt
    if engaged:
      self.engaged_s += dt
      if moving:
        self.engaged_moving_s += dt
    if lat:
      self.lat_active_s += dt
      if moving:
        self.lat_active_moving_s += dt
    if cc is not None and cc.longActive:
      self.long_active_s += dt

    lat_accel = None
    if ctl is not None:
      if lat:
        lcs = ctl.lateralControlState
        self.lat_ctl_s += dt
        if getattr(getattr(lcs, lcs.which()), "saturated", False):
          self.lat_sat_s += dt
          self._sat_t = t
      k = abs(ctl.curvature)
      if moving and k <= MAX_CURVATURE:
        lat_accel = v * v * k
        if self.max_lat_accel is None or lat_accel > self.max_lat_accel:
          self.max_lat_accel = lat_accel
        if lat and (self.max_lat_accel_active is None or lat_accel > self.max_lat_accel_active):
          self.max_lat_accel_active = lat_accel

    # steer override: steering input after OVERRIDE_DEBOUNCE without any, while steering and moving.
    # qlogs keep 1 in 10 carStates, so a short nudge is often only visible as MADS (or selfdrive) overriding.
    if mads is not None and mads.enabled:
      override = cs.steeringPressed or mads.state == "overriding"
    else:
      override = cs.steeringPressed or (sd.state == "overriding" and not cs.gasPressed)
    if override:
      if t - self._override_t >= OVERRIDE_DEBOUNCE and moving and (lat or self._lat):
        self.steer_overrides += 1
        self._add_event({"t": round(t - self.t0, 1), "type": "steer_override", "cause": "steering", "speed": round(v, 2),
                         "alert": "", "saturated": t - self._sat_t <= 1.0, "blinker": t - self._blinker_t <= BLINKER_WINDOW,
                         "lat_accel": None if lat_accel is None else round(lat_accel, 2)})
      self._override_t = t

    # disengagement: engaged -> not engaged while moving, cause resolved CAUSE_AFTER later
    if self._engaged and not engaged and moving:
      if self._pending is not None:
        self._resolve()
      self.disengagements += 1
      event, _, kind = sd.alertType.partition("/")
      self._pending = [t, v, sd.alertText1, sd.alertText2, event, kind]
    elif self._pending is not None and not self._pending[4]:
      # the disengage alert can show up a frame late
      event, _, kind = sd.alertType.partition("/")
      if event:
        self._pending[4], self._pending[5] = event, kind

    self._engaged = engaged
    self._lat = lat
    self._cruise = cruise

  def finish(self) -> None:
    if self._pending is not None:
      self._resolve()

  def _resolve(self) -> None:
    t, v, alert, alert2, event, kind = self._pending
    self._pending = None
    since = t - CAUSE_BEFORE
    soft = t - self._soft_t <= 1.0
    if kind in ALERT_TYPES or soft:
      cause = "alert"
      if soft and not (kind in ALERT_TYPES and alert):
        alert, alert2, atype = self._soft_alert
        event = atype.partition("/")[0]
    elif event in STEER_EVENTS:
      cause = "steering"
    elif self._brake_t >= since:
      cause = "brake"
    elif self._steer_t >= since:
      cause = "steering"
    elif self._gas_t >= since:
      cause = "gas"
    elif event in CANCEL_EVENTS or self._cancel_t >= since or self._cruise_off_t >= since:
      cause = "cancel"
    elif event in BRAKE_EVENTS:
      cause = "brake"
    else:
      cause = "unknown"
    ev = {"t": round(t - self.t0, 1), "type": "disengage", "cause": cause, "speed": round(v, 2), "alert": alert, "event": event}
    if alert2:
      ev["alert2"] = alert2
    self._add_event(ev)

  def _add_event(self, ev: dict) -> None:
    if len(self.events) < MAX_EVENTS:
      self.events.append(ev)


# *** route summaries ***

def _log_root() -> str:
  from openpilot.common.hardware.hw import Paths
  return Paths.log_root()


def list_routes(root: str | None = None) -> dict[str, list[str]]:
  """{route: [segment dirs, in segment order]}"""
  root = root or _log_root()
  routes: dict[str, list[tuple[int, str]]] = {}
  try:
    names = os.listdir(root)
  except OSError:
    return {}
  for name in names:
    m = SEGMENT_RE.match(name)
    if m:
      routes.setdefault(m["route"], []).append((int(m["seg"]), os.path.join(root, name)))
  return {r: [p for _, p in sorted(segs)] for r, segs in routes.items()}


def route_sort_key(route: str) -> tuple:
  """Routes are named <hex counter>--<random>, so the counter orders them."""
  try:
    return (0, int(route.split("--")[0], 16), route)
  except ValueError:
    return (1, 0, route)


def route_locked(seg_dirs: list[str], now: float | None = None) -> bool:
  """True while loggerd may still be writing the route (a .lock file in any segment)."""
  now = time.time() if now is None else now
  for d in seg_dirs:
    try:
      names = os.listdir(d)
    except OSError:
      continue
    if any(n.endswith(".lock") for n in names):
      try:
        last_write = max(os.path.getmtime(os.path.join(d, n)) for n in names)
      except (OSError, ValueError):
        return True
      if now - last_write < LOCK_STALE_AFTER:
        return True
  return False


def _read_events(fn: str):
  import capnp
  import zstandard
  from openpilot.cereal import log

  with open(fn, "rb") as f:
    dat = f.read()
  if dat.startswith(b"\x28\xB5\x2F\xFD"):
    # stream_reader handles frames without a content size (files cut short by a power loss)
    dat = zstandard.ZstdDecompressor().stream_reader(dat).read()
  try:
    yield from log.Event.read_multiple_bytes(dat)
  except capnp.KjException:
    pass  # truncated file: keep what was read


def _plausible(ts: float) -> bool:
  return ts > PLAUSIBLE_WALL_TIME


def summarize_route(route: str, root: str | None = None) -> dict:
  """Summarize a route from its qlogs."""
  seg_dirs = list_routes(root).get(route)
  if not seg_dirs:
    raise FileNotFoundError(f"no segments for route {route}")

  acc = DriveAccumulator()
  cs = sd = sdsp = cc = ctl = None
  gps_wall = clock_wall = init_wall = None  # (unix seconds, mono seconds)
  for d in seg_dirs:
    fn = next((os.path.join(d, n) for n in ("qlog.zst", "qlog") if os.path.isfile(os.path.join(d, n))), None)
    if fn is None:
      continue
    for e in _read_events(fn):
      w = e.which()
      if w == "carState":
        cs = e.carState
        if sd is not None:
          acc.step(e.logMonoTime * 1e-9, cs, sd, sdsp, cc, ctl)
      elif w == "selfdriveState":
        sd = e.selfdriveState
      elif w == "selfdriveStateSP":
        sdsp = e.selfdriveStateSP
      elif w == "carControl":
        cc = e.carControl
      elif w == "controlsState":
        ctl = e.controlsState
      elif w in ("gpsLocation", "gpsLocationExternal"):
        if gps_wall is None:
          gps = getattr(e, w)
          if gps.hasFix and _plausible(gps.unixTimestampMillis / 1e3):
            gps_wall = (gps.unixTimestampMillis / 1e3, e.logMonoTime * 1e-9)
      elif w == "clocks":
        if clock_wall is None and _plausible(e.clocks.wallTimeNanos / 1e9):
          clock_wall = (e.clocks.wallTimeNanos / 1e9, e.logMonoTime * 1e-9)
      elif w == "initData":
        if init_wall is None and _plausible(e.initData.wallTimeNanos / 1e9):
          init_wall = (e.initData.wallTimeNanos / 1e9, e.logMonoTime * 1e-9)
  acc.finish()

  start = None
  ref = gps_wall or clock_wall or init_wall
  if ref is not None and acc.t0 is not None:
    start = round(ref[0] - (ref[1] - acc.t0), 1)

  def r(x, n=1):
    return None if x is None else round(x, n)

  return {
    "route": route,
    "start": start,
    "start_source": "gps" if gps_wall else "clock" if (clock_wall or init_wall) else None,
    "segments": len(seg_dirs),
    "duration_s": r(acc.duration_s),
    "moving_s": r(acc.moving_s),
    "distance_m": r(acc.distance_m),
    "engaged_s": r(acc.engaged_s),
    "engaged_moving_s": r(acc.engaged_moving_s),
    "lat_active_s": r(acc.lat_active_s),
    "lat_active_moving_s": r(acc.lat_active_moving_s),
    "long_active_s": r(acc.long_active_s),
    "disengagements": acc.disengagements,
    "steer_overrides": acc.steer_overrides,
    "saturated_frac": r(acc.lat_sat_s / acc.lat_ctl_s, 4) if acc.lat_ctl_s > 0 else None,
    "max_lat_accel": r(acc.max_lat_accel, 2),
    "max_lat_accel_active": r(acc.max_lat_accel_active, 2),
    "events": acc.events,
    "summarized_at": round(time.time(), 1),
  }


# *** drives.json ***

def load_drives(path: str = DRIVES_PATH) -> dict:
  try:
    with open(path) as f:
      data = json.load(f)
    if isinstance(data, dict) and isinstance(data.get("drives"), list):
      return data
  except (OSError, ValueError):
    pass
  return {"version": VERSION, "drives": []}


def save_drives(data: dict, path: str = DRIVES_PATH) -> None:
  os.makedirs(os.path.dirname(path), exist_ok=True)
  tmp = f"{path}.tmp"
  with open(tmp, "w") as f:
    json.dump(data, f, separators=(",", ":"))
    f.flush()
    os.fsync(f.fileno())
  os.replace(tmp, path)


def is_drive(summary: dict) -> bool:
  return summary["moving_s"] >= MIN_MOVING_S and summary["distance_m"] >= MIN_DISTANCE_M


def update_drives(path: str = DRIVES_PATH, root: str | None = None, failed: dict | None = None) -> int:
  """Summarize finished routes that are new or grew, newest first. Returns how many were summarized."""
  from openpilot.common.swaglog import cloudlog

  failed = {} if failed is None else failed  # route -> segment count that failed, retried once it grows
  data = load_drives(path)
  routes = list_routes(root)
  known = {d["route"]: d.get("segments", 0) for d in data["drives"]}
  ignored = {r: n for r, n in data.get("ignored", {}).items() if r in routes}  # too short to be a drive

  done = 0
  for route in sorted(routes, key=route_sort_key, reverse=True):
    n = len(routes[route])
    if n <= max(known.get(route, 0), ignored.get(route, 0), failed.get(route, 0)) or route_locked(routes[route]):
      continue
    try:
      t = time.monotonic()
      summary = summarize_route(route, root)
      cloudlog.info(f"jaydrives: summarized {route} ({n} segments) in {time.monotonic() - t:.1f} s")
    except Exception:
      cloudlog.exception(f"jaydrives: failed to summarize {route}")
      failed[route] = n
      continue

    drives = [d for d in data["drives"] if d["route"] != route]
    if is_drive(summary):
      drives.append(summary)
      drives.sort(key=lambda d: route_sort_key(d["route"]))
      ignored.pop(route, None)
    else:
      ignored[route] = n
    data = {**data, "version": VERSION, "drives": drives[-MAX_DRIVES:], "ignored": ignored}
    known = {d["route"]: d.get("segments", 0) for d in data["drives"]}
    save_drives(data, path)
    done += 1
  return done


def main(argv: list[str] | None = None) -> None:
  import argparse
  parser = argparse.ArgumentParser(description="Per-drive stats from qlogs")
  parser.add_argument("--backfill", action="store_true", help="summarize every finished route now, then exit")
  parser.add_argument("--route", help="print one route's summary as JSON")
  parser.add_argument("--out", default=DRIVES_PATH, help="drives file (default %(default)s)")
  args = parser.parse_args([] if argv is None else argv)  # the manager calls main() with its own sys.argv

  try:
    os.nice(19)
  except OSError:
    pass

  if args.route:
    print(json.dumps(summarize_route(args.route), indent=1))
    return
  if args.backfill:
    t = time.monotonic()
    n = update_drives(args.out)
    print(f"summarized {n} routes in {time.monotonic() - t:.1f} s, {len(load_drives(args.out)['drives'])} drives in {args.out}")
    return

  from openpilot.common.swaglog import cloudlog
  failed: dict[str, int] = {}
  while True:
    try:
      update_drives(args.out, failed=failed)
    except Exception:
      cloudlog.exception("jaydrives: update failed")
    time.sleep(POLL_INTERVAL)


if __name__ == "__main__":
  main(sys.argv[1:])
