"""Run with PYTHONPATH pointing at a patched openpilot checkout."""
import pickle
import struct
import tempfile
import unittest
from pathlib import Path

from openpilot.jay.model_info import installed_model

CD210 = '6a7d09ad-bcc9-43bc-916d-29287e60cee2/200/a27b3122-733e-4a65-938b-acfebebbe5e8/100'


class ModelInfoTest(unittest.TestCase):
  def setUp(self):
    self.tmp = tempfile.TemporaryDirectory()
    self.addCleanup(self.tmp.cleanup)
    self.path = Path(self.tmp.name) / 'model.pkl'

  def artifact(self, checkpoint):
    payload = pickle.dumps({'metadata': {'metadata': {'output_slices': 'unused', 'model_checkpoint': checkpoint}},
                            'run': 'unused'}, protocol=4)
    self.path.write_bytes(struct.pack('<q', len(payload)) + payload)

  def test_identifies_known_checkpoint(self):
    self.artifact(CD210)
    info = installed_model(self.path)
    self.assertEqual(info.name, 'CD210')
    self.assertEqual(info.released, '4 Feb 2026')
    self.assertEqual(info.checkpoint, CD210)

  def test_new_model_never_reuses_old_release(self):
    checkpoint = '11111111-1111-1111-1111-111111111111/200'
    self.artifact(checkpoint)
    info = installed_model(self.path)
    self.assertEqual(info.name, 'comma driving model')
    self.assertEqual(info.released, 'Unavailable')
    self.assertEqual(info.checkpoint, checkpoint)

  def test_missing_truncated_invalid_and_missing_metadata(self):
    self.assertEqual(installed_model(self.path).name, 'Unavailable')
    for data in [b'', b'123', struct.pack('<q', -1), struct.pack('<q', 100) + b'broken']:
      self.path.write_bytes(data)
      self.assertEqual(installed_model(self.path).name, 'Unavailable')
    self.artifact('not a checkpoint')
    self.assertEqual(installed_model(self.path).name, 'Unavailable')
    payload = pickle.dumps({'metadata': {}}, protocol=4)
    self.path.write_bytes(struct.pack('<q', len(payload)) + payload)
    self.assertEqual(installed_model(self.path).name, 'Unavailable')

  def test_does_not_execute_pickle(self):
    marker = Path(self.tmp.name) / 'must-not-exist'

    class Dangerous:
      def __reduce__(self):
        return eval, (f'open({str(marker)!r}, "w").close()',)

    payload = pickle.dumps({'run': Dangerous(), 'model_checkpoint': CD210}, protocol=4)
    self.path.write_bytes(struct.pack('<q', len(payload)) + payload)
    self.assertEqual(installed_model(self.path).name, 'CD210')
    self.assertFalse(marker.exists())


if __name__ == '__main__':
  unittest.main()
