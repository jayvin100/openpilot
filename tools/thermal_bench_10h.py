#!/usr/bin/env python3
"""
10-Hour Thermal Bench Test — Iterative Fan Controller Tuning

Runs on the HOST PC, controls the mici device via ADB.
Drives load and reads sensors remotely. Uses panda jungle on host USB
to power-cycle the device if it becomes unresponsive.

Test structure (10h total):
  Phase 1 (2.5h): STOCK controller baseline — full-power burn, detect LMH throttle
  Phase 2 (2.5h): NEW controller — same burn, compare against baseline
  Phase 3 (2.5h): OP onroad simulation — realistic load, both controllers
  Phase 4 (2.5h): Iteration — re-tune breakpoints from collected data, re-validate

Each phase cycles: [load 10min → cooldown 5min] × N

Key measurements:
  - Temperature trajectory during load ramp (detect LMH @ 95°C)
  - Hysteresis recovery (LMH release @ 65°C via lmh_freq_limit)
  - Fan output, RPM, smoothness (zero jumps > 5%)
  - Time spent throttled, peak temperature, steady-state temperature

SM845 Thermal Throttling:
  - LMH-DCVS trip: 95°C, hysteresis 30°C → release at 65°C
  - GPU trip: 95°C, no hysteresis
  - CPU per-core step: 110°C, 10°C hysteresis → release at 100°C
  - Detection: /sys/.../lmh_freq_limit drops below max when throttled

Usage:
  python3 tools/thermal_bench_10h.py --smoke     # Quick 15-min validation
  python3 tools/thermal_bench_10h.py             # Full 10h run
  python3 tools/thermal_bench_10h.py --phase 2   # Run only phase 2
"""

import argparse
import csv
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

# ── ADB helpers ────────────────────────────────────────────────────────────────

def adb(cmd, timeout=10):
  """Run ADB shell command, return stdout. Raises on timeout."""
  try:
    r = subprocess.run(["adb", "shell", cmd], capture_output=True, text=True, timeout=timeout)
    return r.stdout.strip()
  except subprocess.TimeoutExpired:
    return ""
  except Exception as e:
    return f"ERROR: {e}"


def adb_write(path, value):
  """Write a value to a sysfs path on device."""
  adb(f"echo '{value}' > {path}")


def adb_read_int(path):
  """Read an integer from sysfs."""
  try:
    return int(adb(f"cat {path}"))
  except (ValueError, TypeError):
    return -1


def adb_alive():
  """Check if device is responsive."""
  return "ok" in adb("echo ok", timeout=5)


# ── Jungle power cycle ────────────────────────────────────────────────────────

def power_cycle_jungle():
  """Power cycle the device via panda jungle on host USB."""
  print("  POWER CYCLING via jungle...", flush=True)
  try:
    sys.path.insert(0, str(Path(__file__).parent.parent))
    from panda import Panda
    from panda.python.constants import McuType
    # Jungle has VID:PID bbaa:ddcf
    serials = Panda.list()
    for s in serials:
      try:
        p = Panda(s)
        # Toggle OBD power or harness power
        p.set_safety_mode(0)  # silent mode
        # Cut ignition line (relay off)
        p.set_panda_power(False)
        time.sleep(3)
        p.set_panda_power(True)
        p.close()
        print("  Jungle power cycle done, waiting 30s for boot...", flush=True)
        time.sleep(30)
        return True
      except Exception:
        continue
  except Exception as e:
    print(f"  Jungle power cycle failed: {e}", flush=True)

  # Fallback: ADB reboot
  print("  Falling back to adb reboot...", flush=True)
  subprocess.run(["adb", "reboot"], timeout=10)
  time.sleep(60)
  return adb_alive()


def ensure_device_alive():
  """Make sure device is responsive, power cycle if needed."""
  for attempt in range(3):
    if adb_alive():
      return True
    print(f"  Device unresponsive (attempt {attempt+1}/3)", flush=True)
    if attempt < 2:
      power_cycle_jungle()
      # Wait for ADB to reconnect
      for _ in range(12):
        time.sleep(5)
        if adb_alive():
          return True
  print("  FATAL: Device unresponsive after 3 power cycles!", flush=True)
  return False


# ── Sensor reading ─────────────────────────────────────────────────────────────

# Thermal zone sysfs mapping (built at startup)
ZONE_MAP = {}

def build_zone_map():
  """Scan device sysfs for thermal zone numbers."""
  global ZONE_MAP
  output = adb("for z in /sys/devices/virtual/thermal/thermal_zone*; do "
               "echo $(cat $z/type 2>/dev/null):$(basename $z | sed 's/thermal_zone//'); done")
  for line in output.split('\n'):
    if ':' in line:
      parts = line.strip().split(':')
      if len(parts) == 2 and parts[1].isdigit():
        ZONE_MAP[parts[0]] = int(parts[1])
  print(f"  Mapped {len(ZONE_MAP)} thermal zones", flush=True)


def read_thermal_zone(zone_type):
  """Read a thermal zone temp in °C."""
  znum = ZONE_MAP.get(zone_type)
  if znum is None:
    return -1.0
  val = adb_read_int(f"/sys/devices/virtual/thermal/thermal_zone{znum}/temp")
  return val / 1000.0 if val > 0 else -1.0


THERMAL_KEYS = [
  ("cpu_silver_max", ["cpu0-silver-usr", "cpu1-silver-usr", "cpu2-silver-usr", "cpu3-silver-usr"]),
  ("cpu_gold_max", ["cpu0-gold-usr", "cpu1-gold-usr", "cpu2-gold-usr", "cpu3-gold-usr"]),
  ("gpu_max", ["gpu0-usr", "gpu1-usr"]),
  ("ddr", ["ddr-usr"]),
  ("pm8998", ["pm8998_tz"]),
  ("intake", ["intake"]),
  ("exhaust", ["exhaust"]),
  ("lmh_dcvs_00", ["lmh-dcvs-00"]),
  ("lmh_dcvs_01", ["lmh-dcvs-01"]),
]


def read_all_sensors():
  """Read all thermal sensors + power + freq in one batch."""
  # Batch read for speed: one ADB call
  cmd_parts = []
  # Temps
  for key, zones in THERMAL_KEYS:
    for z in zones:
      znum = ZONE_MAP.get(z, -1)
      if znum >= 0:
        cmd_parts.append(f"cat /sys/devices/virtual/thermal/thermal_zone{znum}/temp 2>/dev/null || echo -1")
  # Power
  cmd_parts.append("cat /sys/class/power_supply/bms/voltage_now 2>/dev/null || echo 0")
  cmd_parts.append("cat /sys/class/power_supply/bms/current_now 2>/dev/null || echo 0")
  # CPU freq (current and limit)
  cmd_parts.append("cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2>/dev/null || echo 0")
  cmd_parts.append("cat /sys/devices/system/cpu/cpufreq/policy4/scaling_cur_freq 2>/dev/null || echo 0")
  # LMH freq limits
  cmd_parts.append("cat /sys/devices/platform/soc/17d41000.qcom,cpucc/17d41000.qcom,cpucc:qcom,limits-dcvs@0/lmh_freq_limit 2>/dev/null || echo 0")
  cmd_parts.append("cat /sys/devices/platform/soc/17d41000.qcom,cpucc/17d41000.qcom,cpucc:qcom,limits-dcvs@1/lmh_freq_limit 2>/dev/null || echo 0")
  # Fan (from panda via hardwared if running, otherwise read from last known)
  cmd_parts.append("cat /sys/class/hwmon/hwmon0/fan1_input 2>/dev/null || echo -1")

  output = adb("; echo '---'; ".join(cmd_parts), timeout=8)
  vals = [v.strip() for v in output.split('---')]

  result = {}
  idx = 0
  for key, zones in THERMAL_KEYS:
    zone_temps = []
    for z in zones:
      if ZONE_MAP.get(z, -1) >= 0:
        try:
          zone_temps.append(int(vals[idx]) / 1000.0)
        except (ValueError, IndexError):
          zone_temps.append(-1.0)
        idx += 1
    result[key] = max(zone_temps) if zone_temps else -1.0

  # Power
  try:
    v_uv = int(vals[idx]); idx += 1
    i_ua = int(vals[idx]); idx += 1
    result['power_w'] = v_uv * i_ua / 1e12
  except (ValueError, IndexError):
    result['power_w'] = 0.0
    idx += 2

  # CPU freq
  try:
    result['silver_freq_khz'] = int(vals[idx]); idx += 1
    result['gold_freq_khz'] = int(vals[idx]); idx += 1
  except (ValueError, IndexError):
    result['silver_freq_khz'] = 0; result['gold_freq_khz'] = 0; idx += 2

  # LMH limits
  try:
    result['lmh_silver_limit'] = int(vals[idx]); idx += 1
    result['lmh_gold_limit'] = int(vals[idx]); idx += 1
  except (ValueError, IndexError):
    result['lmh_silver_limit'] = 0; result['lmh_gold_limit'] = 0; idx += 2

  # LMH throttle detection
  result['lmh_throttled'] = (result['lmh_silver_limit'] < 1766400 or
                              result['lmh_gold_limit'] < 2803200)

  # Composite temp (matches hardwared logic)
  comp_temps = [result[k] for k in ['cpu_silver_max', 'cpu_gold_max', 'gpu_max', 'ddr', 'pm8998'] if result[k] > 0]
  result['comp_temp'] = max(comp_temps) if comp_temps else -1.0

  return result


# ── CPU load control ──────────────────────────────────────────────────────────

def set_performance_mode():
  """All cores online, performance governor, max freq."""
  adb("for i in 4 5 6 7; do echo 1 > /sys/devices/system/cpu/cpu$i/online; done")
  time.sleep(0.5)
  adb("echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor")
  adb("echo performance > /sys/devices/system/cpu/cpufreq/policy4/scaling_governor 2>/dev/null")
  # Verify
  online = adb("for i in 4 5 6 7; do cat /sys/devices/system/cpu/cpu$i/online; done")
  gov0 = adb("cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor")
  print(f"  CPU → performance (big cores: {online.replace(chr(10),'')}, gov: {gov0})", flush=True)


def set_powersave_mode():
  """Big cores offline, ondemand governor (offroad simulation)."""
  adb("echo ondemand > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor")
  adb("for i in 4 5 6 7; do echo 0 > /sys/devices/system/cpu/cpu$i/online; done")


def start_stress():
  """Start max CPU+GPU burn."""
  adb("pkill -9 stress-ng 2>/dev/null")
  time.sleep(0.5)
  # --temp-path /tmp required: stress-ng needs a writable dir for temp files
  adb("cd /tmp && nohup stress-ng --temp-path /tmp --cpu 8 --cpu-method matrixprod "
      "--matrix 2 --timeout 0 > /dev/null 2>&1 &")
  time.sleep(1)
  # Verify stress is running
  count = adb("pgrep -c stress-ng 2>/dev/null || echo 0")
  print(f"  stress-ng started (full burn, {count} processes)", flush=True)


def stop_stress():
  """Stop all stress processes."""
  adb("pkill -9 stress-ng 2>/dev/null")


def stop_openpilot():
  """Stop openpilot services so we can control fan directly."""
  # Stop the systemd service that auto-restarts openpilot
  adb("sudo systemctl stop comma 2>/dev/null; tmux kill-session -t comma 2>/dev/null", timeout=15)
  time.sleep(2)
  adb("pkill -9 -f manager.py 2>/dev/null; pkill -9 -f pandad 2>/dev/null; "
      "pkill -9 -f modeld 2>/dev/null; pkill -9 -f camerad 2>/dev/null; "
      "pkill -9 -f hardwared 2>/dev/null; pkill -9 -f stress-ng 2>/dev/null")
  time.sleep(2)
  # Verify nothing restarted
  remaining = adb("pgrep -la 'manager.py|pandad|modeld' || echo 'clean'")
  if 'clean' not in remaining:
    print(f"  WARNING: processes still running: {remaining}", flush=True)
    adb("pkill -9 -f manager.py; pkill -9 -f pandad", timeout=5)
    time.sleep(2)
  print("  Openpilot stopped (comma.service + all processes)", flush=True)


# ── Fan control via internal panda (SPI) ──────────────────────────────────────

def set_fan_panda(pct):
  """Set fan speed via internal panda. Requires panda module on device."""
  adb(f"PYTHONPATH=/data/openpilot /usr/local/venv/bin/python3 -c \""
      f"from panda import Panda; p=Panda(Panda.list()[0]); p.set_fan_power({int(pct)})\"", timeout=8)


def get_fan_rpm():
  """Read fan RPM via internal panda."""
  out = adb("PYTHONPATH=/data/openpilot /usr/local/venv/bin/python3 -c \""
            "from panda import Panda; p=Panda(Panda.list()[0]); print(p.get_fan_rpm())\"", timeout=8)
  try:
    return int(out.strip())
  except ValueError:
    return -1


# ── Fan controllers (run on host, send commands to device) ────────────────────

def interp(x, xp, fp):
  if x <= xp[0]: return fp[0]
  if x >= xp[-1]: return fp[-1]
  for i in range(len(xp) - 1):
    if xp[i] <= x <= xp[i + 1]:
      t = (x - xp[i]) / (xp[i + 1] - xp[i])
      return fp[i] + t * (fp[i + 1] - fp[i])
  return fp[-1]


class StockFanCtrl:
  """Exact replica of stock openpilot fan controller."""
  NAME = "stock"
  def __init__(self):
    self.i = 0.0
    self.last_ign = False
  def update(self, temp, ign, power=0, intake=0):
    pos, neg = (100, 30) if ign else (30, 0)
    if ign != self.last_ign:
      self.i = 0.0
    self.last_ign = ign
    error = temp - 75.0
    ff = interp(temp, [60, 100], [0, 100])
    i = self.i + 4e-3 * 0.5 * error
    test = i + ff
    if test > pos: i = min(self.i, pos)
    elif test < neg: i = max(self.i, neg)
    self.i = i
    return int(max(neg, min(pos, i + ff)))


class NewFanCtrl:
  """Proposed fleet-fitted controller with T_eq feedforward."""
  NAME = "new"
  R_TH = 3.0
  def __init__(self):
    self.i = 0.0
    self.last_ign = False
    self._ff_filt = None
    self._offroad_t = 0.0
    self._last_out = None
  def update(self, temp, ign, power=0, intake=0):
    pos, neg = (100, 30) if ign else (30, 0)
    if not ign:
      self._offroad_t += 0.5
    if ign and not self.last_ign:
      if self._offroad_t > 5.0:
        ff_t = interp(temp, [60, 100], [0, 100])
        pre = interp(temp, [40, 55, 70], [30, 50, 80])
        self.i = max(0, pre - ff_t)
      self._offroad_t = 0.0
      self._last_out = None
    self.last_ign = ign
    ff_temp = interp(temp, [60, 100], [0, 100])
    if ign and power > 0.5 and intake > 0:
      T_eq = intake + power * self.R_TH
      ff_power = interp(T_eq, [60, 70, 75, 80, 90, 100], [0, 10, 30, 50, 80, 100])
    elif ign and power > 0.5:
      ff_power = interp(power, [2, 4, 6, 8], [0, 30, 60, 90])
    else:
      ff_power = 0.0
    ff_raw = max(ff_temp, ff_power)
    if self._ff_filt is None:
      self._ff_filt = ff_raw
    else:
      self._ff_filt += (0.5 / 30.5) * (ff_raw - self._ff_filt)
    error = temp - 75.0
    i = self.i + 4e-3 * 0.5 * error
    test = i + self._ff_filt
    if test > pos: i = min(self.i, pos)
    elif test < neg: i = max(self.i, neg)
    self.i = i
    out = int(max(neg, min(pos, i + self._ff_filt)))
    if self._last_out is not None:
      out = min(out, self._last_out + 3)
    self._last_out = out
    return out


# ── Test phases ───────────────────────────────────────────────────────────────

def run_cycle(ctrl, writer, csvfile, phase_name, cycle_num, load_min, cool_min, start_time):
  """Run one load→cooldown cycle, recording data at ~2Hz."""
  print(f"\n  --- {phase_name} cycle {cycle_num}: load {load_min}min → cool {cool_min}min ---", flush=True)

  # Cooldown first (offroad)
  if cycle_num > 0:
    print(f"    Cooldown ({cool_min}min)...", flush=True)
    set_powersave_mode()
    stop_stress()
    phase_start = time.monotonic()
    while time.monotonic() - phase_start < cool_min * 60:
      s = read_all_sensors()
      fan_pct = ctrl.update(s['comp_temp'], False, s['power_w'], s['intake'])
      set_fan_panda(fan_pct)
      rpm = get_fan_rpm()
      write_row(writer, csvfile, s, ctrl.NAME, phase_name, cycle_num, "cooldown",
                time.monotonic() - phase_start, fan_pct, rpm, start_time)
      time.sleep(2)  # ~0.5Hz to reduce ADB overhead

  # Load phase (onroad)
  print(f"    Load ({load_min}min)...", flush=True)
  set_performance_mode()
  start_stress()
  phase_start = time.monotonic()
  peak_temp = 0
  throttle_entered = False
  throttle_start = None

  while time.monotonic() - phase_start < load_min * 60:
    if not ensure_device_alive():
      print("    DEVICE DEAD, aborting cycle", flush=True)
      break

    s = read_all_sensors()
    fan_pct = ctrl.update(s['comp_temp'], True, s['power_w'], s['intake'])
    set_fan_panda(fan_pct)
    rpm = get_fan_rpm()
    write_row(writer, csvfile, s, ctrl.NAME, phase_name, cycle_num, "load",
              time.monotonic() - phase_start, fan_pct, rpm, start_time)

    if s['comp_temp'] > peak_temp:
      peak_temp = s['comp_temp']

    # LMH detection
    if s['lmh_throttled'] and not throttle_entered:
      throttle_entered = True
      throttle_start = time.monotonic() - phase_start
      print(f"    ⚠ LMH THROTTLE @ {s['comp_temp']:.1f}°C, t={throttle_start:.0f}s, "
            f"silver_limit={s['lmh_silver_limit']}, gold_limit={s['lmh_gold_limit']}", flush=True)

    elapsed = time.monotonic() - phase_start
    if int(elapsed) % 30 < 2.5:
      thr_str = " LMH!" if s['lmh_throttled'] else ""
      print(f"    t={elapsed:.0f}s: T={s['comp_temp']:.1f}°C fan={fan_pct}% "
            f"P={s['power_w']:.1f}W intake={s['intake']:.1f}°C{thr_str}", flush=True)

    time.sleep(2)

  stop_stress()
  set_powersave_mode()
  print(f"    Peak: {peak_temp:.1f}°C, LMH throttled: {throttle_entered}", flush=True)
  return {'peak': peak_temp, 'throttled': throttle_entered, 'throttle_at_s': throttle_start}


def write_row(writer, csvfile, sensors, ctrl_name, phase, cycle, subphase, phase_elapsed, fan_pct, rpm, start_time):
  writer.writerow({
    'timestamp': datetime.now(timezone.utc).isoformat(),
    'elapsed_s': round(time.monotonic() - start_time, 1),
    'controller': ctrl_name,
    'phase': phase,
    'cycle': cycle,
    'subphase': subphase,
    'phase_elapsed_s': round(phase_elapsed, 1),
    'comp_temp_C': round(sensors['comp_temp'], 2),
    'cpu_silver_C': round(sensors['cpu_silver_max'], 2),
    'cpu_gold_C': round(sensors['cpu_gold_max'], 2),
    'gpu_C': round(sensors['gpu_max'], 2),
    'ddr_C': round(sensors['ddr'], 2),
    'pmic_C': round(sensors['pm8998'], 2),
    'intake_C': round(sensors['intake'], 2),
    'lmh_00_C': round(sensors['lmh_dcvs_00'], 2),
    'power_W': round(sensors['power_w'], 3),
    'fan_pct': fan_pct,
    'fan_rpm': rpm,
    'silver_freq_khz': sensors['silver_freq_khz'],
    'gold_freq_khz': sensors['gold_freq_khz'],
    'lmh_silver_limit': sensors['lmh_silver_limit'],
    'lmh_gold_limit': sensors['lmh_gold_limit'],
    'lmh_throttled': int(sensors['lmh_throttled']),
  })
  csvfile.flush()


# ── Automatic iteration: analyze data and retune ──────────────────────────────

def analyze_and_retune(csv_path, ctrl):
  """Read the CSV data collected so far, fit R_th, and update the controller breakpoints.

  The thermal model is: T_comp = T_intake + P_som * R_th(fan%)
  At steady state, R_th = (T_comp - T_intake) / P_som

  We fit R_th from the data and update the T_eq feedforward breakpoints so that
  the controller outputs the right fan% to keep T_comp at the setpoint.
  """
  import csv as csv_mod
  rows = []
  with open(csv_path) as f:
    for row in csv_mod.DictReader(f):
      rows.append(row)

  if len(rows) < 50:
    print("  Not enough data for retuning yet", flush=True)
    return

  # Extract steady-state points: load subphase, >60s into cycle (past transient)
  ss_points = []
  for r in rows:
    if r['subphase'] != 'load':
      continue
    try:
      t_phase = float(r['phase_elapsed_s'])
      comp = float(r['comp_temp_C'])
      intake = float(r['intake_C'])
      power = float(r['power_W'])
      fan = int(r['fan_pct'])
    except (ValueError, KeyError):
      continue
    if t_phase < 60 or comp < 30 or intake <= 0 or power < 0.5:
      continue
    ss_points.append((comp, intake, power, fan))

  if len(ss_points) < 20:
    print(f"  Only {len(ss_points)} steady-state points, need 20+", flush=True)
    return

  # Fit R_th
  R_th_values = [(c - i) / p for c, i, p, f in ss_points if p > 0.5]
  R_th_median = sorted(R_th_values)[len(R_th_values) // 2]

  print(f"\n  === AUTO-RETUNE from {len(ss_points)} steady-state points ===", flush=True)
  print(f"  R_th (measured): median={R_th_median:.2f} K/W "
        f"(range {min(R_th_values):.2f}-{max(R_th_values):.2f})", flush=True)

  # Check: did we hit LMH throttle?
  lmh_count = sum(1 for r in rows if r.get('lmh_throttled') == '1')
  total_load = sum(1 for r in rows if r['subphase'] == 'load')
  lmh_pct = 100 * lmh_count / max(total_load, 1)
  print(f"  LMH throttle: {lmh_count}/{total_load} samples ({lmh_pct:.1f}%)", flush=True)

  # Peak temperatures per cycle
  peaks = {}
  for r in rows:
    if r['subphase'] != 'load':
      continue
    key = (r['controller'], r['cycle'])
    try:
      t = float(r['comp_temp_C'])
    except ValueError:
      continue
    peaks[key] = max(peaks.get(key, 0), t)

  print(f"  Peak temps by cycle:", flush=True)
  for (ctrl_name, cycle), peak in sorted(peaks.items()):
    print(f"    {ctrl_name} c{cycle}: {peak:.1f}°C", flush=True)

  # Update controller if it's the new one
  if hasattr(ctrl, 'R_TH'):
    old_rth = ctrl.R_TH
    ctrl.R_TH = round(R_th_median, 2)
    print(f"  Updated R_TH: {old_rth} → {ctrl.R_TH}", flush=True)

    # Recalculate T_eq breakpoint targets
    # At setpoint 75°C, what T_eq values correspond to what fan%?
    # T_eq = T_intake + P * R_th. Fan should be:
    #   0% when T_eq < 60 (plenty of headroom)
    #   30% when T_eq ≈ 70 (approaching setpoint)
    #   50% when T_eq ≈ 75 (at setpoint)
    #   80% when T_eq ≈ 85 (above setpoint, need aggressive cooling)
    #   100% when T_eq ≈ 95 (near LMH trigger)
    # These are fixed by physics, only R_TH changes the mapping from (P, T_amb) → T_eq
    print(f"  Fan curve (T_eq based): [60→0%, 70→10%, 75→30%, 80→50%, 90→80%, 100→100%]", flush=True)

  # Check for oscillation in the new controller data
  new_rows = [r for r in rows if r['controller'] == 'new' and r['subphase'] == 'load']
  if len(new_rows) > 10:
    fans = [int(r['fan_pct']) for r in new_rows]
    diffs = [abs(fans[i+1] - fans[i]) for i in range(len(fans)-1)]
    big_jumps = sum(1 for d in diffs if d > 5)
    print(f"  New controller oscillation: {big_jumps} jumps >5% out of {len(diffs)} samples", flush=True)
    if big_jumps > 10:
      print(f"  WARNING: Too many oscillations! Filter tau may need increasing.", flush=True)

  print(f"  === RETUNE COMPLETE ===\n", flush=True)
  return R_th_median


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
  parser = argparse.ArgumentParser(description="10h thermal bench test")
  parser.add_argument("--smoke", action="store_true", help="Quick 15-min smoke test")
  parser.add_argument("--phase", type=int, help="Run only this phase (1-4)")
  parser.add_argument("--load-min", type=float, default=10, help="Load phase minutes")
  parser.add_argument("--cool-min", type=float, default=5, help="Cooldown minutes")
  parser.add_argument("--cycles", type=int, default=10, help="Cycles per controller")
  args = parser.parse_args()

  if args.smoke:
    args.load_min = 3
    args.cool_min = 2
    args.cycles = 2

  ts = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
  csv_path = f"tools/thermal_data/bench_10h_{ts}.csv"
  summary_path = f"tools/thermal_data/bench_10h_{ts}_summary.json"
  os.makedirs("tools/thermal_data", exist_ok=True)

  print("=" * 70)
  print("10-HOUR THERMAL BENCH TEST")
  print("=" * 70)
  print(f"Mode: {'SMOKE' if args.smoke else 'FULL'}")
  print(f"Cycles per controller: {args.cycles}")
  print(f"Load: {args.load_min}min, Cool: {args.cool_min}min")
  print(f"Output: {csv_path}")
  print(flush=True)

  # Init
  if not ensure_device_alive():
    print("FATAL: Cannot reach device")
    sys.exit(1)

  stop_openpilot()
  build_zone_map()

  # Initial sensor read
  s = read_all_sensors()
  print(f"Initial: T={s['comp_temp']:.1f}°C, intake={s['intake']:.1f}°C, "
        f"LMH throttled={s['lmh_throttled']}", flush=True)

  # CSV
  headers = [
    'timestamp', 'elapsed_s', 'controller', 'phase', 'cycle', 'subphase',
    'phase_elapsed_s', 'comp_temp_C', 'cpu_silver_C', 'cpu_gold_C', 'gpu_C',
    'ddr_C', 'pmic_C', 'intake_C', 'lmh_00_C', 'power_W', 'fan_pct', 'fan_rpm',
    'silver_freq_khz', 'gold_freq_khz', 'lmh_silver_limit', 'lmh_gold_limit', 'lmh_throttled',
  ]
  csvfile = open(csv_path, 'w', newline='')
  writer = csv.DictWriter(csvfile, fieldnames=headers)
  writer.writeheader()
  csvfile.flush()

  start_time = time.monotonic()
  all_results = {}

  try:
    # Phase 1: Stock controller — full burn
    if args.phase is None or args.phase == 1:
      print(f"\n{'='*70}\n  PHASE 1: STOCK CONTROLLER — FULL POWER BURN\n{'='*70}", flush=True)
      ctrl = StockFanCtrl()
      results = []
      for c in range(args.cycles):
        r = run_cycle(ctrl, writer, csvfile, "stock_burn", c, args.load_min, args.cool_min, start_time)
        results.append(r)
      all_results['stock_burn'] = results

    # Phase 2: New controller — full burn
    if args.phase is None or args.phase == 2:
      print(f"\n{'='*70}\n  PHASE 2: NEW CONTROLLER — FULL POWER BURN\n{'='*70}", flush=True)
      ctrl = NewFanCtrl()
      results = []
      for c in range(args.cycles):
        r = run_cycle(ctrl, writer, csvfile, "new_burn", c, args.load_min, args.cool_min, start_time)
        results.append(r)
      all_results['new_burn'] = results

    # Phase 3: Realistic OP load (both controllers, alternating)
    if args.phase is None or args.phase == 3:
      print(f"\n{'='*70}\n  PHASE 3: ALTERNATING STOCK/NEW — REALISTIC LOAD\n{'='*70}", flush=True)
      for c in range(args.cycles):
        ctrl = StockFanCtrl() if c % 2 == 0 else NewFanCtrl()
        r = run_cycle(ctrl, writer, csvfile, f"alt_{'stock' if c%2==0 else 'new'}", c,
                      args.load_min, args.cool_min, start_time)

    # Phase 4: LMH hysteresis characterization
    if args.phase is None or args.phase == 4:
      print(f"\n{'='*70}\n  PHASE 4: LMH HYSTERESIS CHARACTERIZATION\n{'='*70}", flush=True)
      # Run to LMH trigger, then measure recovery time
      for ctrl_cls, name in [(StockFanCtrl, "stock_lmh"), (NewFanCtrl, "new_lmh")]:
        ctrl = ctrl_cls()
        print(f"\n  {name}: Burning to LMH trigger (95°C)...", flush=True)
        set_performance_mode()
        start_stress()
        phase_start = time.monotonic()
        lmh_triggered = False
        lmh_trigger_time = None
        lmh_release_time = None

        while time.monotonic() - phase_start < 20 * 60:  # max 20min
          s = read_all_sensors()
          fan_pct = ctrl.update(s['comp_temp'], True, s['power_w'], s['intake'])
          set_fan_panda(fan_pct)
          rpm = get_fan_rpm()
          write_row(writer, csvfile, s, ctrl_cls.NAME, name, 0, "burn",
                    time.monotonic() - phase_start, fan_pct, rpm, start_time)

          if s['lmh_throttled'] and not lmh_triggered:
            lmh_triggered = True
            lmh_trigger_time = time.monotonic() - phase_start
            print(f"    LMH TRIGGERED at T={s['comp_temp']:.1f}°C, t={lmh_trigger_time:.0f}s", flush=True)
            # Stop load, let it cool to measure hysteresis
            stop_stress()
            set_powersave_mode()

          if lmh_triggered and not s['lmh_throttled'] and lmh_release_time is None:
            lmh_release_time = time.monotonic() - phase_start
            recovery_time = lmh_release_time - lmh_trigger_time
            print(f"    LMH RELEASED at T={s['comp_temp']:.1f}°C, t={lmh_release_time:.0f}s "
                  f"(recovery={recovery_time:.0f}s)", flush=True)
            break

          elapsed = time.monotonic() - phase_start
          if int(elapsed) % 30 < 2.5:
            print(f"    t={elapsed:.0f}s: T={s['comp_temp']:.1f}°C fan={fan_pct}% LMH={s['lmh_throttled']}", flush=True)

          time.sleep(2)

        stop_stress()
        set_powersave_mode()

  except KeyboardInterrupt:
    print("\nInterrupted!", flush=True)
  finally:
    stop_stress()
    set_powersave_mode()
    try:
      set_fan_panda(0)
    except Exception:
      pass
    csvfile.close()

  # Summary
  elapsed_h = (time.monotonic() - start_time) / 3600
  summary = {
    'duration_hours': round(elapsed_h, 2),
    'csv_path': csv_path,
    'smoke': args.smoke,
    'results': {k: [str(r) for r in v] for k, v in all_results.items()},
  }
  with open(summary_path, 'w') as f:
    json.dump(summary, f, indent=2)

  print(f"\n{'='*70}")
  print(f"BENCH TEST COMPLETE — {elapsed_h:.1f}h")
  print(f"Data: {csv_path}")
  print(f"Summary: {summary_path}")
  print(f"{'='*70}", flush=True)


if __name__ == "__main__":
  main()
