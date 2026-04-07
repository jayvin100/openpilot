import numpy as np
from openpilot.common.constants import ACCELERATION_DUE_TO_GRAVITY
from openpilot.common.realtime import DT_CTRL, DT_MDL

MIN_SPEED = 1.0
CONTROL_N = 17
CAR_ROTATION_RADIUS = 0.0
# This is a turn radius smaller than most cars can achieve
MAX_CURVATURE = 0.2
MAX_VEL_ERR = 5.0  # m/s

# EU guidelines
MAX_LATERAL_JERK = 5.0  # m/s^3
MAX_LATERAL_ACCEL_NO_ROLL = 3.0  # m/s^2
ACTION_GRID_ACCEL = 1
ACTION_GRID_MAX_ABS_ACCEL = 5.0
ACTION_GRID_STOP_THRESHOLD = 0.5
ACTION_GRID_STOP_EPS = 1e-6


def clamp(val, min_val, max_val):
  clamped_val = float(np.clip(val, min_val, max_val))
  return clamped_val, clamped_val != val

def smooth_value(val, prev_val, tau, dt=DT_MDL):
  alpha = 1 - np.exp(-dt/tau) if tau > 0 else 1
  return alpha * val + (1 - alpha) * prev_val

def clip_curvature(v_ego, prev_curvature, new_curvature, roll) -> tuple[float, bool]:
  # This function respects ISO lateral jerk and acceleration limits + a max curvature
  v_ego = max(v_ego, MIN_SPEED)
  max_curvature_rate = MAX_LATERAL_JERK / (v_ego ** 2)  # inexact calculation, check https://github.com/commaai/openpilot/pull/24755
  new_curvature = np.clip(new_curvature,
                          prev_curvature - max_curvature_rate * DT_CTRL,
                          prev_curvature + max_curvature_rate * DT_CTRL)

  roll_compensation = roll * ACCELERATION_DUE_TO_GRAVITY
  max_lat_accel = MAX_LATERAL_ACCEL_NO_ROLL + roll_compensation
  min_lat_accel = -MAX_LATERAL_ACCEL_NO_ROLL + roll_compensation
  new_curvature, limited_accel = clamp(new_curvature, min_lat_accel / v_ego ** 2, max_lat_accel / v_ego ** 2)

  new_curvature, limited_max_curv = clamp(new_curvature, -MAX_CURVATURE, MAX_CURVATURE)
  return float(new_curvature), limited_accel or limited_max_curv


def _quadratic_bins(max_abs: float, n_bins: int) -> np.ndarray:
  half = (n_bins - 1) // 2
  idxs = np.arange(-half, half + 1, dtype=np.float64)
  bins = np.sign(idxs) * max_abs * (np.abs(idxs) / half) ** 2
  bins[half] = 0.0
  return bins.astype(np.float32)


def _get_stop_probability_from_action_grid(action_grid, t_idxs, action_t):
  probs = np.asarray(action_grid, dtype=np.float32)
  if probs.ndim != 3 or probs.shape[0] <= ACTION_GRID_ACCEL or len(t_idxs) < probs.shape[1]:
    return None

  accel_bins = _quadratic_bins(ACTION_GRID_MAX_ABS_ACCEL, probs.shape[2])
  stop_probs = probs[ACTION_GRID_ACCEL][:, accel_bins <= 0.0].sum(axis=-1)
  action_grid_t_idxs = np.asarray(t_idxs[:probs.shape[1]], dtype=np.float32)
  return float(np.interp(action_t, action_grid_t_idxs, stop_probs))


def get_accel_from_plan(speeds, accels, t_idxs, action_t=DT_MDL, vEgoStopping=0.05, action_grid=None):
  if len(speeds) == len(t_idxs):
    v_now = speeds[0]
    a_now = accels[0]
    v_target = np.interp(action_t, t_idxs, speeds)
    a_target = 2 * (v_target - v_now) / (action_t) - a_now
    v_target_1sec = np.interp(action_t + 1.0, t_idxs, speeds)
  else:
    v_target = 0.0
    v_target_1sec = 0.0
    a_target = 0.0
  should_stop = (v_target < vEgoStopping and
                 v_target_1sec < vEgoStopping)

  if action_grid is not None:
    stop_probability = _get_stop_probability_from_action_grid(action_grid, t_idxs, action_t)
    if stop_probability is not None:
      should_stop = stop_probability > (ACTION_GRID_STOP_THRESHOLD + ACTION_GRID_STOP_EPS)

  return a_target, should_stop

def curv_from_psis(psi_target, psi_rate, vego, action_t):
  vego = np.clip(vego, MIN_SPEED, np.inf)
  curv_from_psi = psi_target / (vego * action_t)
  return 2*curv_from_psi - psi_rate / vego

def get_curvature_from_plan(yaws, yaw_rates, t_idxs, vego, action_t):
  psi_target = np.interp(action_t, t_idxs, yaws)
  psi_rate = yaw_rates[0]
  return curv_from_psis(psi_target, psi_rate, vego, action_t)
