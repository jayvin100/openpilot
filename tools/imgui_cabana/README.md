# ImGui Cabana Structure

`tools/imgui_cabana/` mirrors the high-level shape of `tools/cabana/` so the rewrite can
land incrementally without another directory-level refactor.

Current real entrypoints:
- `imgui_cabana.cc`: thin launcher
- `app_state.h/.cc`: launch-time config and future framework-agnostic app model
- `mainwin.h/.cc`: current GLFW/OpenGL/ImGui shell

Mirrored modules exist now as landing zones for the rewrite:
- top-level widgets: `messageswidget`, `detailwidget`, `videowidget`, `binaryview`, `signalview`, etc.
- chart area: `chart/`
- DBC layer: `dbc/`
- stream layer: `streams/`
- feature tools: `tools/`
- shared helpers: `utils/`

The expectation is:
- state and business logic move toward `app_state` + `dbc` + `streams`
- immediate-mode rendering moves out of `mainwin` and into the mirrored widget files
- validation and smoketest hooks stay framework-agnostic where possible
