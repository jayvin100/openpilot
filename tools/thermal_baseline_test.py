#!/usr/bin/env python3
"""
Thermal Baseline Test for comma mici device.

Cycles between idle and full load over 12 hours, recording every thermal zone,
power draw, CPU/GPU state, and fan speed every 5 seconds.

Two load modes run back-to-back per cycle:
  1. stress-ng synthetic load (30 min) — worst-case thermal envelope
  2. realistic openpilot load (30 min) — actual onroad processes (modeld, camerad, etc.)
  3. cooldown idle (30 min)

Cycle: [stress 30m] → [cooldown 30m] → [openpilot 30m] → [cooldown 30m] → repeat
Total cycle = 2h, so 6 full cycles in 12h.

Output: CSV at /data/thermal_baseline_{timestamp}.csv
        Summary JSON at /data/thermal_baseline_{timestamp}_summary.json
        Status JSON at /data/thermal_test_status.json (for remote monitoring)

Usage:
  python3 thermal_baseline_test.py [--duration-hours 12] [--load-minutes 30] [--cool-minutes 30] [--sample-interval 5]
  python3 thermal_baseline_test.py --mode stress    # stress-ng only
  python3 thermal_baseline_test.py --mode openpilot # openpilot only
  python3 thermal_baseline_test.py --mode both      # alternating (default)
"""

import argparse
import csv
import json
import os
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path


# ── Thermal zone configuration (all zones found on mici SDM845) ──────────────

THERMAL_ZONES = {
    # CPU cores (silver cluster = little, gold = big)
    "cpu0_silver": "cpu0-silver-usr",
    "cpu1_silver": "cpu1-silver-usr",
    "cpu2_silver": "cpu2-silver-usr",
    "cpu3_silver": "cpu3-silver-usr",
    "cpu0_gold": "cpu0-gold-usr",
    "cpu1_gold": "cpu1-gold-usr",
    "cpu2_gold": "cpu2-gold-usr",
    "cpu3_gold": "cpu3-gold-usr",
    # GPU
    "gpu0": "gpu0-usr",
    "gpu1": "gpu1-usr",
    # DSP / compute
    "dsp_hvx": "compute-hvx-usr",
    # Memory
    "ddr": "ddr-usr",
    # PMIC
    "pm8998": "pm8998_tz",
    "pm8005": "pm8005_tz",
    # SoC / platform
    "aoss0": "aoss0-usr",
    "aoss1": "aoss1-usr",
    "camera": "camera-usr",
    "mmss": "mmss-usr",
    "mdm_dsp": "mdm-dsp-usr",
    "mdm_core": "mdm-core-usr",
    "wlan": "wlan-usr",
    # L3 cache
    "kryo_l3_0": "kryo-l3-0-usr",
    "kryo_l3_1": "kryo-l3-1-usr",
    # LMH (limits management hardware)
    "lmh_dcvs_00": "lmh-dcvs-00",
    "lmh_dcvs_01": "lmh-dcvs-01",
    # XO thermistor (board-level ambient proxy)
    "xo_therm": "xo-therm-adc",
    # External thermistors
    "pa_therm1": "pa-therm1-adc",
    # mici-specific
    "intake": "intake",
    "exhaust": "exhaust",
    "gnss": "gnss",
    "bottom_soc": "bottom_soc",
}

# Map zone type name → zone number (populated at startup)
_zone_map: dict[str, int] = {}


def _build_zone_map():
    """Scan sysfs once to map zone type names to zone numbers."""
    base = Path("/sys/devices/virtual/thermal")
    for d in base.iterdir():
        if not d.name.startswith("thermal_zone"):
            continue
        try:
            zone_type = (d / "type").read_text().strip()
            zone_num = int(d.name.removeprefix("thermal_zone"))
            _zone_map[zone_type] = zone_num
        except (OSError, ValueError):
            continue


def read_thermal_zone(zone_type: str) -> float | None:
    """Read a thermal zone in °C. Returns None if unavailable."""
    zone_num = _zone_map.get(zone_type)
    if zone_num is None:
        return None
    try:
        raw = Path(f"/sys/devices/virtual/thermal/thermal_zone{zone_num}/temp").read_text().strip()
        val = int(raw)
        if abs(val) > 200_000:
            return None  # invalid reading
        if abs(val) > 1000:
            result = val / 1000.0
        else:
            result = float(val)
        # Filter disconnected thermistors (-40°C is a common sentinel)
        if result <= -30.0:
            return None
        return result
    except (OSError, ValueError):
        return None


def read_all_temps() -> dict[str, float | None]:
    """Read all configured thermal zones."""
    return {name: read_thermal_zone(zone_type) for name, zone_type in THERMAL_ZONES.items()}


# ── Power & system readings ──────────────────────────────────────────────────

def read_file_int(path: str) -> int | None:
    try:
        return int(Path(path).read_text().strip())
    except (OSError, ValueError):
        return None


def read_power_draw_w() -> float | None:
    val = read_file_int("/sys/class/hwmon/hwmon1/power1_input")
    return val / 1e6 if val is not None else None


def read_som_power_w() -> float | None:
    v = read_file_int("/sys/class/power_supply/bms/voltage_now")
    i = read_file_int("/sys/class/power_supply/bms/current_now")
    if v is not None and i is not None:
        return v * i / 1e12
    return None


def read_cpu_freqs() -> list[int]:
    freqs = []
    for i in range(8):
        f = read_file_int(f"/sys/devices/system/cpu/cpu{i}/cpufreq/scaling_cur_freq")
        freqs.append(f if f is not None else 0)
    return freqs


def read_gpu_freq() -> int | None:
    return read_file_int("/sys/class/kgsl/kgsl-3d0/gpuclk")


def read_gpu_busy_pct() -> int | None:
    try:
        val = Path("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage").read_text().strip()
        return int(val.split("%")[0].strip())
    except (OSError, ValueError, IndexError):
        return None


def read_fan_rpm() -> int | None:
    """Read fan RPM. Try sysfs first, fall back to cereal deviceState."""
    # Try sysfs hwmon
    for h in range(10):
        val = read_file_int(f"/sys/class/hwmon/hwmon{h}/fan1_input")
        if val is not None:
            return val
    # Fan is controlled via panda — read from shared memory if available
    return _last_fan_data.get("rpm")


def read_fan_from_cereal() -> dict:
    """Read fan speed and RPM from cereal deviceState (non-blocking)."""
    try:
        import importlib
        if importlib.util.find_spec("capnp") is None:
            # Try system venv
            import sys
            sys.path.insert(0, "/usr/local/venv/lib/python3.12/site-packages")
        import cereal.messaging as messaging
        if not hasattr(read_fan_from_cereal, "_sm"):
            read_fan_from_cereal._sm = messaging.SubMaster(["deviceState"])
        sm = read_fan_from_cereal._sm
        sm.update(0)  # non-blocking
        if sm.updated["deviceState"]:
            ds = sm["deviceState"]
            return {
                "rpm": getattr(ds, "fanSpeedRpm", None),
                "desired_pct": getattr(ds, "fanSpeedPercentDesired", None),
            }
    except Exception:
        pass
    return {}


# Cache for fan data from cereal
_last_fan_data: dict = {}


def read_cpu_usage() -> list[float]:
    """Per-core CPU usage via /proc/stat delta over 0.1s."""
    def parse_stat():
        cores = {}
        with open("/proc/stat") as f:
            for line in f:
                if line.startswith("cpu") and not line.startswith("cpu "):
                    parts = line.split()
                    name = parts[0]
                    times = [int(x) for x in parts[1:]]
                    idle = times[3] + (times[4] if len(times) > 4 else 0)
                    total = sum(times)
                    cores[name] = (idle, total)
        return cores

    s1 = parse_stat()
    time.sleep(0.1)
    s2 = parse_stat()

    usage = []
    for i in range(8):
        name = f"cpu{i}"
        if name in s1 and name in s2:
            d_idle = s2[name][0] - s1[name][0]
            d_total = s2[name][1] - s1[name][1]
            if d_total > 0:
                usage.append(round(100.0 * (1.0 - d_idle / d_total), 1))
            else:
                usage.append(0.0)
        else:
            usage.append(0.0)
    return usage


# ── Load generation ──────────────────────────────────────────────────────────

_load_procs: list[subprocess.Popen] = []


def bring_cores_online():
    """Bring all CPU cores online (gold cluster may be offlined by power management)."""
    for i in range(8):
        online_path = f"/sys/devices/system/cpu/cpu{i}/online"
        try:
            with open(online_path) as f:
                if f.read().strip() == "0":
                    # Need root to bring cores online
                    subprocess.run(["sudo", "sh", "-c", f"echo 1 > {online_path}"],
                                   capture_output=True, timeout=5)
                    print(f"[LOAD] Brought cpu{i} online")
        except (OSError, subprocess.TimeoutExpired):
            pass

    # Verify
    online = []
    for i in range(8):
        try:
            with open(f"/sys/devices/system/cpu/cpu{i}/online") as f:
                if f.read().strip() == "1":
                    online.append(i)
        except OSError:
            online.append(i)  # cpu0 has no online file, always on
    print(f"[LOAD] Online cores: {online}")
    return online


def start_stress_load():
    """Start stress-ng MAX power load on ALL cores.

    Brings gold cores online first (they may be offlined by power management),
    then pins one heavy worker per core to guarantee all 8 cores are maxed.
    Also runs memory bandwidth and matrix stressors.
    """
    global _load_procs

    online_cores = bring_cores_online()
    procs = []

    # Pin one heavy worker per online core
    for core in online_cores:
        p = subprocess.Popen([
            "taskset", "-c", str(core),
            "stress-ng", "--cpu", "1", "--cpu-method", "matrixprod", "--timeout", "0",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
        procs.append(p)

    # Memory bandwidth stress (pushes DDR temps)
    p = subprocess.Popen([
        "stress-ng", "--vm", "2", "--vm-bytes", "256M", "--vm-method", "all",
        "--timeout", "0",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
    procs.append(p)

    # Matrix multiply (pushes memory subsystem + cache)
    p = subprocess.Popen([
        "stress-ng", "--matrix", "2", "--matrix-size", "512", "--timeout", "0",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
    procs.append(p)

    _load_procs = procs
    print(f"[LOAD] Started MAX stress on {len(online_cores)} cores + VM + matrix ({len(procs)} procs)")


def start_openpilot_load():
    """Start realistic openpilot-equivalent load using stress-ng.

    Real openpilot onroad uses ~3-4W SOM: modeld saturates 2 gold cores + GPU,
    camerad uses 1-2 silver cores, dmonitoringmodeld uses 1 gold core + GPU,
    plus misc processes. We match this with pinned workers:
      - 2 gold cores (4,5) at 100% — modeld CPU portion
      - 2 silver cores (0,1) at 100% — camerad + misc
      - 1 gold core (6) at 100% — dmonitoringmodeld
      - 1 VM worker — memory bandwidth from frame buffers
    Total: 5 cores loaded (~60-65% overall), matching real onroad.
    """
    global _load_procs

    online_cores = bring_cores_online()
    procs = []

    # modeld on gold cluster
    for core in [4, 5]:
        if core in online_cores:
            p = subprocess.Popen([
                "taskset", "-c", str(core),
                "stress-ng", "--cpu", "1", "--cpu-method", "matrixprod", "--timeout", "0",
            ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
            procs.append(p)

    # camerad on silver cluster
    for core in [0, 1]:
        if core in online_cores:
            p = subprocess.Popen([
                "taskset", "-c", str(core),
                "stress-ng", "--cpu", "1", "--cpu-method", "matrixprod", "--timeout", "0",
            ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
            procs.append(p)

    # dmonitoringmodeld on gold
    if 6 in online_cores:
        p = subprocess.Popen([
            "taskset", "-c", "6",
            "stress-ng", "--cpu", "1", "--cpu-method", "matrixprod", "--timeout", "0",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
        procs.append(p)

    # memory bandwidth (frame buffers + video encoding)
    p = subprocess.Popen([
        "stress-ng", "--vm", "1", "--vm-bytes", "128M", "--timeout", "0",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid)
    procs.append(p)

    _load_procs = procs
    loaded = [c for c in [0, 1, 4, 5, 6] if c in online_cores]
    print(f"[LOAD] Started openpilot-equivalent: cores {loaded} loaded + VM")


def stop_load():
    """Stop all load processes (stress-ng or openpilot)."""
    global _load_procs

    for proc in _load_procs:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except (OSError, ProcessLookupError):
            pass
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            try:
                proc.kill()
                proc.wait(timeout=3)
            except Exception:
                pass

    _load_procs = []

    # Kill any orphaned stress-ng
    subprocess.run(["pkill", "-9", "stress-ng"], capture_output=True)
    # Kill any orphaned openpilot test processes (but NOT the main manager)
    # We only kill processes we started — use process group


# ── CSV logging ──────────────────────────────────────────────────────────────

def get_csv_headers() -> list[str]:
    headers = [
        "timestamp",
        "elapsed_s",
        "cycle_num",
        "phase",          # "load" or "cooldown"
        "load_mode",      # "stress", "openpilot", or "none"
        "phase_elapsed_s",
    ]
    # All thermal zones
    headers += [f"temp_{name}_C" for name in THERMAL_ZONES]
    # Derived
    headers += ["temp_max_C", "temp_cpu_max_C", "temp_gpu_max_C"]
    # Power
    headers += ["power_system_W", "power_som_W"]
    # CPU
    headers += [f"cpu{i}_freq_khz" for i in range(8)]
    headers += [f"cpu{i}_usage_pct" for i in range(8)]
    # GPU
    headers += ["gpu_freq_hz", "gpu_busy_pct"]
    # Fan
    headers += ["fan_rpm", "fan_desired_pct"]
    return headers


def collect_sample(start_time: float, cycle_num: int, phase: str,
                   load_mode: str, phase_start: float) -> dict:
    now = time.time()
    temps = read_all_temps()
    cpu_freqs = read_cpu_freqs()
    cpu_usage = read_cpu_usage()

    # Exclude LMH zones from max — they report thermal limit setpoints, not real temps
    LMH_ZONES = {"lmh_dcvs_00", "lmh_dcvs_01"}
    valid_temps = [v for k, v in temps.items() if v is not None and k not in LMH_ZONES]
    cpu_temps = [temps.get(f"cpu{i}_silver") for i in range(4)]
    cpu_temps += [temps.get(f"cpu{i}_gold") for i in range(4)]
    cpu_valid = [t for t in cpu_temps if t is not None]
    gpu_temps = [temps.get("gpu0"), temps.get("gpu1")]
    gpu_valid = [t for t in gpu_temps if t is not None]

    row = {
        "timestamp": datetime.fromtimestamp(now, tz=timezone.utc).isoformat(),
        "elapsed_s": round(now - start_time, 1),
        "cycle_num": cycle_num,
        "phase": phase,
        "load_mode": load_mode,
        "phase_elapsed_s": round(now - phase_start, 1),
    }

    for name in THERMAL_ZONES:
        row[f"temp_{name}_C"] = round(temps[name], 2) if temps[name] is not None else ""

    row["temp_max_C"] = round(max(valid_temps), 2) if valid_temps else ""
    row["temp_cpu_max_C"] = round(max(cpu_valid), 2) if cpu_valid else ""
    row["temp_gpu_max_C"] = round(max(gpu_valid), 2) if gpu_valid else ""

    p_sys = read_power_draw_w()
    p_som = read_som_power_w()
    row["power_system_W"] = round(p_sys, 3) if p_sys is not None else ""
    row["power_som_W"] = round(p_som, 3) if p_som is not None else ""

    for i in range(8):
        row[f"cpu{i}_freq_khz"] = cpu_freqs[i]
    for i in range(8):
        row[f"cpu{i}_usage_pct"] = cpu_usage[i]

    row["gpu_freq_hz"] = read_gpu_freq() or ""
    row["gpu_busy_pct"] = read_gpu_busy_pct() or ""

    # Update fan data from cereal
    global _last_fan_data
    fan_update = read_fan_from_cereal()
    if fan_update:
        _last_fan_data.update(fan_update)
    row["fan_rpm"] = _last_fan_data.get("rpm") or read_fan_rpm() or ""
    row["fan_desired_pct"] = _last_fan_data.get("desired_pct", "")

    return row


# ── Summary generation ───────────────────────────────────────────────────────

@dataclass
class PhaseSummary:
    phase: str
    load_mode: str
    cycle: int
    start_temp_max: float = 0.0
    end_temp_max: float = 0.0
    peak_temp_max: float = 0.0
    peak_cpu: float = 0.0
    peak_gpu: float = 0.0
    avg_power_w: float = 0.0
    duration_s: float = 0.0
    samples: int = 0
    _power_sum: float = field(default=0.0, repr=False)

    def update(self, row: dict):
        self.samples += 1
        t_max = row.get("temp_max_C")
        t_cpu = row.get("temp_cpu_max_C")
        t_gpu = row.get("temp_gpu_max_C")
        p_sys = row.get("power_system_W")

        if isinstance(t_max, (int, float)):
            if self.samples == 1:
                self.start_temp_max = t_max
            self.end_temp_max = t_max
            self.peak_temp_max = max(self.peak_temp_max, t_max)
        if isinstance(t_cpu, (int, float)):
            self.peak_cpu = max(self.peak_cpu, t_cpu)
        if isinstance(t_gpu, (int, float)):
            self.peak_gpu = max(self.peak_gpu, t_gpu)
        if isinstance(p_sys, (int, float)):
            self._power_sum += p_sys

    def finalize(self):
        if self.samples > 0:
            self.avg_power_w = round(self._power_sum / self.samples, 3)

    def to_dict(self):
        return {
            "phase": self.phase, "load_mode": self.load_mode, "cycle": self.cycle,
            "start_temp_max_C": self.start_temp_max, "end_temp_max_C": self.end_temp_max,
            "peak_temp_max_C": self.peak_temp_max, "peak_cpu_C": self.peak_cpu,
            "peak_gpu_C": self.peak_gpu, "avg_power_W": self.avg_power_w,
            "duration_s": self.duration_s, "samples": self.samples,
        }


# ── Status file for remote monitoring ────────────────────────────────────────

def write_status(status_path: str, start_time: float, cycle_num: int, phase: str,
                 load_mode: str, phase_elapsed: float, total_cycles: int, last_row: dict,
                 duration_hours: float):
    elapsed = time.time() - start_time
    status = {
        "updated": datetime.now(tz=timezone.utc).isoformat(),
        "elapsed_h": round(elapsed / 3600, 2),
        "remaining_h": round(max(0, duration_hours - elapsed / 3600), 2),
        "cycle": cycle_num,
        "total_cycles_expected": total_cycles,
        "phase": phase,
        "load_mode": load_mode,
        "phase_elapsed_s": round(phase_elapsed, 0),
        "temp_max_C": last_row.get("temp_max_C", ""),
        "temp_cpu_max_C": last_row.get("temp_cpu_max_C", ""),
        "temp_gpu_max_C": last_row.get("temp_gpu_max_C", ""),
        "power_system_W": last_row.get("power_system_W", ""),
        "fan_rpm": last_row.get("fan_rpm", ""),
        "status": "running",
    }
    try:
        with open(status_path, "w") as f:
            json.dump(status, f, indent=2)
    except OSError:
        pass


# ── Phase runner ─────────────────────────────────────────────────────────────

def run_phase(writer, csvfile, start_time: float, cycle: int, phase: str,
              load_mode: str, duration_s: float, total_duration_s: float,
              sample_interval: float, status_path: str, total_cycles: int,
              duration_hours: float, running_flag: list) -> PhaseSummary:
    """Run one phase (load or cooldown), collecting samples."""
    phase_start = time.time()
    summary = PhaseSummary(phase=phase, load_mode=load_mode, cycle=cycle)
    sample_count = 0

    while running_flag[0] and (time.time() - phase_start) < duration_s and (time.time() - start_time) < total_duration_s:
        row = collect_sample(start_time, cycle, phase, load_mode, phase_start)
        writer.writerow(row)
        sample_count += 1
        summary.update(row)

        if sample_count % 12 == 0:  # every ~60s
            t = row.get("temp_max_C", "?")
            p = row.get("power_system_W", "?")
            phs = round(time.time() - phase_start)
            cpu = row.get("temp_cpu_max_C", "?")
            gpu = row.get("temp_gpu_max_C", "?")
            fan = row.get("fan_rpm", "?")
            print(f"  [{phs:>4}s] max={t}°C  cpu={cpu}°C  gpu={gpu}°C  pwr={p}W  fan={fan}rpm")

        if sample_count % 10 == 0:
            csvfile.flush()

        write_status(status_path, start_time, cycle, phase, load_mode,
                     time.time() - phase_start, total_cycles, row, duration_hours)

        # Align to sample interval grid
        next_sample = phase_start + (((time.time() - phase_start) // sample_interval) + 1) * sample_interval
        sleep_time = next_sample - time.time()
        if sleep_time > 0:
            time.sleep(sleep_time)

    summary.duration_s = time.time() - phase_start
    summary.finalize()
    return summary


# ── Main test loop ───────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Thermal baseline test for mici")
    parser.add_argument("--duration-hours", type=float, default=12.0)
    parser.add_argument("--load-minutes", type=float, default=30.0)
    parser.add_argument("--cool-minutes", type=float, default=30.0)
    parser.add_argument("--sample-interval", type=float, default=5.0)
    parser.add_argument("--output-dir", type=str, default="/data")
    parser.add_argument("--mode", choices=["stress", "openpilot", "both"], default="both",
                        help="Load mode: stress-ng only, openpilot processes only, or both alternating")
    args = parser.parse_args()

    # Build thermal zone map
    _build_zone_map()
    available = {n: z for n, z in THERMAL_ZONES.items() if z in _zone_map}
    missing = {n: z for n, z in THERMAL_ZONES.items() if z not in _zone_map}
    print(f"[INIT] Found {len(available)}/{len(THERMAL_ZONES)} thermal zones")
    if missing:
        print(f"[INIT] Missing zones: {list(missing.keys())}")

    # File paths
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = os.path.join(args.output_dir, f"thermal_baseline_{ts}.csv")
    summary_path = os.path.join(args.output_dir, f"thermal_baseline_{ts}_summary.json")
    status_path = os.path.join(args.output_dir, "thermal_test_status.json")

    print(f"[INIT] CSV: {csv_path}")
    print(f"[INIT] Mode: {args.mode}")

    duration_s = args.duration_hours * 3600
    load_s = args.load_minutes * 60
    cool_s = args.cool_minutes * 60

    # Calculate cycle structure based on mode
    if args.mode == "both":
        # stress load → cool → openpilot load → cool
        phases_per_cycle = [
            ("load", "stress", load_s),
            ("cooldown", "none", cool_s),
            ("load", "openpilot", load_s),
            ("cooldown", "none", cool_s),
        ]
        cycle_s = 2 * load_s + 2 * cool_s
    else:
        # single mode: load → cool
        phases_per_cycle = [
            ("load", args.mode, load_s),
            ("cooldown", "none", cool_s),
        ]
        cycle_s = load_s + cool_s

    total_cycles = max(1, int(duration_s / cycle_s))
    print(f"[INIT] Duration: {args.duration_hours}h, {total_cycles} cycles, cycle={cycle_s/60:.0f}m")
    print(f"[INIT] Phases per cycle: {[(p, m, f'{d/60:.0f}m') for p, m, d in phases_per_cycle]}")

    headers = get_csv_headers()
    csvfile = open(csv_path, "w", newline="")
    writer = csv.DictWriter(csvfile, fieldnames=headers, extrasaction="ignore")
    writer.writeheader()
    csvfile.flush()

    phase_summaries: list[PhaseSummary] = []
    start_time = time.time()
    total_samples = 0
    running_flag = [True]  # mutable for signal handler

    def handle_signal(sig, frame):
        print(f"\n[SIGNAL] Received {sig}, stopping gracefully...")
        running_flag[0] = False

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    # Clean start
    stop_load()

    # Baseline
    print("[INIT] Collecting baseline...")
    baseline = collect_sample(start_time, 0, "baseline", "none", start_time)
    writer.writerow(baseline)
    csvfile.flush()
    total_samples += 1
    print(f"[INIT] Baseline — max: {baseline.get('temp_max_C', '?')}°C, "
          f"cpu: {baseline.get('temp_cpu_max_C', '?')}°C, "
          f"gpu: {baseline.get('temp_gpu_max_C', '?')}°C")

    try:
        cycle = 1
        while running_flag[0] and (time.time() - start_time) < duration_s:
            for phase, load_mode, phase_dur in phases_per_cycle:
                if not running_flag[0] or (time.time() - start_time) >= duration_s:
                    break

                elapsed_h = (time.time() - start_time) / 3600
                print(f"\n[CYCLE {cycle}/{total_cycles}] [{elapsed_h:.1f}h] "
                      f"=== {phase.upper()} ({load_mode}) === ({phase_dur/60:.0f}m)")

                # Start/stop load
                if phase == "load":
                    if load_mode == "stress":
                        start_stress_load()
                    elif load_mode == "openpilot":
                        start_openpilot_load()

                summary = run_phase(
                    writer, csvfile, start_time, cycle, phase, load_mode,
                    phase_dur, duration_s, args.sample_interval,
                    status_path, total_cycles, args.duration_hours, running_flag,
                )

                if phase == "load":
                    stop_load()

                phase_summaries.append(summary)
                total_samples += summary.samples

                print(f"  Phase done: peak={summary.peak_temp_max}°C, "
                      f"start={summary.start_temp_max}°C→end={summary.end_temp_max}°C, "
                      f"avg_pwr={summary.avg_power_w}W")

            cycle += 1

    except Exception as e:
        print(f"\n[ERROR] {e}")
        import traceback
        traceback.print_exc()
    finally:
        stop_load()
        csvfile.flush()
        csvfile.close()

        elapsed = time.time() - start_time
        summary = {
            "test": "thermal_baseline",
            "device": "mici",
            "ip": "192.168.60.136",
            "started": datetime.fromtimestamp(start_time, tz=timezone.utc).isoformat(),
            "finished": datetime.now(tz=timezone.utc).isoformat(),
            "elapsed_hours": round(elapsed / 3600, 2),
            "total_samples": total_samples,
            "cycles_completed": cycle - 1,
            "mode": args.mode,
            "config": {
                "duration_hours": args.duration_hours,
                "load_minutes": args.load_minutes,
                "cool_minutes": args.cool_minutes,
                "sample_interval_s": args.sample_interval,
            },
            "thermal_zones_available": len(available),
            "thermal_zones_missing": list(missing.keys()),
            "phases": [ps.to_dict() for ps in phase_summaries],
            "csv_file": csv_path,
        }
        with open(summary_path, "w") as f:
            json.dump(summary, f, indent=2)

        try:
            with open(status_path, "w") as f:
                json.dump({"status": "finished", "elapsed_h": round(elapsed / 3600, 2),
                           "samples": total_samples, "csv": csv_path}, f, indent=2)
        except OSError:
            pass

        print(f"\n{'='*60}")
        print(f"[DONE] Test completed after {elapsed/3600:.1f} hours")
        print(f"[DONE] Samples: {total_samples}")
        print(f"[DONE] CSV: {csv_path}")
        print(f"[DONE] Summary: {summary_path}")
        print(f"{'='*60}")


if __name__ == "__main__":
    main()
