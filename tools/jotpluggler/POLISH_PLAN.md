# JotPlugger UI Polish Plan

## Goal

Make JotPlugger feel like a calm, premium diagnostic instrument: dense and highly functional like a serious tool, but visually disciplined enough to feel designed rather than assembled.

## Visual Direction

- Calm instrument: cool neutrals, restrained steel-blue accent, quiet chrome.
- Dense by default: keep information-rich layouts and compact controls.
- Data-first color: reserve stronger hues for plots, alerts, playback state, and selected data.
- Premium restraint: typography, spacing, and alignment do most of the work.

## Non-Goals

- No major workflow rewrite.
- No redesign of docking, panes, or layout semantics.
- No dark mode pass during the first polish wave.
- No decorative motion or visual effects that reduce legibility.

## Validation Loop

Use screenshots as the main regression harness for visual work.

Quick direct capture:

```bash
./tools/jotpluggler/jotpluggler \
  --layout longitudinal \
  --demo \
  --width 1600 \
  --height 900 \
  --output /tmp/jotpluggler-longitudinal.png
```

Black-box validation against cached baselines:

```bash
./tools/jotpluggler_validator/validate.py validate-command \
  --layouts longitudinal gps \
  --candidate-dir /tmp/jotpluggler-validate/candidate \
  --output-dir /tmp/jotpluggler-validate/report \
  --command-template './tools/jotpluggler/jotpluggler --sync-load --layout {layout} --data-dir {data_dir} --show {route}'
```

Recommended validation rhythm while polishing:

1. Run one direct screenshot for the layout you are actively refining.
2. Run targeted validator layouts for the surfaces you touched.
3. Before merging a polish milestone, run the full validator sweep.
4. Manually inspect candidate and diff images for any change that is technically passing but visually suspicious.

Representative layout set by surface:

- Shell and sidebar: `longitudinal`, `gps`, `thermal_debug`
- Dense plots: `torque-controller`, `tuning`, `system_lag_debug`
- State blocks and mixed curves: `controls_mismatch_debug`, `CAN-bus-debug`
- Logs and timing-heavy views: `camera-timings`, `ublox-debug`

## Workstreams

### Phase 1: Theme Foundation

Primary files:

- `tools/jotpluggler/app.cc`
- `tools/jotpluggler/jotpluggler.h`

Tasks:

- Introduce semantic theme tokens for surfaces, borders, text, accent, and semantic states.
- Standardize spacing, radii, border weights, control heights, and default padding.
- Replace repeated neutral `color_rgb(...)` literals with named theme helpers.
- Define one typography hierarchy using the existing UI, bold, and mono fonts.
- Keep plot and map data colors separate from chrome colors.

Definition of done:

- Most neutral chrome colors come from a shared token set.
- Controls and containers share one density model.
- Theme changes can be made by editing a small, centralized surface.

### Phase 2: Global Chrome and Layout Hierarchy

Primary files:

- `tools/jotpluggler/app_session_flow.cc`
- `tools/jotpluggler/app.cc`
- `tools/jotpluggler/app_sidebar_flow.cc`

Tasks:

- Refine the menu bar into a quieter top chrome layer.
- Make the route chip the canonical compact high-polish component.
- Separate app shell into three tiers: global chrome, secondary chrome, and content panes.
- Redesign sidebar section rhythm so sections feel grouped without heavy separators everywhere.
- Tighten the status bar and timeline so transport, scrubber, and status text feel intentionally aligned.
- Unify workspace tab strip styling and new-tab affordance.

Definition of done:

- Hierarchy is obvious at a glance.
- Sidebar, top bar, tabs, and footer feel like one family.
- The app looks calmer without losing density.

### Phase 3: Shared Compact Components

Primary files:

- `tools/jotpluggler/app_browser.cc`
- `tools/jotpluggler/app_logs.cc`
- `tools/jotpluggler/app_session_flow.cc`
- `tools/jotpluggler/app_layout_flow.cc`
- `tools/jotpluggler/app.cc`

Tasks:

- Build shared helpers for section headers, compact button rows, chips, selectable rows, and quiet empty states.
- Unify hover, selected, active, and disabled treatments.
- Apply the same row language to sidebar special items, timeseries rows, and log rows.
- Normalize popup action buttons and link-style buttons.
- Standardize inline metadata placement for values, counts, and monospace readouts.

Definition of done:

- Repeated patterns no longer have slightly different visual rules.
- New list-like surfaces can be built from shared primitives instead of custom one-off drawing.

### Phase 4: Data Surface Polish

Primary files:

- `tools/jotpluggler/app.cc`
- `tools/jotpluggler/app_map.cc`
- `tools/jotpluggler/app_runtime.cc`
- `tools/jotpluggler/app_logs.cc`

Tasks:

- Replace the default curve rotation with a calmer, curated palette that still separates overlapping lines.
- Lower plot grid and legend contrast so data carries visual emphasis.
- Refine cursor, tracker, and state-block rendering to feel precise rather than heavy.
- Bring map overlays and action controls into the same chrome system.
- Replace camera loading and empty states with a shared placeholder card style.
- Tune logs table headers, active row state, and expansion styling.

Definition of done:

- Plots feel premium and easier to scan.
- Map and camera panes feel like first-class citizens, not utility exceptions.
- Logs feel integrated with the rest of the app instead of like a separate widget set.

### Phase 5: Editors, Dialogs, and Edge States

Primary files:

- `tools/jotpluggler/app_custom_series.cc`
- `tools/jotpluggler/app_layout_flow.cc`
- `tools/jotpluggler/app_session_flow.cc`

Tasks:

- Normalize modal spacing, button ordering, labels, and helper text.
- Refine custom-series editor layout so preview, source selection, and code panes feel intentionally balanced.
- Standardize error, empty, loading, and disconnected states.
- Make small utility popups match the main chrome system.

Definition of done:

- Edge states feel designed, not improvised.
- Dialogs match the same component and spacing rules as the main app.

## Issue-Sized Task List

Recommended execution order:

1. Add semantic theme tokens and theme helpers.
2. Convert shell surfaces to use the new tokens.
3. Restyle menu bar, route chip, and status bar.
4. Restyle workspace tabs and pane window chrome.
5. Add shared compact row helper and apply it to sidebar special items.
6. Apply shared row styling to timeseries browser leaves.
7. Apply shared row styling to logs.
8. Replace the default plot curve palette.
9. Tune plot grid, legend, and tracker visuals.
10. Polish map overlay buttons and loading callouts.
11. Polish camera placeholders and loading cards.
12. Normalize modal and editor styling.
13. Run the full screenshot validation sweep and manual visual review.

## Design Rules

- Prefer fewer stronger surfaces over many outlined boxes.
- Use borders sparingly; use spacing and tone first.
- Keep text contrast high for labels and values, moderate for supporting metadata.
- Use semibold selectively for section titles, selected tabs, and key labels.
- Keep monospace for route ids, values, code, and numeric data.
- Avoid adding new component variants unless an existing one truly cannot fit.
- Any new custom-drawn control must use the same hover and selected language as the rest of the app.

## Merge Criteria

Before a polish milestone is considered done:

- Targeted validator layouts pass.
- Full validator sweep passes.
- Candidate screenshots look intentionally improved, not merely different.
- No untouched surface looks out of place next to the polished ones.
- Raw neutral color literals have not started spreading again.
