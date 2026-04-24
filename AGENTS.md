# JayPilot Agent Guide

JayPilot is a personal sunnypilot-based fork for a 2025 Tesla Model 3 Highland right-hand drive. The goal is a custom, lean, efficient, light, modern build tuned for this specific car, not a general-purpose downstream distribution.

## Project Direction

- Base work on sunnypilot unless the user explicitly asks otherwise.
- For this comma 3X device, use sunnypilot's recommended install branches as the practical baseline. The current working baseline is `dev`.
- Prefer keeping useful sunnypilot driving behavior over matching upstream openpilot exactly.
- Do not optimize for upstream mergeability. This fork is for personal use, and there is no planned upstream merge path.
- Keep the fork focused on the target car. Avoid adding generic features unless they directly support the user's car, workflow, testing, or safety.
- Favor removing unnecessary surface area over adding toggles for every possible behavior.

## Vehicle Target

- Primary car: 2025 Tesla Model 3 Highland, right-hand drive.
- Hardware assumptions should be verified before changing control behavior.
- Treat Tesla-specific steering, safety, CAN, fingerprint, harness, and longitudinal-control changes as high-risk.
- Right-hand-drive behavior should not be assumed to match left-hand-drive behavior unless confirmed in code or logs.

## Engineering Priorities

1. Safety-critical behavior first.
2. Stable driving behavior second.
3. Simplicity and maintainability third.
4. UI polish and convenience features after the driving stack is understood.

## Working Rules

- Read the existing sunnypilot pattern before editing.
- Keep changes small, scoped, and reversible.
- Do not revert user changes unless explicitly asked.
- Use existing helpers, parameter patterns, and process structure where practical.
- Use absolute package imports such as `openpilot.selfdrive`, `openpilot.common`, `openpilot.system`, and `openpilot.tools`; local top-level imports are banned by the repo lint config.
- Avoid broad refactors unless they remove real complexity from JayPilot.
- Prefer deleting unused code only after confirming it is not needed by the Tesla target or build process.
- Document car-specific decisions when they affect safety, steering, longitudinal control, model behavior, or process startup.

## Repo Layout

- `selfdrive/`: core driving, UI, controls, model, logging, and tests.
- `system/`: manager, updater, hardware, networking, athena, UI support, and device services.
- `sunnypilot/`: sunnypilot-specific features, model management, MADS, mapd, sunnylink, custom services, and overlay code under `sunnypilot/selfdrive` and `sunnypilot/system`.
- `opendbc_repo/opendbc/car/tesla/`: primary Tesla platform logic, fingerprints, interface, controller, CAN messages, and safety integration.
- `opendbc_repo/opendbc/sunnypilot/car/tesla/`: sunnypilot Tesla extensions such as cooperative steering and vehicle-bus handling.
- `cereal/`: message schemas and generated bindings. Be careful with compatibility.
- `panda/`, `tinygrad_repo/`, `teleoprtc_repo/`, `rednose_repo/`, `msgq_repo/`, and `opendbc_repo/` are large vendored dependency trees on the `dev` branch. Do not edit them casually.

## Dev Branch Shape

- This checkout tracks sunnypilot `dev`, which is an installable/prebuilt branch for comma 3X rather than the clean source integration layout from `master`.
- There is no root `.gitmodules` file on `dev`; dependency trees are checked in directly.
- There is no root `SConstruct` on `dev`; prebuilt binaries, generated C/C++ files, generated cereal bindings, and model artifacts are present in the tree.
- Do not delete generated or binary artifacts just because they look build-produced. On `dev`, many of them are intentional tracked files.
- If source-build behavior needs investigation, compare against `upstream/master` or `upstream/master-dev`, but keep JayPilot's runnable baseline on `dev` unless the user chooses otherwise.

## Local Commands

- Python version: `>=3.12.3,<3.13`.
- Environment management is configured for `uv` with managed Python.
- The `dev` branch is a prebuilt/install branch, so source-build commands from `master` may not exist here.
- Run focused Python tests with `pytest path/to/test.py` or `pytest path/to/test.py -k name`.
- Run focused lint with `ruff check path/to/file.py`.
- Run type checks with `ty check path/to/file.py` when the touched area benefits from it.
- When commands need the project environment, prefer the same command through `uv run`, for example `uv run pytest ...`.

## Git And Remotes

- Current base branch is expected to track `upstream/dev` from `https://github.com/sunnypilot/sunnypilot.git`.
- `dev` is the latest experimental/prebuilt branch recommended by sunnypilot for testers and developers on comma 3X; `release-tizi` is the stable C3X release branch, and `staging` is the pre-release validation branch.
- `origin` may still point at the user's previous openpilot fork until the user chooses a final GitHub repo.
- Do not force-push or rewrite remote history without explicit confirmation.

## Validation Expectations

- Run targeted checks for files touched when practical.
- For Python logic, prefer focused tests or import checks over broad test runs unless the change warrants it.
- For car-control changes, inspect the relevant Tesla files in `opendbc_repo/opendbc/car/tesla` and sunnypilot Tesla extensions before editing.
- For process startup changes, inspect `system/manager` and sunnypilot process definitions together.
- For UI changes, check both standard UI paths and sunnypilot UI paths because settings and onroad surfaces are split.
- If a change cannot be tested locally, say exactly what was not tested and why.

## Near-Term Roadmap

- Keep this clean sunnypilot `dev` base runnable on the comma 3X.
- Decide the final GitHub `origin` remote.
- Inventory sunnypilot features and processes.
- Identify what is required for the user's Tesla and what can be removed.
- Trim nonessential services and UI surface carefully.
- Only then start tuning behavior for the car.
