from pathlib import Path

from PIL import Image, ImageDraw

from openpilot.tools.jotpluggler.validate import compare_image_pair, message_series_paths, parse_layout_spec, sanitized_layout_bytes


class _FakeStruct:
  def __init__(self, payload):
    self.payload = payload

  def to_dict(self, verbose: bool = True):
    return self.payload


class _FakeMessage:
  valid = True
  logMonoTime = 0

  def __init__(self, typ: str, payload):
    self.typ = typ
    setattr(self, typ, payload)

  def which(self):
    return self.typ


def test_parse_layout_spec_extracts_first_tab_services(tmp_path: Path):
  layout = tmp_path / "layout.xml"
  layout.write_text(
    """<?xml version='1.0' encoding='UTF-8'?>
<root>
  <tabbed_widget name="Main Window" parent="main_window">
    <Tab tab_name="tab0" containers="1">
      <Container>
        <DockSplitter orientation="-" count="1" sizes="1">
          <DockArea name="plot">
            <plot style="Lines" mode="TimeSeries">
              <curve name="/carState/vEgo"/>
              <curve name="engaged_accel_plan"/>
            </plot>
          </DockArea>
        </DockSplitter>
      </Container>
    </Tab>
    <Tab tab_name="tab1" containers="1">
      <Container>
        <DockSplitter orientation="-" count="1" sizes="1">
          <DockArea name="plot">
            <plot style="Lines" mode="TimeSeries">
              <curve name="/gpsLocationExternal/hasFix"/>
            </plot>
          </DockArea>
        </DockSplitter>
      </Container>
    </Tab>
    <currentTabIndex index="1"/>
  </tabbed_widget>
  <Plugins>
    <plugin ID="DataLoad Rlog"/>
  </Plugins>
  <customMathEquations>
    <snippet name="engaged_accel_plan">
      <linked_source>/longitudinalPlan/accels/0</linked_source>
      <additional_sources>
        <v1>/carState/brakePressed</v1>
      </additional_sources>
    </snippet>
  </customMathEquations>
</root>
"""
  )

  spec = parse_layout_spec(layout, tab_index=0)
  assert spec.tab_name == "tab0"
  assert spec.services == ("carState", "longitudinalPlan")


def test_sanitized_layout_keeps_only_selected_tab_and_prunes_invalid_curves(tmp_path: Path):
  layout = tmp_path / "layout.xml"
  layout.write_text(
    """<?xml version='1.0' encoding='UTF-8'?>
<root>
  <tabbed_widget name="Main Window" parent="main_window">
    <Tab tab_name="tab0" containers="1">
      <Container>
        <DockSplitter orientation="-" count="1" sizes="1">
          <DockArea name="plot">
            <plot style="Lines" mode="TimeSeries">
              <curve name="/carState/vEgo"/>
              <curve name="/controlsState/missing"/>
              <curve name="valid_snippet"/>
              <curve name="invalid_snippet"/>
            </plot>
          </DockArea>
        </DockSplitter>
      </Container>
    </Tab>
    <Tab tab_name="tab1" containers="1">
      <Container>
        <DockSplitter orientation="-" count="1" sizes="1">
          <DockArea name="plot">
            <plot style="Lines" mode="TimeSeries">
              <curve name="/modelV2/modelExecutionTime"/>
            </plot>
          </DockArea>
        </DockSplitter>
      </Container>
    </Tab>
    <currentTabIndex index="1"/>
  </tabbed_widget>
  <Plugins>
    <plugin ID="DataLoad CSV"/>
    <plugin ID="DataLoad Rlog"/>
    <plugin ID="Cereal Subscriber"/>
  </Plugins>
  <customMathEquations>
    <snippet name="valid_snippet">
      <linked_source>/carState/brakePressed</linked_source>
    </snippet>
    <snippet name="invalid_snippet">
      <linked_source>/carState/doesNotExist</linked_source>
    </snippet>
  </customMathEquations>
  <previouslyLoaded_Datafiles/>
</root>
"""
  )

  sanitized = sanitized_layout_bytes(
    layout,
    tab_index=0,
    available_paths={"/carState/vEgo", "/carState/brakePressed"},
  ).decode("utf-8")
  assert 'currentTabIndex index="0"' in sanitized
  assert sanitized.count("<Tab ") == 1
  assert "DataLoad CSV" not in sanitized
  assert "DataLoad Rlog" in sanitized
  assert "Cereal Subscriber" in sanitized
  assert "previouslyLoaded_Datafiles" not in sanitized
  assert "/controlsState/missing" not in sanitized
  assert "/modelV2/modelExecutionTime" not in sanitized
  assert "invalid_snippet" not in sanitized
  assert "valid_snippet" in sanitized


def test_compare_image_pair_scores_identical_higher(tmp_path: Path):
  ref = tmp_path / "reference.png"
  good = tmp_path / "good.png"
  bad = tmp_path / "bad.png"
  diff_good = tmp_path / "diff_good.png"
  diff_bad = tmp_path / "diff_bad.png"

  base = Image.new("RGB", (400, 200), "black")
  draw = ImageDraw.Draw(base)
  draw.rectangle((40, 30, 360, 170), outline="white", width=3)
  draw.line((60, 140, 340, 60), fill="#00ff00", width=4)
  draw.line((60, 60, 340, 120), fill="#ff6600", width=4)
  base.save(ref)
  base.save(good)

  shifted = Image.new("RGB", (400, 200), "black")
  draw = ImageDraw.Draw(shifted)
  draw.rectangle((90, 30, 390, 170), outline="white", width=3)
  draw.line((100, 145, 360, 70), fill="#00ff00", width=4)
  draw.line((90, 70, 350, 130), fill="#ff6600", width=4)
  shifted.save(bad)

  good_metrics = compare_image_pair(ref, good, diff_good, score_threshold=0.78, compare_size=(200, 100))
  bad_metrics = compare_image_pair(ref, bad, diff_bad, score_threshold=0.78, compare_size=(200, 100))

  assert good_metrics.passed
  assert good_metrics.score > 0.99
  assert bad_metrics.score < good_metrics.score
  assert diff_good.exists()
  assert diff_bad.exists()


def test_message_series_paths_handles_list_payloads():
  msg = _FakeMessage("pandaStates", [_FakeStruct({"canState0": {"totalRxCnt": 1}, "fanPower": 30})])
  paths = message_series_paths(msg)

  assert "/pandaStates/0/canState0/totalRxCnt" in paths
  assert "/pandaStates/0/fanPower" in paths
