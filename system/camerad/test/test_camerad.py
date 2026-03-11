import os
import time
import pytest
import numpy as np

from cereal.services import SERVICE_LIST
from openpilot.tools.lib.log_time_series import msgs_to_time_series
from openpilot.system.camerad.snapshot import get_snapshots
from openpilot.selfdrive.test.helpers import collect_logs, log_collector, processes_context

TEST_TIMESPAN = 10
CAMERAS = ('roadCameraState', 'driverCameraState', 'wideRoadCameraState')
EXPOSURE_STABLE_COUNT = 3
EXPOSURE_RANGE = (0.15, 0.35)
MAX_TEST_TIME = 25


def _numpy_rgb2gray(im):
  return np.clip(im[:,:,2] * 0.114 + im[:,:,1] * 0.587 + im[:,:,0] * 0.299, 0, 255).astype(np.uint8)

def _exposure_stats(im):
  h, w = im.shape[:2]
  gray = _numpy_rgb2gray(im[h//10:9*h//10, w//10:9*w//10])
  return float(np.median(gray) / 255.), float(np.mean(gray) / 255.)

def _in_range(median, mean):
  lo, hi = EXPOSURE_RANGE
  return lo < median < hi and lo < mean < hi

def _exposure_stable(results):
  return all(
    len(v) >= EXPOSURE_STABLE_COUNT and all(_in_range(*s) for s in v[-EXPOSURE_STABLE_COUNT:])
    for v in results.values()
  )


def run_and_log(procs, services, duration):
  with processes_context(procs):
    return collect_logs(services, duration)

@pytest.fixture(scope="module")
def logs():
  """Clean camerad session for timing tests (no VisionIPC recv interference).
     VisionIPC recv from get_snapshots can cause occasional PHY switch timing
     issues with IFE sharing, so timing tests use this dedicated fixture."""
  raw_logs = run_and_log(["camerad"], CAMERAS, TEST_TIMESPAN)
  ts = msgs_to_time_series(raw_logs)

  elapsed = TEST_TIMESPAN
  for cam in CAMERAS:
    cnt = len(ts[cam]['t'])
    expected = SERVICE_LIST[cam].frequency * elapsed
    print(f"  {cam}: {cnt} frames in {elapsed:.1f}s ({cnt/elapsed:.1f}fps, expected {expected:.0f})")

  for cam in CAMERAS:
    expected_frames = SERVICE_LIST[cam].frequency * elapsed
    cnt = len(ts[cam]['t'])
    assert expected_frames*0.8 < cnt < expected_frames*1.2, f"unexpected frame count {cam}: {expected_frames=}, got {cnt}"

    sof_ms = ts[cam]['timestampSof'] / 1e6
    d_fid = np.diff(ts[cam]['frameId'])
    d_sof = np.diff(sof_ms)
    expected_period = 1000 / SERVICE_LIST[cam].frequency
    dts = np.abs(d_sof - d_fid * expected_period)
    # Skip first 5 timing measurements: FSIN stagger takes a few frames to stabilize
    dts_steady = dts[5:]
    assert (dts_steady < 1.0).all(), f"{cam} dts(ms) out of spec: max diff {dts_steady.max():.2f}ms, 99 percentile {np.percentile(dts_steady, 99):.2f}ms"

  return ts

@pytest.fixture(scope="module")
def exposure_data():
  """Camerad session with get_snapshots for exposure testing."""
  with processes_context(["camerad"]), log_collector(CAMERAS) as (raw_logs, lock):
    exposure = {cam: [] for cam in CAMERAS}
    start = time.monotonic()
    while time.monotonic() - start < MAX_TEST_TIME:
      rpic, dpic = get_snapshots(frame="roadCameraState", front_frame="driverCameraState")
      wpic, _ = get_snapshots(frame="wideRoadCameraState")
      for cam, img in zip(CAMERAS, [rpic, dpic, wpic], strict=True):
        exposure[cam].append(_exposure_stats(img))

      if time.monotonic() - start >= TEST_TIMESPAN and _exposure_stable(exposure):
        break

  return exposure

@pytest.mark.tici
class TestCamerad:
  @pytest.mark.parametrize("cam", CAMERAS)
  def test_camera_exposure(self, exposure_data, cam):
    lo, hi = EXPOSURE_RANGE
    checks = exposure_data[cam]
    assert len(checks) >= EXPOSURE_STABLE_COUNT, f"{cam}: only got {len(checks)} samples"

    # check that exposure converges into the valid range
    passed = sum(_in_range(med, mean) for med, mean in checks)
    assert passed >= EXPOSURE_STABLE_COUNT, \
      f"{cam}: only {passed}/{len(checks)} checks in range. " + \
      " | ".join(f"#{i+1}: med={m:.4f} mean={u:.4f}" for i, (m, u) in enumerate(checks))

    # check that exposure is stable once converged (no regressions)
    in_range = False
    for i, (median, mean) in enumerate(checks):
      ok = _in_range(median, mean)
      if in_range and not ok:
        pytest.fail(f"{cam}: exposure regressed on sample {i+1} " +
                    f"(median={median:.4f}, mean={mean:.4f}, expected: ({lo}, {hi}))")
      in_range = ok

  def test_frame_skips(self, logs):
    for c in CAMERAS:
      diffs = np.diff(logs[c]['frameId'])
      assert np.all(diffs > 0) and np.all(diffs <= 5), \
        f"{c} has frame skips: diffs range [{diffs.min()}, {diffs.max()}]"

  def test_frame_sync(self, logs):
    SYNCED_CAMS = ('roadCameraState', 'wideRoadCameraState')

    # Build frame_id → index mapping for each camera
    fid_idx = {cam: {fid: i for i, fid in enumerate(logs[cam]['frameId'])} for cam in CAMERAS}

    # Find common frame_ids (exclude last 10 for boundary effects)
    common_fids = set(logs[CAMERAS[0]]['frameId'][:-10])
    for cam in CAMERAS[1:]:
      common_fids &= set(logs[cam]['frameId'])
    common_fids = sorted(common_fids)
    assert len(common_fids) > 20, f"too few common frames: {len(common_fids)}"

    # road and wide cameras should be synced within 1.1ms
    laggy_frames = {}
    for fid in common_fids:
      ts = [logs[cam]['timestampSof'][fid_idx[cam][fid]] for cam in SYNCED_CAMS]
      diff_ms = (max(ts) - min(ts)) / 1e6
      if diff_ms > 1.1:
        laggy_frames[fid] = diff_ms
    assert len(laggy_frames) == 0, f"Frames not synced properly: {laggy_frames=}"

    # driver camera: either staggered ~24ms (OX03C10 IFE sharing) or synced <2ms (OS04C10 BPS fallback)
    for fid in common_fids:
      offset_ms = abs(logs['driverCameraState']['timestampSof'][fid_idx['driverCameraState'][fid]] -
                      logs['roadCameraState']['timestampSof'][fid_idx['roadCameraState'][fid]]) / 1e6
      assert offset_ms < 2 or 15 < offset_ms < 30, f"driver camera offset out of range at frame {fid}: {offset_ms:.1f}ms (expected <2ms or 15-30ms)"

  def test_sanity_checks(self, logs):
    self._sanity_checks(logs)

  def _sanity_checks(self, ts):
    for c in CAMERAS:
      assert c in ts
      assert len(ts[c]['t']) > 20

      # not a valid request id
      assert 0 not in ts[c]['requestId']

      # should monotonically increase
      assert np.all(np.diff(ts[c]['frameId']) >= 1)
      assert np.all(np.diff(ts[c]['requestId']) >= 1)

      # EOF > SOF
      assert np.all((ts[c]['timestampEof'] - ts[c]['timestampSof']) > 0)

      # logMonoTime > SOF
      assert np.all((ts[c]['t'] - ts[c]['timestampSof']/1e9) > 1e-7)

      # logMonoTime > EOF, needs some tolerance since EOF is (SOF + readout time) but there is noise in the SOF timestamping (done via IRQ)
      assert np.mean((ts[c]['t'] - ts[c]['timestampEof']/1e9) > 1e-7) > 0.7  # should be mostly logMonoTime > EOF
      assert np.all((ts[c]['t'] - ts[c]['timestampEof']/1e9) > -0.10)        # when EOF > logMonoTime, it should never be more than two frames

  def test_stress_test(self):
    os.environ['SPECTRA_ERROR_PROB'] = '0.008'
    try:
      logs = run_and_log(["camerad", ], CAMERAS, 10)
    finally:
      del os.environ['SPECTRA_ERROR_PROB']
    ts = msgs_to_time_series(logs)

    # errors should cause some frame drops (fewer frames than expected)
    for c in CAMERAS:
      cnt = len(ts[c]['t'])
      expected = SERVICE_LIST[c].frequency * 10
      assert cnt < expected * 0.95, f"{c}: expected frame drops from errors, got {cnt}/{expected}"

    self._sanity_checks(ts)
