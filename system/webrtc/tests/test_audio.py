import asyncio
import math
import time
from types import SimpleNamespace

import numpy as np
import pytest
from aiortc.mediastreams import VideoStreamTrack

from openpilot.system.webrtc.device import audio as audio_module
from openpilot.system.webrtc.webrtcd import StreamSession


AUDIO_RECVONLY_OFFER_SDP = """v=0
o=- 3910210904 3910210904 IN IP4 0.0.0.0
s=-
t=0 0
a=group:BUNDLE 0
a=msid-semantic:WMS *
m=audio 9 UDP/TLS/RTP/SAVPF 96 0 8
c=IN IP4 0.0.0.0
a=recvonly
a=extmap:1 urn:ietf:params:rtp-hdrext:sdes:mid
a=extmap:2 urn:ietf:params:rtp-hdrext:ssrc-audio-level
a=mid:0
a=msid:eb1d3f1a-569a-465f-b419-319477bfded6 e44eecb2-1a04-4547-97d8-481389f50d5b
a=rtcp:9 IN IP4 0.0.0.0
a=rtcp-mux
a=ssrc:1233332626 cname:ca4dede8-4994-4a6d-9ae3-923b28177ca5
a=rtpmap:96 opus/48000/2
a=rtpmap:0 PCMU/8000
a=rtpmap:8 PCMA/8000
a=ice-ufrag:1234
a=ice-pwd:1234
a=fingerprint:sha-256 40:4B:14:CF:70:B8:67:E1:B1:FF:7E:F9:22:6E:60:7D:73:B5:1E:38:4B:10:20:9C:CD:1C:47:02:52:ED:45:25
a=setup:actpass"""


def tone_chunk(samples: int = 800, sample_rate: int = audio_module.MIC_SAMPLE_RATE) -> bytes:
  t = np.arange(samples, dtype=np.float32) / sample_rate
  pcm = (0.4 * np.sin(2 * math.pi * 440.0 * t) * 32767).astype(np.int16)
  return pcm.tobytes()


class FakeSubMaster:
  def __init__(self, payload: bytes):
    self.updated = {'rawAudioData': False}
    self._payload = payload
    self._msg = SimpleNamespace(data=b'', sampleRate=audio_module.MIC_SAMPLE_RATE)

  def update(self, timeout: int):
    time.sleep(0.005)
    self.updated['rawAudioData'] = True
    self._msg.data = self._payload

  def __getitem__(self, key: str):
    assert key == 'rawAudioData'
    return self._msg


async def wait_for_buffer(track: audio_module.DeviceToWebAudioTrack, timeout: float = 1.0):
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    if track._buffer.available() >= track._samples_per_frame:
      return
    await asyncio.sleep(0.01)
  raise TimeoutError("audio track did not buffer a full frame")


@pytest.mark.asyncio
async def test_device_to_web_audio_track_reads_raw_audio(monkeypatch):
  payload = tone_chunk()
  monkeypatch.setattr(audio_module.messaging, "SubMaster", lambda services: FakeSubMaster(payload))

  track = audio_module.DeviceToWebAudioTrack()
  try:
    await wait_for_buffer(track)
    frame = await asyncio.wait_for(track.recv(), timeout=1)
  finally:
    track.stop()
    track._thread.join(timeout=1)

  pcm = frame.to_ndarray()
  assert frame.sample_rate == audio_module.MIC_SAMPLE_RATE
  assert frame.samples == int(audio_module.MIC_SAMPLE_RATE * audio_module.AUDIO_PTIME)
  assert pcm.shape[-1] == frame.samples
  assert np.abs(pcm).sum() > 0


@pytest.mark.asyncio
async def test_stream_session_uses_device_to_web_audio_track(monkeypatch):
  payload = tone_chunk()
  monkeypatch.setattr(audio_module.messaging, "SubMaster", lambda services: FakeSubMaster(payload))
  monkeypatch.setattr("openpilot.system.webrtc.device.video.LiveStreamVideoStreamTrack", lambda camera_type: VideoStreamTrack())
  monkeypatch.setattr("openpilot.system.webrtc.webrtcd.Params", lambda: SimpleNamespace(get=lambda key: None))

  session = StreamSession(AUDIO_RECVONLY_OFFER_SDP, [], [], [], audio_output=None, debug_mode=False)
  try:
    assert isinstance(session.outgoing_audio_track, audio_module.DeviceToWebAudioTrack)
  finally:
    if session.outgoing_audio_track is not None:
      session.outgoing_audio_track.stop()
      session.outgoing_audio_track._thread.join(timeout=1)
    await session.stream.stop()
