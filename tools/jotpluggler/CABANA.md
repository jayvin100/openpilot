# Cabana Features -> JotPlugger Integration Plan

This is the refined plan for bringing the useful Cabana workflows into JotPlugger without forcing Cabana's UI structure onto it.

The main correction from the first draft is: JotPlugger does **not** currently retain a real raw-CAN message model. It decodes CAN into signal series during extraction, then drops the raw message stream. That means most Cabana-style features do **not** fit cleanly until we add that foundation first.

## What JotPlugger Already Has

- Shared DBC parse/decode core via [`tools/cabana/dbc/dbc_core.h`](../cabana/dbc/dbc_core.h) and [`tools/cabana/dbc/dbc_core.cc`](../cabana/dbc/dbc_core.cc)
- Route and stream extraction in [`sketch_layout.cc`](./sketch_layout.cc) and [`StreamAccumulator`](./jotpluggler.h)
- Existing workspace pane model (`Plot`, `Map`, `Camera`) in [`jotpluggler.h`](./jotpluggler.h)
- Existing decoded signal storage as `RouteSeries`
- Existing route logs and timeline surfaces
- Existing DBC selection / override UX

## What Is Missing

### 1. A raw CAN message model

Today `append_event_fast(...)` in [`sketch_layout.cc`](./sketch_layout.cc) decodes `can` / `sendcan` into scalar series when a DBC is available, but does not keep:

- per-message frame history
- last raw payload
- counts / rate stats
- per-bit flip counters
- per-message selection state for UI

Without that, these features have nowhere natural to read from:

- raw CAN table
- bit viewer
- message history
- raw CSV export
- similar-bit analysis

### 2. A CAN-focused pane surface

Adding separate pane kinds for:

- raw table
- bit viewer
- history log
- signal editor

would make the workspace clumsy.

The better fit for JotPlugger is one new special pane:

- `PaneKind::CanInspector`

That pane can own:

- message table
- selected message details
- bit view
- signal list / editor
- history tab

### 3. Editable DBC document state

JotPlugger currently treats DBCs mostly as load-only decode inputs.

For Cabana-style editing we need a session-level DBC document model:

- current source path / generated source
- editable in-memory `dbc::Database`
- dirty flag
- save / save-as path
- re-decode trigger when the document changes

That is separate from the current "chosen DBC name" string.

## Proposed Architecture

### A. Add a raw CAN data layer to `RouteData`

Add a route-level CAN model alongside `series`:

- `CanMessageId`
  - direction or service (`can` vs `sendcan`)
  - bus
  - address
- `CanFrameSample`
  - time
  - payload bytes
  - size
- `CanMessageStats`
  - message count
  - first / last time
  - approximate rate
  - last payload
  - byte-change mask
  - bit-flip counters
- `CanMessageData`
  - id
  - optional DBC message pointer / resolved name
  - frame samples
  - stats

Then add to `RouteData` and `StreamExtractBatch`:

- `std::vector<CanMessageData>` or a keyed message collection

Important design point:

- route mode and stream mode should populate the **same** raw CAN model
- decoded `RouteSeries` should become a derived view of that raw model, not the only surviving representation

### B. Split CAN extraction into two stages

Instead of "decode and drop raw CAN", the extraction path should become:

1. capture raw CAN frames into the message model
2. if a DBC is active, derive decoded signal series from those frames

That should happen in both:

- route loading in [`load_route_data(...)`](./sketch_layout.cc)
- live accumulation in [`StreamAccumulator`](./jotpluggler.h)

This is the key enabling step for almost everything else in this plan.

### C. Add one new special pane: `CAN Inspector`

This should be exposed the same way `Map` and cameras are exposed now:

- one special item in `Data Sources`
- one pane kind in layouts

Inside the inspector pane, use internal tabs or subpanes:

- `Messages`
- `Bits`
- `Signals`
- `History`

This fits JotPlugger much better than scattering CAN tools across sidebars and separate pane types.

### D. Keep the main signal browser focused

The global browser on the left should stay about data sources and decoded series.

That means these Cabana ideas should **not** primarily live there:

- signal property editor
- per-message sparkline rows
- bit selection workflow

Those belong inside the `CAN Inspector`, scoped to the selected message.

## Feature-by-Feature Fit

### 1. Raw CAN Message Table

### Best JotPlugger fit

- Landing tab of `CAN Inspector`

### Why

- This is the top-level navigation surface for the rest of the CAN workflow.
- It needs the new raw CAN model first.

### Notes

- Columns: name, address, bus, direction, rate, count, last payload
- Filtering and sorting belong here
- Byte-change highlighting should use `last payload + change mask` from `CanMessageStats`

### 2. Binary Bit Viewer

### Best JotPlugger fit

- `Bits` tab inside `CAN Inspector`

### Why

- It depends on a selected message from the message table.
- It should reuse the same selection and stats state.

### Notes

- Per-bit flip counts should be accumulated during extraction, not recomputed on demand every time
- Signal ownership coloring comes from the selected DBC message
- Bit-range selection can later feed signal creation/editing

### 3. DBC Signal Editor

### Best JotPlugger fit

- `Signals` tab inside `CAN Inspector`

### Why

- Editing only makes sense in the context of a selected CAN message
- It should sit next to the bit viewer, not in the global browser

### Notes

- Needs session-level editable DBC document state
- Re-decode should be scoped:
  - immediate message-level preview if possible
  - full route re-extract only when necessary
- Undo here should be DBC-document undo, not layout undo

### 4. DBC File Management

### Best JotPlugger fit

- Small DBC management modal / popup launched from `CAN Inspector`
- Keep the existing sidebar DBC selector simple

### Why

- The sidebar combo is still good for fast "which DBC do I use?"
- Full file/document management is a deeper workflow and does not belong in the main left rail

### Notes

- Needs save / save-as / import / new
- Can likely reuse Cabana-side DBC generation logic rather than inventing a second write path

### 5. Signal Sparklines

### Best JotPlugger fit

- Inline in the `Signals` tab of `CAN Inspector`

### Why

- They are most useful while browsing/editing one message's decoded signals
- Putting them in the global browser would add noise and cost to an already dense surface

### Notes

- Reuse existing decoded `RouteSeries`
- Render only for visible rows to keep it cheap

### 6. Find Similar Bits

### Best JotPlugger fit

- Tool launched from the `Bits` tab

### Why

- The natural starting point is a selected bit in the bit viewer

### Notes

- Depends on per-bit stats in the raw CAN model
- Results should navigate back into the `CAN Inspector` message selection

### 7. Find Signal

### Best JotPlugger fit

- Separate modal, not part of `CAN Inspector`

### Why

- This is the one feature that really does operate over the existing decoded `RouteSeries` world
- It is broader than CAN-message inspection

### Notes

- Can be built mostly from current `RouteData::series`
- Should jump plots / select signals when a result is chosen

### 8. Live Panda USB Connection

### Best JotPlugger fit

- Additional stream source

### Why

- It belongs with streaming, not with route loading

### Important refinement

Do **not** force panda frames through fake cereal events if we do not need to.

Better model:

- add a raw-CAN ingestion path that both panda and SocketCAN can feed
- then derive decoded series and inspector updates from that shared raw path

### Concrete fit in current JotPlugger

JotPlugger now has an app-level `Cabana` mode, not just a pane-level inspector, so Panda belongs as:

- a new option in the existing `Live Stream` popup
- a new `StreamSourceKind::Panda`
- a background worker that feeds `StreamExtractBatch::can_messages`

It should **not**:

- pretend to be a remote cereal stream
- depend on Qt `QObject` / `LiveStream`
- require route-style segment loading

### Reuse boundary

Reuse:

- [`tools/cabana/panda.h`](../cabana/panda.h)
- [`tools/cabana/panda.cc`](../cabana/panda.cc)

Do **not** reuse directly:

- [`tools/cabana/streams/pandastream.h`](../cabana/streams/pandastream.h)
- [`tools/cabana/streams/pandastream.cc`](../cabana/streams/pandastream.cc)

Those stream classes are tied to Cabana's Qt event model and currently wrap raw CAN back into synthetic cereal `Event::Can` messages. JotPlugger should stay native and feed raw frames into its existing raw CAN model directly.

### 9. SocketCAN Support

### Best JotPlugger fit

- Same as panda: additional stream source

### Notes

- Depends on the same raw-CAN live ingestion layer
- Should arrive after the route + stream raw CAN model exists
- Should use the same `Live Stream` popup as the other stream modes

### Reuse boundary

Reuse the low-level socket setup / read path from:

- [`tools/cabana/streams/socketcanstream.cc`](../cabana/streams/socketcanstream.cc)

Do **not** reuse:

- the Qt widget/open-dialog layer
- the synthetic cereal event wrapping

SocketCAN should be just another producer of raw `CanFrameSample` batches.

## Live Source Architecture

The clean implementation is:

1. Add a source kind to JotPlugger streaming state
   - `CerealLocal`
   - `CerealRemote`
   - `Panda`
   - `SocketCAN`

2. Split the current `StreamPoller` into:
   - source selection / worker lifecycle
   - source-specific poll loops
   - one shared batch merge path

3. Add a source-agnostic raw CAN ingest API to `StreamAccumulator`
   - current path: `appendEvent(cereal::Event::Which, ...)`
   - new path: `appendCanFrames(CanServiceKind service, std::span<const LiveCanFrame>)`

4. Keep one shared UI/data path after ingestion
   - `takeBatch()`
   - `apply_stream_batch(...)`
   - `rebuild_cabana_messages(...)`
   - decoded series derivation from active DBC

This keeps all route/stream/Cabana views reading from the same post-ingest model.

### Suggested structs

- `enum class StreamSourceKind`
- `struct PandaStreamConfig`
  - serial
  - per-bus CAN speed
  - per-bus CAN-FD enable/data speed
- `struct SocketCanStreamConfig`
  - device
- `struct StreamSourceConfig`
  - kind
  - cereal address / remote flag
  - panda config
  - socketcan config

### Worker model

Keep one worker thread in `StreamPoller`, but branch by source kind:

- cereal source
  - existing ZMQ/MSGQ subscription path
- panda source
  - open panda
  - receive raw frames
  - append directly to the accumulator
  - send heartbeat
- socketcan source
  - open CAN socket
  - read frames
  - append directly to the accumulator

This is simpler than introducing separate poller classes unless the source count grows further.

### UI / UX shape

Extend the existing `Live Stream` popup to choose among:

- `Local (MSGQ)`
- `Remote (ZMQ)`
- `Panda`
- `SocketCAN`

When `Panda` is selected:

- show serial dropdown + refresh
- show per-bus speed / CAN-FD settings

When `SocketCAN` is selected:

- show CAN interface dropdown + refresh

When connected:

- status text should say which source is active
- `Close Stream` should behave the same across all source kinds

### Logging / persistence

Do not block Panda/SocketCAN on a full logging design.

First pass:

- live-only, in-memory streaming into the existing rolling stream buffer

Optional later follow-up:

- capture raw live CAN into a route-like local artifact for later replay

### 10. CSV Export

### Best JotPlugger fit

- Actions inside `CAN Inspector`
- optionally mirrored in `File`

### Important refinement

This is **not** actually the easiest feature if we want true raw-message export, because current JotPlugger does not keep those frames.

After the raw CAN model exists, export becomes straightforward:

- raw message CSV from `CanMessageData::frame samples`
- decoded message CSV from selected message + DBC decode

### 11. Message History Log

### Best JotPlugger fit

- `History` tab inside `CAN Inspector`

### Why

- It is another selected-message detail surface
- It should share selection and filtering with the table / bits / signals tabs

### Notes

- Use virtual scrolling
- Rows: time, payload, decoded summary

## Revised Priority Order

The old priority order overvalued features that sound small but actually depend on raw CAN storage.

This is the better sequence for JotPlugger:

### Phase 1: Foundation

1. Add raw CAN route / stream data model
2. Populate it during route load and live stream
3. Add `PaneKind::CanInspector`
4. Add inspector pane shell with message selection state

### Phase 2: Inspector MVP

5. Raw CAN message table
6. Message history tab
7. Raw / decoded CSV export

This gives immediate usefulness and validates the data model.

### Phase 3: Signal / bit workflow

8. Bit viewer
9. Signal list with sparklines
10. Find Similar Bits

At this point JotPlugger becomes a strong read-only CAN inspection tool.

### Phase 4: DBC authoring

11. Editable DBC document state
12. DBC save / save-as / import / new
13. Signal editor

This is the large workflow step and should come after the inspector read path is solid.

### Phase 5: Extra analysis and live sources

14. Find Signal
15. Add source-agnostic live CAN ingest path
16. Extend `Live Stream` popup with source selection
17. Panda live source
18. SocketCAN source

## Suggested First Milestone

The first milestone should be:

- new `CAN Inspector` special pane
- raw CAN message table
- message history
- CSV export

That is the smallest coherent slice that:

- adds obvious new capability
- proves the raw CAN data model
- gives a solid base for bit view and DBC editing later

## Non-Goals for v1

- Multiple separate CAN pane kinds
- Full Cabana UI parity
- Putting DBC editing into the global left sidebar
- Live panda / SocketCAN before the raw CAN route model exists
- Recomputing bit statistics on demand from scratch

## Summary

The right way to port Cabana ideas into JotPlugger is:

- first add a real raw CAN model
- then build one `CAN Inspector` pane on top of it
- then add deeper tools inside that pane

That keeps the JotPlugger UI coherent and avoids bolting a second, parallel app architecture onto it.
