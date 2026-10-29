"""Run in the openpilot Python environment after applying patches 010 and 011."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from opendbc.car.structs import car
from openpilot.jay import road_mode
from openpilot.jay.update_check import check_remote


class Messages(dict):
  healthy = True

  def all_checks(self, services):
    return self.healthy


class RoadModeTest(unittest.TestCase):
  def setUp(self):
    self.tmp = tempfile.TemporaryDirectory()
    self.addCleanup(self.tmp.cleanup)
    self.marker = Path(self.tmp.name) / 'request'
    self.mock = patch.object(road_mode, 'REQUEST', self.marker)
    self.mock.start()
    self.addCleanup(self.mock.stop)
    self.mode = road_mode.OffroadOverride()
    self.sm = Messages(carState=SimpleNamespace(standstill=True, gearShifter=car.CarState.GearShifter.park),
                       selfdriveState=SimpleNamespace(enabled=False))

  def test_enter_and_resume(self):
    self.marker.touch()
    self.assertFalse(self.mode.allow_onroad(self.sm, True))
    # Driving processes stop offroad, so their messages no longer need to be alive.
    self.sm.healthy = False
    self.assertFalse(self.mode.allow_onroad(self.sm, True))
    self.marker.unlink()
    self.assertTrue(self.mode.allow_onroad(self.sm, True))

  def test_reject_unsafe_requests(self):
    for field, value in [('standstill', False), ('gearShifter', car.CarState.GearShifter.drive)]:
      with self.subTest(field=field):
        before = getattr(self.sm['carState'], field)
        setattr(self.sm['carState'], field, value)
        self.marker.touch()
        self.assertTrue(self.mode.allow_onroad(self.sm, True))
        self.assertFalse(self.marker.exists())
        setattr(self.sm['carState'], field, before)
    self.sm['selfdriveState'].enabled = True
    self.marker.touch()
    self.assertTrue(self.mode.allow_onroad(self.sm, True))
    self.sm['selfdriveState'].enabled = False
    self.sm.healthy = False
    self.marker.touch()
    self.assertTrue(self.mode.allow_onroad(self.sm, True))

  def test_ignition_off_and_restart_clear(self):
    self.marker.touch()
    self.assertFalse(self.mode.allow_onroad(self.sm, True))
    self.assertTrue(self.mode.allow_onroad(self.sm, False))
    self.assertFalse(self.marker.exists())
    self.marker.touch()
    road_mode.OffroadOverride()
    self.assertFalse(self.marker.exists())


class UpdateCheckTest(unittest.TestCase):
  @patch('openpilot.jay.update_check.subprocess.check_output')
  def test_current_and_new_build(self, run):
    for current, expected in [('abc', 'Up to date'), ('old', 'Update available')]:
      run.side_effect = ['abc\trefs/heads/nightly-jay\n', current]
      self.assertTrue(check_remote('/tmp', 'nightly-jay').startswith(expected))
    for call in run.call_args_list:
      self.assertIn(call.args[0][1], ('ls-remote', 'rev-parse'))
      self.assertEqual(call.kwargs['timeout'], 20)

  @patch('openpilot.jay.update_check.subprocess.check_output')
  def test_bad_branch_and_network_failure(self, run):
    for branch in ['', '--heads']:
      with self.assertRaises(ValueError):
        check_remote('/tmp', branch)
    run.side_effect = subprocess.TimeoutExpired('git', 20)
    with self.assertRaises(subprocess.TimeoutExpired):
      check_remote('/tmp', 'nightly-jay')
    run.side_effect = ['abc\trefs/heads/other\n']
    with self.assertRaises(ValueError):
      check_remote('/tmp', 'nightly-jay')


if __name__ == '__main__':
  unittest.main()
