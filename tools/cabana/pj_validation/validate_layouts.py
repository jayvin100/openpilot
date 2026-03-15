#!/usr/bin/env python3

import argparse
import json
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


def inspect_layout(path: Path) -> dict:
  root = ET.parse(path).getroot()
  plots = root.findall(".//plot")
  return {
      "layout": path.name,
      "tabs": len(root.findall(".//Tab")),
      "plots": len(plots),
      "splitters": len(root.findall(".//DockSplitter")),
      "xy_plots": sum(1 for plot in plots if plot.get("mode") == "XYPlot"),
      "timeseries_plots": sum(1 for plot in plots if plot.get("mode") in {None, "TimeSeries"}),
      "custom_math_snippets": len(root.findall(".//customMathEquations/snippet")),
      "reactive_script_editor": any(
          plugin.get("ID") == "Reactive Script Editor" for plugin in root.findall(".//Plugins/plugin")
      ),
      "transforms": sorted({transform.get("name") for transform in root.findall(".//transform") if transform.get("name")}),
  }


def compare_contract(expected: dict, actual: dict) -> list[str]:
  mismatches = []
  for key in (
      "tabs",
      "plots",
      "splitters",
      "xy_plots",
      "timeseries_plots",
      "custom_math_snippets",
      "reactive_script_editor",
      "transforms",
  ):
    if expected[key] != actual[key]:
      mismatches.append(f"{key}: expected {expected[key]!r}, got {actual[key]!r}")
  return mismatches


def run_runtime_capture(
    repo_root: Path, wrapper: Path, layout: Path, output_png: Path, delay_ms: int, timeout_s: int
) -> tuple[int, str, bool]:
  env = os.environ.copy()
  env["CABANA_PJ_SCREENSHOT"] = str(output_png)
  env["CABANA_PJ_SCREENSHOT_EXIT"] = "1"
  env["CABANA_PJ_SCREENSHOT_DELAY_MS"] = str(delay_ms)
  cmd = [
      "xvfb-run",
      "-a",
      str(wrapper),
      "--pj",
      "--demo",
      "--pj-layout",
      str(layout),
  ]
  try:
    proc = subprocess.run(
        cmd,
        cwd=repo_root,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout_s,
        check=False,
    )
    return proc.returncode, proc.stdout, False
  except subprocess.TimeoutExpired as exc:
    output = exc.stdout or ""
    if isinstance(output, bytes):
      output = output.decode("utf-8", errors="replace")
    return 124, output, True


def run_layout_model_validation(repo_root: Path, contract_path: Path) -> tuple[int, str]:
  binary = repo_root / "tools/cabana/pj_validation/pj_layout_test"
  proc = subprocess.run(
      [str(binary), "--contract", str(contract_path)],
      cwd=repo_root,
      stdout=subprocess.PIPE,
      stderr=subprocess.STDOUT,
      text=True,
      check=False,
  )
  return proc.returncode, proc.stdout


def main() -> int:
  parser = argparse.ArgumentParser(description="Validate Cabana PJ layouts against the checked-in contract.")
  parser.add_argument("--contract", default="tools/cabana/pj_validation/layout_contract.json")
  parser.add_argument("--build", action="store_true", help="Build tools/cabana/_cabana before validation.")
  parser.add_argument("--model", action="store_true", help="Run the extracted pj_layout parser round-trip test.")
  parser.add_argument("--runtime", action="store_true", help="Run headless screenshot smoke validation for each layout.")
  parser.add_argument("--layout", action="append", default=[], help="Limit validation to specific layout filenames.")
  parser.add_argument("--output-dir", default="", help="Directory for generated screenshots and summary JSON.")
  parser.add_argument("--screenshot-delay-ms", type=int, default=12000)
  parser.add_argument("--timeout-sec", type=int, default=30)
  args = parser.parse_args()

  repo_root = Path(__file__).resolve().parents[3]
  contract_path = repo_root / args.contract
  contract = json.loads(contract_path.read_text(encoding="utf-8"))
  layout_dir = repo_root / contract["layout_dir"]
  expected_by_layout = {entry["layout"]: entry for entry in contract["layouts"]}

  requested_layouts = set(args.layout)
  layout_names = sorted(requested_layouts or expected_by_layout.keys())

  missing_from_disk = [name for name in layout_names if not (layout_dir / name).exists()]
  if missing_from_disk:
    for name in missing_from_disk:
      print(f"missing layout file: {name}", file=sys.stderr)
    return 2

  if args.build:
    build_targets = ["tools/cabana/_cabana"]
    if args.model:
      build_targets.append("tools/cabana/pj_validation/pj_layout_test")
    subprocess.run(
        ["scons", "-j1", *build_targets],
        cwd=repo_root,
        check=True,
    )

  if args.output_dir:
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
  else:
    output_dir = Path(tempfile.mkdtemp(prefix="cabana_pj_layouts_"))

  wrapper = repo_root / "tools/cabana/cabana"
  summary = {
      "contract": str(contract_path.relative_to(repo_root)),
      "output_dir": str(output_dir),
      "layout_model": None,
      "layouts": [],
  }

  failures = 0
  if args.model:
    exit_code, output = run_layout_model_validation(repo_root, contract_path)
    summary["layout_model"] = {
        "ok": exit_code == 0,
        "exit_code": exit_code,
        "log": output,
    }
    if exit_code != 0:
      failures += 1
    print(f"pj_layout_model: {'ok' if exit_code == 0 else 'fail'}")

  for layout_name in layout_names:
    layout_path = layout_dir / layout_name
    expected = expected_by_layout[layout_name]
    actual = inspect_layout(layout_path)
    mismatches = compare_contract(expected, actual)
    result = {
        "layout": layout_name,
        "contract_ok": not mismatches,
        "mismatches": mismatches,
        "actual": actual,
    }

    if args.runtime:
      screenshot_path = output_dir / f"{layout_path.stem}.png"
      exit_code, output, timed_out = run_runtime_capture(
          repo_root, wrapper, layout_path, screenshot_path, args.screenshot_delay_ms, args.timeout_sec
      )
      result["runtime_ok"] = exit_code == 0 and screenshot_path.exists()
      result["runtime_exit_code"] = exit_code
      result["runtime_timed_out"] = timed_out
      result["runtime_log"] = output
      result["screenshot"] = str(screenshot_path)
      if not result["runtime_ok"]:
        failures += 1
    if mismatches:
      failures += 1

    summary["layouts"].append(result)
    status_bits = ["contract=ok" if not mismatches else "contract=fail"]
    if args.runtime:
      status_bits.append("runtime=ok" if result["runtime_ok"] else "runtime=fail")
    print(f"{layout_name}: {' '.join(status_bits)}")

  summary_path = output_dir / "summary.json"
  summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
  print(f"summary: {summary_path}")
  return 1 if failures else 0


if __name__ == "__main__":
  raise SystemExit(main())
