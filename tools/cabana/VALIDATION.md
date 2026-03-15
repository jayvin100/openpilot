# Cabana Validation

Cabana has two local headless validation runners:

- `smoke_test.py`: the fast startup screenshot and diff check
- `validation_suite.py`: the broader 3-minute readiness gate

Both runners launch Cabana under `Xvfb`, use deterministic route state, and write artifacts plus timing data.

## Quick Start

Create or refresh the baseline:

```bash
./.venv/bin/python tools/cabana/smoke_test.py --timeout 90 --update-baseline
./.venv/bin/python tools/cabana/validation_suite.py --update-baseline
```

Run the validation check:

```bash
./.venv/bin/python tools/cabana/smoke_test.py --timeout 90
./.venv/bin/python tools/cabana/validation_suite.py --continue-on-failure
```

Useful focused runs:

```bash
./.venv/bin/python tools/cabana/validation_suite.py --scenario startup_visual
./.venv/bin/python tools/cabana/validation_suite.py --scenario input_fuzz
```

On this machine, the smoke test is about 2 seconds per run and the full suite is about 100 seconds.

## Default Behavior

By default, the smoke test:

- builds `tools/cabana/_cabana` if needed
- runs headlessly under `Xvfb`
- uses only the first segment of the demo route
- disables VIPC with `--no-vipc`
- writes artifacts to `tools/cabana/smoke_artifacts/latest`
- stores the baseline in `tools/cabana/smoke_baseline`

By default, the full suite:

- uses the same demo segment route
- writes artifacts to `tools/cabana/validation_artifacts/latest`
- stores baselines in `tools/cabana/validation_baseline`
- runs all scenarios serially with one isolated temp profile per scenario
- records screenshot diffs, dialog captures, and timing metrics per scenario

## Artifacts

Each run writes:

- `actual.png`: current screenshot
- `baseline.png`: copied baseline used for comparison
- `diff.png`: highlighted diff image
- `stats.json`: timing and UI-gap metrics from Cabana
- `summary.json`: pass/fail plus paths and diff metrics
- `cabana.log`: captured process log

## Useful Flags

Smoke runner:

- `--update-baseline`: replace the saved screenshot baseline
- `--route <route>`: run against a specific route instead of the default demo segment
- `--data-dir <dir>`: load route data from a local cache
- `--size <WxH>`: change the deterministic window size
- `--threshold <n>`: change the per-pixel diff threshold
- `--max-diff-pct <pct>`: change the smoke screenshot failure threshold
- `--build`: force a rebuild before running

Full suite:

- `--update-baseline`: refresh screenshot and metric baselines
- `--scenario <name>`: run only one scenario
- `--continue-on-failure`: keep running after the first failure
- `--fuzz-duration <seconds>`: change the input fuzz budget
- `--seed <n>`: set the fuzz seed
- `--route <route>`: run against a specific route instead of the default demo segment
- `--data-dir <dir>`: load route data from a local cache
- `--size <WxH>`: change the deterministic window size
- `--threshold <n>`: change the screenshot diff threshold
- `--build`: force a rebuild before running

## Reading Results

The smoke runner passes when:

- Cabana reaches the ready state before timeout
- the screenshot diff stays below `--max-diff-pct`

The full suite passes when every scenario in `summary.json` passes.

The most useful numbers in `stats.json` are:

- `route_load_ms`
- `first_events_merged_ms`
- `auto_paused_ms`
- `steady_state_ms`
- `max_ui_gap_ms`

## Notes

- Use `--data-dir` if you want the fastest and most repeatable local runs.
- If the baseline is intentionally changing, update it first and rerun once to confirm the diff goes back to near zero.

## Full 3-Minute Suite

`validation_suite.py` is the release gate for the rewrite.

Passing it should mean:

- core Cabana workflows are usable end to end
- the app does not hang or visibly freeze during common interactions
- startup and interaction latency stay close to the current Qt baseline
- the rewrite still looks broadly correct in the major UI states

It does not prove literal "100%" correctness. It is a strong local readiness gate.

## Runner Model

The full suite keeps the same external black-box structure as the smoke test:

- one top-level local runner in `tools/cabana`
- one isolated `Xvfb` display per worker
- one isolated temp profile directory per scenario
- local fixtures only
- X11 input injection for clicks, drags, and typing
- screenshot and diff artifacts for each steady state
- the same scenario language for both Qt Cabana and the future ImGui rewrite

## Time Budget

Current wall-clock runtime on this machine is about `97s`.

Target wall-clock budget on this machine:

| Phase | Budget |
|---|---:|
| fixture prewarm | 15s |
| scenario execution | 120s |
| diff and report writing | 15s |
| margin | 30s |
| total | 180s |

The smoke test should remain the fast precheck. The full suite should usually run after smoke passes.

## Scenario Set

The current suite runs these scenarios:

| Scenario | Goal | Main checks | Typical runtime |
|---|---|---|---:|
| `startup_visual` | startup and default layout | screenshot diff, startup stats, widget/menu manifest | 3s |
| `core_workflow` | main interaction path | select message, filter, chart open, zoom, seek, playback stall checks | 30s |
| `edit_save_reload` | edit persistence | edit message, save DBC, relaunch, verify persistence | 35s |
| `session_restore` | restart continuity | relaunch and restore selected message plus charts | 6s |
| `video_sanity` | camera/video path | video widget visible, playback advances, screenshot diff | 25s |
| `invalid_dbc` | expected failure path | invalid DBC raises the right dialog and then recovers | 5s |
| `input_fuzz` | bounded stability fuzz | random clicks, scrolls, plot toggles, seeks, filter edits, stall checks | 20s |

## Fixtures

The current implementation uses:

- `demo/0`: the first demo segment for all scenarios
- the route-matched DBC discovered from Cabana state or the fingerprint map
- a temp copied DBC for save/reload and fuzz
- a generated invalid DBC file for the error-path scenario

All source fixtures are local and read-only. The save/reload and fuzz scenarios copy the DBC into a per-run temp directory.

## Required Assertions

Every scenario asserts both behavior and timing.

Behavior assertions:

- app window appears
- no unexpected dialogs or crashes
- expected panel or widget becomes visible
- expected state changes after each action
- expected steady-state screenshot stays within diff threshold

Timing assertions:

- startup steady-state timing
- input-to-visible-change latency
- playback stall metrics
- max UI gap metrics from Cabana instrumentation

## Screenshot Policy

Do not require full pixel parity for every step.

The implemented suite uses screenshots in three ways:

- one strong startup baseline screenshot
- one or two steady-state screenshots per major workflow
- one generated `diff.png` per captured screenshot

Recommended diff policy:

- strict thresholds for the current Qt baseline
- tolerant thresholds for Qt vs ImGui comparison
- mask or ignore known dynamic regions only when justified

## Freeze Detection

Use both internal and external freeze signals.

Internal signals from Cabana:

- `max_ui_gap_ms`
- `ui_gaps_over_16ms`
- `ui_gaps_over_33ms`
- `ui_gaps_over_50ms`
- `ui_gaps_over_100ms`

External signals from the runner:

- sample a dynamic ROI during playback
- fail if the ROI does not visibly change for more than `500 ms` when playback is expected to advance
- measure click-to-visible-change and drag-to-visible-change latency

## Pass Gate

The rewrite should only be considered ready when all of the following are true:

- smoke passes
- every full-suite scenario passes
- no crashes, hangs, or timeouts occur
- no scenario exceeds its wall-clock budget by more than 20%
- startup and interaction metrics are within 20% of the current Qt baseline on this machine
- no new stall class appears relative to baseline
- major screenshot diffs stay within threshold

For freeze metrics, compare against baseline rather than using absolute zero-tolerance thresholds. The current Qt baseline already has some event-loop gaps.

## Current Findings

On the current Qt Cabana baseline, the suite currently passes:

- `startup_visual`
- `core_workflow`
- `session_restore`
- `video_sanity`
- `invalid_dbc`

It currently fails:

- `edit_save_reload`: Cabana saves an edited DBC that it then cannot reopen.
- `input_fuzz`: a bounded random interaction run with seed `1337` triggers a `Failed to load DBC file` dialog.
