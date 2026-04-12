import asyncio
import fractions
import logging
import threading
import time
from collections import deque

import numpy as np
from av import AudioFrame, AudioResampler
from aiortc.mediastreams import AudioStreamTrack, MediaStreamError

from cereal import car, messaging

AUDIO_PTIME = 0.020
MIC_SAMPLE_RATE = 16000
SPEAKER_SAMPLE_RATE = 48000
LOGGER = logging.getLogger("webrtcd")

AudibleAlert = car.CarControl.HUDControl.AudibleAlert
BODY_SOUND_ALERTS = {
  "engage": AudibleAlert.engage,
  "disengage": AudibleAlert.disengage,
  "prompt": AudibleAlert.prompt,
  "warning": AudibleAlert.warningImmediate,
}
BODY_SOUND_NAMES = frozenset(BODY_SOUND_ALERTS)


def audio_plane_to_bytes(plane) -> bytes:
  to_bytes = getattr(plane, "to_bytes", None)
  if callable(to_bytes):
    return to_bytes()

  # PyAV 16 dropped AudioPlane.to_bytes(), but still exposes the plane buffer.
  return memoryview(plane).tobytes()


def audio_frame_to_bytes(frame: AudioFrame) -> bytes:
  return b"".join(audio_plane_to_bytes(plane) for plane in frame.planes)


class PcmBuffer:
  def __init__(self, dtype=np.int16):
    self._chunks: deque[np.ndarray] = deque()
    self._size = 0
    self._dtype = dtype

  def __len__(self) -> int:
    return self._size

  def push(self, samples: np.ndarray):
    if samples.size:
      chunk = np.ascontiguousarray(samples, dtype=self._dtype)
      self._chunks.append(chunk)
      self._size += chunk.size

  def pop(self, size: int) -> np.ndarray:
    if size > len(self):
      raise ValueError(f"requested {size} samples, only {len(self)} available")

    self._size -= size
    parts: list[np.ndarray] = []
    while size:
      chunk = self._chunks.popleft()
      take = min(size, chunk.size)
      parts.append(chunk[:take])
      if take < chunk.size:
        self._chunks.appendleft(chunk[take:])
      size -= take

    return np.concatenate(parts) if parts else np.empty(0, dtype=self._dtype)


class DeviceToWebAudioTrack(AudioStreamTrack):
  def __init__(self):
    super().__init__()
    self._loop = asyncio.get_running_loop()
    self._buffer = PcmBuffer()
    self._buffer_event = asyncio.Event()
    self._sample_rate = MIC_SAMPLE_RATE
    self._samples_per_frame = int(self._sample_rate * AUDIO_PTIME)
    self._time_base = fractions.Fraction(1, self._sample_rate)
    self._running = True
    self._thread = threading.Thread(target=self._poll_cereal, daemon=True)
    self._thread.start()

  def _push_samples(self, samples: np.ndarray):
    self._buffer.push(samples)
    self._buffer_event.set()

  def _poll_cereal(self):
    sm = messaging.SubMaster(['rawAudioData'])
    while self._running:
      sm.update(20)
      if not sm.updated['rawAudioData']:
        continue

      raw_bytes = sm['rawAudioData'].data
      if not raw_bytes:
        continue

      # .copy() required: frombuffer is a view over the cereal message buffer, invalidated by next sm.update()
      samples = np.frombuffer(raw_bytes, dtype=np.int16).copy()
      self._loop.call_soon_threadsafe(self._push_samples, samples)

  async def _next_frame_samples(self) -> np.ndarray:
    while self.readyState == "live":
      if len(self._buffer) >= self._samples_per_frame:
        return self._buffer.pop(self._samples_per_frame)

      await self._buffer_event.wait()
      self._buffer_event.clear()

    raise MediaStreamError

  async def _next_timestamp(self) -> int:
    if not hasattr(self, "_timestamp"):
      self._start = time.monotonic()
      self._timestamp = 0
      return self._timestamp

    self._timestamp += self._samples_per_frame
    wait = self._start + (self._timestamp / self._sample_rate) - time.monotonic()
    if wait > 0:
      await asyncio.sleep(wait)
    return self._timestamp

  async def recv(self):
    if self.readyState != "live":
      raise MediaStreamError

    frame_samples = await self._next_frame_samples()
    timestamp = await self._next_timestamp()

    frame = AudioFrame(format="s16", layout="mono", samples=self._samples_per_frame)
    frame.planes[0].update(frame_samples.tobytes())
    frame.pts = timestamp
    frame.sample_rate = self._sample_rate
    frame.time_base = self._time_base
    return frame

  def stop(self):
    super().stop()
    self._running = False
    try:
      self._loop.call_soon_threadsafe(self._buffer_event.set)
    except RuntimeError:
      self._buffer_event.set()


class WebToDeviceAudioTrack:
  def __init__(self):
    self._pm = messaging.PubMaster(['soundRequest', 'webrtcAudioData'])
    self._task: asyncio.Task | None = None

  def play_sound(self, sound_name: str):
    msg = messaging.new_message('soundRequest')
    msg.soundRequest.sound = BODY_SOUND_ALERTS[sound_name]
    self._pm.send('soundRequest', msg)

  def start_track(self, track):
    self._task = asyncio.create_task(self._consume_track(track))

  def _send_audio_data(self, data: bytes):
    msg = messaging.new_message('webrtcAudioData')
    msg.webrtcAudioData.data = data
    msg.webrtcAudioData.sampleRate = SPEAKER_SAMPLE_RATE
    self._pm.send('webrtcAudioData', msg)

  async def _consume_track(self, track):
    resampler = AudioResampler(format='s16', layout='mono', rate=SPEAKER_SAMPLE_RATE)
    try:
      while True:
        frame = await track.recv()
        for resampled in resampler.resample(frame):
          self._send_audio_data(audio_frame_to_bytes(resampled))
    except MediaStreamError:
      LOGGER.info("Incoming browser audio track ended")
    except asyncio.CancelledError:
      raise
    except Exception:
      LOGGER.exception("BodySpeaker track consumption error")

  async def stop(self):
    if self._task is not None and not self._task.done():
      self._task.cancel()
      try:
        await self._task
      except asyncio.CancelledError:
        pass
    self._task = None
