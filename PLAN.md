# Cabana PlotJuggler Plan

## Current Focus
- Keep `cabana --pj` visually close to standalone PlotJuggler for the openpilot layouts we actually use.
- Preserve Cabana's video dock while avoiding extra Cabana chrome inside PJ mode.
- Keep the imported PJ subtree easy to validate from a headless CI-style shell.

## Validation Flow
1. Build the binary:
```bash
scons -j1 tools/cabana/_cabana
```

2. Run a headless smoke test with the demo route and a representative layout:
```bash
xvfb-run -a timeout 25s tools/cabana/cabana --pj --demo \
  --pj-layout tools/plotjuggler/layouts/locationd_debug.xml
```

3. Capture a full-window screenshot and exit cleanly after the grab:
```bash
xvfb-run -a bash -lc '
  export CABANA_PJ_SCREENSHOT=/tmp/cabana_pj.png
  export CABANA_PJ_SCREENSHOT_EXIT=1
  export CABANA_PJ_SCREENSHOT_DELAY_MS=22000
  tools/cabana/cabana --pj --demo \
    --pj-layout tools/plotjuggler/layouts/locationd_debug.xml
'
```

4. Compare the captured image against the standalone PJ reference screenshot:
```bash
xdg-open /tmp/cabana_pj.png
xdg-open /tmp/pj_old_verify.png
```

5. If window embedding regresses, inspect the X tree while the app is live:
```bash
xvfb-run -a bash -lc '
  export CABANA_PJ_SCREENSHOT=/tmp/cabana_pj_probe.png
  export CABANA_PJ_SCREENSHOT_EXIT=1
  export CABANA_PJ_SCREENSHOT_DELAY_MS=5000
  tools/cabana/cabana --pj --demo \
    --pj-layout tools/plotjuggler/layouts/locationd_debug.xml &
  pid=$!
  sleep 3
  xwininfo -root -tree
  wait "$pid"
'
```
