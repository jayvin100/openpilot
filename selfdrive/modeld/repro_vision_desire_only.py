#!/usr/bin/env python3
import json
import os
import pickle

import numpy as np
from tinygrad.device import Device
from tinygrad.engine.jit import TinyJit
from tinygrad.helpers import Context
from tinygrad.nn.onnx import OnnxRunner
from tinygrad.tensor import Tensor
from tinygrad.uop.ops import UOpMetaClass

from openpilot.common.transformations.camera import _ar_ox_fisheye, _os_fisheye
from openpilot.common.transformations.model import MEDMODEL_INPUT_SIZE
from openpilot.selfdrive.modeld.compile_modeld import make_frame_prepare, shift_and_sample
from openpilot.selfdrive.modeld.constants import ModelConstants
from openpilot.system.camerad.cameras.nv12_info import get_nv12_info


MODELS_DIR = os.path.join(os.path.dirname(__file__), "models")
SEED = int(os.environ.get("SEED", "0"))
FRAME_COUNT = int(os.environ.get("FRAME_COUNT", "2"))
CAMERA = os.environ.get("CAMERA", "tizi")
CLEAR_UOP_CACHE = bool(int(os.environ.get("CLEAR_UOP_CACHE", "0")))
RANDOM_DESIRE = bool(int(os.environ.get("RANDOM_DESIRE", "0")))


def get_camera_dims():
  if CAMERA == "tizi":
    return _ar_ox_fisheye.width, _ar_ox_fisheye.height
  if CAMERA == "mici":
    return _os_fisheye.width, _os_fisheye.height
  raise ValueError(f"unknown CAMERA={CAMERA!r}")


def make_state(vision_input_shapes, policy_input_shapes, frame_skip):
  img = vision_input_shapes['img']
  n_frames = img[1] // 6
  img_buf_shape = (frame_skip * (n_frames - 1) + 1, 6, img[2], img[3])
  dp = policy_input_shapes['desire_pulse']
  return {
    "img_buf": Tensor.zeros(img_buf_shape, dtype="uint8").contiguous().realize(),
    "big_img_buf": Tensor.zeros(img_buf_shape, dtype="uint8").contiguous().realize(),
    "desire_q": Tensor.zeros(frame_skip * dp[1], dp[0], dp[2]).contiguous().realize(),
  }


def make_runner(vision_runner, cam_w, cam_h, frame_skip):
  model_w, model_h = MEDMODEL_INPUT_SIZE
  frame_prepare = make_frame_prepare(cam_w, cam_h, model_w, model_h)

  def sample_skip(buf):
    return buf[::frame_skip].contiguous().flatten(0, 1).unsqueeze(0)

  def sample_desire(buf):
    return buf.reshape(-1, frame_skip, *buf.shape[1:]).max(1).flatten(0, 1).unsqueeze(0)

  def run(img_buf, big_img_buf, desire_q, tfm, big_tfm, desire, frame, big_frame):
    with Context(IMAGE=0):
      img = shift_and_sample(img_buf, frame_prepare(frame, tfm.to(Device.DEFAULT)).unsqueeze(0), sample_skip)
      big_img = shift_and_sample(big_img_buf, frame_prepare(big_frame, big_tfm.to(Device.DEFAULT)).unsqueeze(0), sample_skip)
    vision_out = next(iter(vision_runner({"img": img, "big_img": big_img}).values())).cast("float32")
    desire_buf = shift_and_sample(desire_q, desire.to(Device.DEFAULT).reshape(1, 1, -1), sample_desire)
    return vision_out, desire_buf

  return run


def main():
  cam_w, cam_h = get_camera_dims()
  with open(os.path.join(MODELS_DIR, "driving_vision_metadata.pkl"), "rb") as f:
    vision_metadata = pickle.load(f)
  with open(os.path.join(MODELS_DIR, "driving_on_policy_metadata.pkl"), "rb") as f:
    policy_metadata = pickle.load(f)

  frame_skip = ModelConstants.MODEL_RUN_FREQ // ModelConstants.MODEL_CONTEXT_FREQ
  vision_runner = OnnxRunner(os.path.join(MODELS_DIR, "driving_vision.onnx"))
  direct_run = make_runner(vision_runner, cam_w, cam_h, frame_skip)
  jit_run = TinyJit(direct_run)

  _, _, _, yuv_size = get_nv12_info(cam_w, cam_h)
  desire_len = policy_metadata["input_shapes"]["desire_pulse"][2]
  rng = np.random.default_rng(SEED)
  steps = []
  for _ in range(FRAME_COUNT):
    steps.append({
      "frame": rng.integers(0, 256, size=yuv_size, dtype=np.uint8),
      "big_frame": rng.integers(0, 256, size=yuv_size, dtype=np.uint8),
      "tfm": np.eye(3, dtype=np.float32),
      "big_tfm": np.eye(3, dtype=np.float32),
      "desire": rng.standard_normal((desire_len,), dtype=np.float32) if RANDOM_DESIRE else np.zeros((desire_len,), dtype=np.float32),
    })

  warm_state = make_state(vision_metadata["input_shapes"], policy_metadata["input_shapes"], frame_skip)
  for step in steps:
    jit_run(
      **warm_state,
      tfm=Tensor(step["tfm"].copy(), device="NPY").realize(),
      big_tfm=Tensor(step["big_tfm"].copy(), device="NPY").realize(),
      desire=Tensor(step["desire"].copy(), device="NPY").realize(),
      frame=Tensor(step["frame"].copy(), dtype="uint8").realize(),
      big_frame=Tensor(step["big_frame"].copy(), dtype="uint8").realize(),
    )

  loaded_run = pickle.loads(pickle.dumps(jit_run))

  def run_mode(name, runner):
    state = make_state(vision_metadata["input_shapes"], policy_metadata["input_shapes"], frame_skip)
    vision_head = []
    desire_head = []
    for step in steps:
      if name == "jit" and CLEAR_UOP_CACHE:
        UOpMetaClass.ucache.clear()
      vision_out, desire_buf = runner(
        **state,
        tfm=Tensor(step["tfm"].copy(), device="NPY").realize(),
        big_tfm=Tensor(step["big_tfm"].copy(), device="NPY").realize(),
        desire=Tensor(step["desire"].copy(), device="NPY").realize(),
        frame=Tensor(step["frame"].copy(), dtype="uint8").realize(),
        big_frame=Tensor(step["big_frame"].copy(), dtype="uint8").realize(),
      )
      vision_head.append(float(vision_out.realize().numpy().reshape(-1)[0]))
      desire_head.append(float(desire_buf.realize().numpy().reshape(-1)[0]))
    return {"vision_head": vision_head, "desire_head": desire_head}

  print(json.dumps({
    "camera": CAMERA,
    "seed": SEED,
    "clear_uop_cache": CLEAR_UOP_CACHE,
    "random_desire": RANDOM_DESIRE,
    "jit": run_mode("jit", loaded_run),
    "unjit": run_mode("unjit", direct_run),
  }))


if __name__ == "__main__":
  main()
