# Cabana ImGui Parity Tracker

## Stream Sources
- [x] route replay loading and playback controls — `wired`
- [x] demo route support — `wired`
- [x] auto-source route loading — `wired`
- [ ] live device streaming — `not started`
- [ ] panda streaming — `not started`
- [ ] socketcan streaming — `not started`
- [ ] zmq/device address streaming — `not started`
- [x] qcam, ecam, dcam handling — `wired`

## DBC
- [x] automatic DBC selection from fingerprint — `wired`
- [x] manual DBC open/save/save-as — `wired`
- [ ] manual DBC new file — `not started`
- [ ] DBC clipboard import/export — `not started`
- [x] DBC management per bus/source — `wired`
- [x] route metadata and fingerprint display — `wired`

## Messages
- [x] message list filtering — `wired`
- [ ] message list sorting — `not started`
- [ ] message visibility behavior — `not started`
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
- [x] video display — `skeleton only`
- [x] video synchronization — `skeleton only`

## Tools
- [ ] find signal — `not started`
- [ ] find similar bits — `not started`
- [ ] export to CSV — `not started`

## App State
- [x] settings persistence — `partial (chart state + dock layout + selection/detail/dbc recents)`
- [x] recent files/state — `partial (recent routes + recent dbc files + per-source dbc assignments)`
- [x] session restore — `partial (chart tabs + dock layout + selected message + detail tab + last route + per-source dbc assignments)`
- [x] undo/redo command stack — `wired for DBC message/signal edit flows`
- [x] help overlay — `wired`

## UI Shell
- [x] dockspace layout — `wired`
- [x] menu bar (File, Edit, View, Tools, Help) — `partial (DBC file flows + per-source DBC management + edit message/add signal + layout/help)`
- [x] messages pane — `wired`
- [x] detail pane with tabs — `wired`
- [x] charts pane — `wired`
