#!/usr/bin/env python3
"""Drive report from the comma 3X, for tuning insight.

  scripts/drive_report.py              report on the last drive
  scripts/drive_report.py N            the Nth most recent drive (1 = last)
  scripts/drive_report.py ROUTE        a route, e.g. 0000002e--a05b7fe0e2
  scripts/drive_report.py --list       recent drives, one line each

  --fresh       re-summarize from the qlogs instead of using /data/jay/drives.json
  --dev [PATH]  upload a local openpilot/jay/drives.py to /tmp on the device and use it
                (default: the first ../op-*/ or ../sp-*/openpilot/jay/drives.py next to this repo)
  --json        print the raw summary

The qlog parsing runs on the device (openpilot.jay.drives from patches/004); this script only formats.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import time

HOST = os.environ.get("COMMA_HOST", "comma@192.168.0.138")
KEY = os.path.expanduser(os.environ.get("COMMA_KEY", "~/.ssh/id_ed25519_comma"))
SSH = ["ssh", "-i", KEY, "-o", "ConnectTimeout=5", HOST]
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEV_DIR = "/tmp/jay_drive_report"
DEVICE_PY = "cd /data/openpilot && source launch_env.sh && PYTHONPATH=/data/openpilot exec /usr/local/venv/bin/python"

# Runs on the device: argv = [module path or "", selection, fresh, list]. Prints one JSON document.
REMOTE = r'''
import importlib.util, json, sys
dev, sel, fresh, list_only = sys.argv[1], sys.argv[2], sys.argv[3] == "1", sys.argv[4] == "1"
def fail(msg):
  print(json.dumps({"error": msg})); sys.exit(0)
if dev:
  spec = importlib.util.spec_from_file_location("jay_drives_dev", dev)
  D = importlib.util.module_from_spec(spec); spec.loader.exec_module(D)
else:
  try:
    import openpilot.jay.drives as D
  except ImportError:
    fail("openpilot.jay.drives isn't installed on the device yet (patches/004 not deployed). Use --dev.")
data = D.load_drives()
drives = data["drives"]
if list_only:
  print(json.dumps({"drives": [{k: v for k, v in d.items() if k != "events"} for d in drives]})); sys.exit(0)
routes = D.list_routes()
out = None
if "--" in sel:
  route = sel
elif drives:
  n = int(sel)
  if not 1 <= n <= len(drives):
    fail(f"there are {len(drives)} drives in {D.DRIVES_PATH}")
  route = drives[-n]["route"]
else:
  # nothing summarized yet: walk back through the finished routes, skipping ones too short to be drives
  n, found = int(sel), []
  for r in sorted(routes, key=D.route_sort_key, reverse=True):
    if len(found) < n and r not in data.get("ignored", {}) and not D.route_locked(routes[r]):
      s = D.summarize_route(r)
      if D.is_drive(s):
        found.append(s)
  if len(found) < n:
    fail(f"only {len(found)} finished drives on the device")
  out = found[-1]
  route = out["route"]
if out is None:
  cached = next((d for d in drives if d["route"] == route), None)
  nseg = len(routes.get(route, []))
  if cached is not None and (cached["segments"] >= nseg and not fresh or not nseg):
    out = dict(cached, cached=True)
  elif nseg:
    out = D.summarize_route(route)
  else:
    fail(f"no route {route} on the device")
idx = [d["route"] for d in drives]
out["drive_index"] = len(idx) - idx.index(route) if route in idx else None
out["total_drives"] = len(drives)
print(json.dumps(out))
'''


def find_dev_module() -> str | None:
  for pattern in ("op-*", "sp-*"):
    for p in sorted(glob.glob(os.path.join(os.path.dirname(REPO), pattern, "openpilot", "jay", "drives.py"))):
      return p
  return None


def run_remote(sel: str, fresh: bool, list_only: bool, dev: str | None) -> dict:
  remote_mod = ""
  if dev:
    subprocess.run(SSH + [f"mkdir -p {DEV_DIR}"], check=True)
    subprocess.run(["scp", "-q", "-i", KEY, dev, f"{HOST}:{DEV_DIR}/drives.py"], check=True)
    remote_mod = f"{DEV_DIR}/drives.py"
  try:
    cmd = f"{DEVICE_PY} - '{remote_mod}' '{sel}' {int(fresh)} {int(list_only)}"
    r = subprocess.run(SSH + [cmd], input=REMOTE.encode(), capture_output=True)
  finally:
    if dev:
      subprocess.run(SSH + [f"rm -rf {DEV_DIR}"])
  lines = r.stdout.decode().strip().splitlines()
  if r.returncode != 0 or not lines:
    sys.exit(f"device error:\n{r.stderr.decode().strip()}")
  out = json.loads(lines[-1])
  if "error" in out:
    sys.exit(out["error"])
  return out


# *** formatting ***

def clock(t: float) -> str:
  t = int(round(t))
  h, m, s = t // 3600, t // 60 % 60, t % 60
  return f"{h}:{m:02d}:{s:02d}" if h else f"{m:02d}:{s:02d}"


def kmh(v: float) -> str:
  return f"{v * 3.6:.0f} km/h"


def pct(a: float, b: float) -> str:
  return f"{100 * a / b:.0f}%" if b > 0 else "-"


def drive_number(route: str) -> str:
  try:
    return f"#{int(route.split('--')[0], 16)}"
  except ValueError:
    return ""


def start_str(d: dict) -> str:
  if not d.get("start"):
    return "unknown date (no GPS fix or synced clock)"
  return time.strftime("%a %d %b %Y %H:%M", time.localtime(d["start"]))


def n_of(n: int, word: str) -> str:
  return f"{n} {word}{'' if n == 1 else 's'}"


def suggestions(d: dict) -> list[str]:
  out = []
  moving, eng = d["moving_s"], d.get("engaged_moving_s", d["engaged_s"])
  lat_km = d["distance_m"] / 1000 * (eng / moving if moving else 0)  # rough engaged distance
  evs = d["events"]
  dis = [e for e in evs if e["type"] == "disengage"]
  ovr = [e for e in evs if e["type"] == "steer_override"]
  ovr_real = [e for e in ovr if not e.get("blinker")]
  sat = d.get("saturated_frac")
  max_lat = d.get("max_lat_accel_active")

  if moving > 0 and eng / moving < 0.25:
    out.append(f"Engaged for only {pct(eng, moving)} of the moving time, so there's little data for tuning conclusions.")
  if ovr and len(ovr) - len(ovr_real):
    out.append(f"{len(ovr) - len(ovr_real)} of {n_of(len(ovr), 'steer override')} had the blinker on: lane changes, not tuning.")
  if ovr_real:
    slow = [e for e in ovr_real if e["speed"] < 20 / 3.6]
    fast = [e for e in ovr_real if e["speed"] >= 20 / 3.6]
    rate = len(fast) / lat_km if lat_km > 0.5 else None  # per engaged km, above 20 km/h
    if len(slow) >= max(2, 0.6 * len(ovr_real)):
      out.append(f"{len(slow)} of {n_of(len(ovr_real), 'steer override')} were under 20 km/h (traffic, junctions, parking). "
                 "openpilot isn't tuned for tight low-speed turns, so these say little about curve tuning.")
    sat_ovr = [e for e in ovr_real if e.get("saturated")]
    if sat is not None and sat > 0.05 and (len(sat_ovr) >= 2 or (rate or 0) > 0.3):
      out.append(f"Many steer overrides with the lateral controller saturated {sat * 100:.1f}% of the time: the car wants more "
                 "steering authority in curves than the limits allow. Worth discussing, NOT auto-changing: on this "
                 "angle-controlled Tesla, saturation means the lateral accel/jerk limits (~3.6 m/s^2, mirrored in panda "
                 "safety) clipped the command.")
    elif rate is not None and rate > 0.3 and (sat is None or sat < 0.01):
      out.append(f"{rate:.1f} steer overrides above 20 km/h per engaged km while the controller was rarely saturated: the overrides "
                 "aren't from hitting steering limits. More likely lane position or path choice (hugging a line, "
                 "cutting or running wide in curves). Check the moments in the video before tuning anything.")
  if max_lat is not None and max_lat >= 3.0:
    out.append(f"Engaged lateral accel reached {max_lat:.1f} m/s^2, close to the ~3.6 m/s^2 cap. Those curves were at the limit.")
  causes: dict[str, int] = {}
  for e in dis:
    causes[e["cause"]] = causes.get(e["cause"], 0) + 1
  if dis and causes.get("brake", 0) >= 0.6 * len(dis):
    out.append(f"{causes['brake']} of {n_of(len(dis), 'disengagement')} were brake presses. That's longitudinal (braking "
               "late or too gently, following distance), not steering. Check the moments; a different driving "
               "personality may help.")
  if causes.get("alert"):
    alerts = sorted({e.get("alert2") or e.get("alert") or e.get("event", "") for e in dis if e["cause"] == "alert"})
    out.append(f"{n_of(causes['alert'], 'disengagement')} forced by alerts ({', '.join(a for a in alerts if a) or 'see above'}). "
               "Faults are worth investigating before any tuning.")
  if causes.get("steering"):
    out.append(f"{n_of(causes['steering'], 'disengagement')} from steering input (the Tesla EPS drops out under firm "
               "torque). If you meant to nudge rather than take over, a lighter touch keeps it engaged.")
  if causes.get("unknown"):
    out.append(f"{n_of(causes['unknown'], 'disengagement')} with no clear cause in the qlogs (10 Hz). The rlog has full-rate data.")
  if not out:
    out.append("Nothing stands out.")
  return out


def report(d: dict) -> None:
  dur, moving = d["duration_s"], d["moving_s"]
  eng = d.get("engaged_moving_s", d["engaged_s"])
  lat = d.get("lat_active_moving_s", d["lat_active_s"])
  idx = f" • drive {d['drive_index']} of the last {d['total_drives']}" if d.get("drive_index") else ""
  print(f"Drive {drive_number(d['route'])}  {d['route']}{idx}")
  src = {"gps": " (GPS time)", "clock": " (device clock)"}.get(d.get("start_source") or "", "")
  print(f"  {start_str(d)}{src} • {d['segments']} segments{' • cached summary' if d.get('cached') else ''}")
  avg = f" • avg {kmh(d['distance_m'] / moving)} while moving" if moving > 0 else ""
  print(f"  {clock(dur)} total, {clock(moving)} moving • {d['distance_m'] / 1000:.1f} km{avg}")
  print(f"  Engaged {pct(eng, moving)} of moving time • lateral active {pct(lat, moving)} • "
        f"longitudinal active {pct(d['long_active_s'], dur)} of the drive")

  evs = d["events"]
  dis = [e for e in evs if e["type"] == "disengage"]
  print(f"\nDisengagements: {d['disengagements']}")
  if dis:
    causes: dict[str, int] = {}
    for e in dis:
      causes[e["cause"]] = causes.get(e["cause"], 0) + 1
    print("  by cause: " + ", ".join(f"{c} {n}" for c, n in sorted(causes.items(), key=lambda x: -x[1])))
    for e in dis:
      extra = " / ".join(x for x in (e.get("alert"), e.get("alert2")) if x)
      detail = f"  {e['event']}" if e.get("event") else ""
      quoted = f'  "{extra}"' if extra else ""
      print(f"  {clock(e['t']):>7}  {e['cause']:<8} {kmh(e['speed']):>8}{detail}{quoted}")

  ovr = [e for e in evs if e["type"] == "steer_override"]
  print(f"\nSteer overrides: {d['steer_overrides']}")
  for e in ovr:
    notes = []
    if e.get("blinker"):
      notes.append("blinker (lane change?)")
    if e.get("saturated"):
      notes.append("controller saturated")
    if e.get("lat_accel") is not None:
      notes.append(f"lat accel {e['lat_accel']:.1f} m/s^2")
    print(f"  {clock(e['t']):>7}  {kmh(e['speed']):>8}  {', '.join(notes)}")

  print("\nLateral")
  sat = d.get("saturated_frac")
  print(f"  Saturated: {'-' if sat is None else f'{sat * 100:.1f}% of lateral active time'}")
  ma, mae = d.get("max_lat_accel"), d.get("max_lat_accel_active")
  print(f"  Max lateral accel: {'-' if mae is None else f'{mae:.2f} m/s^2'} engaged, "
        f"{'-' if ma is None else f'{ma:.2f} m/s^2'} overall")

  print("\nWhat this suggests")
  for s in suggestions(d):
    print(f"  - {s}")


def list_drives(drives: list[dict]) -> None:
  if not drives:
    print("No drives summarized yet.")
    return
  for i, d in enumerate(reversed(drives[-20:]), 1):
    when = time.strftime("%d %b %H:%M", time.localtime(d["start"])) if d.get("start") else "     ?      "
    eng = pct(d.get("engaged_moving_s", d["engaged_s"]), d["moving_s"])
    print(f"{i:>3}  {when}  {drive_number(d['route']):>5}  {d['distance_m'] / 1000:5.1f} km  {clock(d['duration_s']):>7}  "
          f"engaged {eng:>4}  disengage {d['disengagements']:>2}  overrides {d['steer_overrides']:>2}")


def main() -> None:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("which", nargs="?", default="1", help="N (1 = last drive) or a route name")
  parser.add_argument("--list", action="store_true")
  parser.add_argument("--fresh", action="store_true")
  parser.add_argument("--dev", nargs="?", const="", default=None, metavar="PATH")
  parser.add_argument("--json", action="store_true")
  args = parser.parse_args()

  if not (args.which.isdigit() or "--" in args.which):
    parser.error("expected N or a route name like 0000002e--a05b7fe0e2")
  dev = None
  if args.dev is not None:
    dev = args.dev or find_dev_module()
    if not dev or not os.path.isfile(dev):
      sys.exit("--dev: no local drives.py found, pass its path")

  out = run_remote(args.which, args.fresh, args.list, dev)
  if args.json:
    print(json.dumps(out, indent=1))
  elif args.list:
    list_drives(out["drives"])
  else:
    report(out)


if __name__ == "__main__":
  main()
