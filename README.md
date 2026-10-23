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

## Back to official

On the device, run `git remote set-url origin https://github.com/sunnypilot/sunnypilot.git` in both `/data/openpilot` and `/data/safe_staging/merged`, which is the updater's overlay and has its own `.git/config`. Then set the target branch to `dev` in Settings → Software.
