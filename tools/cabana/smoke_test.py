#!/usr/bin/env python3

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import tempfile
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
CABANA_DIR = ROOT / "tools" / "cabana"
DEFAULT_OUTPUT_DIR = CABANA_DIR / "smoke_artifacts" / "latest"
DEFAULT_BASELINE_DIR = CABANA_DIR / "smoke_baseline"
REPLAY_HEADER = ROOT / "tools" / "replay" / "replay.h"


def run(cmd, **kwargs):
  return subprocess.run(cmd, check=True, text=True, **kwargs)


def find_free_display(start=1000, end=10000):
  for display in range(start, end):
    if not Path(f"/tmp/.X11-unix/X{display}").exists():
      return display
  raise RuntimeError("No free Xvfb display found")


def wait_for_socket(display, timeout=5.0):
  socket_path = Path(f"/tmp/.X11-unix/X{display}")
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    if socket_path.exists():
      return
    time.sleep(0.05)
  raise RuntimeError(f"Xvfb display :{display} did not become ready")


def ensure_cabana_built(build):
  binary = CABANA_DIR / "_cabana"
  if build or not binary.exists():
    run(["scons", f"-j{os.cpu_count() or 1}", "tools/cabana/_cabana"], cwd=ROOT)
  return binary


def demo_segment_route():
  try:
    header = REPLAY_HEADER.read_text()
  except OSError:
    return None

  match = re.search(r'#define\s+DEMO_ROUTE\s+"([^"]+)"', header)
  if not match:
    return None
  return f"{match.group(1)}/0"


def load_stats(path, log_path, timeout):
  deadline = time.monotonic() + timeout
  seen_log_size = 0
  while time.monotonic() < deadline:
    if path.exists():
      try:
        with path.open() as f:
          data = json.load(f)
        if data.get("ready"):
          return data
      except json.JSONDecodeError:
        pass
    if log_path.exists():
      with log_path.open() as f:
        f.seek(seen_log_size)
        for line in f:
          if line.startswith("CABANA_SMOKETEST_STATS "):
            payload = line.split(" ", 1)[1].strip()
            try:
              data = json.loads(payload)
            except json.JSONDecodeError:
              continue
            if data.get("ready"):
              path.write_text(json.dumps(data, indent=2))
              return data
        seen_log_size = f.tell()
    time.sleep(0.05)
  raise TimeoutError(f"Timed out waiting for ready stats at {path}")


def window_area(window_id, display):
  env = {"DISPLAY": display}
  result = subprocess.run(
    ["xwininfo", "-id", str(window_id)],
    env=env,
    check=True,
    capture_output=True,
    text=True,
  )
  width_match = re.search(r"Width:\\s+(\\d+)", result.stdout)
  height_match = re.search(r"Height:\\s+(\\d+)", result.stdout)
  if not width_match or not height_match:
    return 0
  return int(width_match.group(1)) * int(height_match.group(1))


def find_window_id(pid, display, timeout):
  deadline = time.monotonic() + timeout
  env = {"DISPLAY": display}
  while time.monotonic() < deadline:
    result = subprocess.run(
      ["xdotool", "search", "--onlyvisible", "--pid", str(pid), "--name", "."],
      env=env,
      capture_output=True,
      text=True,
    )
    if result.returncode == 0 and result.stdout.strip():
      window_ids = result.stdout.strip().splitlines()
      return max(window_ids, key=lambda window_id: window_area(window_id, display))
    time.sleep(0.05)
  raise TimeoutError(f"Timed out waiting for Cabana window for pid {pid}")


def capture_window(window_id, output_png, display):
  raw = output_png.with_suffix(".xwd")
  env = {"DISPLAY": display}
  run(["xwd", "-silent", "-id", str(window_id), "-out", str(raw)], env=env)
  try:
    run(["convert", str(raw), str(output_png)])
  except subprocess.CalledProcessError:
    with raw.open("rb") as raw_file:
      xwdtopnm = subprocess.Popen(["xwdtopnm"], stdin=raw_file, stdout=subprocess.PIPE)
      try:
        subprocess.run(["convert", "pnm:-", str(output_png)], stdin=xwdtopnm.stdout, check=True)
      finally:
        if xwdtopnm.stdout is not None:
          xwdtopnm.stdout.close()
      if xwdtopnm.wait() != 0:
        raise subprocess.CalledProcessError(xwdtopnm.returncode, ["xwdtopnm"])
  finally:
    raw.unlink(missing_ok=True)


def load_image_array(path):
  return np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)


def image_looks_painted(path, *, mean_threshold=10.0, stddev_threshold=5.0):
  arr = load_image_array(path)
  return float(arr.mean()) > mean_threshold and float(arr.std()) > stddev_threshold


def changed_pixels_pct(prev_arr, curr_arr, threshold):
  if prev_arr.shape != curr_arr.shape:
    return 100.0
  changed = np.abs(curr_arr.astype(np.int16) - prev_arr.astype(np.int16)).max(axis=2) > threshold
  return 100.0 * float(changed.mean())


def capture_steady_window(window_id, output_png, display, timeout=2.0, interval=0.1):
  deadline = time.monotonic() + timeout
  prev_capture = None
  prev_arr = None
  attempt = 0

  try:
    while time.monotonic() < deadline:
      capture_path = output_png.with_name(f"{output_png.stem}.capture_{attempt}.png")
      capture_window(window_id, capture_path, display)
      curr_arr = load_image_array(capture_path)
      mean = float(curr_arr.mean())
      stddev = float(curr_arr.std())
      painted = mean > 10.0 and stddev > 5.0

      if painted and prev_arr is not None and changed_pixels_pct(prev_arr, curr_arr, threshold=12) <= 0.05:
        capture_path.replace(output_png)
        return

      if prev_capture is not None:
        prev_capture.unlink(missing_ok=True)
      prev_capture = capture_path
      prev_arr = curr_arr
      attempt += 1
      time.sleep(interval)

    if prev_capture is None:
      raise RuntimeError("Failed to capture a Cabana window frame")
    prev_capture.replace(output_png)
  finally:
    for stale_capture in output_png.parent.glob(f"{output_png.stem}.capture_*.png"):
      stale_capture.unlink(missing_ok=True)


def wait_for_file(path, timeout=2.0):
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    if path.exists() and path.stat().st_size > 0:
      return
    time.sleep(0.02)
  raise TimeoutError(f"Timed out waiting for file {path}")


def make_diff_image(actual_path, baseline_path, diff_path, threshold):
  actual = Image.open(actual_path).convert("RGBA")
  baseline = Image.open(baseline_path).convert("RGBA")
  if actual.size != baseline.size:
    canvas = Image.new("RGBA", (max(actual.width, baseline.width) * 2, max(actual.height, baseline.height)), (20, 20, 20, 255))
    canvas.paste(baseline, (0, 0))
    canvas.paste(actual, (canvas.width // 2, 0))
    draw = ImageDraw.Draw(canvas)
    draw.text((10, 10), "baseline", fill=(255, 255, 255, 255))
    draw.text((canvas.width // 2 + 10, 10), "actual", fill=(255, 255, 255, 255))
    canvas.save(diff_path)
    return {
      "changed_pixels_pct": 100.0,
      "size_mismatch": True,
      "baseline_size": baseline.size,
      "actual_size": actual.size,
    }

  diff = ImageChops.difference(actual, baseline)
  diff_arr = np.asarray(diff, dtype=np.uint8)
  mask = diff_arr[..., :3].max(axis=2) > threshold
  changed_pixels = int(mask.sum())
  changed_pixels_pct = 100.0 * changed_pixels / mask.size

  overlay = np.asarray(actual, dtype=np.uint8).copy()
  if changed_pixels:
    overlay[mask, 0] = 255
    overlay[mask, 1] = (overlay[mask, 1] * 0.2).astype(np.uint8)
    overlay[mask, 2] = (overlay[mask, 2] * 0.2).astype(np.uint8)

    ys, xs = np.nonzero(mask)
    x1, x2 = int(xs.min()), int(xs.max())
    y1, y2 = int(ys.min()), int(ys.max())
  else:
    x1 = y1 = x2 = y2 = 0

  diff_img = Image.fromarray(overlay, mode="RGBA")
  if changed_pixels:
    draw = ImageDraw.Draw(diff_img)
    draw.rectangle((x1, y1, x2, y2), outline=(255, 255, 0, 255), width=3)
  diff_img.save(diff_path)

  return {
    "changed_pixels_pct": changed_pixels_pct,
    "changed_pixels": changed_pixels,
    "size_mismatch": False,
    "bbox": [x1, y1, x2, y2] if changed_pixels else None,
  }


def terminate_process(proc):
  if proc.poll() is not None:
    return
  try:
    os.killpg(proc.pid, signal.SIGKILL)
  except ProcessLookupError:
    return
  proc.wait(timeout=1)


def parse_args():
  parser = argparse.ArgumentParser(description="Fast Cabana smoke test")
  parser.add_argument("--build", action="store_true", help="Build Cabana before running")
  parser.add_argument("--cabana-bin", help="Explicit Cabana binary or wrapper path")
  parser.add_argument("--timeout", type=float, default=30.0, help="Seconds to wait for smoke readiness")
  parser.add_argument("--size", default="1600x900", help="Window size for deterministic screenshots")
  parser.add_argument("--threshold", type=int, default=12, help="Per-pixel diff threshold")
  parser.add_argument("--max-diff-pct", type=float, default=0.5, help="Fail if changed pixels exceed this percentage")
  parser.add_argument("--output-dir", default=str(DEFAULT_OUTPUT_DIR), help="Artifact directory")
  parser.add_argument("--baseline-dir", default=str(DEFAULT_BASELINE_DIR), help="Baseline directory")
  parser.add_argument("--update-baseline", action="store_true", help="Replace the baseline with the current screenshot")
  parser.add_argument("--data-dir", help="Pass through to Cabana --data_dir")
  parser.add_argument("--route", help="Explicit route instead of --demo")
  return parser.parse_args()


def main():
  args = parse_args()

  output_dir = Path(args.output_dir).resolve()
  baseline_dir = Path(args.baseline_dir).resolve()
  output_dir.mkdir(parents=True, exist_ok=True)
  baseline_dir.mkdir(parents=True, exist_ok=True)

  actual_png = output_dir / "actual.png"
  baseline_copy_png = output_dir / "baseline.png"
  diff_png = output_dir / "diff.png"
  stats_json = output_dir / "stats.json"
  summary_json = output_dir / "summary.json"
  log_path = output_dir / "cabana.log"
  baseline_png = baseline_dir / "baseline.png"
  baseline_stats = baseline_dir / "stats.json"

  for stale_artifact in [actual_png, baseline_copy_png, diff_png, stats_json, summary_json, log_path]:
    stale_artifact.unlink(missing_ok=True)

  profile_dir = Path(tempfile.mkdtemp(prefix="cabana_smoke_", dir=output_dir))
  xvfb_proc = None
  cabana_proc = None

  try:
    display_num = find_free_display()
    display = f":{display_num}"
    xvfb_proc = subprocess.Popen(
      ["Xvfb", display, "-screen", "0", f"{args.size}x24"],
      stdout=subprocess.DEVNULL,
      stderr=subprocess.DEVNULL,
      start_new_session=True,
    )
    wait_for_socket(display_num)

    binary = Path(args.cabana_bin).resolve() if args.cabana_bin else ensure_cabana_built(args.build)
    env = os.environ.copy()
    env.update({
      "DISPLAY": display,
      "HOME": str(profile_dir),
      "XDG_CONFIG_HOME": str(profile_dir / ".config"),
      "LIBGL_ALWAYS_SOFTWARE": "1",
      "CABANA_SMOKETEST": "1",
      "CABANA_SMOKETEST_SIZE": args.size,
      "CABANA_SMOKETEST_STATS": str(stats_json),
      "CABANA_SMOKETEST_SCREENSHOT": str(actual_png),
    })

    cmd = [str(binary), "--no-vipc"]
    if args.route:
      cmd.append(args.route)
    else:
      route = demo_segment_route()
      if route:
        cmd.append(route)
      else:
        cmd.append("--demo")
    if args.data_dir:
      cmd.extend(["--data_dir", args.data_dir])

    with log_path.open("w") as log_file:
      cabana_proc = subprocess.Popen(
        cmd,
        cwd=ROOT,
        env=env,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        start_new_session=True,
      )

      stats = None
      try:
        stats = load_stats(stats_json, log_path, args.timeout)
      except TimeoutError:
        if cabana_proc.poll() is not None:
          raise RuntimeError(f"Cabana exited early with code {cabana_proc.returncode}. See {log_path}")
        raise

    window_id = None
    try:
      wait_for_file(actual_png, timeout=1.0)
    except TimeoutError:
      window_id = find_window_id(cabana_proc.pid, display, timeout=5.0)
      capture_steady_window(window_id, actual_png, display)
    else:
      if not image_looks_painted(actual_png):
        if window_id is None:
          window_id = find_window_id(cabana_proc.pid, display, timeout=5.0)
        capture_steady_window(window_id, actual_png, display)

    if args.update_baseline or not baseline_png.exists():
      shutil.copy2(actual_png, baseline_png)
      if stats_json.exists():
        shutil.copy2(stats_json, baseline_stats)
    shutil.copy2(baseline_png, baseline_copy_png)

    diff_metrics = make_diff_image(actual_png, baseline_png, diff_png, args.threshold)
    summary = {
      "command": cmd,
      "display": display,
      "window_id": window_id,
      "stats": stats,
      "diff": diff_metrics,
      "baseline": str(baseline_png),
      "baseline_copy": str(baseline_copy_png),
      "actual": str(actual_png),
      "diff_image": str(diff_png),
      "log": str(log_path),
      "pass": diff_metrics["changed_pixels_pct"] <= args.max_diff_pct,
    }
    summary_json.write_text(json.dumps(summary, indent=2))

    print(f"actual:   {actual_png}")
    print(f"baseline: {baseline_copy_png}")
    print(f"diff:     {diff_png}")
    print(f"stats:    {stats_json}")
    print(f"log:      {log_path}")
    print(f"changed:  {diff_metrics['changed_pixels_pct']:.4f}%")

    if not summary["pass"]:
      raise SystemExit(1)
  finally:
    if cabana_proc is not None:
      terminate_process(cabana_proc)
    if xvfb_proc is not None:
      terminate_process(xvfb_proc)
    shutil.rmtree(profile_dir, ignore_errors=True)


if __name__ == "__main__":
  main()
