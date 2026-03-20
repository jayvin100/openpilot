import numpy as np
from typing import Any
from functools import cache
from collections import deque

from cereal import log
from openpilot.common.transformations.orientation import rot_from_euler, euler_from_rot


def masked_symmetric_moving_average(x: np.ndarray, mask: np.ndarray, k: int, sigma: float) -> np.ndarray:
  assert k >= 1 and k % 2 == 1, "k must be positive and odd"
  pad = k // 2
  i = np.arange(k) - pad
  w = np.exp(-0.5 * (i / sigma) ** 2)
  w /= w.sum()
  xp = np.pad(x * mask, pad, mode="edge")
  mp = np.pad(mask, pad, mode="edge")
  num = np.convolve(xp, w, mode="valid")
  den = np.convolve(mp, w, mode="valid")
  return np.divide(num, den, out=np.full_like(num, np.nan, dtype=np.float64), where=den != 0)


@cache
def fft_next_good_size(n: int) -> int:
    """
    smallest composite of 2, 3, 5, 7, 11 that is >= n
    inspired by pocketfft
    """
    if n <= 6:
      return n
    best, f2 = 2 * n, 1
    while f2 < best:
        f23 = f2
        while f23 < best:
            f235 = f23
            while f235 < best:
                f2357 = f235
                while f2357 < best:
                    f235711 = f2357
                    while f235711 < best:
                        best = f235711 if f235711 >= n else best
                        f235711 *= 11
                    f2357 *= 7
                f235 *= 5
            f23 *= 3
        f2 *= 2
    return best


def parabolic_peak_interp(R, max_index):
  if max_index == 0 or max_index == len(R) - 1:
    return max_index

  y_m1, y_0, y_p1 = R[max_index - 1], R[max_index], R[max_index + 1]
  offset = 0.5 * (y_p1 - y_m1) / (2 * y_0 - y_p1 - y_m1)

  return max_index + offset


def rotate_cov(rot_matrix, cov_in):
  return rot_matrix @ cov_in @ rot_matrix.T

def rotate_std(rot_matrix, std_in):
  return np.sqrt(np.diag(rotate_cov(rot_matrix, np.diag(std_in**2))))


class NPQueue:
  def __init__(self, maxlen: int, rowsize: int) -> None:
    self.maxlen = maxlen
    self.arr = np.empty((0, rowsize))

  def __len__(self) -> int:
    return len(self.arr)

  def append(self, pt: list[float]) -> None:
    if len(self.arr) < self.maxlen:
      self.arr = np.append(self.arr, [pt], axis=0)
    else:
      self.arr[:-1] = self.arr[1:]
      self.arr[-1] = pt


class PointBuckets:
  def __init__(self, x_bounds: list[tuple[float, float]], min_points: list[float], min_points_total: int, points_per_bucket: int, rowsize: int) -> None:
    self.x_bounds = x_bounds
    self.buckets = {bounds: NPQueue(maxlen=points_per_bucket, rowsize=rowsize) for bounds in x_bounds}
    self.buckets_min_points = dict(zip(x_bounds, min_points, strict=True))
    self.min_points_total = min_points_total

  def __len__(self) -> int:
    return sum([len(v) for v in self.buckets.values()])

  def is_valid(self) -> bool:
    individual_buckets_valid = all(len(v) >= min_pts for v, min_pts in zip(self.buckets.values(), self.buckets_min_points.values(), strict=True))
    total_points_valid = self.__len__() >= self.min_points_total
    return individual_buckets_valid and total_points_valid

  def get_valid_percent(self) -> int:
    total_points_perc = min(self.__len__() / self.min_points_total * 100, 100)
    individual_buckets_perc = min(min(len(v) / min_pts * 100 for v, min_pts in
                                      zip(self.buckets.values(), self.buckets_min_points.values(), strict=True)), 100)
    return int((total_points_perc + individual_buckets_perc) / 2)

  def is_calculable(self) -> bool:
    return all(len(v) > 0 for v in self.buckets.values())

  def add_point(self, x: float, y: float) -> None:
    raise NotImplementedError

  def get_points(self, num_points: int | None = None) -> Any:
    points = np.vstack([x.arr for x in self.buckets.values()])
    if num_points is None:
      return points
    return points[np.random.choice(np.arange(len(points)), min(len(points), num_points), replace=False)]

  def load_points(self, points: list[list[float]]) -> None:
    for point in points:
      self.add_point(*point)


class ParameterEstimator:
  """ Base class for parameter estimators """
  def reset(self) -> None:
    raise NotImplementedError

  def handle_log(self, t: int, which: str, msg: log.Event) -> None:
    raise NotImplementedError

  def get_msg(self, valid: bool, with_points: bool) -> log.Event:
    raise NotImplementedError


class Measurement:
  x, y, z = (property(lambda self: self.xyz[0]), property(lambda self: self.xyz[1]), property(lambda self: self.xyz[2]))
  x_std, y_std, z_std = (property(lambda self: self.xyz_std[0]), property(lambda self: self.xyz_std[1]), property(lambda self: self.xyz_std[2]))
  roll, pitch, yaw = x, y, z
  roll_std, pitch_std, yaw_std = x_std, y_std, z_std

  def __init__(self, xyz: np.ndarray, xyz_std: np.ndarray):
    self.xyz: np.ndarray = xyz
    self.xyz_std: np.ndarray = xyz_std

  @classmethod
  def from_measurement_xyz(cls, measurement: log.LivePose.XYZMeasurement) -> 'Measurement':
    return cls(
      xyz=np.array([measurement.x, measurement.y, measurement.z]),
      xyz_std=np.array([measurement.xStd, measurement.yStd, measurement.zStd])
    )


class Pose:
  def __init__(self, orientation: Measurement, velocity: Measurement, acceleration: Measurement, angular_velocity: Measurement):
    self.orientation = orientation
    self.velocity = velocity
    self.acceleration = acceleration
    self.angular_velocity = angular_velocity

  @classmethod
  def from_live_pose(cls, live_pose: log.LivePose) -> 'Pose':
    return Pose(
      orientation=Measurement.from_measurement_xyz(live_pose.orientationNED),
      velocity=Measurement.from_measurement_xyz(live_pose.velocityDevice),
      acceleration=Measurement.from_measurement_xyz(live_pose.accelerationDevice),
      angular_velocity=Measurement.from_measurement_xyz(live_pose.angularVelocityDevice)
    )


class PoseCalibrator:
  def __init__(self):
    self.calib_valid = False
    self.calib_from_device = np.eye(3)

  def _transform_calib_from_device(self, meas: Measurement):
    new_xyz = self.calib_from_device @ meas.xyz
    new_xyz_std = rotate_std(self.calib_from_device, meas.xyz_std)
    return Measurement(new_xyz, new_xyz_std)

  def _ned_from_calib(self, orientation: Measurement):
    ned_from_device = rot_from_euler(orientation.xyz)
    ned_from_calib = ned_from_device @ self.calib_from_device.T
    ned_from_calib_euler_meas = Measurement(euler_from_rot(ned_from_calib), np.full(3, np.nan))
    return ned_from_calib_euler_meas

  def build_calibrated_pose(self, pose: Pose) -> Pose:
    ned_from_calib_euler = self._ned_from_calib(pose.orientation)
    angular_velocity_calib = self._transform_calib_from_device(pose.angular_velocity)
    acceleration_calib = self._transform_calib_from_device(pose.acceleration)
    velocity_calib = self._transform_calib_from_device(pose.velocity)

    return Pose(ned_from_calib_euler, velocity_calib, acceleration_calib, angular_velocity_calib)

  def feed_live_calib(self, live_calib: log.LiveCalibrationData):
    calib_rpy = np.array(live_calib.rpyCalib)
    device_from_calib = rot_from_euler(calib_rpy)
    self.calib_from_device = device_from_calib.T
    self.calib_valid = live_calib.calStatus == log.LiveCalibrationData.Status.calibrated


class PoseSmoother:
  def __init__(self, k: int, sigma: float):
    self.k, self.sigma = k, sigma
    self.t_buf = deque(maxlen=k)
    self.pose_buf = deque(maxlen=k)
    self.w = self._symmetric_average_weights(k, sigma)

  def _symmetric_average_weights(self, k, sigma) -> np.ndarray:
    i = np.arange(k) - (k // 2)
    w = np.exp(-0.5 * (i / sigma) ** 2)
    w /= w.sum()
    return w

  def _symmetric_average(self, meas: list[Measurement]) -> Measurement:
    xyz = np.array([meas_i.xyz for meas_i in meas])
    xyz_std = np.array([meas_i.xyz_std for meas_i in meas])
    avg_xyz = np.einsum('i,ij->j', self.w, xyz)
    avg_xyz_std = np.einsum('i,ij->j', self.w, xyz_std)
    return Measurement(avg_xyz, avg_xyz_std)

  def feed_pose(self, t: int, pose: Pose):
    self.t_buf.append(t)
    self.pose_buf.append(pose)

  def build_smoothed_pose(self) -> tuple[int, Pose] | None:
    if len(self.pose_buf) != self.k or len(self.t_buf) != self.k:
      return None

    t = self.t_buf[self.k // 2]
    ned_from_calib_smooth = self._symmetric_average([pose.orientation for pose in self.pose_buf])
    velocity_smooth = self._symmetric_average([pose.velocity for pose in self.pose_buf])
    acceleration_smooth = self._symmetric_average([pose.acceleration for pose in self.pose_buf])
    angular_velocity_smooth = self._symmetric_average([pose.angular_velocity for pose in self.pose_buf])
    return t, Pose(ned_from_calib_smooth, velocity_smooth, acceleration_smooth, angular_velocity_smooth)
