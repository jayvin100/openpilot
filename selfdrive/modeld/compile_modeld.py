#!/usr/bin/env python3
import time
import pickle
import numpy as np
from pathlib import Path
from tinygrad.tensor import Tensor
from tinygrad.helpers import Context
from tinygrad.device import Device
from tinygrad.engine.jit import TinyJit

from openpilot.system.camerad.cameras.nv12_info import get_nv12_info
from openpilot.common.transformations.model import MEDMODEL_INPUT_SIZE, DM_INPUT_SIZE
from openpilot.common.transformations.camera import _ar_ox_fisheye, _os_fisheye

MODELS_DIR = Path(__file__).parent / 'models'

CAMERA_CONFIGS = [
  (_ar_ox_fisheye.width, _ar_ox_fisheye.height),  # tici: 1928x1208
  (_os_fisheye.width, _os_fisheye.height),        # mici: 1344x760
]

UV_SCALE_MATRIX = np.array([[0.5, 0, 0], [0, 0.5, 0], [0, 0, 1]], dtype=np.float32)
UV_SCALE_MATRIX_INV = np.linalg.inv(UV_SCALE_MATRIX)

def policy_pkl_path(w, h):
  return MODELS_DIR / f'driving_{w}x{h}_tinygrad.pkl'


def dm_warp_pkl_path(w, h):
  return MODELS_DIR / f'dm_warp_{w}x{h}_tinygrad.pkl'


def warp_perspective_tinygrad(src_flat, M_inv, dst_shape, src_shape, stride_pad):
  w_dst, h_dst = dst_shape
  h_src, w_src = src_shape

  x = Tensor.arange(w_dst).reshape(1, w_dst).expand(h_dst, w_dst).reshape(-1)
  y = Tensor.arange(h_dst).reshape(h_dst, 1).expand(h_dst, w_dst).reshape(-1)

  # inline 3x3 matmul as elementwise to avoid reduce op (enables fusion with gather)
  src_x = M_inv[0, 0] * x + M_inv[0, 1] * y + M_inv[0, 2]
  src_y = M_inv[1, 0] * x + M_inv[1, 1] * y + M_inv[1, 2]
  src_w = M_inv[2, 0] * x + M_inv[2, 1] * y + M_inv[2, 2]

  src_x = src_x / src_w
  src_y = src_y / src_w

  x_nn_clipped = Tensor.round(src_x).clip(0, w_src - 1).cast('int')
  y_nn_clipped = Tensor.round(src_y).clip(0, h_src - 1).cast('int')
  idx = y_nn_clipped * (w_src + stride_pad) + x_nn_clipped

  return src_flat[idx]


def frames_to_tensor(frames, model_w, model_h):
  H = (frames.shape[0] * 2) // 3
  W = frames.shape[1]
  in_img1 = Tensor.cat(frames[0:H:2, 0::2],
                       frames[1:H:2, 0::2],
                       frames[0:H:2, 1::2],
                       frames[1:H:2, 1::2],
                       frames[H:H+H//4].reshape((H//2, W//2)),
                       frames[H+H//4:H+H//2].reshape((H//2, W//2)), dim=0).reshape((6, H//2, W//2))
  return in_img1


def make_frame_prepare(cam_w, cam_h, model_w, model_h):
  stride, y_height, uv_height, _ = get_nv12_info(cam_w, cam_h)
  uv_offset = stride * y_height
  stride_pad = stride - cam_w

  def frame_prepare_tinygrad(input_frame, M_inv):
    # UV_SCALE @ M_inv @ UV_SCALE_INV simplifies to elementwise scaling
    M_inv_uv = M_inv * Tensor([[1.0, 1.0, 0.5], [1.0, 1.0, 0.5], [2.0, 2.0, 1.0]])
    # deinterleave NV12 UV plane (UVUV... -> separate U, V)
    uv = input_frame[uv_offset:uv_offset + uv_height * stride].reshape(uv_height, stride)
    with Context(SPLIT_REDUCEOP=0):
      y = warp_perspective_tinygrad(input_frame[:cam_h*stride],
                                    M_inv, (model_w, model_h),
                                    (cam_h, cam_w), stride_pad).realize()
      u = warp_perspective_tinygrad(uv[:cam_h//2, :cam_w:2].flatten(),
                                    M_inv_uv, (model_w//2, model_h//2),
                                    (cam_h//2, cam_w//2), 0).realize()
      v = warp_perspective_tinygrad(uv[:cam_h//2, 1:cam_w:2].flatten(),
                                    M_inv_uv, (model_w//2, model_h//2),
                                    (cam_h//2, cam_w//2), 0).realize()
    yuv = y.cat(u).cat(v).reshape((model_h * 3 // 2, model_w))
    tensor = frames_to_tensor(yuv, model_w, model_h)
    return tensor
  return frame_prepare_tinygrad


def make_buffers(vision_input_shapes, policy_input_shapes, frame_skip):
  img = vision_input_shapes['img']  # (1, 12, 128, 256)
  n_frames = img[1] // 6
  img_buf_shape = (frame_skip * (n_frames - 1) + 1, 6, img[2], img[3])

  fb = policy_input_shapes['features_buffer']  # (1, 25, 512)
  dp = policy_input_shapes['desire_pulse']  # (1, 25, 8)
  tc = policy_input_shapes['traffic_convention']  # (1, 2)

  npy = {
    'features_buffer': np.zeros(fb, dtype=np.float32),
    'desire_pulse': np.zeros(dp, dtype=np.float32),
    'traffic_convention': np.zeros(tc, dtype=np.float32),
    'tfm': np.zeros((3, 3), dtype=np.float32),
    'big_tfm': np.zeros((3, 3), dtype=np.float32),
  }
  bufs = {
    'img_buf': Tensor.zeros(img_buf_shape, dtype='uint8').contiguous().realize(),
    'big_img_buf': Tensor.zeros(img_buf_shape, dtype='uint8').contiguous().realize(),
    **{k: Tensor(v, device='NPY').realize() for k, v in npy.items()},
  }
  return bufs, npy


def make_policy_state(policy_input_shapes, frame_skip):
  fb = policy_input_shapes['features_buffer']
  dp = policy_input_shapes['desire_pulse']
  # Keep recurrent queues on the host side. The loaded outer TinyJit was
  # corrupting multi-step outputs when it owned these state transitions.
  return {
    'feat_q': np.zeros((frame_skip * (fb[1] - 1) + 1, fb[0], fb[2]), dtype=np.float32),
    'desire_q': np.zeros((frame_skip * dp[1], dp[0], dp[2]), dtype=np.float32),
  }


def sample_skip_np(buf, frame_skip):
  return np.ascontiguousarray(buf[::frame_skip].reshape(1, -1, buf.shape[-1]))


def sample_desire_np(buf, frame_skip):
  sampled = buf.reshape(-1, frame_skip, *buf.shape[1:]).max(axis=1)
  return np.ascontiguousarray(sampled.reshape(1, -1, buf.shape[-1]))


def update_features_buffer(policy_state, vision_features, features_buffer, frame_skip):
  feat_q = policy_state['feat_q']
  feat_q[:-1] = feat_q[1:]
  feat_q[-1, :, :] = np.asarray(vision_features, dtype=np.float32).reshape(feat_q.shape[1], feat_q.shape[2])
  features_buffer[:] = sample_skip_np(feat_q, frame_skip)


def update_desire_pulse(policy_state, desire, desire_pulse, frame_skip):
  desire_q = policy_state['desire_q']
  desire_q[:-1] = desire_q[1:]
  desire_q[-1, :, :] = np.asarray(desire, dtype=np.float32).reshape(desire_q.shape[1], desire_q.shape[2])
  desire_pulse[:] = sample_desire_np(desire_q, frame_skip)


def shift_and_sample(buf, new_val, sample_fn):
  buf.assign(buf[1:].cat(new_val, dim=0).contiguous())
  return sample_fn(buf)


def make_warp_dm(cam_w, cam_h, dm_w, dm_h):
  stride, y_height, _, _ = get_nv12_info(cam_w, cam_h)
  stride_pad = stride - cam_w

  def warp_dm(input_frame, M_inv):
    M_inv = M_inv.to(Device.DEFAULT)
    result = warp_perspective_tinygrad(input_frame[:cam_h*stride], M_inv, (dm_w, dm_h), (cam_h, cam_w), stride_pad).reshape(-1, dm_h * dm_w)
    return result
  return warp_dm


def make_run_vision(vision_runner, cam_w, cam_h, frame_skip):
  model_w, model_h = MEDMODEL_INPUT_SIZE
  frame_prepare = make_frame_prepare(cam_w, cam_h, model_w, model_h)

  def sample_skip(buf):
    return buf[::frame_skip].contiguous().flatten(0, 1).unsqueeze(0)

  def run_vision(img_buf, big_img_buf, tfm, big_tfm, frame, big_frame):
    with Context(IMAGE=0):
      img = shift_and_sample(img_buf, frame_prepare(frame, tfm.to(Device.DEFAULT)).unsqueeze(0), sample_skip)
      big_img = shift_and_sample(big_img_buf, frame_prepare(big_frame, big_tfm.to(Device.DEFAULT)).unsqueeze(0), sample_skip)
    return next(iter(vision_runner({'img': img, 'big_img': big_img}).values())).cast('float32')
  return run_vision


def run_policy_heads(on_policy_runner, off_policy_runner, features_buffer, desire_pulse, traffic_convention):
  inputs = {
    'features_buffer': features_buffer.to(Device.DEFAULT),
    'desire_pulse': desire_pulse.to(Device.DEFAULT),
    'traffic_convention': traffic_convention.to(Device.DEFAULT),
  }
  on_policy_out = next(iter(on_policy_runner(inputs).values())).cast('float32')
  off_policy_out = next(iter(off_policy_runner(inputs).values())).cast('float32')
  return on_policy_out, off_policy_out


def compile_modeld(cam_w, cam_h):
  from tinygrad.nn.onnx import OnnxRunner
  from openpilot.selfdrive.modeld.constants import ModelConstants

  _, _, _, yuv_size = get_nv12_info(cam_w, cam_h)
  print(f"Compiling vision JIT for {cam_w}x{cam_h}...")

  vision_runner = OnnxRunner(MODELS_DIR / 'driving_vision.onnx')

  with open(MODELS_DIR / 'driving_vision_metadata.pkl', 'rb') as f:
    vision_metadata = pickle.load(f)
    vision_input_shapes = vision_metadata['input_shapes']
  with open(MODELS_DIR / 'driving_on_policy_metadata.pkl', 'rb') as f:
    policy_input_shapes = pickle.load(f)['input_shapes']

  frame_skip = ModelConstants.MODEL_RUN_FREQ // ModelConstants.MODEL_CONTEXT_FREQ

  run_vision = make_run_vision(vision_runner, cam_w, cam_h, frame_skip)
  run_vision_jit = TinyJit(run_vision, prune=True)
  rng = np.random.default_rng(0)
  frame_np = rng.integers(0, 256, yuv_size, dtype=np.uint8)
  big_frame_np = rng.integers(0, 256, yuv_size, dtype=np.uint8)
  for i in range(3):
    bufs, npy = make_buffers(vision_input_shapes, policy_input_shapes, frame_skip)
    frame = Tensor(frame_np.copy()).realize()
    big_frame = Tensor(big_frame_np.copy()).realize()
    npy['tfm'][:] = np.eye(3, dtype=np.float32)
    npy['big_tfm'][:] = np.eye(3, dtype=np.float32)
    Device.default.synchronize()

    st = time.perf_counter()
    with Context(OPENPILOT_HACKS=1):
      inputs = {
        'img_buf': bufs['img_buf'],
        'big_img_buf': bufs['big_img_buf'],
        'tfm': bufs['tfm'],
        'big_tfm': bufs['big_tfm'],
        'frame': frame,
        'big_frame': big_frame,
      }
      out = run_vision_jit(**inputs)
    mt = time.perf_counter()
    Device.default.synchronize()
    et = time.perf_counter()
    print(f"  [{i+1}/10] enqueue {(mt-st)*1e3:6.2f} ms -- total {(et-st)*1e3:6.2f} ms")

    if i == 2:
      with Context(OPENPILOT_HACKS=1):
        test_val = np.copy(run_vision(**inputs).numpy())
      test_inputs = {k: Tensor(v.numpy().copy(), device=v.device) for k, v in inputs.items()}

  pkl_path = policy_pkl_path(cam_w, cam_h)
  with open(pkl_path, "wb") as f:
    pickle.dump(run_vision_jit, f)
  print(f"  Saved to {pkl_path}")
  return test_inputs, test_val


def test_vs_compile(run, inputs: dict[str, Tensor], test_val: np.ndarray):
  # run 20 times
  for i in range(20):
    st = time.perf_counter()
    out = run(**inputs)
    mt = time.perf_counter()
    val = out.numpy()
    et = time.perf_counter()
    print(f"enqueue {(mt-st)*1e3:6.2f} ms -- total run {(et-st)*1e3:6.2f} ms")

    if test_val is not None and i == 0:  # check output matches before buffers get mutated by the jit
      np.testing.assert_equal(test_val, val)

  # test that changing the inputs changes the model outputs
  changed_inputs = {k: Tensor(v.numpy().copy(), device=v.device) for k, v in inputs.items()}
  tfm_delta = np.zeros((3, 3), dtype=np.float32)
  tfm_delta[0, 2] = 8.0
  tfm_delta[1, 2] = 4.0
  changed_inputs['tfm'] = Tensor((inputs['tfm'].numpy() + tfm_delta).astype(np.float32), device=inputs['tfm'].device)
  changed_inputs['big_tfm'] = Tensor((inputs['big_tfm'].numpy() + tfm_delta).astype(np.float32), device=inputs['big_tfm'].device)
  changed_val = run(**changed_inputs).numpy()
  assert not np.array_equal(val, changed_val), "vision output didn't change when transforms changed"
  print('test_vs_compile OK')


def compile_dm_warp(cam_w, cam_h):
  dm_w, dm_h = DM_INPUT_SIZE
  _, _, _, yuv_size = get_nv12_info(cam_w, cam_h)

  print(f"Compiling DM warp for {cam_w}x{cam_h}...")

  warp_dm = make_warp_dm(cam_w, cam_h, dm_w, dm_h)
  warp_dm_jit = TinyJit(warp_dm, prune=True)

  for i in range(10):
    inputs = [Tensor(np.random.randint(0, 256, yuv_size, dtype=np.uint8)).realize(),
              Tensor(Tensor.randn(3, 3).mul(8).realize().numpy(), device='NPY')]
    Device.default.synchronize()
    st = time.perf_counter()
    warp_dm_jit(*inputs)
    mt = time.perf_counter()
    Device.default.synchronize()
    et = time.perf_counter()
    print(f"  [{i+1}/10] enqueue {(mt-st)*1e3:6.2f} ms -- total {(et-st)*1e3:6.2f} ms")

  pkl_path = dm_warp_pkl_path(cam_w, cam_h)
  with open(pkl_path, "wb") as f:
    pickle.dump(warp_dm_jit, f)
  print(f"  Saved to {pkl_path}")


def run_and_save_pickle():
  for cam_w, cam_h in CAMERA_CONFIGS:
    inputs, outputs = compile_modeld(cam_w, cam_h)
    pickle_loaded = pickle.load(open(policy_pkl_path(cam_w, cam_h), "rb"))
    test_vs_compile(pickle_loaded, inputs, outputs)

    compile_dm_warp(cam_w, cam_h)


if __name__ == "__main__":
  run_and_save_pickle()
