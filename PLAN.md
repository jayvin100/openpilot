# Cabana Plotting Migration Plan

## Goals
- Make PlotJuggler functionality a native part of Cabana, not an embedded app-within-app.
- Keep a single Cabana process and a single cohesive product surface.
- Ensure the UI thread never performs unbounded application work such as route parsing, large data merges, transform recalculation, or synchronous full-plot rebuilds.
- Support every current layout under `tools/plotjuggler/layouts`.
- Support the workflows we actively use:
  - curve hide/show
  - drag and drop curves onto plots
  - layout splitting
  - tabs
  - Lua editor and custom math
  - save and edit layout
  - XY plots
- Image export is out of scope.

## Locked Scope
- No runtime fallback path is required.
- Every current layout file is in scope:
  - `CAN-bus-debug.xml`
  - `camera-timings.xml`
  - `can-states.xml`
  - `controls_mismatch_debug.xml`
  - `gps.xml`
  - `gps_vs_llk.xml`
  - `locationd_debug.xml`
  - `longitudinal.xml`
  - `max-torque-debug.xml`
  - `system_lag_debug.xml`
  - `thermal_debug.xml`
  - `torque-controller.xml`
  - `tuning.xml`
  - `ublox-debug.xml`

## Immediate Direction
- Pause broad PlotJuggler subtree trimming.
- Only remove obvious dead assets or metadata when they are clearly unrelated to:
  - layout parsing and saving
  - transforms and Lua
  - plot interaction
  - curve list behavior
  - XY plots
- The current embedded PJ code is reference material during extraction, not the target architecture.

## Architecture Direction

### Keep Long-Term
- Data model and series storage:
  - `tools/cabana/plotjuggler/plotjuggler_base/include/PlotJuggler/plotdata.h`
- Openpilot parser logic:
  - `tools/cabana/plotjuggler/plotjuggler_plugins/DataLoadRlog/rlog_parser.cpp`
- Transform system:
  - `tools/cabana/plotjuggler/plotjuggler_base/include/PlotJuggler/transform_function.h`
  - `tools/cabana/plotjuggler/plotjuggler_app/transforms/*`
- Lua and reactive math:
  - `tools/cabana/plotjuggler/plotjuggler_base/src/reactive_function.cpp`
  - `tools/cabana/plotjuggler/plotjuggler_plugins/ToolboxLuaEditor/*`

### Replace Long-Term
- App shell and app-level state orchestration:
  - `tools/cabana/plotjuggler/plotjuggler_app/mainwindow.cpp`
- QWidget/Qwt plot UI:
  - `tools/cabana/plotjuggler/plotjuggler_app/plotwidget.cpp`
  - `tools/cabana/plotjuggler/plotjuggler_base/src/plotwidget_base.cpp`
- PJ app chrome and widget glue:
  - curve list panel
  - tabbed docking shell
  - plot editor widgets

### Core Rule
- The UI thread may only:
  - submit commands
  - swap immutable snapshots
  - schedule repaint
  - paint visible state
- The UI thread may not:
  - parse route data
  - merge large series maps
  - run transforms or Lua over full datasets
  - synchronously rebuild all plots
  - synchronously replot hidden or non-visible content

## Target Modules

### `pj_layout`
- Cabana-owned layout parser and serializer.
- Reads and writes the existing PJ XML format.
- Produces plain structs, not widgets.

Expected models:
- `LayoutModel`
- `TabModel`
- `PlotModel`
- `CurveBinding`
- `AxisConfig`
- `TrackerConfig`
- `GridConfig`
- `SplitTree`
- `TransformConfig`
- `LuaEditorState`

### `pj_engine`
- Worker-thread-owned engine.
- Owns:
  - `PlotDataMapRef`
  - replay ingestion
  - parser invocation
  - transform execution
  - Lua/custom math execution
  - visibility state
  - plot membership state
  - visible-range decimation / sampling caches

Engine inputs:
- `load_layout`
- `save_layout`
- `append_segments`
- `seek`
- `pause`
- `set_visible_range`
- `set_curve_visibility`
- `move_curve_to_plot`
- `split_plot`
- `create_tab`
- `remove_tab`
- `update_transform`
- `update_lua`

Engine outputs:
- immutable plot snapshots
- immutable curve tree snapshots
- immutable tab/layout snapshots
- status / perf signals

### `cabana_plot_ui`
- Cabana-owned UI and interaction layer.
- Consumes immutable snapshots from `pj_engine`.
- Implements:
  - curve list
  - plot containers
  - tabs
  - splitters
  - drag and drop
  - Lua/custom math editor host

## Phases

### Phase 0: Freeze Contract
Deliverables:
- Supported layout list locked to all current files.
- Supported feature list locked to current in-use workflows.
- Golden validation artifacts recorded for every layout.
- Manual interaction checklist defined.
- Checked-in layout contract:
  - `tools/cabana/pj_validation/layout_contract.json`
- Repeatable validator:
  - `tools/cabana/pj_validation/validate_layouts.py`

Validation to capture for each layout:
- screenshot
- plot count
- tab count
- whether XY plots are present
- whether Lua/custom math is required
- whether split layouts are present

Manual workflow checklist:
- hide and show curve
- drag curve into plot
- split plot horizontally
- split plot vertically
- create tab
- switch tabs
- open Lua editor
- edit custom math
- save layout
- reload saved layout
- XY plot interaction

### Phase 1: Extract Layout Layer
Goal:
- Parse and save PJ layouts without depending on PJ widgets.

Source references:
- `tools/cabana/plotjuggler/plotjuggler_app/mainwindow.cpp`
- `tools/cabana/plotjuggler/plotjuggler_app/plotwidget.cpp`

Deliverables:
- `pj_layout` library
- tests for every layout file
- round-trip layout save/load tests

Exit criteria:
- All current layout XML files load into `LayoutModel`.
- Layout round-trip preserves the fields we use.

### Phase 2: Extract Engine Layer
Goal:
- Move data ingestion, transforms, and Lua execution off the UI thread.

Deliverables:
- `pj_engine` worker thread
- async command queue
- immutable snapshot outputs
- perf instrumentation around engine stages

Exit criteria:
- Route parsing and transform execution are no longer initiated from the UI thread.
- UI thread never directly owns mutable route-scale series state.

### Phase 3: Build Read-Only Cabana Plot Surface
Goal:
- Render all current layouts from `LayoutModel + PlotSnapshot`.

Notes:
- This is Cabana-owned UI.
- Qwt may be used as a temporary rendering backend if it accelerates parity, but PJ app widgets are not the architecture target.

Deliverables:
- Cabana plot containers
- Cabana tabs and split hierarchy
- tracker
- axes, grid, legend, XY mode
- curve list bound to engine snapshots

Exit criteria:
- All current layouts render in Cabana without the embedded PJ `MainWindow`.
- Seek, pause, tracker, and zoom work against engine snapshots.

### Phase 4: Port Interactive Features
Goal:
- Reproduce the workflows we use on top of the new Cabana-owned UI.

Feature order:
1. hide/show curves
2. drag and drop curves into plots
3. layout splitting
4. tabs
5. save and edit layout
6. Lua editor and custom math
7. XY plots

Exit criteria:
- Manual workflow checklist passes for the new path.

### Phase 5: Remove Legacy PJ App Layer
Goal:
- Delete the embedded PJ app shell and widget glue once parity exists.

Expected removals:
- embedded `MainWindow` host path
- legacy adapter in `tools/cabana/plotjuggler/cabana_plotjuggler_widget.cpp`
- PJ app-layer docking and app-shell logic no longer used by Cabana

Exit criteria:
- `cabana --pj` runs entirely on Cabana-owned UI backed by extracted layout and engine layers.

## Validation Plan

### Automated
1. Build:
```bash
scons -j4 tools/cabana/_cabana
```

2. Fast local validation:
```bash
python3 tools/cabana/pj_validation/validate_layouts.py \
  --build \
  --model \
  --runtime \
  --layout locationd_debug.xml \
  --output-dir /tmp/cabana_pj_fast
```

3. Full layout validation:
```bash
python3 tools/cabana/pj_validation/validate_layouts.py \
  --build \
  --model \
  --runtime \
  --output-dir /tmp/cabana_pj_layouts
```

Validator notes:
- Runtime screenshots are readiness-based by default. Do not add a fixed delay unless debugging.
- Runtime jobs auto-scale conservatively and currently cap at `2` by default for stability.
- Runtime failures are retried once serially to filter out rare teardown flakes while still failing deterministic regressions.
- Override the default concurrency with `--runtime-jobs <n>` if you explicitly want a higher-stress run.
- Limit validation to a subset with repeated `--layout <file>`.

4. Perf smoke:
```bash
CABANA_PJ_PERF=1 tools/cabana/cabana --pj --demo --pj-layout <layout>
```

5. Layout round-trip tests:
- load XML
- serialize
- reload
- compare normalized model

6. Red-team sanity check:
- Make a temporary copy of one layout, remove or rename a plot/curve reference, point `--contract` at a temp contract using that copied layout, and verify the validator returns non-zero with `contract=fail` or `pj_layout_model: fail`.
- The runtime part may still pass in this case; the contract/model layers are expected to catch the regression.

### Manual
- curve hide/show
- drag/drop curve to plot
- split plot
- create and switch tabs
- open and use Lua editor
- save layout and reload it
- verify XY layouts behave correctly

## Qwt Strategy
- Qwt is the current plotting/rendering backend used by PJ.
- It provides:
  - plot widgets
  - curves
  - grid
  - axes
  - zoom, pan, magnifier
  - markers
  - legend
- It is a QWidget-style synchronous rendering stack.
- Because of that, Qwt is not the place to put route parsing, large series merges, or transform execution.

Decision:
- Do not replace Qwt first.
- First extract layout and engine boundaries.
- Then measure the new Cabana-owned UI with Qwt as a temporary renderer.
- If Qwt still causes unacceptable UI stalls after engine extraction, replace only the rendering layer.

## What Not To Do
- Do not keep optimizing the current embedded PJ `MainWindow` as the long-term architecture.
- Do not continue broad subtree trimming until layout and engine boundaries exist.
- Do not accept UI-thread ownership of route-scale mutable data.
