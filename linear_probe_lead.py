#!/usr/bin/env python3
"""
Linear probe: can a linear model predict lead velocity from 512-dim vision features?

Two phases:
1. Quick check: compare existing model vLead predictions with radarState on Lexus routes (no replay needed)
2. Linear probe: rerun modeld with SEND_RAW_PRED to capture 512-dim features, fit Ridge regression

Also plots Tesla braking event comparison.
"""
import os
os.environ['SEND_RAW_PRED'] = '1'
from openpilot.selfdrive.modeld.tinygrad_helpers import MODELS_DIR, set_tinygrad_backend_from_compiled_flags
set_tinygrad_backend_from_compiled_flags()

import time
import pickle
from concurrent.futures import ProcessPoolExecutor, as_completed

import torch
import torch.nn as nn
import numpy as np
import matplotlib.pyplot as plt
from tqdm import tqdm
from tinygrad.tensor import Tensor
from sklearn.metrics import r2_score, mean_absolute_error
from sklearn.model_selection import train_test_split

from openpilot.tools.lib.logreader import LogReader
from openpilot.tools.lib.framereader import FrameReader
from openpilot.tools.lib.route import Route, SegmentRange
from openpilot.system.camerad.cameras.nv12_info import get_nv12_info
from openpilot.common.transformations.model import get_warp_matrix
from openpilot.common.transformations.camera import DEVICE_CAMERAS
from openpilot.selfdrive.modeld.modeld import ModelState

# Vision output: features are at indices 117:629 (512 dims)
FEATURE_SLICE = slice(117, 629)

LEXUS_TRAIN = [
  '7830b8e854d6713c/000000f9--a736138536/5:',
  '7830b8e854d6713c/000000c9--e769ceac34/7:21',
  # '7830b8e854d6713c/000000d1--9f7385daf2',
  # '7830b8e854d6713c/000000d4--ac4ec365b4',
  # '7830b8e854d6713c/000000cb--8d6a2f4626',
  # '7830b8e854d6713c/000000cc--d30fb0aa7f',
  # '7830b8e854d6713c/000000cd--741c93fcb1',
]
# LEXUS_VAL = '7830b8e854d6713c/000000db--d5575f785a'
LEXUS_VAL = '7830b8e854d6713c/000000db--d5575f785a/13'
TESLA_ROUTE = 'dffcf1de8723a20f/000000be--f00fb3e5b5/4'

CACHE_DIR = '/tmp/linear_probe_cache'

# Temporal probe matching off-policy architecture:
# 9 history frames at 5Hz (every 4th from 20Hz), 1-layer causal transformer, 8 heads, 512 dim
SEQ_LEN = 9
SUBSAMPLE = 4  # 20Hz -> 5Hz
LEAD_T_IDXS = [0.0, 2.0, 4.0, 6.0, 8.0, 10.0]  # prediction horizons in seconds
N_HORIZONS = len(LEAD_T_IDXS)
N_LEAD_VARS = 4  # x, y, v, a
LEAD_OUT = N_HORIZONS * N_LEAD_VARS * 2  # mu + log_sigma per var per horizon = 48

class TemporalProbe(nn.Module):
  """Matches off-policy temporal model: transformer + hydra-style lead head with Laplacian density."""
  def __init__(self, n_embd=512, n_head=8, n_layer=1, hidden_size=64, dropout=0.1):
    super().__init__()
    self.pos_embedding = nn.Embedding(SEQ_LEN, n_embd)
    encoder_layer = nn.TransformerEncoderLayer(d_model=n_embd, nhead=n_head, dim_feedforward=n_embd*4,
                                                batch_first=True, norm_first=True,
                                                dropout=dropout)
    self.transformer = nn.TransformerEncoder(encoder_layer, num_layers=n_layer)
    mask = torch.triu(torch.ones(SEQ_LEN, SEQ_LEN), diagonal=1).bool()
    self.register_buffer('causal_mask', mask)
    # Hydra-style head
    self.resblock_ln = nn.LayerNorm(n_embd)
    self.resblock_fc1 = nn.Linear(n_embd, n_embd)
    self.resblock_fc2 = nn.Linear(n_embd, n_embd)
    self.head_in = nn.Linear(n_embd, hidden_size)
    self.head_res1 = nn.Linear(hidden_size, hidden_size)
    self.head_res2 = nn.Linear(hidden_size, hidden_size)
    self.head_out = nn.Linear(hidden_size, LEAD_OUT)  # 6 horizons × 4 vars × 2 (mu + log_sigma)
    # Separate lead_prob head (like actual model)
    self.prob_head = nn.Linear(hidden_size, 1)

  def forward(self, x):
    # x: [B, SEQ_LEN, 512]
    pos = self.pos_embedding(torch.arange(SEQ_LEN, device=x.device))
    x = x + pos
    x = self.transformer(x, mask=self.causal_mask)
    x = x[:, -1]  # last token
    # resblock
    r = self.resblock_ln(x)
    r = torch.relu(self.resblock_fc1(r))
    r = self.resblock_fc2(r)
    x = x + r
    # lead head
    h = torch.relu(self.head_in(x))
    h = torch.relu(h + self.head_res2(torch.relu(self.head_res1(h))))
    lead = self.head_out(h).view(-1, N_HORIZONS, N_LEAD_VARS * 2)
    prob = self.prob_head(h).squeeze(-1)  # logit
    return lead, prob


def laplacian_nll(pred, target, std_clamp=1e-3, loss_clamp=1000.):
  """Laplacian density loss matching xx DensityLoss('laplacian') exactly."""
  import math
  # Mask NaN targets (same as num_from_nan)
  mask = ~torch.isnan(target)
  target = target.masked_fill(~mask, 0).detach()

  mu = pred[..., :N_LEAD_VARS]
  log_sigma_raw = pred[..., N_LEAD_VARS:]
  err = torch.abs(target - mu)
  log_sigma_min = torch.clamp(log_sigma_raw, min=math.log(std_clamp))
  log_sigma = torch.max(log_sigma_raw, torch.log(1e-6 + err / loss_clamp))
  log_lik = err * torch.exp(-log_sigma) + log_sigma_min
  return (mask * log_lik).sum() / mask.sum().clamp(min=1)


def make_temporal_sequences(aligned_data):
  """Create temporal sequences from aligned data. Groups by contiguous timestamps,
  subsamples to 5Hz, and creates sliding windows of SEQ_LEN frames."""
  # Sort by time
  sorted_data = sorted(aligned_data, key=lambda x: x['t'])

  # Split into contiguous segments (gap > 1s = new segment)
  segments = []
  current = [sorted_data[0]]
  for d in sorted_data[1:]:
    if d['t'] - current[-1]['t'] > 1.0:
      segments.append(current)
      current = [d]
    else:
      current.append(d)
  segments.append(current)

  features_seqs = []
  lead_targets = []    # [N, 6, 4] = x, y, v, a per horizon (NaN when no lead)
  lead_prob_targets = []  # [N] = 1.0 if lead, 0.0 if not
  vego_targets = []
  t_targets = []
  for seg in segments:
    seg_sub = seg[::SUBSAMPLE]
    if len(seg_sub) < SEQ_LEN:
      continue
    feats = np.array([d['features'] for d in seg_sub])
    vlead = np.array([d.get('vLead_radar', np.nan) for d in seg_sub])
    drel = np.array([d.get('dRel_radar', np.nan) for d in seg_sub])
    status = np.array([d.get('lead_status', True) for d in seg_sub])  # old cache was pre-filtered to lead-only
    vego = np.array([d['vEgo'] for d in seg_sub])
    ts = np.array([d['t'] for d in seg_sub])

    for i in range(len(seg_sub) - SEQ_LEN + 1):
      t_now = ts[i + SEQ_LEN - 1]
      has_lead = status[i + SEQ_LEN - 1]

      # Build ground truth for each horizon
      horizon_gt = np.full((N_HORIZONS, N_LEAD_VARS), np.nan)
      if has_lead:
        for h, t_offset in enumerate(LEAD_T_IDXS):
          t_future = t_now + t_offset
          idx = np.argmin(np.abs(ts - t_future))
          if abs(ts[idx] - t_future) > 0.5 or not status[idx]:
            continue  # leave as nan
          horizon_gt[h, 0] = drel[idx]
          horizon_gt[h, 1] = 0.0
          horizon_gt[h, 2] = vlead[idx]
          if idx > 0 and status[idx - 1]:
            dt = ts[idx] - ts[idx - 1]
            horizon_gt[h, 3] = (vlead[idx] - vlead[idx - 1]) / dt if dt > 0 else 0.0

      features_seqs.append(feats[i:i+SEQ_LEN])
      lead_targets.append(horizon_gt)
      lead_prob_targets.append(1.0 if has_lead else 0.0)
      vego_targets.append(vego[i+SEQ_LEN-1])
      t_targets.append(t_now)

  return (np.array(features_seqs), np.array(lead_targets), np.array(lead_prob_targets),
          np.array(vego_targets), np.array(t_targets))


def train_temporal_probe(train_seqs, train_lead, train_prob, val_seqs, val_lead, val_prob, epochs=200, lr=1e-3, batch_size=256):
  """Train temporal probe on full lead trajectory + lead_prob."""
  device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
  model = TemporalProbe().to(device)

  train_x = torch.tensor(train_seqs, dtype=torch.float32)
  train_y = torch.tensor(train_lead, dtype=torch.float32)
  train_p = torch.tensor(train_prob, dtype=torch.float32)
  val_x = torch.tensor(val_seqs, dtype=torch.float32).to(device)
  val_y = torch.tensor(val_lead, dtype=torch.float32).to(device)
  val_p = torch.tensor(val_prob, dtype=torch.float32).to(device)

  optimizer = torch.optim.Adam(model.parameters(), lr=lr)
  scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=epochs)
  dataset = torch.utils.data.TensorDataset(train_x, train_y, train_p)
  loader = torch.utils.data.DataLoader(dataset, batch_size=batch_size, shuffle=True)

  for epoch in range(epochs):
    model.train()
    total_loss = 0
    for bx, by, bp in loader:
      bx, by, bp = bx.to(device), by.to(device), bp.to(device)
      lead_pred, prob_pred = model(bx)
      loss = laplacian_nll(lead_pred, by) + nn.functional.binary_cross_entropy_with_logits(prob_pred, bp)
      optimizer.zero_grad()
      loss.backward()
      optimizer.step()
      total_loss += loss.item() * len(bx)
    scheduler.step()

    if epoch % 10 == 0 or epoch == epochs - 1:
      model.eval()
      with torch.no_grad():
        # Train metrics
        train_lead_pred, train_prob_pred = model(train_x.to(device))
        train_has = train_p > 0.5
        if train_has.any():
          tr_x_mae = (train_lead_pred[train_has, 0, 0] - train_y[train_has, 0, 0].to(device)).abs().mean().item()
          tr_v_mae = (train_lead_pred[train_has, 0, 2] - train_y[train_has, 0, 2].to(device)).abs().mean().item()
        else:
          tr_x_mae, tr_v_mae = 0, 0
        # Val metrics
        val_lead_pred, val_prob_pred = model(val_x)
        has_lead = val_p > 0.5
        if has_lead.any():
          val_x_mae = (val_lead_pred[has_lead, 0, 0] - val_y[has_lead, 0, 0]).abs().mean().item()
          val_v_mae = (val_lead_pred[has_lead, 0, 2] - val_y[has_lead, 0, 2]).abs().mean().item()
        else:
          val_x_mae, val_v_mae = 0, 0
        val_prob_acc = ((val_prob_pred > 0) == (val_p > 0.5)).float().mean().item()
      print(f"  epoch {epoch:>3d}: loss={total_loss/len(train_x):.3f} | "
            f"train dRel={tr_x_mae:.2f}m vLead={tr_v_mae:.2f}m/s | "
            f"val dRel={val_x_mae:.2f}m vLead={val_v_mae:.2f}m/s prob_acc={val_prob_acc:.2f}")

  model.eval()
  return model


def get_cache_path(route_name, seg, suffix='features'):
  os.makedirs(CACHE_DIR, exist_ok=True)
  safe_name = route_name.replace('/', '_')
  return os.path.join(CACHE_DIR, f'{safe_name}_seg{seg}_{suffix}.pkl')


def extract_from_logs(logs):
  """Extract model lead predictions and radarState from logs."""
  model_data = []
  radar_data = []
  car_data = []

  for msg in logs:
    w = msg.which()
    t = msg.logMonoTime * 1e-9
    if w == 'radarState':
      lo = msg.radarState.leadOne
      radar_data.append({'t': t, 'dRel': lo.dRel, 'vLead': lo.vLead, 'status': lo.status, 'radar': lo.radar})
    elif w == 'modelV2':
      mv = msg.modelV2
      lead = mv.leadsV3[0]
      entry = {'t': t, 'v': list(lead.v), 'x': list(lead.x), 'prob': lead.prob}
      if len(mv.rawPredictions) > 0:
        raw = np.frombuffer(mv.rawPredictions, dtype=np.float32)
        entry['features'] = raw[FEATURE_SLICE].copy()
      model_data.append(entry)
    elif w == 'carState':
      car_data.append({'t': t, 'vEgo': msg.carState.vEgo})

  return model_data, radar_data, car_data


def align_data(model_data, radar_data, car_data):
  """Align model predictions with radar ground truth by nearest timestamp. Only where radar status==True."""
  radar_valid = [r for r in radar_data if r['status']]
  if not radar_valid:
    return None

  model_t = np.array([m['t'] for m in model_data])
  car_t = np.array([c['t'] for c in car_data])

  aligned = []
  for r in radar_valid:
    mi = np.argmin(np.abs(model_t - r['t']))
    ci = np.argmin(np.abs(car_t - r['t']))
    if abs(model_t[mi] - r['t']) < 0.1:  # within 100ms
      entry = {
        'vLead_radar': r['vLead'],
        'dRel_radar': r['dRel'],
        'vLead_model': model_data[mi]['v'][0],
        'dRel_model': model_data[mi]['x'][0],
        'prob': model_data[mi]['prob'],
        'vEgo': car_data[ci]['vEgo'],
        't': r['t'],
      }
      if 'features' in model_data[mi]:
        entry['features'] = model_data[mi]['features']
      aligned.append(entry)

  return aligned


def phase1_existing_logs():
  """Phase 1: compare model predictions with radar using existing logs (no replay needed)."""
  print("=" * 60)
  print("PHASE 1: Model vLead vs Radar Ground Truth (existing logs)")
  print("=" * 60)

  all_aligned = []
  for seg_range_str in [LEXUS_TRAIN, LEXUS_VAL]:
    sr = SegmentRange(seg_range_str)
    print(f"\nRoute: {sr.route_name} segs: {sr.seg_idxs[:3]}...")
    for s in sr.seg_idxs:
      try:
        logs = list(LogReader(f'{sr.route_name}/{s}', sort_by_time=True))
        model_data, radar_data, car_data = extract_from_logs(logs)
        aligned = align_data(model_data, radar_data, car_data)
        if aligned:
          all_aligned.extend(aligned)
          print(f"  seg {s}: {len(aligned)} matched samples")
      except Exception as e:
        print(f"  seg {s}: failed ({e})")

  if not all_aligned:
    print("No aligned data!")
    return

  vLead_radar = np.array([a['vLead_radar'] for a in all_aligned])
  vLead_model = np.array([a['vLead_model'] for a in all_aligned])
  dRel_radar = np.array([a['dRel_radar'] for a in all_aligned])
  dRel_model = np.array([a['dRel_model'] for a in all_aligned])
  vEgo = np.array([a['vEgo'] for a in all_aligned])
  prob = np.array([a['prob'] for a in all_aligned])

  mask = prob > 0.5
  print(f"\nTotal aligned samples: {len(all_aligned)}, prob>0.5: {mask.sum()}")

  vr, vm, dr, dm, ve = vLead_radar[mask], vLead_model[mask], dRel_radar[mask], dRel_model[mask], vEgo[mask]

  v_err = vm - vr
  d_err = dm - dr
  v_delta = np.abs(ve - vr)

  print(f"\n--- Velocity (v) ---")
  print(f"  MAE: {np.mean(np.abs(v_err)):.2f} m/s")
  print(f"  Bias: {np.mean(v_err):.2f} m/s")
  print(f"  RMSE: {np.sqrt(np.mean(v_err**2)):.2f} m/s")
  print(f"  Correlation with vEgo: {np.corrcoef(vm, ve)[0,1]:.3f} (radar: {np.corrcoef(vr, ve)[0,1]:.3f})")

  print(f"\n--- Distance (d) ---")
  print(f"  MAE: {np.mean(np.abs(d_err)):.2f} m")
  print(f"  RMSE: {np.sqrt(np.mean(d_err**2)):.2f} m")

  print(f"\n--- v MAE by |vEgo - vLead| bucket ---")
  buckets = [(0, 2), (2, 5), (5, 10), (10, 15), (15, 100)]
  for lo, hi in buckets:
    bmask = (v_delta >= lo) & (v_delta < hi)
    if bmask.sum() > 0:
      print(f"  [{lo:>2}-{hi:>2} m/s]: MAE={np.mean(np.abs(v_err[bmask])):.2f} m/s, n={bmask.sum()}")

  fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=False)

  ax = axes[0]
  ax.scatter(vr, vm, s=2, alpha=0.3)
  lims = [min(vr.min(), vm.min()), max(vr.max(), vm.max())]
  ax.plot(lims, lims, 'k--', lw=0.8)
  ax.set_xlabel('vLead radar (m/s)')
  ax.set_ylabel('vLead model (m/s)')
  ax.set_title('Model vs Radar Lead Speed')

  ax = axes[1]
  ax.scatter(v_delta, v_err, s=2, alpha=0.3)
  ax.axhline(0, color='k', lw=0.8)
  ax.set_xlabel('|vEgo - vLead| (m/s)')
  ax.set_ylabel('vLead error (model - radar) (m/s)')
  ax.set_title('Speed Prediction Error vs Speed Delta')

  ax = axes[2]
  ax.scatter(dr, dm, s=2, alpha=0.3)
  lims = [min(dr.min(), dm.min()), max(dr.max(), dm.max())]
  ax.plot(lims, lims, 'k--', lw=0.8)
  ax.set_xlabel('dRel radar (m)')
  ax.set_ylabel('dRel model (m)')
  ax.set_title('Model vs Radar Lead Distance')

  plt.tight_layout()
  plt.savefig('/tmp/phase1_model_vs_radar.png', dpi=150)
  print(f"\nSaved plot to /tmp/phase1_model_vs_radar.png")
  plt.show()

  return all_aligned


class FakeBuf:
  """Minimal VisionBuf replacement for direct model running."""
  def __init__(self, data, w, h):
    self.data = data
    self.width = w
    self.height = h


def process_segment(route_name, seg, camera_path, ecamera_path):
  """Decode frames and run vision inference for one segment. Caches result."""
  from openpilot.common.transformations.camera import DEVICE_CAMERAS

  cache_path = get_cache_path(route_name, seg)
  if os.path.exists(cache_path):
    with open(cache_path, 'rb') as f:
      return pickle.load(f)

  t0 = time.time()
  logs = list(LogReader(f'{route_name}/{seg}', sort_by_time=True))
  fr_road = FrameReader(camera_path, pix_fmt='nv12')
  fr_wide = FrameReader(ecamera_path, pix_fmt='nv12')
  w, h = fr_road.w, fr_road.h

  # Get calibration
  calib, device_type, sensor = None, None, None
  for msg in logs:
    if msg.which() == 'liveCalibration' and calib is None:
      calib = np.array(msg.liveCalibration.rpyCalib, dtype=np.float32)
    elif msg.which() == 'deviceState' and device_type is None:
      device_type = str(msg.deviceState.deviceType)
    elif msg.which() == 'roadCameraState' and sensor is None:
      sensor = str(msg.roadCameraState.sensor)
    if calib is not None and device_type is not None and sensor is not None:
      break
  if calib is None:
    return []

  model = ModelState()

  dc = DEVICE_CAMERAS[(device_type, sensor)]
  transform_road = get_warp_matrix(calib, dc.fcam.intrinsics, False).astype(np.float32)
  transform_wide = get_warp_matrix(calib, dc.ecam.intrinsics, True).astype(np.float32)

  stride, y_height, _, yuv_size = get_nv12_info(w, h)
  uv_offset = stride * y_height

  def pad_nv12(img):
    padded = np.zeros(((uv_offset // stride) + (h // 2), stride), dtype=np.uint8)
    padded[:h, :w] = img[:h * w].reshape((-1, w))
    padded[uv_offset // stride:uv_offset // stride + h // 2, :w] = img[h * w:].reshape((-1, w))
    buf = np.zeros(yuv_size, dtype=np.uint8)
    buf[:padded.size] = padded.flatten()
    return buf

  # Get ground truth from logs
  frame_msgs = sorted([m for m in logs if m.which() == 'roadCameraState'], key=lambda m: m.logMonoTime)
  radar_data = []
  car_data = []
  for msg in logs:
    if msg.which() == 'radarState':
      lo = msg.radarState.leadOne
      radar_data.append({'t': msg.logMonoTime * 1e-9, 'dRel': lo.dRel, 'vLead': lo.vLead, 'status': lo.status})
    elif msg.which() == 'carState':
      car_data.append({'t': msg.logMonoTime * 1e-9, 'vEgo': msg.carState.vEgo})

  # Init warp if needed
  if model.update_imgs is None:
    model.frame_buf_params['img'] = get_nv12_info(w, h)
    model.frame_buf_params['big_img'] = get_nv12_info(w, h)
    warp_path = MODELS_DIR / f'warp_{w}x{h}_tinygrad.pkl'
    with open(warp_path, 'rb') as f:
      model.update_imgs = pickle.load(f)

  # Decode + infer one frame at a time
  first_frame_id = frame_msgs[0].roadCameraState.frameId
  model_data = []
  for fm in tqdm(frame_msgs, desc=f'seg {seg}'):
    local_idx = fm.roadCameraState.frameId - first_frame_id
    try:
      img_road = fr_road.get(local_idx)
      img_wide = fr_wide.get(local_idx)
    except StopIteration:
      break
    if img_road is None or img_wide is None:
      continue

    buf_road = pad_nv12(img_road)
    buf_wide = pad_nv12(img_wide)

    for key, buf in [('img', buf_road), ('big_img', buf_wide)]:
      model.full_frames[key] = Tensor(buf)
    model.transforms_np['img'][:] = transform_road
    model.transforms_np['big_img'][:] = transform_wide

    out = model.update_imgs(model.img_queues['img'], model.full_frames['img'], model.transforms['img'],
                            model.img_queues['big_img'], model.full_frames['big_img'], model.transforms['big_img'])
    model.vision_output = model.vision_run(img=out[0], big_img=out[1]).contiguous().realize().uop.base.buffer.numpy().flatten()
    model_data.append({'t': fm.logMonoTime * 1e-9, 'features': model.vision_output[FEATURE_SLICE].copy()})

  if not model_data or not car_data:
    return []

  # Align features with nearest radar and car data
  feat_t = np.array([m['t'] for m in model_data])
  car_t_arr = np.array([c['t'] for c in car_data])
  radar_t_arr = np.array([r['t'] for r in radar_data]) if radar_data else np.array([])
  aligned = []
  for m in model_data:
    ci = np.argmin(np.abs(car_t_arr - m['t']))
    entry = {
      'features': m['features'],
      'vEgo': car_data[ci]['vEgo'],
      't': m['t'],
      'lead_status': False,
      'vLead_radar': np.nan,
      'dRel_radar': np.nan,
    }
    if len(radar_t_arr) > 0:
      ri = np.argmin(np.abs(radar_t_arr - m['t']))
      if abs(radar_t_arr[ri] - m['t']) < 0.1 and radar_data[ri]['status']:
        entry['lead_status'] = True
        entry['vLead_radar'] = radar_data[ri]['vLead']
        entry['dRel_radar'] = radar_data[ri]['dRel']
    aligned.append(entry)

  # Cache only features + timestamps
  features_only = [{'features': e['features'], 't': e['t'], 'vEgo': e['vEgo']} for e in aligned]
  with open(cache_path, 'wb') as f:
    pickle.dump(features_only, f)
  print(f"  seg {seg}: {len(features_only)} samples in {time.time()-t0:.0f}s")
  return aligned


def align_with_radar(route_name, seg, cached_features):
  """Align cached features with radar ground truth from logs."""
  logs = list(LogReader(f'{route_name}/{seg}', sort_by_time=True))
  radar_data = []
  car_data = []
  for msg in logs:
    if msg.which() == 'radarState':
      lo = msg.radarState.leadOne
      radar_data.append({'t': msg.logMonoTime * 1e-9, 'dRel': lo.dRel, 'vLead': lo.vLead, 'status': lo.status})
    elif msg.which() == 'carState':
      car_data.append({'t': msg.logMonoTime * 1e-9, 'vEgo': msg.carState.vEgo})

  radar_t_arr = np.array([r['t'] for r in radar_data]) if radar_data else np.array([])
  car_t_arr = np.array([c['t'] for c in car_data])

  aligned = []
  for sample in cached_features:
    t = sample['t']
    ci = np.argmin(np.abs(car_t_arr - t))
    entry = {
      'features': sample['features'],
      't': t,
      'lead_status': False,
      'vLead_radar': np.nan,
      'dRel_radar': np.nan,
      'vEgo': car_data[ci]['vEgo'] if car_data else sample.get('vEgo', 0),
    }
    if len(radar_t_arr) > 0:
      ri = np.argmin(np.abs(radar_t_arr - t))
      if abs(radar_t_arr[ri] - t) < 0.1 and radar_data[ri]['status']:
        entry['lead_status'] = True
        entry['vLead_radar'] = radar_data[ri]['vLead']
        entry['dRel_radar'] = radar_data[ri]['dRel']
    aligned.append(entry)
  return aligned


def collect_features(seg_range_str):
  """Load cached features, align with radar from logs. Decode + infer if no cache."""
  sr = SegmentRange(seg_range_str)
  route = Route(sr.route_name)
  all_aligned = []

  work = []
  for s in sr.seg_idxs:
    cache_path = get_cache_path(sr.route_name, s)
    if os.path.exists(cache_path):
      with open(cache_path, 'rb') as f:
        cached = pickle.load(f)
      aligned = align_with_radar(sr.route_name, s, cached)
      n_lead = sum(1 for a in aligned if a['lead_status'])
      all_aligned.extend(aligned)
      print(f"  seg {s}: {len(aligned)} samples ({n_lead} with lead)")
    else:
      work.append(s)

  if not work:
    return all_aligned

  with ProcessPoolExecutor(max_workers=8) as pool:
    futures = {pool.submit(process_segment, sr.route_name, s, route.camera_paths()[s], route.ecamera_paths()[s]): s for s in work}
    for fut in tqdm(as_completed(futures), total=len(futures), desc='segments'):
      all_aligned.extend(fut.result())

  return all_aligned


def phase2_temporal_probe():
  """Phase 2: rerun modeld to capture features, train on route 1, validate on route 2."""
  print(f"\n{'=' * 60}")
  print(f"PHASE 2: Linear Probe on 512-dim Features")
  print(f"{'=' * 60}")

  train_aligned = []
  for route in LEXUS_TRAIN:
    print(f"\n--- Collecting TRAIN features: {route} ---")
    train_aligned.extend(collect_features(route))
  print(f"\n--- Collecting VAL features: {LEXUS_VAL} ---")
  val_aligned = collect_features(LEXUS_VAL)

  if val_aligned:
    train_split, val_split = train_aligned, val_aligned
  else:
    train_split, val_split = train_test_split(train_aligned, test_size=0.2, random_state=42)

  # Temporal probe: transformer matching off-policy architecture
  print(f"\n--- Training Temporal Probe (1L transformer, 8 heads, 512 dim, 9 frames @ 5Hz) ---")
  train_seqs, train_lead, train_prob, train_vego, train_t = make_temporal_sequences(train_split)
  val_seqs, val_lead, val_prob, val_vego, val_t = make_temporal_sequences(val_split)
  n_train_lead = int((train_prob > 0.5).sum())
  n_val_lead = int((val_prob > 0.5).sum())
  print(f"  Train: {train_seqs.shape[0]} sequences ({n_train_lead} with lead)")
  print(f"  Val:   {val_seqs.shape[0]} sequences ({n_val_lead} with lead)")

  temporal_model = train_temporal_probe(train_seqs, train_lead, train_prob, val_seqs, val_lead, val_prob)

  device = next(temporal_model.parameters()).device
  with torch.no_grad():
    val_lead_pred, val_prob_pred = temporal_model(torch.tensor(val_seqs, dtype=torch.float32).to(device))
    val_pred_raw = val_lead_pred.cpu().numpy()
  val_pred_drel = val_pred_raw[:, 0, 0]
  val_pred_vlead = val_pred_raw[:, 0, 2]
  val_pred_prob = torch.sigmoid(val_prob_pred).cpu().numpy()
  # Only evaluate on samples with lead
  has_lead = val_prob > 0.5
  val_vlead = val_lead[has_lead, 0, 2]
  val_drel = val_lead[has_lead, 0, 0]

  print(f"\n--- Temporal Probe (lead samples only): features[9 frames @ 5Hz] -> lead trajectory + prob ---")
  print(f"  Val dRel MAE: {mean_absolute_error(val_drel, val_pred_drel[has_lead]):.2f} m")
  print(f"  Val vLead R²: {r2_score(val_vlead, val_pred_vlead[has_lead]):.3f}, MAE: {mean_absolute_error(val_vlead, val_pred_vlead[has_lead]):.2f} m/s")

  print(f"\n--- Baseline: predict vEgo as vLead ---")
  print(f"  Val R²: {r2_score(val_vlead, val_vego[has_lead]):.3f}, MAE: {mean_absolute_error(val_vlead, val_vego[has_lead]):.2f} m/s")

  v_delta = np.abs(val_vego[has_lead] - val_vlead)
  v_err = val_pred_vlead[has_lead] - val_vlead
  print(f"\n--- Val MAE by |vEgo - vLead| bucket ---")
  print(f"  {'bucket':>12s}  {'probe':>8s}  {'vEgo':>8s}  {'n':>5s}")
  for lo, hi in [(0, 2), (2, 5), (5, 10), (10, 15), (15, 100)]:
    bmask = (v_delta >= lo) & (v_delta < hi)
    if bmask.sum() > 0:
      print(f"  [{lo:>2}-{hi:>2} m/s]:  {np.mean(np.abs(v_err[bmask])):>7.2f}  "
            f"{mean_absolute_error(val_vlead[bmask], val_vego[has_lead][bmask]):>7.2f}  {bmask.sum():>5d}")

  # Scatter plot
  vp = val_pred_vlead[has_lead]
  ve = val_vego[has_lead]
  fig, axes = plt.subplots(1, 2, figsize=(12, 5))
  ax = axes[0]
  ax.scatter(val_vlead, vp, s=3, alpha=0.3)
  lims = [val_vlead.min(), val_vlead.max()]
  ax.plot(lims, lims, 'k--', lw=0.8)
  ax.set_xlabel('vLead radar (m/s)')
  ax.set_ylabel('vLead predicted (m/s)')
  ax.set_title(f'Temporal Probe on Val (R²={r2_score(val_vlead, vp):.3f})')
  ax = axes[1]
  ax.scatter(val_vlead, ve, s=3, alpha=0.3)
  ax.plot(lims, lims, 'k--', lw=0.8)
  ax.set_xlabel('vLead radar (m/s)')
  ax.set_ylabel('vEgo (m/s)')
  ax.set_title(f'vEgo Baseline (R²={r2_score(val_vlead, ve):.3f})')
  plt.tight_layout()
  plt.savefig('/tmp/phase2_probe.png', dpi=150)
  print(f"\nSaved plot to /tmp/phase2_probe.png")

  # Load stock model predictions from logs for comparison
  def get_model_preds_from_logs(seg_range_str):
    sr = SegmentRange(seg_range_str)
    model_t, model_vlead, model_drel, model_prob = [], [], [], []
    for s in sr.seg_idxs:
      for msg in LogReader(f'{sr.route_name}/{s}', sort_by_time=True):
        if msg.which() == 'modelV2':
          lead = msg.modelV2.leadsV3[0]
          model_t.append(msg.logMonoTime * 1e-9)
          model_vlead.append(lead.v[0])
          model_drel.append(lead.x[0])
          model_prob.append(lead.prob)
    return np.array(model_t), np.array(model_vlead), np.array(model_drel), np.array(model_prob)

  print("  Loading stock model predictions from logs...")
  train_model_t, train_model_vlead, train_model_drel, train_model_prob = get_model_preds_from_logs(LEXUS_TRAIN[0])
  val_model_t, val_model_vlead, val_model_drel, val_model_prob = get_model_preds_from_logs(LEXUS_VAL)

  # Lexus train route time series
  train_seqs_all, train_lead_all, train_prob_all, train_vego_all, train_t_all = make_temporal_sequences(train_split)
  train_t_all = train_t_all - train_t_all[0]
  with torch.no_grad():
    train_lead_pred_all, train_prob_pred_all = temporal_model(torch.tensor(train_seqs_all, dtype=torch.float32).to(device))
    train_preds_all_raw = train_lead_pred_all.cpu().numpy()
    train_preds_all_prob = torch.sigmoid(train_prob_pred_all).cpu().numpy()
  train_preds_all_vlead = train_preds_all_raw[:, 0, 2]
  train_preds_all_drel = train_preds_all_raw[:, 0, 0]
  train_vlead_all = train_lead_all[:, 0, 2]
  train_drel_all = train_lead_all[:, 0, 0]

  train_model_t_rel = train_model_t - train_model_t[0]

  fig, axes = plt.subplots(4, 1, figsize=(14, 12), sharex=True)
  ax = axes[0]
  ax.plot(train_t_all, train_drel_all, label='dRel radar')
  ax.plot(train_t_all, train_preds_all_drel, label='dRel probe')
  ax.plot(train_model_t_rel, train_model_drel, label='dRel model (prod)')
  ax.set_ylabel('distance (m)')
  ax.legend()
  ax.set_title('Lexus Train Route — Temporal Probe vs Radar Ground Truth vs Stock Model')
  ax = axes[1]
  ax.plot(train_t_all, train_vlead_all, label='vLead radar')
  ax.plot(train_t_all, train_preds_all_vlead, label='vLead probe')
  ax.plot(train_model_t_rel, train_model_vlead, label='vLead model (prod)')
  ax.plot(train_t_all, train_vego_all, label='vEgo')
  ax.set_ylabel('speed (m/s)')
  ax.legend()
  ax = axes[2]
  ax.plot(train_t_all, train_prob_all, label='lead prob (radar)')
  ax.plot(train_t_all, train_preds_all_prob, label='lead prob (probe)')
  ax.plot(train_model_t_rel, train_model_prob, label='lead prob (prod)')
  ax.set_ylabel('probability')
  ax.set_ylim(-0.05, 1.05)
  ax.legend()
  ax = axes[3]
  ax.plot(train_t_all, train_preds_all_vlead - train_vlead_all, label='vLead probe error')
  ax.axhline(0, color='k', lw=0.8)
  ax.set_ylabel('error (m/s)')
  ax.set_xlabel('time (s)')
  ax.legend()
  plt.tight_layout()
  plt.savefig('/tmp/phase2_lexus_train_timeseries.png', dpi=150)
  print(f"Saved plot to /tmp/phase2_lexus_train_timeseries.png")

  # Lexus val route time series
  val_seqs_all, val_lead_all, val_prob_all, val_vego_all, val_t_all = make_temporal_sequences(val_split)
  val_t_all = val_t_all - val_t_all[0]

  with torch.no_grad():
    val_lead_pred_all, val_prob_pred_all = temporal_model(torch.tensor(val_seqs_all, dtype=torch.float32).to(device))
    val_preds_all_raw = val_lead_pred_all.cpu().numpy()
    val_preds_all_prob = torch.sigmoid(val_prob_pred_all).cpu().numpy()
  val_preds_all_vlead = val_preds_all_raw[:, 0, 2]
  val_preds_all_drel = val_preds_all_raw[:, 0, 0]
  val_vlead_all = val_lead_all[:, 0, 2]
  val_drel_all = val_lead_all[:, 0, 0]

  val_model_t_rel = val_model_t - val_model_t[0]

  fig, axes = plt.subplots(4, 1, figsize=(14, 12), sharex=True)
  ax = axes[0]
  ax.plot(val_t_all, val_drel_all, label='dRel radar')
  ax.plot(val_t_all, val_preds_all_drel, label='dRel probe')
  ax.plot(val_model_t_rel, val_model_drel, label='dRel model (prod)')
  ax.set_ylabel('distance (m)')
  ax.legend()
  ax.set_title('Lexus Val Route — Temporal Probe vs Radar Ground Truth vs Stock Model')
  ax = axes[1]
  ax.plot(val_t_all, val_vlead_all, label='vLead radar')
  ax.plot(val_t_all, val_preds_all_vlead, label='vLead probe')
  ax.plot(val_model_t_rel, val_model_vlead, label='vLead model (prod)')
  ax.plot(val_t_all, val_vego_all, label='vEgo')
  ax.set_ylabel('speed (m/s)')
  ax.legend()
  ax = axes[2]
  ax.plot(val_t_all, val_prob_all, label='lead prob (radar)')
  ax.plot(val_t_all, val_preds_all_prob, label='lead prob (probe)')
  ax.plot(val_model_t_rel, val_model_prob, label='lead prob (prod)')
  ax.set_ylabel('probability')
  ax.set_ylim(-0.05, 1.05)
  ax.legend()
  ax = axes[3]
  ax.plot(val_t_all, val_preds_all_vlead - val_vlead_all, label='vLead probe error')
  ax.axhline(0, color='k', lw=0.8)
  ax.set_ylabel('error (m/s)')
  ax.set_xlabel('time (s)')
  ax.legend()
  plt.tight_layout()
  plt.savefig('/tmp/phase2_lexus_timeseries.png', dpi=150)
  print(f"Saved plot to /tmp/phase2_lexus_timeseries.png")

  plot_tesla_braking(temporal_model)
  plt.show()


def get_tesla_features():
  """Run vision model on Tesla segment, return (timestamps, features). Cached."""
  cache_path = get_cache_path('tesla', 4, suffix='tesla_features')
  if os.path.exists(cache_path):
    with open(cache_path, 'rb') as f:
      return pickle.load(f)

  print("  Running vision model on Tesla segment...")
  sr = SegmentRange(TESLA_ROUTE)
  seg_idx = sr.seg_idxs[0]
  route = Route(sr.route_name)
  model = ModelState()

  logs = list(LogReader(TESLA_ROUTE, sort_by_time=True))
  fr_road = FrameReader(route.camera_paths()[seg_idx], pix_fmt='nv12')
  fr_wide = FrameReader(route.ecamera_paths()[seg_idx], pix_fmt='nv12')
  w, h = fr_road.w, fr_road.h

  calib, device_type, sensor = None, None, None
  for msg in logs:
    if msg.which() == 'liveCalibration' and calib is None:
      calib = np.array(msg.liveCalibration.rpyCalib, dtype=np.float32)
    elif msg.which() == 'deviceState' and device_type is None:
      device_type = str(msg.deviceState.deviceType)
    elif msg.which() == 'roadCameraState' and sensor is None:
      sensor = str(msg.roadCameraState.sensor)
    if calib is not None and device_type is not None and sensor is not None:
      break

  dc = DEVICE_CAMERAS[(device_type, sensor)]
  transform_road = get_warp_matrix(calib, dc.fcam.intrinsics, False).astype(np.float32)
  transform_wide = get_warp_matrix(calib, dc.ecam.intrinsics, True).astype(np.float32)
  stride, y_height, _, yuv_size = get_nv12_info(w, h)
  uv_offset = stride * y_height

  def pad_nv12(img):
    padded = np.zeros(((uv_offset // stride) + (h // 2), stride), dtype=np.uint8)
    padded[:h, :w] = img[:h * w].reshape((-1, w))
    padded[uv_offset // stride:uv_offset // stride + h // 2, :w] = img[h * w:].reshape((-1, w))
    buf = np.zeros(yuv_size, dtype=np.uint8)
    buf[:padded.size] = padded.flatten()
    return buf

  if model.update_imgs is None:
    model.frame_buf_params['img'] = get_nv12_info(w, h)
    model.frame_buf_params['big_img'] = get_nv12_info(w, h)
    warp_path = MODELS_DIR / f'warp_{w}x{h}_tinygrad.pkl'
    with open(warp_path, 'rb') as f:
      model.update_imgs = pickle.load(f)

  frame_msgs = sorted([m for m in logs if m.which() == 'roadCameraState'], key=lambda m: m.logMonoTime)
  first_frame_id = frame_msgs[0].roadCameraState.frameId
  timestamps, features_list = [], []
  for fm in tqdm(frame_msgs, desc='tesla infer'):
    local_idx = fm.roadCameraState.frameId - first_frame_id
    try:
      img_road = fr_road.get(local_idx)
      img_wide = fr_wide.get(local_idx)
    except StopIteration:
      break
    if img_road is None or img_wide is None:
      continue
    for key, buf in [('img', pad_nv12(img_road)), ('big_img', pad_nv12(img_wide))]:
      model.full_frames[key] = Tensor(buf)
    model.transforms_np['img'][:] = transform_road
    model.transforms_np['big_img'][:] = transform_wide
    out = model.update_imgs(model.img_queues['img'], model.full_frames['img'], model.transforms['img'],
                            model.img_queues['big_img'], model.full_frames['big_img'], model.transforms['big_img'])
    model.vision_output = model.vision_run(img=out[0], big_img=out[1]).contiguous().realize().uop.base.buffer.numpy().flatten()
    timestamps.append(fm.logMonoTime * 1e-9)
    features_list.append(model.vision_output[FEATURE_SLICE].copy())

  result = (np.array(timestamps), np.array(features_list))
  with open(cache_path, 'wb') as f:
    pickle.dump(result, f)
  return result


def plot_tesla_braking(temporal_model=None):
  """Plot Tesla braking event. If temporal_model provided, show predictions."""
  print(f"\n{'=' * 60}")
  print(f"TESLA BRAKING EVENT: {TESLA_ROUTE}")
  print(f"{'=' * 60}")

  logs = list(LogReader(TESLA_ROUTE, sort_by_time=True))
  model_data, radar_data, car_data = extract_from_logs(logs)

  t0 = model_data[0]['t']
  model_t = np.array([m['t'] for m in model_data]) - t0
  model_vlead = np.array([m['v'][0] for m in model_data])
  model_drel = np.array([m['x'][0] for m in model_data])
  model_prob = np.array([m['prob'] for m in model_data])

  car_t = np.array([c['t'] for c in car_data]) - t0
  vEgo = np.array([c['vEgo'] for c in car_data])

  # Temporal probe predictions
  temporal_data = None
  if temporal_model is not None:
    tesla_ts, tesla_feats = get_tesla_features()
    device = next(temporal_model.parameters()).device
    feats_sub = tesla_feats[::SUBSAMPLE]
    ts_sub = tesla_ts[::SUBSAMPLE]
    temporal_vlead = []
    temporal_drel = []
    temporal_prob = []
    temporal_ts = []
    with torch.no_grad():
      for i in range(len(feats_sub) - SEQ_LEN + 1):
        seq = torch.tensor(feats_sub[i:i+SEQ_LEN], dtype=torch.float32).unsqueeze(0).to(device)
        lead_out, prob_out = temporal_model(seq)
        temporal_drel.append(lead_out[0, 0, 0].cpu().item())
        temporal_vlead.append(lead_out[0, 0, 2].cpu().item())
        temporal_prob.append(torch.sigmoid(prob_out[0]).cpu().item())
        temporal_ts.append(ts_sub[i+SEQ_LEN-1])
    temporal_t = np.array(temporal_ts) - t0
    temporal_data = (temporal_t, np.array(temporal_vlead), np.array(temporal_drel), np.array(temporal_prob))

  # Giga model comparison
  giga_model_data = None
  giga_path = '/tmp/giga_model_seg4_merged.zst'
  if os.path.exists(giga_path):
    print("Loading giga model comparison...")
    giga_logs = list(LogReader(giga_path, sort_by_time=True))
    giga_model, _, _ = extract_from_logs(giga_logs)
    if giga_model:
      giga_t = np.array([m['t'] for m in giga_model]) - t0
      giga_vlead = np.array([m['v'][0] for m in giga_model])
      giga_drel = np.array([m['x'][0] for m in giga_model])
      giga_prob = np.array([m['prob'] for m in giga_model])
      giga_model_data = (giga_t, giga_vlead, giga_drel, giga_prob)

  fig, axes = plt.subplots(4, 1, figsize=(14, 10), sharex=True,
                           gridspec_kw={'height_ratios': [2, 2, 1, 1]})

  ax = axes[0]
  ax.plot(model_t, model_drel, label='dRel model (prod)')
  if temporal_data is not None:
    ax.plot(temporal_data[0], temporal_data[2], label='dRel temporal probe')
  if giga_model_data is not None:
    ax.plot(giga_model_data[0], giga_model_data[2], label='dRel model (giga)')
  ax.set_ylabel('distance (m)')
  ax.legend()
  ax.set_title(f'Tesla Braking Event — {TESLA_ROUTE}')

  ax = axes[1]
  ax.plot(model_t, model_vlead, label='vLead model (prod)')
  if temporal_data is not None:
    ax.plot(temporal_data[0], temporal_data[1], label='vLead temporal probe')
  if giga_model_data is not None:
    ax.plot(giga_model_data[0], giga_model_data[1], label='vLead model (giga)')
  ax.plot(car_t, vEgo, label='vEgo')
  ax.set_ylabel('speed (m/s)')
  ax.legend()

  ax = axes[2]
  ax.plot(model_t, model_prob, label='lead prob (prod)')
  if temporal_data is not None:
    ax.plot(temporal_data[0], temporal_data[3], label='lead prob (probe)')
  if giga_model_data is not None:
    ax.plot(giga_model_data[0], giga_model_data[3], label='lead prob (giga)')
  ax.set_ylabel('probability')
  ax.set_ylim(-0.05, 1.05)
  ax.legend()

  ax = axes[3]
  ax.plot(car_t, vEgo, label='vEgo')
  ax.set_ylabel('speed (m/s)')
  ax.set_xlabel('time (s)')
  ax.legend()

  plt.tight_layout()
  plt.savefig('/tmp/tesla_braking_comparison.png', dpi=150)
  print(f"Saved plot to /tmp/tesla_braking_comparison.png")


if __name__ == '__main__':
  import sys
  mode = sys.argv[1] if len(sys.argv) > 1 else 'all'

  if mode in ('phase1', 'all'):
    phase1_existing_logs()

  if mode in ('tesla', 'all'):
    plot_tesla_braking()

  if mode in ('phase2', 'all'):
    phase2_temporal_probe()

  if mode == 'all':
    print("\n\nPhase 2 (linear probe) requires camera uploads.")
    print("Run with: python linear_probe_lead.py phase2")
