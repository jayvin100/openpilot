#!/usr/bin/env python3
"""
Thermal fan control simulation: old (reactive) vs current (model-based) controller.

Replays fleet thermal data through both controllers and uses the thermal model
to predict temperature trajectories.

Usage:
  python3 tools/thermal_fan_sim.py --data thermal_fleet_data.pkl
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

from openpilot.system.hardware.fan_controller import (
  FanController, simulate_thermal, T_SETPOINT,
)


# ── Old controller (baseline for comparison) ─────────────────────────────────

class OldFanController:
  """Original reactive controller: integral-only, no power FF, hard reset on ignition."""
  def __init__(self, rate: int = 2):
    self.last_ignition = False
    self.i = 0.0
    self.k_i = 4e-3
    self.dt = 1.0 / rate

  def update(self, cur_temp: float, ignition: bool, **kwargs) -> int:
    pos_limit = 100 if ignition else 30
    neg_limit = 30 if ignition else 0

    if ignition != self.last_ignition:
      self.i = 0.0
    self.last_ignition = ignition

    error = cur_temp - T_SETPOINT
    ff = float(np.interp(cur_temp, [60.0, 100.0], [0, 100]))

    i = self.i + self.k_i * self.dt * error
    control = i + ff
    if control > pos_limit:
      i = min(self.i, pos_limit)
    elif control < neg_limit:
      i = max(self.i, neg_limit)
    self.i = i

    return int(np.clip(i + ff, neg_limit, pos_limit))


# ── Simulation ────────────────────────────────────────────────────────────────

DEFAULT_MODEL_PARAMS = {'tau_heat': 60.0, 'tau_cool': 120.0, 'R_passive': 10.0, 'k_fan': 3.0}


def simulate_transition(rec, model_params):
  """Run both controllers on one transition, predict temperatures with thermal model."""
  ts = np.array([s.t for s in rec.samples])
  temps = np.array([s.max_temp_c for s in rec.samples])
  powers = np.array([s.som_power_draw_w for s in rec.samples])
  started = np.array([s.started for s in rec.samples])
  intakes = np.array([s.intake_temp_c for s in rec.samples])

  # Find transition and estimate ambient
  trans_idx = next((i for i in range(1, len(started)) if not started[i-1] and started[i]), 0)
  pre_intakes = intakes[:max(trans_idx, 1)]
  valid = pre_intakes[pre_intakes > 0]
  T_amb = float(np.mean(valid)) if len(valid) > 0 else max(20.0, temps[0] - 15.0)
  T_init = temps[0]

  # Run both controllers
  old_ctrl = OldFanController()
  new_ctrl = FanController(2)
  old_fans = np.zeros(len(ts))
  new_fans = np.zeros(len(ts))

  for i in range(len(ts)):
    ign = bool(started[i])
    old_fans[i] = old_ctrl.update(temps[i], ign)
    new_fans[i] = new_ctrl.update(temps[i], ign, power_draw_w=powers[i], t_amb=T_amb)

  return {
    't': ts,
    'temp_actual': temps,
    'temp_pred_old': simulate_thermal(T_init, T_amb, powers, old_fans, model_params),
    'temp_pred_new': simulate_thermal(T_init, T_amb, powers, new_fans, model_params),
    'fan_old': old_fans,
    'fan_new': new_fans,
    'power': powers,
    'T_amb': T_amb,
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

  # Individual transitions (first 9)
  n_show = min(9, len(results))
  fig, axes = plt.subplots(n_show, 2, figsize=(14, 4 * n_show))
  if n_show == 1:
    axes = axes.reshape(1, 2)

  for i in range(n_show):
    r = results[i]
    t = r['t']

    ax = axes[i, 0]
    ax.plot(t, r['temp_actual'], 'k-', lw=1.5, label='Actual', alpha=0.8)
    ax.plot(t, r['temp_pred_old'], 'r--', lw=1.5, label='Old ctrl')
    ax.plot(t, r['temp_pred_new'], 'b--', lw=1.5, label='New ctrl')
    ax.axhline(T_SETPOINT, color='orange', ls=':', alpha=0.3)
    ax.set_ylabel('Temp (C)')
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)
    ax.set_title(f'Transition {i+1}: Tamb={r["T_amb"]:.0f}C', fontsize=9)

    post = t >= 0
    if np.any(post):
      improvement = np.max(r['temp_pred_old'][post]) - np.max(r['temp_pred_new'][post])
      ax.text(0.98, 0.98, f'Peak reduction: {improvement:.1f}C',
              transform=ax.transAxes, fontsize=8, va='top', ha='right',
              bbox=dict(boxstyle='round', facecolor='lightgreen' if improvement > 0 else 'lightyellow', alpha=0.7))

    ax = axes[i, 1]
    ax.plot(t, r['fan_old'], 'r-', lw=1.5, label='Old ctrl')
    ax.plot(t, r['fan_new'], 'b-', lw=1.5, label='New ctrl')
    ax.set_ylabel('Fan %')
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)

  plt.tight_layout()
  fig.savefig(output_path / 'fan_sim_individual.png', dpi=150)
  plt.close(fig)
  print(f"  Saved fan_sim_individual.png")

  # Aggregate stats
  improvements = []
  for r in results:
    post = r['t'] >= 0
    if np.any(post):
      improvements.append(np.max(r['temp_pred_old'][post]) - np.max(r['temp_pred_new'][post]))

  improvements = np.array(improvements)
  print(f"\n{'='*60}")
  print(f"SUMMARY ({len(results)} transitions)")
  print(f"  Peak temp reduction: median={np.median(improvements):.1f}C, "
        f"mean={np.mean(improvements):.1f}C, range=[{np.min(improvements):.1f}, {np.max(improvements):.1f}]")
  print(f"  Improved: {np.sum(improvements > 0)}/{len(improvements)} "
        f"({100*np.sum(improvements > 0)/len(improvements):.0f}%)")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
  parser = argparse.ArgumentParser(description='Simulate old vs new fan controller')
  parser.add_argument('--data', required=True, help='Fleet data pickle from thermal_fleet_extract.py')
  parser.add_argument('--model', help='Thermal model JSON from thermal_model_fit.py')
  parser.add_argument('--plot-dir', default='.', help='Output directory for plots')
  args = parser.parse_args()

  with open(args.data, 'rb') as f:
    records = pickle.load(f)
  print(f"Loaded {len(records)} transitions")

  if args.model:
    with open(args.model) as f:
      model_params = json.load(f)['params']
  else:
    model_params = DEFAULT_MODEL_PARAMS
  print(f"Model params: {model_params}")

  results = []
  for i, rec in enumerate(records):
    if len(rec.samples) < 40:
      continue
    try:
      results.append(simulate_transition(rec, model_params))
    except Exception as e:
      print(f"  Transition {i}: {e}")

  print(f"Simulated {len(results)} transitions")
  plot_comparisons(results, args.plot_dir)


if __name__ == '__main__':
  main()
