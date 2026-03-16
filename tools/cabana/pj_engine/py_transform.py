#!/usr/bin/env python3
"""Batch math transform evaluator for Cabana PJ custom math.

Protocol: reads JSON commands from stdin, writes JSON results to stdout.
Each command: {"global": "...", "function": "...", "time": [...], "value": [...], "v1": [...], ...}
Result: {"result": [...]} or {"error": "..."}
"""
import json
import math
import sys

def evaluate_snippet(global_code, function_code, time_arr, value_arr, extra_vars):
  """Evaluate a math snippet over arrays of data points."""
  # Build the function
  ns = {"math": math, "abs": abs, "min": min, "max": max}
  if global_code.strip():
    exec(global_code, ns)

  # Wrap function code: def calc(time, value, v1, v2, ...): <code>
  args = ["time", "value"] + [f"v{i+1}" for i in range(len(extra_vars))]
  func_code = f"def calc({', '.join(args)}):\n"
  for line in function_code.strip().split('\n'):
    func_code += f"  {line}\n"
  func_code += "  return None\n"  # fallback if no return

  exec(func_code, ns)
  calc = ns["calc"]

  results = []
  n = len(time_arr)
  for i in range(n):
    t = time_arr[i]
    v = value_arr[i]
    extras = [extra_vars[f"v{j+1}"][i] if i < len(extra_vars.get(f"v{j+1}", [])) else 0.0
              for j in range(len(extra_vars))]
    try:
      r = calc(t, v, *extras)
      if r is not None:
        results.append(r)
      else:
        results.append(float('nan'))
    except Exception:
      results.append(float('nan'))

  return results


def main():
  for line in sys.stdin:
    line = line.strip()
    if not line:
      continue
    try:
      cmd = json.loads(line)
      global_code = cmd.get("global", "")
      function_code = cmd.get("function", "")
      time_arr = cmd.get("time", [])
      value_arr = cmd.get("value", [])
      extra_vars = {k: v for k, v in cmd.items() if k.startswith("v") and k[1:].isdigit()}

      result = evaluate_snippet(global_code, function_code, time_arr, value_arr, extra_vars)
      print(json.dumps({"result": result}), flush=True)
    except Exception as e:
      print(json.dumps({"error": str(e)}), flush=True)


if __name__ == "__main__":
  main()
