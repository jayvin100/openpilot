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

Validation must be completely self-contained outside the app.

Suggested tree:

```text
tools/cabana_imgui_validation/
  parity/
  smoke/
  workflows/
  perf/
  goldens/
```

### Validation Rules

- No production file in `tools/cabana_imgui/` should exist solely for validation.
- Validation may launch the app as a black box, but may not require app-only test modes.
- Validation may directly test shared production libraries below the UI layer.
- Validation may use cached routes on disk and existing route slices.
- Validation state isolation should be external via environment/config/output directories.

### Validation Types

#### `parity/`

Route-based and behavior-based parity checks for:

- message inventory
- latest bytes for selected messages
- decoded signal values at fixed timestamps
- history/log behavior
- chart data behavior
- DBC round-trip and mapping behavior
- fingerprint-based DBC selection
- CSV export behavior
- session persistence behavior

These should compare legacy Cabana behavior to the new app's shared logic and outputs where possible.

#### `smoke/`

Black-box app launch checks:

- app boots
- cached route opens
- UI stays alive for a stable interval
- screenshots can be captured
- exit is clean

#### `workflows/`

External UI automation for real workflows:

- open stream
- select message
- open/edit message or signal
- split and manage charts
- save DBC
- restore session
- use find dialogs
- exercise video-related flows

#### `perf/`

External timing and process metrics only:

- startup time
- route load time
- memory footprint
- CPU usage
- steady-state responsiveness

No in-app performance instrumentation should be added solely for this.

## Fast Iteration Loop

The iteration loop should rely on:

- cached routes already available on disk
- building only the new app target
- validating specific layers independently

The expected development rhythm is:

1. build `tools/cabana_imgui`
2. run against a fixed cached route slice
3. validate relevant shared library behavior in `tools/cabana_imgui_validation/parity`
4. run black-box smoke/workflow checks externally

Fast iteration comes from architecture, not app instrumentation:

- thin UI layer
- reusable core/model code
- parity tests that do not require full UI automation for every check
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
