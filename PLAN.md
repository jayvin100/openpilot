# Cabana ImGui Rewrite Plan

## Core Tenets

- No validation code in the app. Validation must live entirely outside the production app and its production codepaths.
- Full parity is the goal. The parallel app must preserve every existing Cabana feature and workflow.
- No Qt in the parallel app. The rewrite must remove Qt entirely rather than wrapping or embedding existing Qt UI.
- Legacy `tools/cabana` remains intact during the rewrite. The new app is developed in parallel until it is ready to replace it.
- The rewrite should optimize for fast iteration by getting the skeleton, file layout, and UI structure right first, then wiring functionality into that structure.
- Shared logic should live below the UI layer so it can be validated directly without adding instrumentation to the app.
- Validation must use normal app behavior only: real CLI entrypoints, cached routes on disk, black-box launch/interaction, external artifact checks, and direct tests of shared non-UI libraries.

## Goal

Rewrite Cabana as a fully non-Qt ImGui/ImPlot application while maintaining complete functional parity with the current Qt Cabana.

The target outcome is:

- legacy `tools/cabana` functionality is fully available in the new app
- the new app has no Qt dependency
- validation is fully self-contained outside the app
- the migration path is incremental, but the final product is a full replacement

## Rewrite Model

The right model is the same broad rewrite strategy used for `origin/jotpluggler`:

- build a new app in parallel instead of porting widget-by-widget in place
- separate core data/model/state from UI rendering
- keep the first implementation passes focused on establishing the permanent structure
- use the old Cabana only as behavioral reference, not as an embedded dependency

For Cabana specifically, the toolkit differs from `jotpluggler`:

- `jotpluggler` demonstrates the rewrite pattern
- `imgui-cabana` provides the dependency/bootstrap direction for Cabana
- the actual Cabana rewrite should be a new C++ ImGui/ImPlot app, not a DearPyGui app and not a hybrid Qt app

## Non-Goals

- No hybrid Qt/ImGui UI.
- No temporary validation hooks in app code.
- No reduced-scope product that permanently omits legacy Cabana features.
- No one-off shell that later needs to be reorganized into a real architecture.

## Production App Structure

Create a new parallel app directory, for example:

```text
tools/cabana_imgui/
  SConscript
  cabana_imgui
  main.cc

  app/
    application.h
    application.cc
    cli.h
    cli.cc
    actions.h
    actions.cc

  core/
    types.h
    settings.h
    settings.cc
    session.h
    session.cc
    selection.h
    selection.cc
    command_stack.h
    command_stack.cc

  dbc/
    dbc_types.h
    dbc_types.cc
    dbc_file.h
    dbc_file.cc
    dbc_store.h
    dbc_store.cc
    fingerprint_map.h
    fingerprint_map.cc

  sources/
    source.h
    source_factory.h
    source_factory.cc
    replay_source.h
    replay_source.cc
    device_source.h
    device_source.cc
    panda_source.h
    panda_source.cc
    socketcan_source.h
    socketcan_source.cc

  model/
    can_store.h
    can_store.cc
    decode_cache.h
    decode_cache.cc
    history_model.h
    history_model.cc
    chart_model.h
    chart_model.cc
    video_model.h
    video_model.cc

  ui/
    shell.h
    shell.cc
    dockspace.h
    dockspace.cc
    theme.h
    theme.cc
    menus.h
    menus.cc
    popups.h
    popups.cc

    panes/
      messages_pane.h
      messages_pane.cc
      detail_pane.h
      detail_pane.cc
      binary_pane.h
      binary_pane.cc
      signals_pane.h
      signals_pane.cc
      history_pane.h
      history_pane.cc
      charts_pane.h
      charts_pane.cc
      video_pane.h
      video_pane.cc
      stream_selector_pane.h
      stream_selector_pane.cc
      settings_pane.h
      settings_pane.cc
      dbc_manager_pane.h
      dbc_manager_pane.cc
      find_signal_pane.h
      find_signal_pane.cc
      find_similar_bits_pane.h
      find_similar_bits_pane.cc
      route_info_pane.h
      route_info_pane.cc
      help_overlay.h
      help_overlay.cc

  PARITY.md
```

## Layering Rules

- `app/` owns startup, shutdown, global wiring, and top-level control flow.
- `core/` owns app state, settings, session state, selection state, and undo/redo abstractions.
- `dbc/` owns DBC parsing, storage, source mapping, and fingerprint-to-DBC logic.
- `sources/` owns replay and live ingress.
- `model/` owns decoded CAN state, message history, chart data, and video state.
- `ui/` owns rendering and interaction only.

Strict rules:

- No Qt includes or Qt types anywhere in the new tree.
- No validation-only code in the new tree.
- No UI pane should be the sole owner of business logic that needs validation.
- Shared logic should be callable without launching the UI.

## UI Structure

The new UI should preserve the current Cabana mental model while using ImGui docking instead of Qt docking:

- top menu bar: `File`, `Edit`, `View`, `Tools`, `Help`
- left dock: `Messages`
- center workspace: `Detail`
- inside detail: `Binary`, `Signals`, and `History`
- right dock: `Video` over `Charts`
- floating/modal windows for stream selection, settings, DBC management, find tools, route info, and help

Functional equivalents to preserve:

- stream open/close flows
- replay/demo/auto route load
- device, panda, socketcan, and zmq/live flows
- DBC load/save/save-as/clipboard/opendbc handling
- fingerprint-based DBC autoload
- message search/filter/sort/inactive handling
- binary view interactions
- signal view and chart interactions
- chart splitting/tabs/layout persistence
- video sync and camera selection
- CSV export
- undo/redo
- recent files and session restore
- fullscreen/help/settings

ImGui docking should replace custom Qt docking/floating behavior while preserving user-visible capability.

## Parity Scope

Parity means all current user-facing Cabana functionality is represented in the new app. This includes at least:

- route replay loading and playback controls
- demo route support
- auto-source route loading
- live device streaming
- panda streaming
- socketcan streaming
- zmq/device address streaming
- qcam, ecam, dcam handling
- route metadata and fingerprint display
- automatic DBC selection from fingerprint
- manual DBC open/new/save/save-as
- DBC clipboard import/export
- DBC management per bus/source
- message list filtering, sorting, visibility behavior
- message selection and tab restoration
- binary view
- signal view
- signal editing workflows
- message editing workflows
- history log
- charts, tabs, split/merge behavior, and layout persistence
- video display and synchronization
- export to CSV
- settings persistence
- recent files
- session restore
- find signal
- find similar bits
- help overlay
- undo/redo command stack behavior

`tools/cabana_imgui/PARITY.md` should track these features explicitly, with each item marked:

- `not started`
- `skeleton only`
- `wired`
- `parity validated`

## Execution Phases

### Phase -1: Validator Baseline (Current Cabana)

Before writing any imgui code, establish the behavioral ground truth of the current Qt Cabana by launching it under xvfb and driving it with xdotool.

#### Toolchain

- `xvfb-run` — headless X server, launches cabana with no physical display
- `xdotool` — find windows, send clicks, keystrokes, and window management
- `scrot` / `import` (imagemagick) — capture screenshots
- Python 3 test scripts — orchestrate launch, interaction, capture, and comparison

#### What to capture

Smoke (app lifecycle):

- boot to window visible (xdotool search --name)
- demo route loads (--demo flag)
- UI stays alive for 10+ seconds without crash
- clean exit (send WM_DELETE, check return code 0)
- startup time (wall clock from launch to window mapped)

Screenshots (visual golden baselines):

- initial window after demo route load
- message list populated
- message selected → detail/binary/signals tabs visible
- chart added for a signal
- DBC loaded (fingerprint auto-select or manual open)

Workflow interactions:

- click a message in the list → detail pane updates
- switch between Binary/Signals/History tabs
- add a chart via double-click or menu
- open DBC file via File menu
- export CSV via menu → verify output file on disk
- open Find Signal dialog
- open Settings dialog

File output captures:

- CSV export content for a known message/signal
- DBC save-as output for a loaded+modified DBC
- settings file content after changing a preference

#### Structure

```text
tools/cabana_imgui_validation/
  conftest.py              # shared fixtures: xvfb launch, window wait, screenshot helpers
  helpers.py               # xdotool wrappers, process management, image comparison
  smoke/
    test_boot.py           # launch, window appears, stays alive, clean exit
    test_demo_route.py     # --demo flag loads route, messages populate
  workflows/
    test_message_select.py # click message, verify detail pane updates
    test_tabs.py           # switch Binary/Signals/History tabs
    test_chart.py          # add chart for signal, screenshot
    test_dbc_load.py       # load DBC, verify signals appear
    test_export_csv.py     # export CSV, verify file contents
    test_find_signal.py    # open find dialog, interact
  goldens/
    boot.png
    demo_loaded.png
    message_selected.png
    chart_added.png
    ...
  compare.py               # image diff (imagemagick compare or pixel-diff threshold)
```

#### Approach

Each test script:

1. Starts cabana under `xvfb-run` with known args (e.g. `--demo`, `--dbc <file>`)
2. Waits for the main window via `xdotool search --sync --name`
3. Optionally interacts via `xdotool key`, `xdotool mousemove --window`, `xdotool click`
4. Captures screenshots via `import -window <wid>` or `scrot`
5. Captures file outputs (CSV, DBC, settings) from temp directories
6. Compares against goldens or validates content programmatically
7. Sends `xdotool key alt+F4` or `kill`, verifies clean exit

Tests run with pytest. No app modifications required.

#### Deliverable

- golden screenshots and file outputs for the current Qt Cabana
- a repeatable test suite that can later be pointed at the imgui cabana binary
- confidence that we know exactly what the current app does before replacing it

### Phase 0: Bootstrap

- create the new target and launcher
- wire ImGui/ImPlot build support
- create the permanent folder structure
- create empty app/state/model/ui skeleton files
- create `PARITY.md`

Deliverable:

- the new app builds and launches an empty docked shell

### Phase 1: Shell and Global State

- implement top-level app object
- implement settings/session state objects
- implement dockspace and pane registration
- implement menus, popups, modal window framework, and basic layout persistence
- stub every major pane so the full app skeleton exists immediately

Deliverable:

- the shell reflects the final intended structure even before data is wired

### Phase 2: Replay Backbone

- implement replay source abstraction
- connect route loading and playback state
- build CAN event store and message inventory
- implement DBC storage and fingerprint mapping
- wire message selection and timeline state

Deliverable:

- replay route can load into the new shell with real underlying state

### Phase 3: Read-Only Core Parity

- wire `Messages`
- wire `Binary`
- wire `Signals`
- wire `History`
- wire `Charts`
- wire DBC loading and automatic source mapping

Deliverable:

- the new app can inspect a route end-to-end in read-only mode with the primary Cabana workflows functioning

### Phase 4: Video and Live Sources

- wire video model and panes
- wire qcam/ecam/dcam behavior
- wire device source
- wire panda source
- wire socketcan source
- wire zmq/live behaviors

Deliverable:

- the new app supports the full set of stream sources and synchronized video workflows

### Phase 5: Editing and Tooling Parity

- wire DBC editing
- wire signal/message editing
- wire undo/redo stack behavior
- wire CSV export
- wire DBC clipboard flows
- wire find tools
- wire route info
- wire help overlay
- finish session restoration and recent file handling

Deliverable:

- the new app has feature-complete parity with legacy Cabana

### Phase 6: Replacement Readiness

- close remaining parity gaps
- validate platform/build requirements
- ensure legacy functionality is matched or intentionally improved
- prepare the cutover plan from `tools/cabana` to the new app

Deliverable:

- the new app is ready to replace legacy Cabana

## Validation Architecture

Validation is completely self-contained outside the app. The primary mechanism is black-box: launch the app under `xvfb-run`, drive it with `xdotool`, capture screenshots and file outputs, compare against goldens.

### Directory

```text
tools/cabana_imgui_validation/
  conftest.py
  helpers.py
  compare.py
  smoke/
  workflows/
  parity/
  perf/
  goldens/
```

### Validation Rules

- No production file in `tools/cabana_imgui/` or `tools/cabana/` should exist solely for validation.
- All validation launches the app as a black box under xvfb. No app-only test modes.
- Validation may also directly test shared production libraries below the UI layer.
- Validation uses cached routes on disk and existing route slices.
- State isolation is external via environment variables, temp config directories, and temp output directories.

### xvfb + xdotool Pattern

Every test follows the same pattern:

1. `xvfb-run` launches the app binary with known CLI args
2. `xdotool search --sync --name` waits for the window
3. `xdotool` sends clicks, keystrokes, and window commands
4. `import -window <wid>` captures screenshots
5. File outputs (CSV, DBC, settings) are written to temp directories and verified
6. `xdotool key alt+F4` or `kill -TERM` ends the app; return code is checked

This pattern works identically for legacy Qt Cabana and the new imgui Cabana — only the binary path changes.

### Validation Types

#### `smoke/`

App lifecycle checks:

- app boots to visible window
- cached route or demo route opens and populates
- UI stays alive for a stable interval
- clean exit on close

#### `workflows/`

UI automation for real workflows:

- select message → detail pane updates
- switch tabs (Binary, Signals, History)
- add/remove charts
- open/save DBC
- export CSV
- open find dialogs
- settings changes persist

#### `parity/`

Side-by-side comparison between legacy and new app:

- same route → same message inventory (screenshot + file output)
- same message selected → same binary/signal view
- same DBC loaded → same decoded values
- same CSV export → identical file content
- same session state → same restore behavior

Parity tests capture outputs from both binaries and diff them.

#### `perf/`

External timing and process metrics only:

- startup time (launch to window mapped)
- route load time
- memory footprint (RSS via /proc)
- CPU usage over steady-state interval

No in-app performance instrumentation.

## Fast Iteration Loop

The iteration loop should rely on:

- cached routes already available on disk
- building only the new app target
- the same xvfb/xdotool test suite used for the baseline, pointed at the new binary

The expected development rhythm is:

1. build `tools/cabana_imgui`
2. run `pytest tools/cabana_imgui_validation/smoke/` against the new binary
3. run `pytest tools/cabana_imgui_validation/workflows/` for feature-level checks
4. run `pytest tools/cabana_imgui_validation/parity/` to diff against legacy goldens

The validator binary is controlled by an environment variable (e.g. `CABANA_BIN`), defaulting to the legacy Qt cabana. Set `CABANA_BIN` to the imgui binary to validate the new app against the same goldens.

Fast iteration comes from architecture, not app instrumentation:

- thin UI layer
- reusable core/model code
- one test suite that validates both apps identically
- stable route fixtures from cache

## Initial Implementation Priorities

The first coding priority is not feature wiring. It is shape:

1. permanent folder structure
2. clear ownership boundaries
3. docked shell that matches the final layout
4. settings/session/core state abstractions
5. source/model interfaces that can absorb the full legacy feature set

Only after that should feature wiring begin.

This reduces rewrite churn and avoids painting the app into a temporary structure that later has to be replaced.

## Replacement Strategy

- develop `tools/cabana_imgui` in parallel
- keep legacy `tools/cabana` as the reference implementation during parity work
- do not partially replace legacy Cabana until the new app is genuinely ready
- only cut over once parity is complete enough that the new app can be the default without feature regression

## Definition of Done

The rewrite is done when all of the following are true:

- the new Cabana app has no Qt dependency
- the new Cabana app preserves every legacy Cabana feature/workflow
- validation remains fully outside the app
- parity is documented and complete in `PARITY.md`
- the new app is stable enough to replace legacy `tools/cabana`
