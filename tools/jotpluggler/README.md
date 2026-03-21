# JotPlugger

`jotpluggler` is the product-side log plotting app.

Current scope:
- native C++ parsing of PlotJuggler layout XML
- native C++ route-wide extraction of direct numeric `/service/path` series
- GLFW + Dear ImGui + ImPlot rendering
- real PlotJuggler tab support with a docked workspace
- route open/reload controls, shared timeline navigation, and pane/curve selection in the UI
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
./tools/jotpluggler/jotpluggler --demo
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

`--demo` opens the default `longitudinal` layout on the bundled demo route.
The app also accepts a positional `route` like the other log tools, plus `--data-dir`.
Multi-tab PlotJuggler layouts now render as real app tabs, and the sidebar can target panes, browse route series, add curves, and toggle visibility.
Plots share a real x-range with follow/reset controls and route-backed direct curve rendering.
Curves without direct sampled data are shown as unsupported instead of being faked.

## Validation

The screenshot validator is intentionally separate and disposable. It now lives under [tools/jotpluggler_validator](/home/batman/threepilot/tools/jotpluggler_validator).
