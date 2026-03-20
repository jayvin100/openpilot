# JotPlugger

This directory currently contains two pieces:
- the fast screenshot validator against cached PlotJuggler baselines
- the first GLFW-based `jotpluggler` shell for deterministic screenshot output

Phase 1 is intentionally narrow:
- first tab only for each PlotJuggler layout
- one-time PlotJuggler baseline generation
- fast candidate image comparison with per-layout diff artifacts
- a placeholder `sketch` renderer that only draws pane geometry, not real plots

The validator is designed for fast local iteration on a many-core machine. PlotJuggler baselines are generated once and cached. Normal validation only compares candidate renders against those stored references.

## Build

Build the local shell:

```bash
scons tools/jotpluggler/jotpluggler
```

Render a placeholder layout sketch:

```bash
./tools/jotpluggler/jotpluggler \
  --mode sketch \
  --layout longitudinal \
  --width 1600 \
  --height 900 \
  --output /tmp/longitudinal-sketch.png
```

Render a validator-passing mock image by copying the cached PlotJuggler baseline:

```bash
./tools/jotpluggler/jotpluggler \
  --mode mock-reference \
  --layout longitudinal \
  --output /tmp/longitudinal.png
```

## Commands

Prepare cached first-tab fixtures from the demo route:

```bash
./tools/jotpluggler/validate.py prepare-fixtures
```

Generate PlotJuggler baselines:

```bash
./tools/jotpluggler/validate.py capture-baselines
```

Compare an existing directory of candidate screenshots against the cached baselines:

```bash
./tools/jotpluggler/validate.py compare-images out/jotpluggler
```

Run a candidate renderer command under `Xvfb`, collect PNGs, and compare them:

```bash
./tools/jotpluggler/validate.py validate-command \
  --command-template 'tools/jotpluggler/jotpluggler --mode mock-reference --layout {layout} --output {output}'
```

The command template receives these placeholders:
- `{layout}`
- `{sanitized_layout}`
- `{fixture}`
- `{output}`
- `{width}`
- `{height}`
- `{tab_index}`
- `{tab_name}`

## Output

Generated artifacts live under `tools/jotpluggler/validation/`:
- `fixtures/`
- `sanitized_layouts/`
- `baselines/`
- `reports/`

Each comparison run writes:
- `candidate.png`
- `reference.png`
- `diff.png`
- `metrics.json`
- `summary.json`
