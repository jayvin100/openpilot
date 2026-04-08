#!/usr/bin/env python3
"""
Thermal Fan Controller Benchmark — Stock vs New Algorithm

Runs on-device (mici), cycling between offroad-idle and onroad-load phases
with two different fan controllers: stock (reactive PID) and new (model-based
with pre-spin + power feedforward). Measures temperature overshoot, peak temps,
time-to-peak, and steady-state behavior.

Architecture:
  - The test manages the fan DIRECTLY via panda USB (bypassing hardwared)
  - CPU onroad/offroad transitions are simulated via governor + core online changes
  - stress-ng provides the thermal load (realistic openpilot-level)
  - All thermal zones, fan RPM, power draw recorded at 2Hz to CSV

Test structure (default 10h = 5h stock + 5h new):
  Each half: ~15 cycles of [load 10min → cooldown 10min]
  Total: ~30 transitions per controller → statistically meaningful comparison

Quick smoke test mode: --smoke (2 cycles per controller, ~10 min total)

Usage:
  # Deploy via op switch, then on device:
  cd /data/openpilot
  python3 tools/thermal_benchmark.py --smoke            # quick validation
  python3 tools/thermal_benchmark.py                    # full 10h benchmark
  python3 tools/thermal_benchmark.py --duration-hours 6 # custom duration

Output:
  /data/thermal_benchmark_{timestamp}.csv
  /data/thermal_benchmark_{timestamp}_summary.json
"""

import argparse
import csv
import json
import math
import os
import signal
import struct
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path


# ── Pure-Python replacements for numpy (AGNOS has no numpy) ────────────────────

def interp(x, xp, fp):
  """Linear interpolation like np.interp (scalar x, sorted xp)."""
  if x <= xp[0]:
    return float(fp[0])
  if x >= xp[-1]:
    return float(fp[-1])
  for i in range(len(xp) - 1):
    if xp[i] <= x <= xp[i + 1]:
      t = (x - xp[i]) / (xp[i + 1] - xp[i])
      return float(fp[i] + t * (fp[i + 1] - fp[i]))
  return float(fp[-1])


def clip(val, lo, hi):
  return max(lo, min(hi, val))


def mean(vals):
  return sum(vals) / len(vals) if vals else 0.0


def median(vals):
  s = sorted(vals)
  n = len(s)
  if n == 0:
    return 0.0
  if n % 2 == 1:
    return float(s[n // 2])
  return (s[n // 2 - 1] + s[n // 2]) / 2.0


def std(vals):
  if len(vals) < 2:
    return 0.0
  m = mean(vals)
  return math.sqrt(sum((v - m) ** 2 for v in vals) / len(vals))


def percentile(vals, pct):
  if not vals:
    return 0.0
  s = sorted(vals)
  k = (len(s) - 1) * pct / 100.0
  f = int(k)
  c = f + 1
  if c >= len(s):
    return float(s[-1])
  return float(s[f] + (k - f) * (s[c] - s[f]))

# ── Panda fan control ──────────────────────────────────────────────────────────

def get_panda():
  """Get a connected panda for fan control."""
  sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
  from panda import Panda
  serials = Panda.list()
  if not serials:
    print("ERROR: No panda found!")
    sys.exit(1)
  p = Panda(serials[0])
  print(f"Connected to panda: {serials[0]}")
  return p


# ── Thermal zone reading ──────────────────────────────────────────────────────

THERMAL_ZONES = {
  "cpu0_silver": "cpu0-silver-usr",
  "cpu1_silver": "cpu1-silver-usr",
  "cpu2_silver": "cpu2-silver-usr",
  "cpu3_silver": "cpu3-silver-usr",
  "cpu0_gold": "cpu0-gold-usr",
  "cpu1_gold": "cpu1-gold-usr",
  "cpu2_gold": "cpu2-gold-usr",
  "cpu3_gold": "cpu3-gold-usr",
  "gpu0": "gpu0-usr",
  "gpu1": "gpu1-usr",
  "dsp_hvx": "compute-hvx-usr",
  "ddr": "ddr-usr",
  "pm8998": "pm8998_tz",
  "pm8005": "pm8005_tz",
  "xo_therm": "xo-therm-adc",
  "intake": "intake",
  "exhaust": "exhaust",
  "gnss": "gnss",
  "bottom_soc": "bottom_soc",
}

_zone_map: dict[str, int] = {}


def _build_zone_map():
  base = Path("/sys/devices/virtual/thermal")
  for d in base.iterdir():
    if not d.name.startswith("thermal_zone"):
      continue
    try:
      zone_type = (d / "type").read_text().strip()
      zone_num = int(d.name.removeprefix("thermal_zone"))
      _zone_map[zone_type] = zone_num
    except (OSError, ValueError):
      pass
  print(f"Mapped {len(_zone_map)} thermal zones")


def read_temp(zone_type: str) -> float:
  """Read temperature in °C from a thermal zone type name."""
  zone_num = _zone_map.get(zone_type)
  if zone_num is None:
    return -1.0
  try:
    raw = Path(f"/sys/devices/virtual/thermal/thermal_zone{zone_num}/temp").read_text().strip()
    return int(raw) / 1000.0
  except (OSError, ValueError):
    return -1.0


def read_all_temps() -> dict[str, float]:
  """Read all configured thermal zones."""
  temps = {}
  for key, zone_type in THERMAL_ZONES.items():
    temps[key] = read_temp(zone_type)
  return temps


def get_max_cpu_temp(temps: dict[str, float]) -> float:
  cpu_keys = [k for k in temps if k.startswith("cpu")]
  valid = [temps[k] for k in cpu_keys if temps[k] > 0]
  return max(valid) if valid else -1.0


def get_max_comp_temp(temps: dict[str, float]) -> float:
  """Composite max temp matching hardwared.py logic: max(cpu, gpu, memory, pmic)."""
  candidates = []
  for prefix in ("cpu", "gpu", "ddr", "pm8998", "pm8005"):
    for k, v in temps.items():
      if k.startswith(prefix) and v > 0:
        candidates.append(v)
  return max(candidates) if candidates else -1.0


# ── Power reading ──────────────────────────────────────────────────────────────

def read_power_draw() -> float:
  """Read system power draw in watts."""
  try:
    raw = Path("/sys/class/hwmon/hwmon1/power1_input").read_text().strip()
    return int(raw) / 1e6  # µW to W
  except (OSError, ValueError):
    pass
  # Fallback: BMS V*I
  try:
    v = int(Path("/sys/class/power_supply/bms/voltage_now").read_text().strip())
    i = int(Path("/sys/class/power_supply/bms/current_now").read_text().strip())
    return v * i / 1e12  # µV * µA → W
  except (OSError, ValueError):
    return 0.0


# ── CPU power state control ───────────────────────────────────────────────────

def sudo_write(val: str, path: str):
  try:
    with open(path, 'w') as f:
      f.write(val)
  except PermissionError:
    os.system(f"echo '{val}' | sudo tee {path} > /dev/null")
  except OSError:
    pass


def set_onroad_cpu():
  """Simulate onroad: big cores online, performance governor."""
  for i in range(4, 8):
    sudo_write('1', f'/sys/devices/system/cpu/cpu{i}/online')
  time.sleep(0.1)
  for n in ('0', '4'):
    sudo_write('performance', f'/sys/devices/system/cpu/cpufreq/policy{n}/scaling_governor')
  print("  CPU → onroad (all cores online, performance)")


def set_offroad_cpu():
  """Simulate offroad: big cores offline, ondemand governor."""
  sudo_write('ondemand', '/sys/devices/system/cpu/cpufreq/policy0/scaling_governor')
  for i in range(4, 8):
    sudo_write('0', f'/sys/devices/system/cpu/cpu{i}/online')
  print("  CPU → offroad (big cores offline, ondemand)")


# ── Workload ──────────────────────────────────────────────────────────────────

_stress_proc = None

def start_stress():
  """Start stress-ng load simulating onroad openpilot workload."""
  global _stress_proc
  stop_stress()
  # 8 CPU workers + 2 matrix workers = full core saturation for thermal stress
  _stress_proc = subprocess.Popen(
    ["stress-ng", "--cpu", "8", "--cpu-method", "matrixprod",
     "--matrix", "2", "--timeout", "0"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
  )
  print(f"  stress-ng started (pid={_stress_proc.pid})")


def stop_stress():
  """Stop stress-ng."""
  global _stress_proc
  if _stress_proc is not None:
    _stress_proc.terminate()
    try:
      _stress_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
      _stress_proc.kill()
      _stress_proc.wait()
    _stress_proc = None
  # Kill any straggler stress-ng processes
  os.system("pkill -9 stress-ng 2>/dev/null")


# ── Fan controllers ───────────────────────────────────────────────────────────

class StockFanController:
  """Replica of stock fan_controller.py: pure integral + temp feedforward, hard reset."""
  NAME = "stock"

  def __init__(self, rate: int = 2):
    self.last_ignition = False
    self.i = 0.0
    self.k_i = 4e-3
    self.dt = 1.0 / rate
    self.control = 0

  def update(self, cur_temp: float, ignition: bool, power_draw_w: float = 0.0) -> int:
    pos_limit = 100 if ignition else 30
    neg_limit = 30 if ignition else 0

    if ignition != self.last_ignition:
      self.i = 0.0
      self.control = 0
    self.last_ignition = ignition

    error = cur_temp - 75.0
    ff = interp(cur_temp, [60.0, 100.0], [0, 100])

    i = self.i + self.k_i * self.dt * error
    test = i + ff
    if test > pos_limit:
      i = min(self.i, pos_limit)
    elif test < neg_limit:
      i = max(self.i, neg_limit)
    self.i = i

    self.control = int(clip(i + ff, neg_limit, pos_limit))
    return self.control


class NewFanController:
  """New controller: P+I, power feedforward, prespin, no hard reset."""
  NAME = "new"

  def __init__(self, rate: int = 2):
    self.last_ignition = False
    self.k_p = 2.0
    self.k_i = 4e-3
    self.dt = 1.0 / rate
    self.i = 0.0
    self.control = 0
    self.power_bp = [2.0, 4.0, 6.0, 8.0]
    self.power_ff = [0, 30, 60, 90]

  def update(self, cur_temp: float, ignition: bool, power_draw_w: float = 0.0) -> int:
    pos_limit = 100 if ignition else 30
    neg_limit = 30 if ignition else 0

    if ignition and not self.last_ignition:
      ff_at_temp = interp(cur_temp, [60.0, 100.0], [0, 100])
      prespin = interp(cur_temp, [40.0, 55.0, 70.0], [30.0, 50.0, 80.0])
      self.i = max(0, prespin - ff_at_temp)
    self.last_ignition = ignition

    error = cur_temp - 75.0
    p = self.k_p * error

    ff_temp = interp(cur_temp, [60.0, 100.0], [0, 100])
    ff_power = interp(power_draw_w, self.power_bp, self.power_ff) if ignition else 0
    ff = max(ff_temp, ff_power)

    i = self.i + self.k_i * self.dt * error
    test = p + i + ff
    if test > pos_limit:
      i = min(self.i, pos_limit)
    elif test < neg_limit:
      i = max(self.i, neg_limit)
    self.i = i

    self.control = int(clip(p + i + ff, neg_limit, pos_limit))
    return self.control


# ── Data recording ────────────────────────────────────────────────────────────

@dataclass
class TransitionStats:
  controller: str
  cycle: int
  t_initial: float = 0.0
  t_peak: float = 0.0
  t_to_peak_s: float = 0.0
  t_steady: float = 0.0
  overshoot: float = 0.0
  t_ambient: float = 0.0
  p_onroad_avg: float = 0.0
  fan_at_peak: float = 0.0
  fan_at_steady: float = 0.0
  fan_at_0s: float = 0.0       # fan % at moment of transition
  fan_at_10s: float = 0.0      # fan % 10s after transition
  fan_at_30s: float = 0.0      # fan % 30s after transition
  would_block: bool = False     # would this have been blocked (T >= 80°C)?


# ── Main benchmark ────────────────────────────────────────────────────────────

def run_benchmark(args):
  timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
  csv_path = f"/data/thermal_benchmark_{timestamp}.csv"
  summary_path = f"/data/thermal_benchmark_{timestamp}_summary.json"
  status_path = "/data/thermal_benchmark_status.json"

  print(f"{'=' * 70}")
  print(f"THERMAL FAN CONTROLLER BENCHMARK")
  print(f"{'=' * 70}")
  print(f"Duration: {args.duration_hours}h ({args.duration_hours/2:.1f}h per controller)")
  print(f"Load phase: {args.load_minutes}min, Cooldown: {args.cool_minutes}min")
  print(f"Output: {csv_path}")
  print()

  # Init
  _build_zone_map()
  panda = get_panda()
  panda.set_fan_power(0)
  print(f"Fan RPM at start: {panda.get_fan_rpm()}")

  cycle_duration_s = (args.load_minutes + args.cool_minutes) * 60
  half_duration_s = args.duration_hours * 3600 / 2
  cycles_per_controller = max(1, int(half_duration_s / cycle_duration_s))

  if args.smoke:
    cycles_per_controller = 2
    args.load_minutes = 3
    args.cool_minutes = 2
    print(f"SMOKE TEST MODE: {cycles_per_controller} cycles, {args.load_minutes}m load, {args.cool_minutes}m cool")

  total_cycles = cycles_per_controller * 2
  print(f"Cycles per controller: {cycles_per_controller}")
  print(f"Total cycles: {total_cycles}")
  print()

  # CSV setup
  temp_headers = [f"temp_{k}_C" for k in THERMAL_ZONES.keys()]
  headers = [
    "timestamp", "elapsed_s", "controller", "cycle", "phase",
    "phase_elapsed_s", "ignition",
    "temp_max_comp_C", "temp_max_cpu_C",
    *temp_headers,
    "power_draw_W", "fan_desired_pct", "fan_rpm",
  ]

  csvfile = open(csv_path, 'w', newline='')
  writer = csv.DictWriter(csvfile, fieldnames=headers)
  writer.writeheader()
  csvfile.flush()

  # Signal handler for clean shutdown
  running = [True]
  def sighandler(sig, frame):
    print("\nInterrupted! Cleaning up...")
    running[0] = False
  signal.signal(signal.SIGINT, sighandler)
  signal.signal(signal.SIGTERM, sighandler)

  all_transitions: list[TransitionStats] = []
  start_time = time.monotonic()
  sample_interval = 0.5  # 2 Hz

  controllers = [
    ("stock", StockFanController),
    ("new", NewFanController),
  ]

  try:
    for ctrl_name, CtrlClass in controllers:
      if not running[0]:
        break

      print(f"\n{'=' * 70}")
      print(f"  PHASE: {ctrl_name.upper()} CONTROLLER ({cycles_per_controller} cycles)")
      print(f"{'=' * 70}")

      for cycle in range(cycles_per_controller):
        if not running[0]:
          break

        print(f"\n--- {ctrl_name} cycle {cycle + 1}/{cycles_per_controller} ---")

        # Ensure we start from offroad/cool state
        set_offroad_cpu()
        stop_stress()

        ctrl = CtrlClass()
        # Let controller settle in offroad for a moment
        print(f"  Settling offroad...", flush=True)
        for si in range(10):
          temps = read_all_temps()
          comp_temp = get_max_comp_temp(temps)
          power = read_power_draw()
          fan_pct = ctrl.update(comp_temp, False, power)
          try:
            panda.set_fan_power(fan_pct)
          except Exception as e:
            print(f"  WARNING: panda.set_fan_power failed: {e}", flush=True)
          time.sleep(sample_interval)
        print(f"  Settled at T={comp_temp:.1f}°C, fan={fan_pct}%", flush=True)

        # ── COOLDOWN PHASE (pre-cycle stabilization) ──
        if cycle > 0:
          print(f"  Cooldown phase ({args.cool_minutes}min)...")
          phase_start = time.monotonic()
          while time.monotonic() - phase_start < args.cool_minutes * 60:
            if not running[0]:
              break
            now = time.monotonic()
            temps = read_all_temps()
            comp_temp = get_max_comp_temp(temps)
            power = read_power_draw()
            fan_pct = ctrl.update(comp_temp, False, power)
            panda.set_fan_power(fan_pct)

            try:
              fan_rpm = panda.get_fan_rpm()
            except Exception:
              fan_rpm = 0

            row = {
              "timestamp": datetime.now(timezone.utc).isoformat(),
              "elapsed_s": round(now - start_time, 1),
              "controller": ctrl_name,
              "cycle": cycle,
              "phase": "cooldown",
              "phase_elapsed_s": round(now - phase_start, 1),
              "ignition": 0,
              "temp_max_comp_C": round(comp_temp, 2),
              "temp_max_cpu_C": round(get_max_cpu_temp(temps), 2),
              **{f"temp_{k}_C": round(v, 2) for k, v in temps.items()},
              "power_draw_W": round(power, 3),
              "fan_desired_pct": fan_pct,
              "fan_rpm": fan_rpm,
            }
            writer.writerow(row)
            csvfile.flush()
            time.sleep(sample_interval)

        # Record pre-transition baseline
        temps = read_all_temps()
        t_initial = get_max_comp_temp(temps)
        t_ambient = temps.get("intake", 0.0)
        if t_ambient < 0:
          t_ambient = temps.get("xo_therm", 0.0)

        # ── LOAD PHASE (onroad simulation) ──
        print(f"  Load phase ({args.load_minutes}min) — T_initial={t_initial:.1f}°C...")
        transition_time = time.monotonic()

        # Transition to onroad
        set_onroad_cpu()
        start_stress()

        # Create fresh controller for this transition (stock resets on ignition change)
        ctrl = CtrlClass()
        # First call with ignition=False to set state, then True to trigger transition
        ctrl.update(t_initial, False, read_power_draw())

        peak_temp = t_initial
        peak_time = 0.0
        peak_fan = 0
        fan_at_0 = 0
        fan_at_10 = 0
        fan_at_30 = 0
        load_temps = []
        load_powers = []
        load_fans = []

        phase_start = time.monotonic()
        while time.monotonic() - phase_start < args.load_minutes * 60:
          if not running[0]:
            break
          now = time.monotonic()
          elapsed = now - phase_start
          temps = read_all_temps()
          comp_temp = get_max_comp_temp(temps)
          power = read_power_draw()
          fan_pct = ctrl.update(comp_temp, True, power)
          panda.set_fan_power(fan_pct)

          try:
            fan_rpm = panda.get_fan_rpm()
          except Exception:
            fan_rpm = 0

          # Track transition stats
          if comp_temp > peak_temp:
            peak_temp = comp_temp
            peak_time = elapsed
            peak_fan = fan_pct

          if elapsed < 1.0:
            fan_at_0 = fan_pct
          if 9.5 < elapsed < 10.5:
            fan_at_10 = fan_pct
          if 29.5 < elapsed < 30.5:
            fan_at_30 = fan_pct

          if elapsed > 60:
            load_temps.append(comp_temp)
            load_powers.append(power)
            load_fans.append(fan_pct)

          row = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "elapsed_s": round(now - start_time, 1),
            "controller": ctrl_name,
            "cycle": cycle,
            "phase": "load",
            "phase_elapsed_s": round(elapsed, 1),
            "ignition": 1,
            "temp_max_comp_C": round(comp_temp, 2),
            "temp_max_cpu_C": round(get_max_cpu_temp(temps), 2),
            **{f"temp_{k}_C": round(v, 2) for k, v in temps.items()},
            "power_draw_W": round(power, 3),
            "fan_desired_pct": fan_pct,
            "fan_rpm": fan_rpm,
          }
          writer.writerow(row)
          csvfile.flush()

          # Print progress every 30s
          if int(elapsed) % 30 == 0 and abs(elapsed - int(elapsed)) < sample_interval:
            print(f"    t={elapsed:.0f}s: T={comp_temp:.1f}°C, fan={fan_pct}%, P={power:.1f}W, RPM={fan_rpm}")

          time.sleep(sample_interval)

        # Stop load
        stop_stress()
        set_offroad_cpu()

        # Compute transition statistics
        t_steady = mean(load_temps) if load_temps else comp_temp
        p_onroad = mean(load_powers) if load_powers else 0
        fan_steady = mean(load_fans) if load_fans else fan_pct

        stats = TransitionStats(
          controller=ctrl_name,
          cycle=cycle,
          t_initial=round(t_initial, 1),
          t_peak=round(peak_temp, 1),
          t_to_peak_s=round(peak_time, 1),
          t_steady=round(t_steady, 1),
          overshoot=round(peak_temp - t_steady, 1),
          t_ambient=round(t_ambient, 1),
          p_onroad_avg=round(p_onroad, 2),
          fan_at_peak=peak_fan,
          fan_at_steady=round(fan_steady, 0),
          fan_at_0s=fan_at_0,
          fan_at_10s=fan_at_10,
          fan_at_30s=fan_at_30,
          would_block=(peak_temp >= 80.0),
        )
        all_transitions.append(stats)

        print(f"  RESULT: T_init={stats.t_initial}°C → T_peak={stats.t_peak}°C "
              f"(+{stats.overshoot}°C overshoot) in {stats.t_to_peak_s}s")
        print(f"          T_steady={stats.t_steady}°C, fan@0s={stats.fan_at_0s}%, "
              f"fan@10s={stats.fan_at_10s}%, fan@30s={stats.fan_at_30s}%")
        print(f"          Would block onroad: {'YES' if stats.would_block else 'no'}")

        # Update status file for remote monitoring
        _write_status(status_path, ctrl_name, cycle, cycles_per_controller,
                      stats, start_time, all_transitions)

      # Final cooldown between controller phases
      if ctrl_name == "stock" and running[0]:
        print(f"\n  Cooling down before switching to new controller...")
        set_offroad_cpu()
        stop_stress()
        ctrl = StockFanController()
        cool_start = time.monotonic()
        while time.monotonic() - cool_start < args.cool_minutes * 60:
          if not running[0]:
            break
          temps = read_all_temps()
          comp_temp = get_max_comp_temp(temps)
          power = read_power_draw()
          fan_pct = ctrl.update(comp_temp, False, power)
          panda.set_fan_power(fan_pct)

          row = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "elapsed_s": round(time.monotonic() - start_time, 1),
            "controller": "transition",
            "cycle": -1,
            "phase": "cooldown",
            "phase_elapsed_s": round(time.monotonic() - cool_start, 1),
            "ignition": 0,
            "temp_max_comp_C": round(comp_temp, 2),
            "temp_max_cpu_C": round(get_max_cpu_temp(temps), 2),
            **{f"temp_{k}_C": round(v, 2) for k, v in temps.items()},
            "power_draw_W": round(power, 3),
            "fan_desired_pct": fan_pct,
            "fan_rpm": 0,
          }
          writer.writerow(row)
          csvfile.flush()
          time.sleep(sample_interval)

  finally:
    # Clean up
    stop_stress()
    set_offroad_cpu()
    try:
      panda.set_fan_power(0)
    except Exception:
      pass
    csvfile.close()

  # ── Generate summary ──────────────────────────────────────────────────────

  print(f"\n{'=' * 70}")
  print(f"BENCHMARK COMPLETE — SUMMARY")
  print(f"{'=' * 70}")

  summary = generate_summary(all_transitions)
  summary["csv_path"] = csv_path
  summary["duration_hours"] = args.duration_hours
  summary["smoke_test"] = args.smoke

  with open(summary_path, 'w') as f:
    json.dump(summary, f, indent=2)
  print(f"\nSaved: {csv_path}")
  print(f"Saved: {summary_path}")

  print_summary(summary)
  return summary


def generate_summary(transitions: list[TransitionStats]) -> dict:
  stock = [t for t in transitions if t.controller == "stock"]
  new = [t for t in transitions if t.controller == "new"]

  def stats(vals):
    vals = vals if vals else [0.0]
    return {
      "n": len(vals),
      "mean": round(mean(vals), 2),
      "median": round(median(vals), 2),
      "std": round(std(vals), 2),
      "min": round(min(vals), 2),
      "max": round(max(vals), 2),
      "p10": round(percentile(vals, 10), 2),
      "p90": round(percentile(vals, 90), 2),
    }

  def ctrl_stats(transitions_list):
    return {
      "n_transitions": len(transitions_list),
      "t_peak": stats([t.t_peak for t in transitions_list]),
      "t_initial": stats([t.t_initial for t in transitions_list]),
      "t_steady": stats([t.t_steady for t in transitions_list]),
      "overshoot": stats([t.overshoot for t in transitions_list]),
      "t_to_peak_s": stats([t.t_to_peak_s for t in transitions_list]),
      "fan_at_0s": stats([t.fan_at_0s for t in transitions_list]),
      "fan_at_10s": stats([t.fan_at_10s for t in transitions_list]),
      "fan_at_30s": stats([t.fan_at_30s for t in transitions_list]),
      "fan_at_peak": stats([t.fan_at_peak for t in transitions_list]),
      "fan_at_steady": stats([t.fan_at_steady for t in transitions_list]),
      "p_onroad_avg": stats([t.p_onroad_avg for t in transitions_list]),
      "would_block_count": sum(1 for t in transitions_list if t.would_block),
      "would_block_pct": round(100 * sum(1 for t in transitions_list if t.would_block) / max(len(transitions_list), 1), 1),
    }

  return {
    "stock": ctrl_stats(stock),
    "new": ctrl_stats(new),
    "improvement": {
      "peak_temp_reduction_mean": round(
        mean([t.t_peak for t in stock]) - mean([t.t_peak for t in new]), 2
      ) if stock and new else 0,
      "overshoot_reduction_mean": round(
        mean([t.overshoot for t in stock]) - mean([t.overshoot for t in new]), 2
      ) if stock and new else 0,
      "block_reduction": (
        sum(1 for t in stock if t.would_block) - sum(1 for t in new if t.would_block)
      ) if stock and new else 0,
      "fan_at_10s_improvement_mean": round(
        mean([t.fan_at_10s for t in new]) - mean([t.fan_at_10s for t in stock]), 2
      ) if stock and new else 0,
    },
    "all_transitions": [
      {
        "controller": t.controller, "cycle": t.cycle,
        "t_initial": t.t_initial, "t_peak": t.t_peak,
        "t_to_peak_s": t.t_to_peak_s, "t_steady": t.t_steady,
        "overshoot": t.overshoot, "t_ambient": t.t_ambient,
        "fan_at_0s": t.fan_at_0s, "fan_at_10s": t.fan_at_10s,
        "fan_at_30s": t.fan_at_30s, "would_block": t.would_block,
        "p_onroad_avg": t.p_onroad_avg,
      }
      for t in transitions
    ],
  }


def print_summary(summary):
  s = summary["stock"]
  n = summary["new"]
  imp = summary["improvement"]

  print(f"\n{'':>30} {'STOCK':>12} {'NEW':>12} {'DELTA':>12}")
  print(f"  {'─' * 66}")

  def row(label, sk, nk, fmt=".1f", invert=False):
    sv = s[sk]["mean"]
    nv = n[nk]["mean"]
    d = sv - nv if not invert else nv - sv
    sign = "+" if d > 0 else ""
    print(f"  {label:>28} {sv:>12{fmt}} {nv:>12{fmt}} {sign}{d:>11{fmt}}")

  row("Peak Temp (°C)", "t_peak", "t_peak")
  row("Overshoot (°C)", "overshoot", "overshoot")
  row("Time to Peak (s)", "t_to_peak_s", "t_to_peak_s")
  row("Steady-State Temp (°C)", "t_steady", "t_steady")
  row("Fan @ 0s (%)", "fan_at_0s", "fan_at_0s", invert=True)
  row("Fan @ 10s (%)", "fan_at_10s", "fan_at_10s", invert=True)
  row("Fan @ 30s (%)", "fan_at_30s", "fan_at_30s", invert=True)
  row("Fan @ Peak (%)", "fan_at_peak", "fan_at_peak", invert=True)
  row("Steady-State Fan (%)", "fan_at_steady", "fan_at_steady", invert=True)

  print(f"\n  Would-block events (T >= 80°C):")
  print(f"    Stock: {s['would_block_count']}/{s['n_transitions']} ({s['would_block_pct']}%)")
  print(f"    New:   {n['would_block_count']}/{n['n_transitions']} ({n['would_block_pct']}%)")
  print(f"    Reduction: {imp['block_reduction']} fewer blocks")

  print(f"\n  KEY IMPROVEMENTS:")
  print(f"    Peak temp reduction:     {imp['peak_temp_reduction_mean']:+.1f}°C")
  print(f"    Overshoot reduction:     {imp['overshoot_reduction_mean']:+.1f}°C")
  print(f"    Fan response @ 10s:      {imp['fan_at_10s_improvement_mean']:+.1f}% faster")
  print()


def _write_status(path, ctrl_name, cycle, total_cycles, stats, start_time, all_trans):
  elapsed_h = (time.monotonic() - start_time) / 3600
  try:
    with open(path, 'w') as f:
      json.dump({
        "controller": ctrl_name,
        "cycle": cycle,
        "total_cycles": total_cycles,
        "elapsed_hours": round(elapsed_h, 2),
        "last_transition": {
          "t_initial": stats.t_initial,
          "t_peak": stats.t_peak,
          "overshoot": stats.overshoot,
          "would_block": stats.would_block,
        },
        "transitions_completed": len(all_trans),
      }, f, indent=2)
  except Exception:
    pass


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
  # Force unbuffered stdout for real-time log output
  sys.stdout = open(sys.stdout.fileno(), 'w', buffering=1, closefd=False)

  parser = argparse.ArgumentParser(description="Thermal fan controller benchmark")
  parser.add_argument("--duration-hours", type=float, default=10.0,
                      help="Total benchmark duration in hours (default: 10)")
  parser.add_argument("--load-minutes", type=float, default=10.0,
                      help="Load phase duration per cycle (default: 10)")
  parser.add_argument("--cool-minutes", type=float, default=10.0,
                      help="Cooldown phase duration per cycle (default: 10)")
  parser.add_argument("--smoke", action="store_true",
                      help="Quick smoke test (2 cycles per controller, ~10 min)")
  args = parser.parse_args()

  if os.geteuid() != 0:
    print("NOTE: Running without root. CPU governor changes may require sudo.")

  run_benchmark(args)


if __name__ == "__main__":
  main()
