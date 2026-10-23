# jayvin100/sunnypilot

Personal sunnypilot build for a comma 3X on a Tesla Model 3 RWD (RHD).

This branch (`tools`) holds only the build automation and patches. The installable branches are generated:

| Branch | Built from | Install |
|---|---|---|
| `dev-jay` | official sunnypilot `dev` + `patches/` | `installer.comma.ai/jayvin100/dev-jay` |
| `staging-jay` | official sunnypilot `staging` + `patches/` | `installer.comma.ai/jayvin100/staging-jay` |

The workflow in `.github/workflows/build.yml` runs every 3 hours, and whenever `patches/` changes. If a patch stops applying, the run fails and the branch keeps its last good build.

## Patches

- Plain `git diff` files against the prebuilt branch tree, applied in filename order (`001-name.patch`, `002-...`).
- Python only. The prebuilt branches ship compiled binaries, and C++ changes would need an on-device rebuild.
- No changes to safety limits, panda safety, or driver monitoring.

## Device screen

`scripts/device.py screen | tap X Y | swipe X1 Y1 X2 Y2 [ms]` takes screenshots and injects touches over SSH. Screenshots need `patches/002`.
`scripts/device.py push TREE FILE...` and `restart-ui` copy files from a worktree to the device and restart the UI, for quick testing.
`scripts/drive_report.py [N|route]` prints a drive's takeovers, their causes, and steering stats.

## What the patches change

- **Home:** a Ready card (clock, software update status, last drive), live Firehose uploads with time left for the current queue, and storage. Replaces the prime ad and the Firehose prompt.
- **Sidebar:** shows the real device temperature; the vehicle and connect boxes only appear when something is wrong; the comma button is hidden offroad, where it does nothing.
- **Screensaver:** a still clock with upload status, shifted a few pixels each minute against OLED burn-in, dimmed at night.
- **Storage:** below 15% free, deletes video comma already uploaded, then video older than 7 days, then video older than a day, until 18% is free. Logs are kept.
- **Drive stats:** each drive is summarized from its qlogs after the car turns off.
- **Driving screen:** optional minimal mode that hides the path, lane lines and HUD extras. Alerts, driver monitoring, turn signals and blind spot are never hidden.
- **Settings:** a "Jay" panel for all of the above, opened by the gear. Unused panels (sunnylink, Trips, Firehose, Navigation) are removed, the rest reordered, and items that don't apply to the car (torque-only steering options on angle-steering cars, cruise button options, GitHub runner, copyparty) are hidden.
- **Processes:** the sunnylink daemon doesn't start while sunnylink is off.

## Back to official

On the device, run `git remote set-url origin https://github.com/sunnypilot/sunnypilot.git` in both `/data/openpilot` and `/data/safe_staging/merged`, which is the updater's overlay and has its own `.git/config`. Then set the target branch to `dev` in Settings → Software.
