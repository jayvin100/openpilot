import numpy as np

from openpilot.selfdrive.modeld.constants import ModelConstants


ACTION_GRID_ACCEL = 1
ACTION_GRID_MAX_ABS_ACCEL = 5.0
ACTION_GRID_T_IDXS = np.asarray(ModelConstants.T_IDXS[:ModelConstants.ACTION_GRID_LEN], dtype=np.float32)


def _quadratic_bins(max_abs: float, n_bins: int) -> np.ndarray:
  half = (n_bins - 1) // 2
  idxs = np.arange(-half, half + 1, dtype=np.float64)
  bins = np.sign(idxs) * max_abs * (np.abs(idxs) / half) ** 2
  bins[half] = 0.0
  return bins.astype(np.float32)


ACCEL_BINS = _quadratic_bins(ACTION_GRID_MAX_ABS_ACCEL, ModelConstants.ACTION_GRID_WIDTH)


STOP_ACCEL_BIN_MASK = ACCEL_BINS <= 0.0
SHOULD_STOP_THRESHOLD = 0.5
SHOULD_STOP_EPS = 1e-6


def get_stop_probability_from_action_grid(action_grid: np.ndarray, action_t: float) -> float:
  probs = np.asarray(action_grid, dtype=np.float32).reshape(
    ModelConstants.ACTION_GRID_NUM_CHANNELS,
    ModelConstants.ACTION_GRID_LEN,
    ModelConstants.ACTION_GRID_WIDTH,
  )
  stop_probs = probs[ACTION_GRID_ACCEL][:, STOP_ACCEL_BIN_MASK].sum(axis=-1)
  return float(np.interp(action_t, ACTION_GRID_T_IDXS, stop_probs))


def get_should_stop_from_action_grid(action_grid: np.ndarray, action_t: float) -> bool:
  return get_stop_probability_from_action_grid(action_grid, action_t) > (SHOULD_STOP_THRESHOLD + SHOULD_STOP_EPS)
