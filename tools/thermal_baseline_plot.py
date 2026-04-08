#!/usr/bin/env python3
"""
Plot thermal baseline test results.

Generates comprehensive plots from CSV data produced by thermal_baseline_test.py.
Run locally (not on device) after scp-ing the CSV file.

Usage:
  python3 thermal_baseline_plot.py /path/to/thermal_baseline_*.csv [--output-dir ./plots]
"""

import argparse
import csv
import os
import sys
from datetime import datetime

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from matplotlib.patches import Patch
import numpy as np


def load_csv(path: str) -> list[dict]:
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            # Convert numeric fields
            for k, v in r.items():
                if v == "":
                    r[k] = None
                else:
                    try:
                        r[k] = float(v)
                    except ValueError:
                        pass
            rows.append(r)
    return rows


def get_elapsed_minutes(rows: list[dict]) -> np.ndarray:
    return np.array([r["elapsed_s"] / 60 if r["elapsed_s"] is not None else 0 for r in rows])


def get_phase_spans(rows: list[dict]) -> list[tuple[float, float, str, str]]:
    """Return list of (start_min, end_min, phase, load_mode) spans."""
    spans = []
    cur_phase = None
    cur_mode = None
    start = 0.0

    for r in rows:
        phase = r.get("phase")
        mode = r.get("load_mode", "none")
        t = r["elapsed_s"] / 60 if r["elapsed_s"] else 0

        if phase != cur_phase or mode != cur_mode:
            if cur_phase is not None:
                spans.append((start, t, cur_phase, cur_mode))
            cur_phase = phase
            cur_mode = mode
            start = t

    if cur_phase is not None:
        spans.append((start, get_elapsed_minutes(rows)[-1], cur_phase, cur_mode))

    return spans


def shade_phases(ax, spans, ymin=None, ymax=None):
    """Add phase shading to an axis."""
    for start, end, phase, mode in spans:
        if phase == "load" and mode == "stress":
            ax.axvspan(start, end, alpha=0.15, color="red", zorder=0)
        elif phase == "load" and mode == "openpilot":
            ax.axvspan(start, end, alpha=0.15, color="orange", zorder=0)
        elif phase == "cooldown":
            ax.axvspan(start, end, alpha=0.08, color="blue", zorder=0)


def get_col(rows, name) -> np.ndarray:
    """Extract a numeric column, replacing None with NaN."""
    return np.array([r.get(name) if r.get(name) is not None else float("nan") for r in rows])


def plot_all(rows: list[dict], output_dir: str, csv_name: str):
    os.makedirs(output_dir, exist_ok=True)
    prefix = os.path.splitext(csv_name)[0]

    t = get_elapsed_minutes(rows)
    spans = get_phase_spans(rows)
    t_hours = t / 60

    legend_patches = [
        Patch(facecolor="red", alpha=0.15, label="Stress Load"),
        Patch(facecolor="orange", alpha=0.15, label="Openpilot Load"),
        Patch(facecolor="blue", alpha=0.08, label="Cooldown"),
    ]

    # ── 1. Master overview: key temps + power + fan ──────────────────────
    fig, axes = plt.subplots(4, 1, figsize=(20, 16), sharex=True)
    fig.suptitle(f"Thermal Baseline Test — {csv_name}", fontsize=14, fontweight="bold")

    # Panel 1: Key temperatures
    ax = axes[0]
    shade_phases(ax, spans)
    ax.plot(t_hours, get_col(rows, "temp_max_C"), "r-", linewidth=1.5, label="Max (all zones)")
    ax.plot(t_hours, get_col(rows, "temp_cpu_max_C"), "b-", linewidth=1, label="CPU max")
    ax.plot(t_hours, get_col(rows, "temp_gpu_max_C"), "g-", linewidth=1, label="GPU max")
    ax.plot(t_hours, get_col(rows, "temp_ddr_C"), "m-", linewidth=0.8, alpha=0.7, label="DDR")
    intake = get_col(rows, "temp_intake_C")
    if not np.all(np.isnan(intake)):
        ax.plot(t_hours, intake, "c-", linewidth=0.8, alpha=0.7, label="Intake")
    exhaust = get_col(rows, "temp_exhaust_C")
    if not np.all(np.isnan(exhaust)):
        ax.plot(t_hours, exhaust, "y-", linewidth=0.8, alpha=0.7, label="Exhaust")
    # Thermal thresholds
    ax.axhline(y=80, color="orange", linestyle="--", alpha=0.5, label="Yellow (80°C)")
    ax.axhline(y=96, color="red", linestyle="--", alpha=0.5, label="Red (96°C)")
    ax.axhline(y=75, color="darkgreen", linestyle=":", alpha=0.5, label="Offroad danger (75°C)")
    ax.set_ylabel("Temperature (°C)")
    ax.legend(loc="upper right", fontsize=8, ncol=3)
    ax.set_title("Key Temperatures with Thermal Bands")
    ax.grid(True, alpha=0.3)

    # Panel 2: Power draw
    ax = axes[1]
    shade_phases(ax, spans)
    sys_power = get_col(rows, "power_system_W")
    som_power = get_col(rows, "power_som_W")
    if not np.all(np.isnan(sys_power)):
        ax.plot(t_hours, sys_power, "r-", linewidth=1, label="System power")
    if not np.all(np.isnan(som_power)):
        ax.plot(t_hours, som_power, "b-", linewidth=1, label="SOM power")
    ax.set_ylabel("Power (W)")
    ax.legend(loc="upper right", fontsize=8)
    ax.set_title("Power Draw")
    ax.grid(True, alpha=0.3)

    # Panel 3: CPU usage
    ax = axes[2]
    shade_phases(ax, spans)
    for i in range(4):
        ax.plot(t_hours, get_col(rows, f"cpu{i}_usage_pct"), linewidth=0.6, alpha=0.6, label=f"Silver {i}")
    for i in range(4, 8):
        ax.plot(t_hours, get_col(rows, f"cpu{i}_usage_pct"), linewidth=0.8, alpha=0.8, label=f"Gold {i-4}")
    ax.set_ylabel("CPU Usage (%)")
    ax.set_ylim(-5, 105)
    ax.legend(loc="upper right", fontsize=7, ncol=4)
    ax.set_title("Per-Core CPU Usage")
    ax.grid(True, alpha=0.3)

    # Panel 4: Fan + GPU
    ax = axes[3]
    shade_phases(ax, spans)
    fan = get_col(rows, "fan_rpm")
    if not np.all(np.isnan(fan)):
        ax.plot(t_hours, fan, "k-", linewidth=1, label="Fan RPM")
    ax.set_ylabel("Fan RPM")
    ax.set_xlabel("Time (hours)")
    ax2 = ax.twinx()
    gpu_busy = get_col(rows, "gpu_busy_pct")
    if not np.all(np.isnan(gpu_busy)):
        ax2.plot(t_hours, gpu_busy, "g-", linewidth=0.8, alpha=0.6, label="GPU busy %")
    ax2.set_ylabel("GPU Busy %", color="green")
    ax.legend(loc="upper left", fontsize=8)
    ax2.legend(loc="upper right", fontsize=8)
    ax.set_title("Fan Speed & GPU Utilization")
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.legend(handles=legend_patches, loc="lower center", ncol=3, fontsize=9,
               bbox_to_anchor=(0.5, -0.01))
    path = os.path.join(output_dir, f"{prefix}_overview.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {path}")

    # ── 2. All CPU temperatures ──────────────────────────────────────────
    fig, ax = plt.subplots(figsize=(20, 8))
    shade_phases(ax, spans)
    colors_silver = ["#1f77b4", "#2196f3", "#64b5f6", "#90caf9"]
    colors_gold = ["#d32f2f", "#f44336", "#e57373", "#ef9a9a"]
    for i in range(4):
        ax.plot(t_hours, get_col(rows, f"temp_cpu{i}_silver_C"), color=colors_silver[i],
                linewidth=0.8, label=f"Silver {i}")
    for i in range(4):
        ax.plot(t_hours, get_col(rows, f"temp_cpu{i}_gold_C"), color=colors_gold[i],
                linewidth=0.8, label=f"Gold {i}")
    ax.axhline(y=80, color="orange", linestyle="--", alpha=0.5)
    ax.axhline(y=96, color="red", linestyle="--", alpha=0.5)
    ax.set_xlabel("Time (hours)")
    ax.set_ylabel("Temperature (°C)")
    ax.set_title(f"All CPU Core Temperatures — {csv_name}")
    ax.legend(loc="upper right", fontsize=8, ncol=4)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = os.path.join(output_dir, f"{prefix}_cpu_temps.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {path}")

    # ── 3. All SoC/platform temperatures ─────────────────────────────────
    fig, ax = plt.subplots(figsize=(20, 8))
    shade_phases(ax, spans)
    soc_zones = ["aoss0", "aoss1", "camera", "mmss", "mdm_dsp", "mdm_core",
                 "wlan", "kryo_l3_0", "kryo_l3_1", "dsp_hvx"]
    for name in soc_zones:
        col = get_col(rows, f"temp_{name}_C")
        if not np.all(np.isnan(col)):
            ax.plot(t_hours, col, linewidth=0.8, label=name)
    ax.set_xlabel("Time (hours)")
    ax.set_ylabel("Temperature (°C)")
    ax.set_title(f"SoC / Platform Temperatures — {csv_name}")
    ax.legend(loc="upper right", fontsize=8, ncol=3)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = os.path.join(output_dir, f"{prefix}_soc_temps.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {path}")

    # ── 4. PMIC + external sensors ───────────────────────────────────────
    fig, ax = plt.subplots(figsize=(20, 8))
    shade_phases(ax, spans)
    ext_zones = ["pm8998", "pm8005", "xo_therm", "intake", "exhaust", "gnss", "bottom_soc"]
    for name in ext_zones:
        col = get_col(rows, f"temp_{name}_C")
        if not np.all(np.isnan(col)):
            ax.plot(t_hours, col, linewidth=1, label=name)
    ax.set_xlabel("Time (hours)")
    ax.set_ylabel("Temperature (°C)")
    ax.set_title(f"PMIC & External Sensor Temperatures — {csv_name}")
    ax.legend(loc="upper right", fontsize=9)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = os.path.join(output_dir, f"{prefix}_pmic_ext_temps.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {path}")

    # ── 5. CPU frequency over time ───────────────────────────────────────
    fig, ax = plt.subplots(figsize=(20, 6))
    shade_phases(ax, spans)
    for i in range(4):
        ax.plot(t_hours, get_col(rows, f"cpu{i}_freq_khz") / 1000, color=colors_silver[i],
                linewidth=0.6, alpha=0.7, label=f"Silver {i}")
    for i in range(4, 8):
        ax.plot(t_hours, get_col(rows, f"cpu{i}_freq_khz") / 1000, color=colors_gold[i-4],
                linewidth=0.6, alpha=0.7, label=f"Gold {i-4}")
    ax.set_xlabel("Time (hours)")
    ax.set_ylabel("Frequency (MHz)")
    ax.set_title(f"CPU Core Frequencies — {csv_name}")
    ax.legend(loc="upper right", fontsize=7, ncol=4)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = os.path.join(output_dir, f"{prefix}_cpu_freqs.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {path}")

    # ── 6. Thermal rate of change (dT/dt) ────────────────────────────────
    fig, ax = plt.subplots(figsize=(20, 6))
    shade_phases(ax, spans)
    temp_max = get_col(rows, "temp_max_C")
    dt = np.diff(t) * 60  # seconds
    dtemp = np.diff(temp_max)
    rate = np.where(dt > 0, dtemp / dt, 0)
    # Smooth with rolling average (30 samples = ~2.5 min at 5s interval)
    kernel = 30
    if len(rate) > kernel:
        rate_smooth = np.convolve(rate, np.ones(kernel)/kernel, mode="same")
    else:
        rate_smooth = rate
    ax.plot(t_hours[1:], rate_smooth, "r-", linewidth=0.8)
    ax.axhline(y=0, color="k", linewidth=0.5)
    ax.set_xlabel("Time (hours)")
    ax.set_ylabel("dT/dt (°C/s)")
    ax.set_title(f"Temperature Rate of Change (smoothed) — {csv_name}")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = os.path.join(output_dir, f"{prefix}_temp_rate.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {path}")

    # ── 7. Per-cycle comparison (superimposed) ───────────────────────────
    cycles = set(int(r["cycle_num"]) for r in rows if r.get("cycle_num") is not None and r["cycle_num"] > 0)
    if len(cycles) > 1:
        fig, axes = plt.subplots(1, 2, figsize=(20, 8))

        for load_type, ax in [("stress", axes[0]), ("openpilot", axes[1])]:
            ax.set_title(f"Per-Cycle Overlay — {load_type} load")
            for c in sorted(cycles):
                cycle_rows = [r for r in rows if r.get("cycle_num") == c and r.get("load_mode") == load_type]
                if not cycle_rows:
                    continue
                ct = np.array([r["phase_elapsed_s"] / 60 for r in cycle_rows])
                ctemp = np.array([r["temp_max_C"] if r["temp_max_C"] is not None else float("nan") for r in cycle_rows])
                ax.plot(ct, ctemp, linewidth=1, label=f"Cycle {c}")
            ax.set_xlabel("Phase elapsed (min)")
            ax.set_ylabel("Max temp (°C)")
            ax.legend(fontsize=8)
            ax.grid(True, alpha=0.3)

        fig.suptitle(f"Cycle-to-Cycle Repeatability — {csv_name}", fontsize=13)
        fig.tight_layout()
        path = os.path.join(output_dir, f"{prefix}_cycle_overlay.png")
        fig.savefig(path, dpi=150, bbox_inches="tight")
        plt.close(fig)
        print(f"  Saved: {path}")

    # ── 8. Thermal model: power vs steady-state temp ─────────────────────
    fig, ax = plt.subplots(figsize=(10, 8))
    # Use last 5 minutes of each load phase as "steady state"
    for ps_type, color, marker in [("stress", "red", "o"), ("openpilot", "orange", "s")]:
        ss_temps = []
        ss_powers = []
        for span_start, span_end, phase, mode in spans:
            if phase != "load" or mode != ps_type:
                continue
            # Last 5 min of this phase
            late_rows = [r for r in rows
                         if r["elapsed_s"] is not None
                         and span_start <= r["elapsed_s"]/60 <= span_end
                         and r["elapsed_s"]/60 > (span_end - 5)]
            if late_rows:
                temps = [r["temp_max_C"] for r in late_rows if isinstance(r.get("temp_max_C"), (int, float))]
                powers = [r["power_system_W"] for r in late_rows if isinstance(r.get("power_system_W"), (int, float))]
                if temps and powers:
                    ss_temps.append(np.mean(temps))
                    ss_powers.append(np.mean(powers))
        if ss_temps:
            ax.scatter(ss_powers, ss_temps, c=color, marker=marker, s=80, label=f"{ps_type} steady-state", zorder=5)

    ax.set_xlabel("Power (W)")
    ax.set_ylabel("Steady-state Max Temp (°C)")
    ax.set_title(f"Thermal Resistance: Power vs Temperature — {csv_name}")
    ax.axhline(y=80, color="orange", linestyle="--", alpha=0.5, label="Yellow threshold")
    ax.axhline(y=96, color="red", linestyle="--", alpha=0.5, label="Red threshold")
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = os.path.join(output_dir, f"{prefix}_thermal_model.png")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {path}")

    # ── 9. Intake/exhaust delta (airflow effectiveness) ──────────────────
    intake_col = get_col(rows, "temp_intake_C")
    exhaust_col = get_col(rows, "temp_exhaust_C")
    if not (np.all(np.isnan(intake_col)) or np.all(np.isnan(exhaust_col))):
        fig, ax = plt.subplots(figsize=(20, 6))
        shade_phases(ax, spans)
        delta = exhaust_col - intake_col
        ax.plot(t_hours, delta, "r-", linewidth=1, label="Exhaust - Intake")
        ax.plot(t_hours, intake_col, "b-", linewidth=0.8, alpha=0.5, label="Intake")
        ax.plot(t_hours, exhaust_col, "r-", linewidth=0.8, alpha=0.5, label="Exhaust")
        ax.set_xlabel("Time (hours)")
        ax.set_ylabel("Temperature (°C)")
        ax.set_title(f"Airflow Effectiveness (Exhaust−Intake ΔT) — {csv_name}")
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        path = os.path.join(output_dir, f"{prefix}_airflow_delta.png")
        fig.savefig(path, dpi=150, bbox_inches="tight")
        plt.close(fig)
        print(f"  Saved: {path}")

    print(f"\nAll plots saved to {output_dir}/")


def main():
    parser = argparse.ArgumentParser(description="Plot thermal baseline test results")
    parser.add_argument("csv_file", help="Path to thermal_baseline_*.csv")
    parser.add_argument("--output-dir", default="./thermal_plots", help="Output directory for plots")
    args = parser.parse_args()

    print(f"Loading {args.csv_file}...")
    rows = load_csv(args.csv_file)
    print(f"Loaded {len(rows)} samples")

    csv_name = os.path.basename(args.csv_file)
    plot_all(rows, args.output_dir, csv_name)


if __name__ == "__main__":
    main()
