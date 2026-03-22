# Cabana ImGui Parity Tracker

## Stream Sources
- [x] route replay loading and playback controls — `wired`
- [x] demo route support — `wired`
- [x] auto-source route loading — `wired`
- [x] live device streaming — `partial (msgq/zmq source layer + cli/dialog wired)`
- [x] panda streaming — `partial (source layer + cli/dialog wired; hardware validation pending)`
- [x] socketcan streaming — `partial (source layer + cli/dialog wired; hardware validation pending)`
- [x] zmq/device address streaming — `partial (bridge-backed source layer + cli/dialog wired)`
- [x] qcam, ecam, dcam handling — `wired`

## DBC
- [x] automatic DBC selection from fingerprint — `wired`
- [x] manual DBC open/save/save-as/opendbc menu load — `wired`
- [x] manual DBC new file — `wired`
- [x] DBC clipboard import/export — `wired`
- [x] DBC management per bus/source — `wired`
- [x] route metadata and fingerprint display — `wired`

## Messages
- [x] message list filtering — `wired`
- [x] message list sorting — `wired`
- [x] message visibility behavior — `wired (show inactive toggle + inactive dimming)`
- [x] message selection and tab restoration — `wired`

## Detail View
- [x] binary view — `wired`
- [x] signal view — `wired`
- [x] signal editing workflows — `wired`
- [x] message editing workflows — `wired`
- [x] history log — `wired`

## Charts
- [x] charts display — `wired`
- [x] chart tabs — `wired`
- [x] split/merge behavior — `wired`
- [x] layout persistence — `wired`

## Video
- [x] video display — `wired for replay VIPC frames`
- [x] video synchronization — `partial (replay timeline + transport/video playback wired; live video pending)`

## Tools
- [x] find signal — `wired`
- [x] find similar bits — `wired`
- [x] route info — `wired`
- [x] export to CSV — `wired (route-wide CAN dump)`

## App State
- [x] settings persistence — `partial (chart state + dock layout + selection/detail/dbc recents)`
- [x] recent files/state — `partial (recent routes + recent dbc files + per-source dbc assignments)`
- [x] session restore — `partial (chart tabs + dock layout + selected message + detail tab + last route + per-source dbc assignments)`
- [x] undo/redo command stack — `wired for DBC message/signal edit flows`
- [x] help overlay — `wired`

## UI Shell
- [x] dockspace layout — `wired`
- [x] menu bar (File, Edit, View, Tools, Help) — `partial (open stream dialog + DBC file flows + opendbc/export + clipboard/new + per-source DBC management + find signal/similar bits/route info + edit message/add signal + layout/help)`
- [x] messages pane — `wired`
- [x] detail pane with tabs — `wired`
- [x] charts pane — `wired`
