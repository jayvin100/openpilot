import numpy as np

from openpilot.selfdrive.modeld.constants import ModelConstants
from xx.common.action_grid import ACCEL_BINS, ACTION_GRID_ACCEL, ACTION_GRID_T_IDXS


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
