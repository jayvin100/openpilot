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


class PcmBuffer:
  def __init__(self, dtype=np.int16):
    self._chunks: deque[np.ndarray] = deque()
    self._offset = 0
    self._size = 0
    self._dtype = dtype

  def push(self, samples: np.ndarray):
    if samples.size == 0:
      return
    chunk = np.ascontiguousarray(samples, dtype=self._dtype)
    self._chunks.append(chunk)
    self._size += chunk.size

  def available(self) -> int:
    return self._size

  def pop(self, size: int) -> np.ndarray:
    out = np.zeros(size, dtype=self._dtype)
    written = 0

    while written < size and self._chunks:
      chunk = self._chunks[0]
      remaining = chunk.size - self._offset
      take = min(size - written, remaining)
      out[written:written + take] = chunk[self._offset:self._offset + take]
      written += take
      self._offset += take

      if self._offset >= chunk.size:
        self._chunks.popleft()
        self._offset = 0

    self._size -= written
    return out


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
      if self._buffer.available() >= self._samples_per_frame:
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
          self._send_audio_data(resampled.planes[0].to_bytes())
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


# Backwards-compatible aliases while older call sites are updated.
BodyMicAudioTrack = DeviceToWebAudioTrack
BodySpeaker = WebToDeviceAudioTrack
