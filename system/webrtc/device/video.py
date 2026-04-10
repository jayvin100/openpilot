import asyncio
import struct
import time

import av
from teleoprtc.tracks import TiciVideoStreamTrack

from cereal import messaging
from openpilot.common.realtime import DT_MDL, DT_DMON

# arbitrary 16-byte UUID identifying openpilot frame-timing SEI messages
TIMING_SEI_UUID = bytes([
  0xa5, 0xe0, 0xc4, 0xa4, 0x5b, 0x6e, 0x4e, 0x1e,
  0x9c, 0x7e, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc,
])


def _escape_rbsp(data: bytes) -> bytearray:
  """
  Prevents frame bytes that might escape into NAL start code.
  Adds 0x03 after two consecutive 0x00 0x00 to escape this.
  """
  out = bytearray()
  zeros = 0
  for b in data:
    if zeros >= 2 and b <= 3:
      out.append(3)
      zeros = 0
    zeros = zeros + 1 if b == 0 else 0
    out.append(b)
  return out


def create_timing_sei(capture_ms: float, encode_ms: float, send_delay_ms: float, send_wall_ms: float) -> bytes:
  """Build an H.264 SEI NAL (user_data_unregistered) carrying frame timing."""
  ts_data = struct.pack('>4d', capture_ms, encode_ms, send_delay_ms, send_wall_ms)
  sei_payload = TIMING_SEI_UUID + ts_data  # 16 + 32 = 48 bytes

  # payload_type=5, payload_size=48, then RBSP stop bit
  rbsp = bytes([5, len(sei_payload)]) + sei_payload + bytes([0x80])
  escaped = _escape_rbsp(rbsp)

  # start-code (4 bytes) + NAL header (forbidden=0, ref_idc=0, type=6 SEI)
  return b'\x00\x00\x00\x01\x06' + bytes(escaped)


class LiveStreamVideoStreamTrack(TiciVideoStreamTrack):
  camera_config = {
    "driver": (DT_DMON, "livestreamDriverEncodeData"),
    "wideRoad": (DT_MDL, "livestreamWideRoadEncodeData"),
  }

  def __init__(self, camera_type: str):
    dt, _ = self.camera_config[camera_type]
    super().__init__(camera_type, dt)

    self._camera_type = ""
    self._sock = None
    self._set_camera(camera_type)
    self._t0_ns = time.monotonic_ns()
    self.timing_sei_enabled = False

  def _set_camera(self, camera_type: str):
    self._camera_type = camera_type
    _, sock_name = self.camera_config[camera_type]
    self._sock = messaging.sub_sock(sock_name, conflate=True)

  def switch_camera(self, camera_type: str):
    if camera_type not in self.camera_config or camera_type == self._camera_type:
      return
    self._set_camera(camera_type)

  async def _recv_message(self):
    while True:
      msg = messaging.recv_one_or_none(self._sock)
      if msg is not None:
        return msg
      await asyncio.sleep(0.005)

  def _build_frame_data(self, msg) -> bytes:
    encode_data = getattr(msg, msg.which())
    if not self.timing_sei_enabled:
      return encode_data.header + encode_data.data

    capture_ms = (encode_data.idx.timestampEof - encode_data.idx.timestampSof) / 1e6
    encode_ms = (msg.logMonoTime - encode_data.idx.timestampEof) / 1e6
    send_delay_ms = (time.monotonic_ns() - msg.logMonoTime) / 1e6
    send_wall_ms = time.time() * 1000  # noqa: TID251
    sei_nal = create_timing_sei(capture_ms, encode_ms, send_delay_ms, send_wall_ms)
    return encode_data.header + sei_nal + encode_data.data

  async def recv(self):
    msg = await self._recv_message()
    packet = av.Packet(self._build_frame_data(msg))
    packet.time_base = self._time_base

    pts = ((time.monotonic_ns() - self._t0_ns) * self._clock_rate) // 1_000_000_000
    packet.pts = pts
    self.log_debug("track sending frame %d", pts)
    return packet

  def codec_preference(self) -> str | None:
    return "H264"
