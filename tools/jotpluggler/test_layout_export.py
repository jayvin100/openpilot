from pathlib import Path

from openpilot.tools.jotpluggler.layout_export import export_layout


LAYOUT_DIR = Path(__file__).resolve().parents[1] / "plotjuggler" / "layouts"


def test_longitudinal_first_tab_exports_four_panes():
  payload = export_layout(LAYOUT_DIR / "longitudinal.xml")
  panes = payload["panes"]
  assert len(panes) == 4
  assert abs(sum(pane["w"] for pane in panes) - 1.0) < 1e-6
  assert all(pane["h"] == 1.0 for pane in panes)


def test_can_states_nested_splitter_exports_expected_bounds():
  payload = export_layout(LAYOUT_DIR / "can-states.xml")
  panes = payload["panes"]
  assert len(panes) == 5
  assert all(0.0 <= pane["x"] <= 1.0 for pane in panes)
  assert all(0.0 <= pane["y"] <= 1.0 for pane in panes)
  assert all(0.0 < pane["w"] <= 1.0 for pane in panes)
  assert all(0.0 < pane["h"] <= 1.0 for pane in panes)
