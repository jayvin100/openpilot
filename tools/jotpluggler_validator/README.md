# JotPlugger Validator

This directory is the temporary screenshot-validation harness for `jotpluggler`.

It is intentionally separate from the product app. Its job is to:
- build cached first-tab fixtures from the demo route
- materialize tiny local route directories for fast candidate renders
- generate PlotJuggler reference screenshots
- run `jotpluggler` under `Xvfb`
- resize the candidate window externally
- compare candidate screenshots against cached references
- write diff artifacts and summary reports

The harness may sanitize layouts for PlotJuggler baseline capture, but `jotpluggler` itself should be invoked with the real layout XML.

## Commands

Prepare cached first-tab fixtures:

```bash
./tools/jotpluggler_validator/validate.py prepare-fixtures
```

Generate PlotJuggler baselines:

```bash
./tools/jotpluggler_validator/validate.py capture-baselines
```

Compare an existing directory of candidate screenshots:

```bash
./tools/jotpluggler_validator/validate.py compare-images out/jotpluggler
```

Run `jotpluggler` as a black-box renderer, then compare:

```bash
./tools/jotpluggler_validator/validate.py validate-command \
  --command-template 'tools/jotpluggler/jotpluggler --layout {layout} --data-dir {data_dir} --show {route}'
```

The command template receives these placeholders:
- `{layout}`: absolute path to the real layout XML
- `{sanitized_layout}`: PlotJuggler-only sanitized temp XML
- `{fixture}`
- `{data_dir}`
- `{route}`
- `{output}`: candidate PNG path if your command wants to write one itself
- `{width}`: validator capture width
- `{height}`: validator capture height
- `{tab_index}`
- `{tab_name}`

## Output

Generated artifacts live under `tools/jotpluggler_validator/validation/`:
- `fixtures/`
- `route_data/`
- `sanitized_layouts/`
- `baselines/`
- `reports/`
