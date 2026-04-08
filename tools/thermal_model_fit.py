#!/usr/bin/env python3
"""
Thermal model fitting from fleet data.

Fits a lumped-capacitance thermal model to fleet transition data extracted
by thermal_fleet_extract.py. Outputs fitted parameters and derived fan curve
breakpoints for use in fan_controller.py.

Model:
  C_th * dT/dt = P_heat - (T - T_amb) * (1/R_passive + fan%/100 * 1/R_fan_eff)

Simplifies to:
  dT/dt = (T_eq - T) / tau
  where tau = C_th * R_total, T_eq = T_amb + P * R_total

Parameters fitted:
  - tau_heat: time constant when heating (s)
  - tau_cool: time constant when cooling (s)
  - R_passive: passive thermal resistance (°C/W)
  - k_fan: fan effectiveness coefficient (R_total = R_passive / (1 + k_fan * fan%/100))

Usage:
  python3 tools/thermal_model_fit.py --input thermal_fleet_data.pkl --output thermal_model.json
  python3 tools/thermal_model_fit.py --input thermal_fleet_data.pkl --plot
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
  simulate_thermal, T_SETPOINT, TEMP_FF_BP, TEMP_FF_FAN,
  POWER_FF_BP, POWER_FF_FAN, PRESPIN_TEMP_BP, PRESPIN_FAN_BP,
)


# ── Thermal model fitting ─────────────────────────────────────────────────────

def _simulate_for_fit(params_tuple, T_init, power_series, fan_series, T_amb, dt=0.5):
  """Wrapper: convert optimizer's param tuple to dict for simulate_thermal."""
  tau_heat, tau_cool, R_passive, k_fan = params_tuple
  return simulate_thermal(T_init, T_amb, power_series, fan_series,
                          {'tau_heat': tau_heat, 'tau_cool': tau_cool,
                           'R_passive': R_passive, 'k_fan': k_fan}, dt)


def cost_function(params, transitions, dt=0.5):
  """
  Total squared error across all transitions.
  transitions: list of (T_init, T_amb, power_array, fan_array, temp_actual_array)
  """
  total_err = 0.0
  total_n = 0

  for T_init, T_amb, power, fan, temp_actual in transitions:
    T_pred = _simulate_for_fit(params, T_init, power, fan, T_amb, dt)
    err = np.sum((T_pred - temp_actual) ** 2)
    total_err += err
    total_n += len(temp_actual)

  return total_err / max(total_n, 1)


def fit_model(transitions, initial_guess=None):
  """
  Fit thermal model parameters using scipy.optimize.minimize.
  Returns: fitted params [tau_heat, tau_cool, R_passive, k_fan]
  """
  from scipy.optimize import minimize, differential_evolution

  if initial_guess is None:
    initial_guess = [60.0, 120.0, 10.0, 3.0]

  bounds = [
    (5.0, 300.0),    # tau_heat: 5-300s
    (10.0, 600.0),   # tau_cool: 10-600s
    (2.0, 30.0),     # R_passive: 2-30 °C/W
    (0.5, 20.0),     # k_fan: 0.5-20
  ]

  # First: global search with differential evolution
  print("  Running global optimization (differential evolution)...")
  result_de = differential_evolution(
    cost_function, bounds, args=(transitions,),
    seed=42, maxiter=200, tol=1e-6, disp=False
  )
  print(f"  DE result: cost={result_de.fun:.4f}, params={result_de.x}")

  # Second: local refinement with Nelder-Mead
  print("  Refining with Nelder-Mead...")
  result = minimize(
    cost_function, result_de.x, args=(transitions,),
    method='Nelder-Mead',
    options={'maxiter': 5000, 'xatol': 0.01, 'fatol': 1e-6}
  )
  print(f"  Final: cost={result.fun:.4f}, params={result.x}")

  return result.x, result.fun


# ── Prepare transition data ────────────────────────────────────────────────────

def prepare_transitions(records, use_post_only=True):
  """
  Convert TransitionRecords into format needed by the fitter.
  Returns: list of (T_init, T_amb, power_array, fan_array, temp_actual_array)
  """
  transitions = []

  for rec in records:
    if len(rec.samples) < 40:  # need at least 20s of data
      continue

    ts = np.array([s.t for s in rec.samples])
    temps = np.array([s.max_temp_c for s in rec.samples])
    powers = np.array([s.som_power_draw_w for s in rec.samples])
    fans = np.array([s.fan_pct_desired for s in rec.samples])
    intakes = np.array([s.intake_temp_c for s in rec.samples])

    if use_post_only:
      # Only fit post-transition data (where the action is)
      mask = ts >= -10  # start 10s before transition for context
    else:
      mask = np.ones(len(ts), dtype=bool)

    ts_win = ts[mask]
    temps_win = temps[mask]
    powers_win = powers[mask]
    fans_win = fans[mask]
    intakes_win = intakes[mask]

    if len(ts_win) < 20:
      continue

    T_init = temps_win[0]

    # Ambient proxy: mean intake temp in first 30s, or fallback to heuristic
    early_intakes = intakes_win[ts_win < 30]
    valid_intakes = early_intakes[early_intakes > 0]
    if len(valid_intakes) > 3:
      T_amb = float(np.mean(valid_intakes))
    elif rec.t_ambient > 0:
      T_amb = rec.t_ambient
    else:
      # Heuristic: ambient is typically 20-40°C, use T_init - 15 as rough guess
      T_amb = max(20.0, T_init - 15.0)

    # Filter out bad data
    if T_init < 10 or T_init > 120:
      continue
    if np.any(powers_win < 0) or np.any(powers_win > 20):
      continue

    transitions.append((T_init, T_amb, powers_win, fans_win, temps_win))

  return transitions


# ── Derive fan curve breakpoints ───────────────────────────────────────────────

def derive_fan_curve(params, T_setpoint=T_SETPOINT, T_amb_range=(20, 45)):
  """
  From fitted model, derive the ideal fan % as a function of power draw.

  T_setpoint = T_amb + P * R_passive / (1 + k_fan * fan/100)
  => fan = 100/k_fan * (P * R_passive / (T_setpoint - T_amb) - 1)
  """
  _, _, R_passive, k_fan = params

  print(f"\n  Derived Fan Curves (setpoint={T_setpoint}°C):")
  print(f"  {'Power (W)':>10} | ", end='')
  for T_amb in range(T_amb_range[0], T_amb_range[1] + 1, 5):
    print(f"Tamb={T_amb}°C ", end='')
  print()
  print(f"  {'-' * 10}-+-{'-' * (8 * len(range(T_amb_range[0], T_amb_range[1] + 1, 5)))}")

  power_bps = []
  fan_bps = []
  for P in np.arange(1.0, 10.0, 0.5):
    print(f"  {P:10.1f} | ", end='')
    fans_at_p = []
    for T_amb in range(T_amb_range[0], T_amb_range[1] + 1, 5):
      delta_T = T_setpoint - T_amb
      if delta_T <= 0:
        fan = 100
      else:
        fan = 100.0 / k_fan * (P * R_passive / delta_T - 1.0)
        fan = np.clip(fan, 0, 100)
      fans_at_p.append(fan)
      print(f"{fan:7.1f}% ", end='')
    print()

    # Use T_amb=35°C as the "design point" for the default fan curve
    design_idx = (35 - T_amb_range[0]) // 5
    if 0 <= design_idx < len(fans_at_p):
      power_bps.append(float(P))
      fan_bps.append(float(fans_at_p[design_idx]))

  return power_bps, fan_bps


def derive_prespin_curve(records):
  """
  From fleet data, determine what fan % would have been ideal at ignition
  based on initial temperature, by looking at what the steady-state fan ended up being.
  """
  t_inits = []
  fan_needed = []

  for rec in records:
    if rec.t_steady > 0 and rec.fan_pct_steady > 0:
      t_inits.append(rec.t_initial)
      # Pre-spin should anticipate the steady-state need
      # Scale up slightly since the fan needs to be ahead of the curve
      fan_needed.append(min(100, rec.fan_pct_steady * 1.2))

  if len(t_inits) < 5:
    print("  Not enough data for prespin curve")
    return [40.0, 55.0, 70.0], [30.0, 50.0, 80.0]  # defaults

  t_inits = np.array(t_inits)
  fan_needed = np.array(fan_needed)

  # Bin by temperature and take median
  temp_bins = np.arange(30, 80, 5)
  prespin_bp_temp = []
  prespin_bp_fan = []

  for t_low, t_high in zip(temp_bins[:-1], temp_bins[1:]):
    mask = (t_inits >= t_low) & (t_inits < t_high)
    if np.sum(mask) >= 3:
      prespin_bp_temp.append(float((t_low + t_high) / 2))
      prespin_bp_fan.append(float(np.median(fan_needed[mask])))

  if len(prespin_bp_temp) < 2:
    return [40.0, 55.0, 70.0], [30.0, 50.0, 80.0]

  print(f"\n  Prespin curve from fleet data:")
  for t, f in zip(prespin_bp_temp, prespin_bp_fan):
    print(f"    {t:.0f}°C → {f:.0f}% fan")

  return prespin_bp_temp, prespin_bp_fan


# ── Plotting ───────────────────────────────────────────────────────────────────

def plot_model_fit(params, transitions, records, output_dir="."):
  """Generate model validation plots."""
  try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
  except ImportError:
    print("matplotlib not available")
    return

  output_path = Path(output_dir)
  tau_heat, tau_cool, R_passive, k_fan = params

  # --- Plot 1: Model predictions vs actual for several transitions ---
  n_show = min(12, len(transitions))
  cols = 3
  rows = (n_show + cols - 1) // cols
  fig, axes = plt.subplots(rows, cols, figsize=(5 * cols, 4 * rows))
  axes = np.array(axes).flatten() if n_show > 1 else [axes]

  for i in range(n_show):
    T_init, T_amb, power, fan, temp_actual = transitions[i]
    T_pred = _simulate_for_fit(params, T_init, power, fan, T_amb)
    t = np.arange(len(temp_actual)) * 0.5

    ax = axes[i]
    ax.plot(t, temp_actual, 'b-', label='Actual', linewidth=1.5)
    ax.plot(t, T_pred, 'r--', label='Model', linewidth=1.5)
    ax.set_title(f'Transition {i + 1} (Tamb={T_amb:.0f}°C)', fontsize=9)
    ax.set_xlabel('Time (s)', fontsize=8)
    ax.set_ylabel('Temp (°C)', fontsize=8)
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)

    rmse = np.sqrt(np.mean((T_pred - temp_actual) ** 2))
    ax.text(0.02, 0.98, f'RMSE={rmse:.1f}°C', transform=ax.transAxes,
            fontsize=7, va='top', ha='left',
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))

  for i in range(n_show, len(axes)):
    axes[i].set_visible(False)

  fig.suptitle(f'Thermal Model Fit: τ_heat={tau_heat:.0f}s, τ_cool={tau_cool:.0f}s, '
               f'R_passive={R_passive:.1f}°C/W, k_fan={k_fan:.1f}', fontsize=11)
  plt.tight_layout()
  fig.savefig(output_path / 'thermal_model_predictions.png', dpi=150)
  plt.close(fig)
  print(f"  Saved thermal_model_predictions.png")

  # --- Plot 2: Residual analysis ---
  all_residuals = []
  all_times = []
  for T_init, T_amb, power, fan, temp_actual in transitions:
    T_pred = _simulate_for_fit(params, T_init, power, fan, T_amb)
    residuals = T_pred - temp_actual
    t = np.arange(len(temp_actual)) * 0.5
    all_residuals.extend(residuals)
    all_times.extend(t)

  all_residuals = np.array(all_residuals)
  all_times = np.array(all_times)

  fig, axes = plt.subplots(1, 2, figsize=(12, 5))

  ax = axes[0]
  ax.hist(all_residuals, bins=50, alpha=0.7, edgecolor='black')
  ax.axvline(0, color='red', linestyle='--')
  ax.set_xlabel('Residual (Predicted - Actual) (°C)')
  ax.set_ylabel('Count')
  ax.set_title(f'Residual Distribution (RMSE={np.sqrt(np.mean(all_residuals ** 2)):.2f}°C)')
  ax.grid(True, alpha=0.3)

  ax = axes[1]
  # Bin residuals by time
  t_bins = np.arange(0, max(all_times) + 10, 10)
  for i in range(len(t_bins) - 1):
    mask = (all_times >= t_bins[i]) & (all_times < t_bins[i + 1])
    if np.sum(mask) > 5:
      ax.boxplot(all_residuals[mask], positions=[t_bins[i]], widths=8, showfliers=False)
  ax.axhline(0, color='red', linestyle='--')
  ax.set_xlabel('Time (s)')
  ax.set_ylabel('Residual (°C)')
  ax.set_title('Residual vs Time')
  ax.grid(True, alpha=0.3)

  plt.tight_layout()
  fig.savefig(output_path / 'thermal_model_residuals.png', dpi=150)
  plt.close(fig)
  print(f"  Saved thermal_model_residuals.png")

  # --- Plot 3: Derived fan curves ---
  fig, axes = plt.subplots(1, 2, figsize=(12, 5))

  ax = axes[0]
  ax.set_title('Ideal Fan % vs Power (from model)')
  for T_amb in [20, 25, 30, 35, 40, 45]:
    powers = np.arange(0.5, 10, 0.1)
    fans = []
    for P in powers:
      delta_T = T_SETPOINT - T_amb
      if delta_T <= 0:
        fans.append(100)
      else:
        fan = 100.0 / k_fan * (P * R_passive / delta_T - 1.0)
        fans.append(np.clip(fan, 0, 100))
    ax.plot(powers, fans, label=f'Tamb={T_amb}°C')
  ax.set_xlabel('Power Draw (W)')
  ax.set_ylabel('Required Fan %')
  ax.legend()
  ax.grid(True, alpha=0.3)
  ax.set_ylim(0, 105)

  # Current vs ideal feedforward
  ax = axes[1]
  ax.set_title('Current vs Model-Based Feedforward')
  temps = np.arange(40, 110)
  current_ff = np.interp(temps, TEMP_FF_BP, TEMP_FF_FAN)
  ax.plot(temps, current_ff, 'r-', linewidth=2, label='Current (temp-only)')
  ax.axhline(30, color='gray', linestyle=':', label='Onroad minimum (30%)')
  ax.set_xlabel('Temperature (°C)')
  ax.set_ylabel('Fan %')
  ax.legend()
  ax.grid(True, alpha=0.3)

  plt.tight_layout()
  fig.savefig(output_path / 'thermal_fan_curves.png', dpi=150)
  plt.close(fig)
  print(f"  Saved thermal_fan_curves.png")


# ── Main ───────────────────────────────────────────────────────────────────────

def main():
  parser = argparse.ArgumentParser(description='Fit thermal model from fleet data')
  parser.add_argument('--input', type=str, required=True, help='Input pickle from thermal_fleet_extract.py')
  parser.add_argument('--output', type=str, default='thermal_model.json', help='Output JSON with fitted params')
  parser.add_argument('--plot', action='store_true', help='Generate validation plots')
  parser.add_argument('--plot-dir', type=str, default='.', help='Directory for plots')
  parser.add_argument('--validation-split', type=float, default=0.2, help='Fraction of data for validation')
  parser.add_argument('--min-ambient', type=float, default=0, help='Only fit transitions with ambient temp >= this (C)')
  parser.add_argument('--min-overshoot', type=float, default=0, help='Only fit transitions with overshoot >= this (C)')
  parser.add_argument('--top-percentile', type=float, default=0,
                      help='Only fit the hottest N%% of transitions by peak temp (e.g. 20 = top 20%%)')
  args = parser.parse_args()

  print(f"Loading fleet data from {args.input}...")
  with open(args.input, 'rb') as f:
    records = pickle.load(f)
  print(f"Loaded {len(records)} transition records")

  # Filter to hot tail if requested
  n_before = len(records)
  if args.min_ambient > 0:
    records = [r for r in records if r.t_ambient >= args.min_ambient]
    print(f"  --min-ambient {args.min_ambient}: {n_before} -> {len(records)} records")
  if args.min_overshoot > 0:
    n_before = len(records)
    records = [r for r in records if r.overshoot >= args.min_overshoot]
    print(f"  --min-overshoot {args.min_overshoot}: {n_before} -> {len(records)} records")
  if args.top_percentile > 0:
    n_before = len(records)
    peaks = sorted([r.t_peak for r in records])
    if peaks:
      cutoff = peaks[max(0, int(len(peaks) * (1 - args.top_percentile / 100)))]
      records = [r for r in records if r.t_peak >= cutoff]
    print(f"  --top-percentile {args.top_percentile}: {n_before} -> {len(records)} records (peak >= {cutoff:.1f}C)")

  # Prepare data
  print("\nPreparing transition data...")
  transitions = prepare_transitions(records)
  print(f"Prepared {len(transitions)} valid transitions for fitting")

  if len(transitions) < 3:
    print("ERROR: Need at least 3 valid transitions for fitting")
    sys.exit(1)

  # Train/validation split
  n_val = max(1, int(len(transitions) * args.validation_split))
  rng = np.random.RandomState(42)
  indices = rng.permutation(len(transitions))
  val_indices = indices[:n_val]
  train_indices = indices[n_val:]

  train_transitions = [transitions[i] for i in train_indices]
  val_transitions = [transitions[i] for i in val_indices]
  print(f"Split: {len(train_transitions)} train, {len(val_transitions)} validation")

  # Fit model
  print("\nFitting thermal model...")
  params, train_cost = fit_model(train_transitions)
  tau_heat, tau_cool, R_passive, k_fan = params

  # Validate
  val_cost = cost_function(params, val_transitions)
  train_rmse = np.sqrt(train_cost)
  val_rmse = np.sqrt(val_cost)

  print(f"\n{'=' * 60}")
  print(f"FITTED PARAMETERS:")
  print(f"  tau_heat  = {tau_heat:.1f} s   (heating time constant)")
  print(f"  tau_cool  = {tau_cool:.1f} s   (cooling time constant)")
  print(f"  R_passive = {R_passive:.2f} °C/W (passive thermal resistance)")
  print(f"  k_fan     = {k_fan:.2f}      (fan effectiveness)")
  print(f"\n  Train RMSE = {train_rmse:.2f} °C")
  print(f"  Val   RMSE = {val_rmse:.2f} °C")
  print(f"{'=' * 60}")

  # Derive fan curve breakpoints
  power_bp, fan_bp = derive_fan_curve(params)
  prespin_temp_bp, prespin_fan_bp = derive_prespin_curve(records)

  # Simplify power fan curve to 4-5 breakpoints for np.interp
  # Find breakpoints at 0%, 30%, 60%, 90% fan
  target_fans = [0, 30, 60, 90]
  simple_power_bp = []
  simple_fan_bp = []
  for target in target_fans:
    # Find power where fan crosses this target
    for p, f in zip(power_bp, fan_bp):
      if f >= target:
        simple_power_bp.append(round(p, 1))
        simple_fan_bp.append(target)
        break
    else:
      simple_power_bp.append(power_bp[-1])
      simple_fan_bp.append(target)

  # Remove duplicates
  seen = set()
  deduped_power = []
  deduped_fan = []
  for p, f in zip(simple_power_bp, simple_fan_bp):
    if p not in seen:
      seen.add(p)
      deduped_power.append(p)
      deduped_fan.append(f)
  simple_power_bp = deduped_power
  simple_fan_bp = deduped_fan

  print(f"\n  Simplified power feedforward breakpoints:")
  print(f"    power_bp = {simple_power_bp}")
  print(f"    fan_bp   = {simple_fan_bp}")

  print(f"\n  Prespin breakpoints:")
  print(f"    temp_bp  = {[round(t, 1) for t in prespin_temp_bp]}")
  print(f"    fan_bp   = {[round(f, 1) for f in prespin_fan_bp]}")

  # Save model
  model = {
    'params': {
      'tau_heat': round(float(tau_heat), 1),
      'tau_cool': round(float(tau_cool), 1),
      'R_passive': round(float(R_passive), 2),
      'k_fan': round(float(k_fan), 2),
    },
    'metrics': {
      'train_rmse_c': round(float(train_rmse), 2),
      'val_rmse_c': round(float(val_rmse), 2),
      'n_train': len(train_transitions),
      'n_val': len(val_transitions),
      'n_total_records': len(records),
    },
    'fan_curve': {
      'power_feedforward_bp': simple_power_bp,
      'power_feedforward_fan': simple_fan_bp,
      'prespin_temp_bp': [round(t, 1) for t in prespin_temp_bp],
      'prespin_fan_bp': [round(f, 1) for f in prespin_fan_bp],
    },
    'controller_params': {
      'k_p': 2.0,
      'k_i': 4e-3,
      'setpoint_c': T_SETPOINT,
      'temp_feedforward_bp': list(TEMP_FF_BP),
      'temp_feedforward_fan': [0, 100],
    },
  }

  with open(args.output, 'w') as f:
    json.dump(model, f, indent=2)
  print(f"\nSaved model to {args.output}")

  # Print update instructions
  print(f"\n{'=' * 60}")
  print("TO APPLY: update these constants in fan_controller.py:")
  print(f"{'=' * 60}")
  print(f"  POWER_FF_BP  = {simple_power_bp}")
  print(f"  POWER_FF_FAN = {simple_fan_bp}")
  print(f"  PRESPIN_TEMP_BP = {[round(t, 1) for t in prespin_temp_bp]}")
  print(f"  PRESPIN_FAN_BP  = {[round(f, 1) for f in prespin_fan_bp]}")

  if args.plot:
    print("\nGenerating plots...")
    plot_model_fit(params, transitions, records, args.plot_dir)


if __name__ == '__main__':
  main()
