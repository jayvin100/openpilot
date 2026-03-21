# JotPlugger

`jotpluggler` is the product-side log plotting app.

Current scope:
- native C++ parsing of PlotJuggler layout XML
- native C++ route-wide extraction of direct numeric `/service/path` series
- schema-indexed fast route loading with parallel segment workers
- GLFW + Dear ImGui + ImPlot rendering
- real PlotJuggler tab support with a docked workspace
- route open/reload controls, layout load/save controls, and pane/curve selection in the UI
- hierarchical timeseries browser for adding route-backed curves to the active pane
- fixed-size screenshot export for deterministic rendering and debugging

Current non-goals:
- PlotJuggler baseline management
- screenshot diffing
- `Xvfb` orchestration
- fake baseline-copy modes
- custom math / Lua snippets

## Build

```bash
scons tools/jotpluggler/jotpluggler
```

## Run

Open the app interactively:

```bash
./tools/jotpluggler/jotpluggler
```

Open a specific PlotJuggler layout:

```bash
./tools/jotpluggler/jotpluggler --layout longitudinal --demo
```

Export a screenshot without opening a visible window:

```bash
./tools/jotpluggler/jotpluggler \
  --layout longitudinal \
  --data-dir /tmp/routes \
  --width 1600 \
  --height 900 \
  --output /tmp/longitudinal.png \
  0000010a--a51155e496/0
```

If you omit `--layout`, JotPlugger starts with a blank one-pane workspace.
If you omit the route too, JotPlugger starts as an empty shell like PlotJuggler.
`--demo` only provides the bundled demo route.
The app also accepts a positional `route` like the other log tools, plus `--data-dir`.
Interactive `--show` mode now opens the window first and loads route data in the background.
Use `--sync-load` when you want deterministic blocking startup, for example screenshot capture or validation.
Multi-tab PlotJuggler layouts now render as real app tabs, and the sidebar exposes PJ-style layout and timeseries sections.
Plots use route-backed direct curve rendering with a shared `t=0` time base and a playback/timeline bar aligned to the plot region.
Curves without direct sampled data are shown as unsupported instead of being faked.

Useful loader knobs:
- `JOTP_LOAD_WORKERS=<n>` overrides the automatic worker count for route loading.
- `JOTP_COMPARE_SLOW_FAST=1` runs a one-shot internal slow-vs-fast loader equivalence check before returning route data.

## Validation

The screenshot validator is intentionally separate and disposable. It now lives under [tools/jotpluggler_validator](/home/batman/threepilot/tools/jotpluggler_validator).
