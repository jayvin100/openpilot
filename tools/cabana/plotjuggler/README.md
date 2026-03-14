## Cabana PlotJuggler

This is a trimmed vendored copy of PlotJuggler used only by `cabana --pj`.

Kept surface:
- embedded PlotJuggler app code used by Cabana
- openpilot-specific plugins: `DataLoadRlog`, `DataStreamCereal`, `ToolboxLuaEditor`
- bundled Qt dependencies required by those paths

Removed surface:
- standalone app packaging and release-update assets
- non-openpilot build metadata and package-manager files
- unused vendored docs, CI files, and editor backends

Build entrypoint:
- `tools/cabana/SConscript` builds this subtree directly and emits generated Qt sources under `tools/cabana/plotjuggler/.gen/`

Licensing:
- upstream PlotJuggler licensing notices are preserved in this subtree
