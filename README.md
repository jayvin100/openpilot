# jayvin100/openpilot

Personal openpilot build for a comma 3X on a Tesla Model 3 RWD (RHD).

This branch (`tools`) holds only the build automation and patches. The installable branches are generated:

| Branch | Built from | Install |
|---|---|---|
| `nightly-jay` | official openpilot `nightly` + `patches/` | `installer.comma.ai/jayvin100/nightly-jay` |

The workflow in `.github/workflows/build.yml` runs every 3 hours, and whenever `patches/` changes. If a patch stops applying, the run fails and the branch keeps its last good build.

## Patches

- Plain `git diff` files against the prebuilt branch tree, applied in filename order (`001-name.patch`, `002-...`).
- Python only. The prebuilt branches ship compiled binaries, and C++ changes would need an on-device rebuild.
- No changes to safety limits, panda safety, or driver monitoring.

## Device screen

`scripts/device.py screen | tap X Y | swipe X1 Y1 X2 Y2 [ms]` takes screenshots and injects touches over SSH. Screenshots need patch 002.
`scripts/device.py push TREE FILE...` and `restart-ui` copy files from a worktree to the device and restart the UI, for quick testing.
`scripts/drive_report.py [N|route]` prints a drive's takeovers, their causes, and steering stats.

## What the patches change

- **Home:** a Ready card (clock, software update status, last drive), live Firehose uploads with time left for the current queue, and storage. Replaces the prime ad and the setup prompt.
- **Sidebar:** shows the real device temperature; the vehicle and connect boxes only appear when something is wrong; the comma button is hidden offroad, where it does nothing.
- **Screensaver:** offroad, a still clock with upload status shows for 5 minutes before the screen turns off, shifted a few pixels each minute against OLED burn-in, dimmed at night.
- **Storage:** below 15% free, deletes video comma already uploaded, then video older than 7 days, then video older than a day, until 18% is free. Logs are kept.
- **Drive stats:** each drive is summarized from its qlogs after the car turns off.
- **Driving screen:** optional minimal mode that hides the path and lane lines. Alerts and driver monitoring are never hidden.
- **Road mode:** Settings > Jay has Go Offroad / Go Onroad. Entering requires fresh vehicle data, Park, standstill and openpilot disengaged. It stops assistance and drive recording until resumed, and clears when ignition turns off or hardwared restarts.
- **Driving model:** Settings > Software shows the installed comma 3X model and its verified upstream release date. Unknown models show their checkpoint and an unavailable date.
- **Update checks:** Settings > Software allows a read-only check of the selected branch while onroad. Downloads and installation use the stock offroad updater.
- **Settings:** a "Jay" panel for all of the above, opened by the gear. The Firehose panel is removed (the home screen shows it) and the rest reordered.

## Back to official

On the device, run `git remote set-url origin https://github.com/commaai/openpilot.git` in both `/data/openpilot` and `/data/safe_staging/merged`, which is the updater's overlay and has its own `.git/config`. Then set the target branch to `nightly` (or `release-tizi`) and let the updater fetch it.
