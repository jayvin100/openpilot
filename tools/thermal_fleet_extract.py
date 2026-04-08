#!/usr/bin/env python3
"""
Fleet thermal data extraction for mici fan curve fitting.

Pulls deviceState + peripheralState from fleet qlogs, detects offroad→onroad
transitions, and extracts thermal characterization windows around each transition.

Output:
  - Pickle file with per-transition time-series and summary statistics
  - Optional analysis plots (--plot flag)

Usage:
  # From route list (one per line, or comma-separated)
  python3 tools/thermal_fleet_extract.py --routes "dongle|2024-01-01--12-00-00" --output thermal_data.pkl

  # From dongle ID (fetches recent routes via API)
  python3 tools/thermal_fleet_extract.py --dongle-id abc123def4567890 --days 30 --output thermal_data.pkl

  # From local data directory
  python3 tools/thermal_fleet_extract.py --data-dir /data/media/0/realdata --output thermal_data.pkl

  # Generate analysis plots from saved data
  python3 tools/thermal_fleet_extract.py --load thermal_data.pkl --plot
"""

import argparse
import pickle
import sys
import time
import traceback
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

# Add openpilot root to path
OPENPILOT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(OPENPILOT_ROOT))

from openpilot.tools.lib.logreader import LogReader, ReadMode


# ── Configuration ──────────────────────────────────────────────────────────────

# Window around each offroad→onroad transition
PRE_TRANSITION_S = 120    # 2 minutes before ignition
POST_TRANSITION_S = 600   # 10 minutes after ignition

# Minimum data requirements
MIN_PRE_SAMPLES = 10      # at least 5s of pre-transition data
MIN_POST_SAMPLES = 120    # at least 60s of post-transition data

# Steady-state window for averaging (seconds after transition)
STEADY_START_S = 300      # 5 minutes
STEADY_END_S = 600        # 10 minutes

# Baseline window for averaging (seconds before transition)
BASELINE_START_S = -60    # 1 minute before
BASELINE_END_S = 0        # up to transition


# ── Data structures ────────────────────────────────────────────────────────────

@dataclass
class ThermalSample:
  """Single 2Hz thermal sample from deviceState + peripheralState."""
  t: float                     # monotonic time (seconds, relative to transition)
  max_temp_c: float            # filtered max temperature
  cpu_temps_c: list[float]     # per-core CPU temperatures
  gpu_temps_c: list[float]     # GPU temperatures
  memory_temp_c: float
  pmic_temps_c: list[float]
  intake_temp_c: float         # mici ambient proxy
  exhaust_temp_c: float
  dsp_temp_c: float
  bottom_soc_temp_c: float
  power_draw_w: float          # total system power
  som_power_draw_w: float      # SoM power
  fan_pct_desired: float       # fan speed % desired by controller
  fan_rpm: float               # actual fan RPM (from peripheralState)
  cpu_usage_pct: list[int]     # per-core CPU usage
  gpu_usage_pct: int
  started: bool                # onroad flag
  thermal_status: int          # 0=green, 1=yellow, 2=red, 3=danger


@dataclass
class TransitionRecord:
  """One offroad→onroad transition with surrounding thermal data."""
  route_id: str
  transition_mono_time: float   # absolute monotonic time of transition

  # Summary statistics
  t_initial: float = 0.0       # avg max temp in baseline window
  t_ambient: float = 0.0       # intake temp at transition (ambient proxy)
  t_peak: float = 0.0          # peak temperature after transition
  t_to_peak_s: float = 0.0     # seconds from transition to peak
  t_steady: float = 0.0        # avg temp in steady-state window
  overshoot: float = 0.0       # t_peak - t_steady
  p_offroad: float = 0.0       # avg power before transition
  p_onroad_avg: float = 0.0    # avg power 60-300s after transition
  fan_pct_at_peak: float = 0.0
  fan_pct_steady: float = 0.0

  # Full time-series (relative to transition t=0)
  samples: list[ThermalSample] = field(default_factory=list)

  def compute_summary(self):
    """Compute summary statistics from samples."""
    if not self.samples:
      return

    ts = np.array([s.t for s in self.samples])
    temps = np.array([s.max_temp_c for s in self.samples])
    powers = np.array([s.som_power_draw_w if s.som_power_draw_w > 0 else s.power_draw_w for s in self.samples])
    fan_pcts = np.array([s.fan_pct_desired for s in self.samples])
    intakes = np.array([s.intake_temp_c for s in self.samples])

    # Baseline (pre-transition)
    baseline_mask = (ts >= BASELINE_START_S) & (ts < BASELINE_END_S)
    if np.any(baseline_mask):
      self.t_initial = float(np.mean(temps[baseline_mask]))
      self.p_offroad = float(np.mean(powers[baseline_mask]))
      valid_intakes = intakes[baseline_mask]
      valid_intakes = valid_intakes[valid_intakes > 0]
      self.t_ambient = float(np.mean(valid_intakes)) if len(valid_intakes) > 0 else 0.0

    # Post-transition peak
    post_mask = ts >= 0
    if np.any(post_mask):
      post_temps = temps[post_mask]
      post_ts = ts[post_mask]
      post_fans = fan_pcts[post_mask]

      peak_idx = np.argmax(post_temps)
      self.t_peak = float(post_temps[peak_idx])
      self.t_to_peak_s = float(post_ts[peak_idx])
      self.fan_pct_at_peak = float(post_fans[peak_idx])

    # Steady-state
    steady_mask = (ts >= STEADY_START_S) & (ts <= STEADY_END_S)
    if np.any(steady_mask):
      self.t_steady = float(np.mean(temps[steady_mask]))
      self.fan_pct_steady = float(np.mean(fan_pcts[steady_mask]))

    # Onroad power (60-300s to skip startup transient)
    onroad_power_mask = (ts >= 60) & (ts <= 300)
    if np.any(onroad_power_mask):
      self.p_onroad_avg = float(np.mean(powers[onroad_power_mask]))

    self.overshoot = self.t_peak - self.t_steady if self.t_steady > 0 else 0.0


# ── Extraction ─────────────────────────────────────────────────────────────────

def extract_from_route(route_id: str, data_dir: str | None = None) -> list[TransitionRecord]:
  """Extract thermal transitions from a single route's qlogs."""
  print(f"  Processing {route_id}...")

  try:
    if data_dir:
      lr = LogReader(f"{route_id}/q", default_mode=ReadMode.QLOG,
                     sources=[], sort_by_time=True)
      # For local, construct with data_dir
      from openpilot.tools.lib.route import Route
      route = Route(route_id, data_dir=data_dir)
      qlog_paths = [p for p in route.qlog_paths() if p is not None]
      if not qlog_paths:
        print(f"    No qlogs found locally for {route_id}")
        return []
      lr = LogReader(qlog_paths, default_mode=ReadMode.QLOG, sort_by_time=True)
    else:
      lr = LogReader(f"{route_id}/q", default_mode=ReadMode.QLOG, sort_by_time=True)
  except Exception as e:
    print(f"    Failed to open qlogs: {e}")
    return []

  # Collect all deviceState and peripheralState messages
  device_states = []   # (mono_time_s, msg_dict)
  periph_states = {}   # mono_time_s -> fan_rpm

  try:
    for msg in lr:
      which = msg.which()
      if which == 'deviceState':
        ds = msg.deviceState
        mono_s = msg.logMonoTime / 1e9
        device_states.append((mono_s, {
          'maxTempC': float(ds.maxTempC),
          'cpuTempC': list(ds.cpuTempC),
          'gpuTempC': list(ds.gpuTempC),
          'memoryTempC': float(ds.memoryTempC),
          'pmicTempC': list(ds.pmicTempC),
          'intakeTempC': float(getattr(ds, 'intakeTempC', 0.0)),
          'exhaustTempC': float(getattr(ds, 'exhaustTempC', 0.0)),
          'dspTempC': float(getattr(ds, 'dspTempC', 0.0)),
          'bottomSocTempC': float(getattr(ds, 'bottomSocTempC', 0.0)),
          'powerDrawW': float(ds.powerDrawW),
          'somPowerDrawW': float(ds.somPowerDrawW),
          'fanSpeedPercentDesired': int(ds.fanSpeedPercentDesired),
          'cpuUsagePercent': list(ds.cpuUsagePercent),
          'gpuUsagePercent': int(ds.gpuUsagePercent),
          'started': bool(ds.started),
          'thermalStatus': ds.thermalStatus.raw,
        }))
      elif which == 'peripheralState':
        ps = msg.peripheralState
        mono_s = msg.logMonoTime / 1e9
        periph_states[round(mono_s, 1)] = int(ps.fanSpeedRpm)
  except Exception as e:
    print(f"    Error reading logs: {e}")
    if not device_states:
      return []

  if len(device_states) < MIN_PRE_SAMPLES + MIN_POST_SAMPLES:
    print(f"    Too few samples ({len(device_states)})")
    return []

  # Detect offroad→onroad transitions
  transitions = []
  for i in range(1, len(device_states)):
    prev_started = device_states[i - 1][1]['started']
    curr_started = device_states[i][1]['started']
    if not prev_started and curr_started:
      transitions.append(device_states[i][0])  # mono time of transition

  # If no explicit transition found, treat start of route as the transition.
  # Routes are created when 'started' becomes True, so seg 0 IS the onroad entry.
  # The thermal ramp (initial temp → peak) is the transient we want to capture.
  if not transitions and device_states and device_states[0][1]['started']:
    transitions.append(device_states[0][0])
    print(f"    Using route start as transition (started=True from seg 0)")
  elif transitions:
    print(f"    Found {len(transitions)} explicit transition(s)")
  else:
    print(f"    No transitions found and route not started")
    return []

  # Extract windows around each transition
  records = []
  all_times = np.array([ds[0] for ds in device_states])

  for trans_time in transitions:
    window_start = trans_time - PRE_TRANSITION_S
    window_end = trans_time + POST_TRANSITION_S

    # Find samples in window
    mask = (all_times >= window_start) & (all_times <= window_end)
    indices = np.where(mask)[0]

    if len(indices) < MIN_PRE_SAMPLES + MIN_POST_SAMPLES:
      print(f"    Skipping transition at {trans_time:.0f}: too few samples in window ({len(indices)})")
      continue

    record = TransitionRecord(
      route_id=route_id,
      transition_mono_time=trans_time,
    )

    for idx in indices:
      mono_s, ds = device_states[idx]
      rel_t = mono_s - trans_time  # relative to transition

      # Find closest peripheralState for fan RPM
      fan_rpm = periph_states.get(round(mono_s, 1), 0)

      record.samples.append(ThermalSample(
        t=rel_t,
        max_temp_c=ds['maxTempC'],
        cpu_temps_c=ds['cpuTempC'],
        gpu_temps_c=ds['gpuTempC'],
        memory_temp_c=ds['memoryTempC'],
        pmic_temps_c=ds['pmicTempC'],
        intake_temp_c=ds['intakeTempC'],
        exhaust_temp_c=ds['exhaustTempC'],
        dsp_temp_c=ds['dspTempC'],
        bottom_soc_temp_c=ds['bottomSocTempC'],
        power_draw_w=ds['powerDrawW'],
        som_power_draw_w=ds['somPowerDrawW'],
        fan_pct_desired=ds['fanSpeedPercentDesired'],
        fan_rpm=fan_rpm,
        cpu_usage_pct=ds['cpuUsagePercent'],
        gpu_usage_pct=ds['gpuUsagePercent'],
        started=ds['started'],
        thermal_status=ds['thermalStatus'],
      ))

    record.compute_summary()
    records.append(record)
    print(f"    Transition: T_init={record.t_initial:.1f}°C → T_peak={record.t_peak:.1f}°C "
          f"(+{record.overshoot:.1f}°C overshoot) in {record.t_to_peak_s:.0f}s, "
          f"T_steady={record.t_steady:.1f}°C, P_onroad={record.p_onroad_avg:.1f}W")

  return records


def get_routes_for_dongle(dongle_id: str, days: int = 30) -> list[str]:
  """Fetch recent route IDs for a dongle via Comma API."""
  from openpilot.tools.lib.auth_config import get_token
  from openpilot.tools.lib.api import CommaApi

  api = CommaApi(get_token())
  routes = api.get(f'v1/devices/{dongle_id}/routes_segments',
                   params={'limit': 500, 'length': days * 86400})

  route_ids = []
  for r in routes:
    route_name = r.get('fullname') or r.get('route', '')
    if route_name and route_name not in route_ids:
      route_ids.append(route_name)

  return route_ids


# ── Analysis plots ─────────────────────────────────────────────────────────────

def plot_analysis(records: list[TransitionRecord], output_dir: str = "."):
  """Generate thermal analysis plots from extracted transition data."""
  try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
  except ImportError:
    print("matplotlib not available, skipping plots")
    return

  output_path = Path(output_dir)

  # --- Plot 1: Temperature trajectories overlay ---
  fig, axes = plt.subplots(3, 1, figsize=(14, 12), sharex=True)

  ax = axes[0]
  ax.set_title(f'Onroad Transition Temperature Trajectories (n={len(records)})')
  for rec in records:
    ts = [s.t for s in rec.samples]
    temps = [s.max_temp_c for s in rec.samples]
    ax.plot(ts, temps, alpha=0.3, linewidth=0.8)
  ax.axvline(0, color='red', linestyle='--', alpha=0.7, label='Ignition ON')
  ax.axhline(75, color='orange', linestyle=':', alpha=0.5, label='Setpoint 75°C')
  ax.axhline(80, color='red', linestyle=':', alpha=0.5, label='Yellow band 80°C')
  ax.set_ylabel('Max Temperature (°C)')
  ax.legend(loc='upper left')
  ax.grid(True, alpha=0.3)

  ax = axes[1]
  ax.set_title('Power Draw')
  for rec in records:
    ts = [s.t for s in rec.samples]
    powers = [s.som_power_draw_w for s in rec.samples]
    ax.plot(ts, powers, alpha=0.3, linewidth=0.8)
  ax.axvline(0, color='red', linestyle='--', alpha=0.7)
  ax.set_ylabel('SoM Power (W)')
  ax.grid(True, alpha=0.3)

  ax = axes[2]
  ax.set_title('Fan Speed (% Desired)')
  for rec in records:
    ts = [s.t for s in rec.samples]
    fans = [s.fan_pct_desired for s in rec.samples]
    ax.plot(ts, fans, alpha=0.3, linewidth=0.8)
  ax.axvline(0, color='red', linestyle='--', alpha=0.7)
  ax.set_ylabel('Fan %')
  ax.set_xlabel('Time relative to ignition ON (s)')
  ax.grid(True, alpha=0.3)

  plt.tight_layout()
  fig.savefig(output_path / 'thermal_transitions_overlay.png', dpi=150)
  plt.close(fig)
  print(f"  Saved thermal_transitions_overlay.png")

  # --- Plot 2: Scatter analysis ---
  fig, axes = plt.subplots(2, 2, figsize=(12, 10))

  # T_initial vs T_peak
  ax = axes[0, 0]
  t_inits = [r.t_initial for r in records]
  t_peaks = [r.t_peak for r in records]
  ax.scatter(t_inits, t_peaks, alpha=0.6, s=20)
  ax.plot([30, 80], [30, 80], 'k--', alpha=0.3, label='y=x')
  ax.set_xlabel('T_initial (°C)')
  ax.set_ylabel('T_peak (°C)')
  ax.set_title('Initial vs Peak Temperature')
  ax.legend()
  ax.grid(True, alpha=0.3)

  # Overshoot histogram
  ax = axes[0, 1]
  overshoots = [r.overshoot for r in records if r.overshoot > 0]
  if overshoots:
    ax.hist(overshoots, bins=20, alpha=0.7, edgecolor='black')
  ax.set_xlabel('Overshoot (T_peak - T_steady) (°C)')
  ax.set_ylabel('Count')
  ax.set_title('Temperature Overshoot Distribution')
  ax.grid(True, alpha=0.3)

  # T_ambient vs T_steady
  ax = axes[1, 0]
  t_ambs = [r.t_ambient for r in records if r.t_ambient > 0]
  t_steadys = [r.t_steady for r in records if r.t_ambient > 0]
  if t_ambs:
    ax.scatter(t_ambs, t_steadys, alpha=0.6, s=20)
    ax.set_xlabel('T_ambient / Intake (°C)')
    ax.set_ylabel('T_steady (°C)')
    ax.set_title('Ambient vs Steady-State Temperature')
  ax.grid(True, alpha=0.3)

  # P_onroad vs T_steady colored by fan%
  ax = axes[1, 1]
  p_onroads = [r.p_onroad_avg for r in records if r.p_onroad_avg > 0]
  t_steadys2 = [r.t_steady for r in records if r.p_onroad_avg > 0]
  fan_steadys = [r.fan_pct_steady for r in records if r.p_onroad_avg > 0]
  if p_onroads:
    sc = ax.scatter(p_onroads, t_steadys2, c=fan_steadys, cmap='coolwarm', alpha=0.6, s=20)
    plt.colorbar(sc, ax=ax, label='Steady Fan %')
    ax.set_xlabel('P_onroad (W)')
    ax.set_ylabel('T_steady (°C)')
    ax.set_title('Power vs Steady Temperature (color=fan%)')
  ax.grid(True, alpha=0.3)

  plt.tight_layout()
  fig.savefig(output_path / 'thermal_scatter_analysis.png', dpi=150)
  plt.close(fig)
  print(f"  Saved thermal_scatter_analysis.png")

  # --- Plot 3: Median trajectory with percentile bands ---
  fig, axes = plt.subplots(3, 1, figsize=(14, 12), sharex=True)

  # Resample all transitions onto common time grid
  t_grid = np.arange(-PRE_TRANSITION_S, POST_TRANSITION_S, 0.5)
  temp_matrix = []
  power_matrix = []
  fan_matrix = []

  for rec in records:
    ts = np.array([s.t for s in rec.samples])
    temps = np.array([s.max_temp_c for s in rec.samples])
    powers = np.array([s.som_power_draw_w for s in rec.samples])
    fans = np.array([s.fan_pct_desired for s in rec.samples])

    if len(ts) < 10:
      continue

    temp_interp = np.interp(t_grid, ts, temps, left=np.nan, right=np.nan)
    power_interp = np.interp(t_grid, ts, powers, left=np.nan, right=np.nan)
    fan_interp = np.interp(t_grid, ts, fans, left=np.nan, right=np.nan)

    temp_matrix.append(temp_interp)
    power_matrix.append(power_interp)
    fan_matrix.append(fan_interp)

  if temp_matrix:
    temp_matrix = np.array(temp_matrix)
    power_matrix = np.array(power_matrix)
    fan_matrix = np.array(fan_matrix)

    for i, (data, label, ylabel) in enumerate([
      (temp_matrix, 'Max Temperature', '°C'),
      (power_matrix, 'SoM Power', 'W'),
      (fan_matrix, 'Fan Desired', '%'),
    ]):
      ax = axes[i]
      median = np.nanmedian(data, axis=0)
      p10 = np.nanpercentile(data, 10, axis=0)
      p90 = np.nanpercentile(data, 90, axis=0)
      p25 = np.nanpercentile(data, 25, axis=0)
      p75 = np.nanpercentile(data, 75, axis=0)

      ax.fill_between(t_grid, p10, p90, alpha=0.15, color='blue', label='10-90th pct')
      ax.fill_between(t_grid, p25, p75, alpha=0.25, color='blue', label='25-75th pct')
      ax.plot(t_grid, median, 'b-', linewidth=2, label='Median')
      ax.axvline(0, color='red', linestyle='--', alpha=0.7, label='Ignition ON')
      if i == 0:
        ax.axhline(75, color='orange', linestyle=':', alpha=0.5, label='Setpoint')
      ax.set_ylabel(f'{label} ({ylabel})')
      ax.legend(loc='upper left', fontsize=8)
      ax.grid(True, alpha=0.3)
      ax.set_title(f'{label} — Median with Percentile Bands (n={len(temp_matrix)})')

    axes[-1].set_xlabel('Time relative to ignition ON (s)')

  plt.tight_layout()
  fig.savefig(output_path / 'thermal_median_trajectory.png', dpi=150)
  plt.close(fig)
  print(f"  Saved thermal_median_trajectory.png")

  # --- Plot 4: Fan lag visualization ---
  fig, ax1 = plt.subplots(figsize=(14, 6))

  if temp_matrix is not None and len(temp_matrix) > 0:
    # Show median temperature rate of change vs fan response
    median_temp = np.nanmedian(temp_matrix, axis=0)
    median_fan = np.nanmedian(fan_matrix, axis=0)
    median_power = np.nanmedian(power_matrix, axis=0)

    # dT/dt (smoothed)
    dt_dt = np.gradient(median_temp, 0.5)  # °C/s
    # Smooth with 10s window
    kernel = np.ones(20) / 20
    dt_dt_smooth = np.convolve(dt_dt, kernel, mode='same')

    ax1.set_title('Fan Response Lag — Temperature Rate of Change vs Fan Speed')
    color1 = 'tab:red'
    ax1.plot(t_grid, dt_dt_smooth, color=color1, linewidth=2, label='dT/dt (°C/s)')
    ax1.set_ylabel('dT/dt (°C/s)', color=color1)
    ax1.tick_params(axis='y', labelcolor=color1)
    ax1.axhline(0, color='gray', linestyle='-', alpha=0.3)
    ax1.axvline(0, color='red', linestyle='--', alpha=0.7, label='Ignition ON')

    ax2 = ax1.twinx()
    color2 = 'tab:blue'
    ax2.plot(t_grid, median_fan, color=color2, linewidth=2, label='Fan %')
    ax2.set_ylabel('Fan Desired (%)', color=color2)
    ax2.tick_params(axis='y', labelcolor=color2)

    ax3 = ax1.twinx()
    ax3.spines['right'].set_position(('outward', 60))
    color3 = 'tab:green'
    ax3.plot(t_grid, median_power, color=color3, linewidth=1.5, alpha=0.7, label='Power (W)')
    ax3.set_ylabel('SoM Power (W)', color=color3)
    ax3.tick_params(axis='y', labelcolor=color3)

    # Combined legend
    lines1 = ax1.get_lines()
    lines2 = ax2.get_lines()
    lines3 = ax3.get_lines()
    ax1.legend(lines1 + lines2 + lines3,
               [l.get_label() for l in lines1 + lines2 + lines3],
               loc='upper right', fontsize=9)

    ax1.set_xlabel('Time relative to ignition ON (s)')
    ax1.grid(True, alpha=0.3)

  plt.tight_layout()
  fig.savefig(output_path / 'thermal_fan_lag.png', dpi=150)
  plt.close(fig)
  print(f"  Saved thermal_fan_lag.png")

  # --- Print summary statistics ---
  print("\n" + "=" * 70)
  print(f"FLEET THERMAL ANALYSIS SUMMARY ({len(records)} transitions)")
  print("=" * 70)

  if records:
    t_inits = np.array([r.t_initial for r in records])
    t_peaks = np.array([r.t_peak for r in records])
    t_steadys = np.array([r.t_steady for r in records if r.t_steady > 0])
    overshoots = np.array([r.overshoot for r in records if r.overshoot > 0])
    t_to_peaks = np.array([r.t_to_peak_s for r in records])
    p_onroads = np.array([r.p_onroad_avg for r in records if r.p_onroad_avg > 0])
    fan_steadys = np.array([r.fan_pct_steady for r in records if r.fan_pct_steady > 0])

    def stat_line(name, arr):
      if len(arr) == 0:
        return f"  {name:30s}: no data"
      return f"  {name:30s}: median={np.median(arr):6.1f}  mean={np.mean(arr):6.1f}  " \
             f"std={np.std(arr):5.1f}  [p10={np.percentile(arr, 10):5.1f}, p90={np.percentile(arr, 90):5.1f}]"

    print(stat_line("T_initial (°C)", t_inits))
    print(stat_line("T_peak (°C)", t_peaks))
    print(stat_line("T_steady (°C)", t_steadys))
    print(stat_line("Overshoot (°C)", overshoots))
    print(stat_line("Time to peak (s)", t_to_peaks))
    print(stat_line("P_onroad (W)", p_onroads))
    print(stat_line("Fan steady (%)", fan_steadys))

    # Thermal model hints
    if len(t_steadys) > 0 and len(p_onroads) > 0:
      t_ambs = np.array([r.t_ambient for r in records if r.t_ambient > 0 and r.t_steady > 0])
      if len(t_ambs) > 5:
        # Rough R_total estimate: (T_steady - T_amb) / P_onroad
        paired = [(r.t_steady, r.t_ambient, r.p_onroad_avg)
                  for r in records if r.t_ambient > 0 and r.t_steady > 0 and r.p_onroad_avg > 0]
        if paired:
          r_totals = [(ts - ta) / p for ts, ta, p in paired if p > 0.5]
          if r_totals:
            print(f"\n  Estimated R_total (°C/W):      median={np.median(r_totals):5.2f}  "
                  f"mean={np.mean(r_totals):5.2f}  std={np.std(r_totals):5.2f}")

    print()


# ── Main ───────────────────────────────────────────────────────────────────────

def main():
  parser = argparse.ArgumentParser(description='Extract thermal data from fleet qlogs')
  parser.add_argument('--routes', type=str, help='Comma-separated route IDs or file with one per line')
  parser.add_argument('--dongle-id', type=str, help='Dongle ID to fetch routes for')
  parser.add_argument('--days', type=int, default=30, help='Number of days of routes to fetch (default: 30)')
  parser.add_argument('--data-dir', type=str, help='Local data directory for offline processing')
  parser.add_argument('--output', type=str, default='thermal_fleet_data.pkl', help='Output pickle file')
  parser.add_argument('--load', type=str, help='Load previously extracted data instead of re-extracting')
  parser.add_argument('--plot', action='store_true', help='Generate analysis plots')
  parser.add_argument('--plot-dir', type=str, default='.', help='Directory for plot output')
  parser.add_argument('--max-routes', type=int, default=100, help='Max routes to process')
  args = parser.parse_args()

  # Load existing data
  if args.load:
    print(f"Loading data from {args.load}...")
    with open(args.load, 'rb') as f:
      records = pickle.load(f)
    print(f"Loaded {len(records)} transition records")
    if args.plot:
      plot_analysis(records, args.plot_dir)
    return

  # Determine route list
  route_ids = []

  if args.routes:
    # Could be comma-separated or a file path
    if Path(args.routes).exists():
      with open(args.routes) as f:
        route_ids = [line.strip() for line in f if line.strip() and not line.startswith('#')]
    else:
      route_ids = [r.strip() for r in args.routes.split(',')]
  elif args.dongle_id:
    print(f"Fetching routes for dongle {args.dongle_id} (last {args.days} days)...")
    route_ids = get_routes_for_dongle(args.dongle_id, args.days)
    print(f"Found {len(route_ids)} routes")
  elif args.data_dir:
    # List routes in local data directory
    data_path = Path(args.data_dir)
    route_dirs = sorted(data_path.iterdir())
    for d in route_dirs:
      if d.is_dir() and '|' in d.name or '--' in d.name:
        route_ids.append(d.name.replace('_', '|'))
    print(f"Found {len(route_ids)} local routes")
  else:
    parser.error("Must specify --routes, --dongle-id, or --data-dir")

  route_ids = route_ids[:args.max_routes]
  print(f"Processing {len(route_ids)} routes...")

  # Extract transitions from each route
  all_records = []
  for i, route_id in enumerate(route_ids):
    print(f"\n[{i + 1}/{len(route_ids)}] {route_id}")
    try:
      records = extract_from_route(route_id, data_dir=args.data_dir)
      all_records.extend(records)
    except Exception as e:
      print(f"  ERROR: {e}")
      traceback.print_exc()
      continue

  print(f"\n{'=' * 70}")
  print(f"Extracted {len(all_records)} total transitions from {len(route_ids)} routes")

  # Save
  if all_records:
    with open(args.output, 'wb') as f:
      pickle.dump(all_records, f)
    print(f"Saved to {args.output}")

    if args.plot:
      plot_analysis(all_records, args.plot_dir)
  else:
    print("No transitions extracted")


if __name__ == '__main__':
  main()
