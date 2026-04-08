#!/usr/bin/env python3
"""
Thermal fan control simulation: old vs new controller comparison.

Replays fleet thermal data through both the old (reactive) and new (model-based)
fan controllers. Uses the fitted thermal model to predict what temperature
*would have been* with the new controller's fan commands.

Usage:
  python3 tools/thermal_fan_sim.py --data thermal_fleet_data.pkl --model thermal_model.json
  python3 tools/thermal_fan_sim.py --data thermal_fleet_data.pkl --model thermal_model.json --plot-dir plots/
"""

import argparse
import json
import pickle
import sys
from pathlib import Path

import numpy as np

OPENPILOT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(OPENPILOT_ROOT))


# ── Old controller (replicates original fan_controller.py) ─────────────────────

class OldFanController:
  """Original fan controller: pure integral + temp feedforward, hard reset on ignition."""
  def __init__(self, rate: int = 2):
    self.last_ignition = False
    # PID state
    self.i = 0.0
    self.k_i = 4e-3
    self.i_dt = 1.0 / rate
    self.control = 0

  def update(self, cur_temp: float, ignition: bool, power_draw_w: float = 0.0) -> int:
    pos_limit = 100 if ignition else 30
    neg_limit = 30 if ignition else 0

    if ignition != self.last_ignition:
      self.i = 0.0
      self.control = 0
    self.last_ignition = ignition

    error = cur_temp - 75.0
    feedforward = float(np.interp(cur_temp, [60.0, 100.0], [0, 100]))

    # Integral with anti-windup
    i = self.i + self.k_i * self.i_dt * error
    test_control = i + feedforward
    if test_control > pos_limit:
      i = min(self.i, pos_limit)
    elif test_control < neg_limit:
      i = max(self.i, neg_limit)
    self.i = i

    control = i + feedforward
    self.control = int(np.clip(control, neg_limit, pos_limit))
    return self.control


class NewFanController:
  """New fan controller: P+I, power feedforward, prespin, no hard reset."""
  def __init__(self, rate: int = 2, power_bp=None, power_ff=None,
               prespin_temp_bp=None, prespin_fan_bp=None):
    self.last_ignition = False
    self.k_p = 2.0
    self.k_i = 4e-3
    self.i_dt = 1.0 / rate
    self.i = 0.0
    self.control = 0
    self.power_bp = power_bp or [2.0, 4.0, 6.0, 8.0]
    self.power_ff = power_ff or [0, 30, 60, 90]
    self.prespin_temp_bp = prespin_temp_bp or [40.0, 55.0, 70.0]
    self.prespin_fan_bp = prespin_fan_bp or [30.0, 50.0, 80.0]

  def update(self, cur_temp: float, ignition: bool, power_draw_w: float = 0.0, t_amb: float = 0.0) -> int:
    pos_limit = 100 if ignition else 30
    neg_limit = 30 if ignition else 0

    if ignition and not self.last_ignition:
      # Pre-spin: seed integrator
      ff_at_temp = float(np.interp(cur_temp, [60.0, 100.0], [0, 100]))
      prespin = float(np.interp(cur_temp, self.prespin_temp_bp, self.prespin_fan_bp))
      self.i = max(0, prespin - ff_at_temp)
    # No reset on offroad transition

    self.last_ignition = ignition

    error = cur_temp - 75.0

    # Proportional
    p = self.k_p * error

    # Integral with anti-windup
    ff_temp = float(np.interp(cur_temp, [60.0, 100.0], [0, 100]))
    ff_power = float(np.interp(power_draw_w, self.power_bp, self.power_ff)) if ignition else 0
    if t_amb > 0:
      ff_power *= (75.0 - 35.0) / max(75.0 - t_amb, 5.0)
    feedforward = max(ff_temp, ff_power)

    i = self.i + self.k_i * self.i_dt * error
    test_control = p + i + feedforward
    if test_control > pos_limit:
      i = min(self.i, pos_limit)
    elif test_control < neg_limit:
      i = max(self.i, neg_limit)
    self.i = i

    control = p + i + feedforward
    self.control = int(np.clip(control, neg_limit, pos_limit))
    return self.control


# ── Thermal model simulation ──────────────────────────────────────────────────

def simulate_temperature(T_init, T_amb, power_series, fan_series, model_params, dt=0.5):
  """Predict temperature given power and fan commands using thermal model."""
  tau_heat = model_params['tau_heat']
  tau_cool = model_params['tau_cool']
  R_passive = model_params['R_passive']
  k_fan = model_params['k_fan']

  n = len(power_series)
  T = np.zeros(n)
  T[0] = T_init

  for i in range(1, n):
    R_total = R_passive / (1.0 + k_fan * fan_series[i - 1] / 100.0)
    T_eq = T_amb + power_series[i - 1] * R_total
    tau = tau_heat if T_eq > T[i - 1] else tau_cool
    tau = max(tau, 1.0)
    T[i] = T[i - 1] + (dt / tau) * (T_eq - T[i - 1])

  return T


# ── Simulation ────────────────────────────────────────────────────────────────

def simulate_transition(rec, model_params, power_bp=None, power_ff=None,
                        prespin_temp_bp=None, prespin_fan_bp=None):
  """
  Simulate a single transition through both old and new controllers.
  Returns dict with time series for comparison.
  """
  ts = np.array([s.t for s in rec.samples])
  temps_actual = np.array([s.max_temp_c for s in rec.samples])
  powers = np.array([s.som_power_draw_w for s in rec.samples])
  fans_actual = np.array([s.fan_pct_desired for s in rec.samples])
  started = np.array([s.started for s in rec.samples])
  intakes = np.array([s.intake_temp_c for s in rec.samples])

  # Find transition point
  trans_idx = 0
  for i in range(1, len(started)):
    if not started[i - 1] and started[i]:
      trans_idx = i
      break

  T_init = temps_actual[0]

  # Ambient estimate
  pre_intakes = intakes[:max(trans_idx, 1)]
  valid = pre_intakes[pre_intakes > 0]
  T_amb = float(np.mean(valid)) if len(valid) > 0 else max(20.0, T_init - 15.0)

  # Simulate old controller
  old_ctrl = OldFanController()
  old_fans = np.zeros(len(ts))
  old_ignition = False
  for i in range(len(ts)):
    ignition = bool(started[i])
    old_fans[i] = old_ctrl.update(temps_actual[i], ignition)

  # Simulate new controller
  new_ctrl = NewFanController(
    power_bp=power_bp, power_ff=power_ff,
    prespin_temp_bp=prespin_temp_bp, prespin_fan_bp=prespin_fan_bp
  )
  new_fans = np.zeros(len(ts))
  for i in range(len(ts)):
    ignition = bool(started[i])
    new_fans[i] = new_ctrl.update(temps_actual[i], ignition, powers[i], t_amb=T_amb)

  # Now predict what temperature WOULD have been with new fan commands
  # using the thermal model
  old_temps_pred = simulate_temperature(T_init, T_amb, powers, old_fans, model_params)
  new_temps_pred = simulate_temperature(T_init, T_amb, powers, new_fans, model_params)

  return {
    't': ts,
    'temp_actual': temps_actual,
    'temp_pred_old': old_temps_pred,
    'temp_pred_new': new_temps_pred,
    'fan_actual': fans_actual,
    'fan_old': old_fans,
    'fan_new': new_fans,
    'power': powers,
    'T_amb': T_amb,
    'trans_idx': trans_idx,
  }


# ── Plotting ──────────────────────────────────────────────────────────────────

def plot_comparisons(results, output_dir="."):
  try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
  except ImportError:
    print("matplotlib not available")
    return

  output_path = Path(output_dir)
  output_path.mkdir(parents=True, exist_ok=True)

  # --- Individual transition comparisons (first 9) ---
  n_show = min(9, len(results))
  fig, axes = plt.subplots(n_show, 2, figsize=(14, 4 * n_show))
  if n_show == 1:
    axes = axes.reshape(1, 2)

  for i in range(n_show):
    r = results[i]
    t = r['t']

    # Temperature comparison
    ax = axes[i, 0]
    ax.plot(t, r['temp_actual'], 'k-', linewidth=1.5, label='Actual', alpha=0.8)
    ax.plot(t, r['temp_pred_old'], 'r--', linewidth=1.5, label='Predicted (old ctrl)')
    ax.plot(t, r['temp_pred_new'], 'b--', linewidth=1.5, label='Predicted (new ctrl)')
    ax.axvline(0, color='gray', linestyle=':', alpha=0.5)
    ax.axhline(75, color='orange', linestyle=':', alpha=0.3)
    ax.set_ylabel('Temp (°C)')
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)
    ax.set_title(f'Transition {i + 1}: Temperature (Tamb={r["T_amb"]:.0f}°C)', fontsize=9)

    # Improvement stats
    post = t >= 0
    if np.any(post):
      old_peak = np.max(r['temp_pred_old'][post])
      new_peak = np.max(r['temp_pred_new'][post])
      improvement = old_peak - new_peak
      ax.text(0.98, 0.98, f'Peak reduction: {improvement:.1f}°C',
              transform=ax.transAxes, fontsize=8, va='top', ha='right',
              bbox=dict(boxstyle='round', facecolor='lightgreen' if improvement > 0 else 'lightyellow', alpha=0.7))

    # Fan comparison
    ax = axes[i, 1]
    ax.plot(t, r['fan_actual'], 'k-', linewidth=1, label='Actual', alpha=0.5)
    ax.plot(t, r['fan_old'], 'r-', linewidth=1.5, label='Old ctrl')
    ax.plot(t, r['fan_new'], 'b-', linewidth=1.5, label='New ctrl')
    ax.axvline(0, color='gray', linestyle=':', alpha=0.5)
    ax.set_ylabel('Fan %')
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)
    ax.set_title(f'Transition {i + 1}: Fan Speed', fontsize=9)

  axes[-1, 0].set_xlabel('Time (s)')
  axes[-1, 1].set_xlabel('Time (s)')

  plt.tight_layout()
  fig.savefig(output_path / 'fan_sim_individual.png', dpi=150)
  plt.close(fig)
  print(f"  Saved fan_sim_individual.png")

  # --- Aggregate comparison ---
  fig, axes = plt.subplots(2, 2, figsize=(12, 10))

  # Resample to common time grid
  t_grid = np.arange(-60, 300, 0.5)
  old_peaks = []
  new_peaks = []
  old_peak_temps = []
  new_peak_temps = []

  for r in results:
    t = r['t']
    post = t >= 0
    if not np.any(post):
      continue
    old_peak = float(np.max(r['temp_pred_old'][post]))
    new_peak = float(np.max(r['temp_pred_new'][post]))
    old_peaks.append(old_peak)
    new_peaks.append(new_peak)

  old_peaks = np.array(old_peaks)
  new_peaks = np.array(new_peaks)
  improvements = old_peaks - new_peaks

  # Peak temperature comparison
  ax = axes[0, 0]
  ax.scatter(old_peaks, new_peaks, alpha=0.5, s=20)
  lim = [min(old_peaks.min(), new_peaks.min()) - 2, max(old_peaks.max(), new_peaks.max()) + 2]
  ax.plot(lim, lim, 'k--', alpha=0.3)
  ax.set_xlabel('Old Controller Peak (°C)')
  ax.set_ylabel('New Controller Peak (°C)')
  ax.set_title('Peak Temperature: Old vs New')
  ax.grid(True, alpha=0.3)

  # Improvement histogram
  ax = axes[0, 1]
  ax.hist(improvements, bins=20, alpha=0.7, edgecolor='black',
          color=['green' if x > 0 else 'red' for x in sorted(improvements)])
  ax.axvline(np.median(improvements), color='red', linestyle='--',
             label=f'Median: {np.median(improvements):.1f}°C')
  ax.set_xlabel('Peak Temperature Reduction (°C)')
  ax.set_ylabel('Count')
  ax.set_title(f'Improvement Distribution (n={len(improvements)})')
  ax.legend()
  ax.grid(True, alpha=0.3)

  # Median fan trajectory comparison
  old_fan_matrix = []
  new_fan_matrix = []
  for r in results:
    old_interp = np.interp(t_grid, r['t'], r['fan_old'], left=np.nan, right=np.nan)
    new_interp = np.interp(t_grid, r['t'], r['fan_new'], left=np.nan, right=np.nan)
    old_fan_matrix.append(old_interp)
    new_fan_matrix.append(new_interp)

  old_fan_matrix = np.array(old_fan_matrix)
  new_fan_matrix = np.array(new_fan_matrix)

  ax = axes[1, 0]
  ax.plot(t_grid, np.nanmedian(old_fan_matrix, axis=0), 'r-', linewidth=2, label='Old ctrl (median)')
  ax.plot(t_grid, np.nanmedian(new_fan_matrix, axis=0), 'b-', linewidth=2, label='New ctrl (median)')
  ax.fill_between(t_grid,
                   np.nanpercentile(old_fan_matrix, 25, axis=0),
                   np.nanpercentile(old_fan_matrix, 75, axis=0),
                   alpha=0.15, color='red')
  ax.fill_between(t_grid,
                   np.nanpercentile(new_fan_matrix, 25, axis=0),
                   np.nanpercentile(new_fan_matrix, 75, axis=0),
                   alpha=0.15, color='blue')
  ax.axvline(0, color='gray', linestyle=':', alpha=0.5)
  ax.set_xlabel('Time (s)')
  ax.set_ylabel('Fan %')
  ax.set_title('Median Fan Trajectory: Old vs New')
  ax.legend()
  ax.grid(True, alpha=0.3)

  # Median temperature trajectory comparison
  old_temp_matrix = []
  new_temp_matrix = []
  for r in results:
    old_interp = np.interp(t_grid, r['t'], r['temp_pred_old'], left=np.nan, right=np.nan)
    new_interp = np.interp(t_grid, r['t'], r['temp_pred_new'], left=np.nan, right=np.nan)
    old_temp_matrix.append(old_interp)
    new_temp_matrix.append(new_interp)

  old_temp_matrix = np.array(old_temp_matrix)
  new_temp_matrix = np.array(new_temp_matrix)

  ax = axes[1, 1]
  ax.plot(t_grid, np.nanmedian(old_temp_matrix, axis=0), 'r-', linewidth=2, label='Old ctrl (median)')
  ax.plot(t_grid, np.nanmedian(new_temp_matrix, axis=0), 'b-', linewidth=2, label='New ctrl (median)')
  ax.fill_between(t_grid,
                   np.nanpercentile(old_temp_matrix, 25, axis=0),
                   np.nanpercentile(old_temp_matrix, 75, axis=0),
                   alpha=0.15, color='red')
  ax.fill_between(t_grid,
                   np.nanpercentile(new_temp_matrix, 25, axis=0),
                   np.nanpercentile(new_temp_matrix, 75, axis=0),
                   alpha=0.15, color='blue')
  ax.axvline(0, color='gray', linestyle=':', alpha=0.5)
  ax.axhline(75, color='orange', linestyle=':', alpha=0.3, label='Setpoint')
  ax.set_xlabel('Time (s)')
  ax.set_ylabel('Temp (°C)')
  ax.set_title('Median Temperature Trajectory: Old vs New')
  ax.legend()
  ax.grid(True, alpha=0.3)

  plt.tight_layout()
  fig.savefig(output_path / 'fan_sim_comparison.png', dpi=150)
  plt.close(fig)
  print(f"  Saved fan_sim_comparison.png")

  # --- Summary stats ---
  print(f"\n{'=' * 60}")
  print(f"SIMULATION SUMMARY ({len(results)} transitions)")
  print(f"{'=' * 60}")
  print(f"  Peak temperature reduction:")
  print(f"    Median: {np.median(improvements):.1f}°C")
  print(f"    Mean:   {np.mean(improvements):.1f}°C")
  print(f"    Std:    {np.std(improvements):.1f}°C")
  print(f"    Range:  [{np.min(improvements):.1f}, {np.max(improvements):.1f}]°C")
  print(f"    Positive (improved): {np.sum(improvements > 0)}/{len(improvements)} "
        f"({100 * np.sum(improvements > 0) / len(improvements):.0f}%)")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
  parser = argparse.ArgumentParser(description='Simulate old vs new fan controller')
  parser.add_argument('--data', type=str, required=True, help='Fleet data pickle from thermal_fleet_extract.py')
  parser.add_argument('--model', type=str, help='Thermal model JSON from thermal_model_fit.py')
  parser.add_argument('--plot-dir', type=str, default='.', help='Output directory for plots')
  args = parser.parse_args()

  # Load fleet data
  print(f"Loading fleet data from {args.data}...")
  with open(args.data, 'rb') as f:
    records = pickle.load(f)
  print(f"Loaded {len(records)} transitions")

  # Load or use default model parameters
  if args.model:
    print(f"Loading thermal model from {args.model}...")
    with open(args.model) as f:
      model = json.load(f)
    model_params = model['params']
    fan_curve = model.get('fan_curve', {})
    power_bp = fan_curve.get('power_feedforward_bp', [2.0, 4.0, 6.0, 8.0])
    power_ff = fan_curve.get('power_feedforward_fan', [0, 30, 60, 90])
    prespin_temp_bp = fan_curve.get('prespin_temp_bp', [40.0, 55.0, 70.0])
    prespin_fan_bp = fan_curve.get('prespin_fan_bp', [30.0, 50.0, 80.0])
  else:
    print("No model specified, using defaults...")
    model_params = {'tau_heat': 60.0, 'tau_cool': 120.0, 'R_passive': 10.0, 'k_fan': 3.0}
    power_bp = [2.0, 4.0, 6.0, 8.0]
    power_ff = [0, 30, 60, 90]
    prespin_temp_bp = [40.0, 55.0, 70.0]
    prespin_fan_bp = [30.0, 50.0, 80.0]

  print(f"Model params: {model_params}")

  # Simulate each transition
  print(f"\nSimulating {len(records)} transitions...")
  results = []
  for i, rec in enumerate(records):
    if len(rec.samples) < 40:
      continue
    try:
      result = simulate_transition(
        rec, model_params,
        power_bp=power_bp, power_ff=power_ff,
        prespin_temp_bp=prespin_temp_bp, prespin_fan_bp=prespin_fan_bp
      )
      results.append(result)
    except Exception as e:
      print(f"  Transition {i}: error - {e}")

  print(f"Successfully simulated {len(results)} transitions")

  # Plot
  plot_comparisons(results, args.plot_dir)


if __name__ == '__main__':
  main()
