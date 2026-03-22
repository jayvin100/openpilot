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
- [ ] DBC management per bus/source — `not started`
- [x] route metadata and fingerprint display — `wired`

## Messages
- [x] message list filtering — `wired`
- [ ] message list sorting — `not started`
- [ ] message visibility behavior — `not started`
- [x] message selection and tab restoration — `wired`

## Detail View
- [x] binary view — `wired`
- [x] signal view — `wired`
- [ ] signal editing workflows — `not started`
- [ ] message editing workflows — `not started`
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
- [x] recent files/state — `partial (recent routes + recent dbc files)`
- [x] session restore — `partial (chart tabs + dock layout + selected message + detail tab + last route/dbc)`
- [ ] undo/redo command stack — `not started`
- [x] help overlay — `wired`

## UI Shell
- [x] dockspace layout — `wired`
- [x] menu bar (File, Edit, View, Tools, Help) — `skeleton only`
- [x] messages pane — `wired`
- [x] detail pane with tabs — `wired`
- [x] charts pane — `wired`
