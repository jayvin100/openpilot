#!/usr/bin/env python3
"""
Live thermal monitor for testing the fan controller changes.
Run on-device, prints a live dashboard every 2 seconds.

Usage:
  python3 tools/thermal_monitor.py
  python3 tools/thermal_monitor.py --csv /data/thermal_log.csv
"""

import argparse
import csv
import os
import sys
import time
from datetime import datetime
from pathlib import Path

# Add openpilot root to path
OPENPILOT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(OPENPILOT_ROOT))

import cereal.messaging as messaging


def main():
  parser = argparse.ArgumentParser(description='Live thermal monitor')
  parser.add_argument('--csv', type=str, help='Also log to CSV file')
  args = parser.parse_args()

  csv_writer = None
  csv_file = None
  if args.csv:
    csv_file = open(args.csv, 'w', newline='')
    csv_writer = csv.writer(csv_file)
    csv_writer.writerow([
      'timestamp', 'elapsed_s', 'maxTempC', 'intakeTempC', 'cpuMaxC',
      'fanPct', 'fanRPM', 'somPowerW', 'powerVI_W',
      'thermalStatus', 'started', 'ignition',
    ])

  sm = messaging.SubMaster(['deviceState', 'peripheralState', 'pandaStates'])

  STATUS_NAMES = {0: 'green', 1: 'yellow', 2: 'red', 3: 'danger'}
  STATUS_COLORS = {
    'green': '\033[92m',   # bright green
    'yellow': '\033[93m',  # bright yellow
    'red': '\033[91m',     # bright red
    'danger': '\033[95m',  # magenta
  }
  RESET = '\033[0m'
  BOLD = '\033[1m'
  DIM = '\033[2m'

  t0 = time.monotonic()
  peak_temp = 0
  peak_time = 0
  prev_started = None
  transition_time = None

  print(f"{BOLD}Thermal Monitor — Ctrl+C to stop{RESET}")
  if args.csv:
    print(f"{DIM}Logging to {args.csv}{RESET}")
  print()

  try:
    while True:
      sm.update(2000)

      ds = sm['deviceState']
      ps = sm['peripheralState']

      max_temp = ds.maxTempC
      intake = ds.intakeTempC
      cpu_max = max(ds.cpuTempC) if len(ds.cpuTempC) else 0
      fan_pct = ds.fanSpeedPercentDesired
      fan_rpm = ps.fanSpeedRpm
      som_power = ds.somPowerDrawW
      power_vi = (ps.voltage / 1000) * (ps.current / 1000)
      status_raw = ds.thermalStatus.raw if hasattr(ds.thermalStatus, 'raw') else int(ds.thermalStatus)
      status_name = STATUS_NAMES.get(status_raw, '?')
      started = ds.started
      elapsed = time.monotonic() - t0

      # Detect ignition from pandaStates
      ignition = False
      if sm.valid['pandaStates']:
        for ps_state in sm['pandaStates']:
          if ps_state.ignitionLine or ps_state.ignitionCan:
            ignition = True

      # Track peak
      if max_temp > peak_temp:
        peak_temp = max_temp
        peak_time = elapsed

      # Detect transition
      if prev_started is not None and started and not prev_started:
        transition_time = elapsed
      prev_started = started

      # Color
      color = STATUS_COLORS.get(status_name, RESET)

      # Format
      time_str = datetime.now().strftime('%H:%M:%S')
      elapsed_str = f"{int(elapsed//60):02d}:{int(elapsed%60):02d}"

      temp_bar = '█' * max(0, int((max_temp - 30) / 2))
      fan_bar = '▮' * (fan_pct // 5) if fan_pct > 0 else '-'

      line = (
        f"{DIM}{time_str} [{elapsed_str}]{RESET}  "
        f"{color}{BOLD}{status_name:6s}{RESET}  "
        f"maxT={BOLD}{max_temp:5.1f}°C{RESET}  "
        f"intake={intake:5.1f}°C  "
        f"cpu={cpu_max:5.1f}°C  "
        f"fan={BOLD}{fan_pct:3d}%{RESET} {fan_rpm:5d}rpm  "
        f"P={som_power:4.1f}W/{power_vi:4.1f}W  "
        f"{'ONROAD' if started else 'OFFROAD':7s}  "
        f"{DIM}{temp_bar}{RESET}"
      )
      print(line)

      # Print transition info
      if transition_time is not None and elapsed - transition_time < 2:
        print(f"  {BOLD}>>> IGNITION ON — watching for prespin and peak{RESET}")

      # Print peak after settling (60s post-transition)
      if transition_time and 59 < elapsed - transition_time < 61:
        print(f"  {BOLD}>>> PEAK after ignition: {peak_temp:.1f}°C at +{peak_time - transition_time:.0f}s{RESET}")

      # CSV
      if csv_writer:
        csv_writer.writerow([
          datetime.now().isoformat(), f'{elapsed:.1f}', f'{max_temp:.1f}',
          f'{intake:.1f}', f'{cpu_max:.1f}', fan_pct, fan_rpm,
          f'{som_power:.2f}', f'{power_vi:.2f}', status_name,
          started, ignition,
        ])
        csv_file.flush()

      time.sleep(2)

  except KeyboardInterrupt:
    print(f"\n{BOLD}Summary:{RESET}")
    print(f"  Duration: {elapsed/60:.1f} min")
    print(f"  Peak temp: {peak_temp:.1f}°C at {peak_time/60:.1f} min")
    if args.csv:
      csv_file.close()
      print(f"  CSV saved: {args.csv}")


if __name__ == '__main__':
  main()
