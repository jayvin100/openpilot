#!/usr/bin/env python3
"""
Hiwonder STM32 motor controller board driver.
Serial protocol: 0xAA 0x55 Function Length Data... CRC8
"""
import fcntl
import glob
import struct
import time

import serial

CRC8_TABLE = [
  0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
  157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
  35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
  190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
  70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
  219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
  101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
  248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
  140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
  17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
  175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
  50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
  202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
  87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
  233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
  116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53,
]

FUNC_BUZZER = 2
FUNC_MOTOR = 3
FUNC_PWM_SERVO = 4


def _crc8(data: bytes) -> int:
  check = 0
  for b in data:
    check = CRC8_TABLE[check ^ b]
  return check & 0xFF


def _find_device() -> str:
  ports = glob.glob("/dev/ttyACM*")
  if not ports:
    raise RuntimeError("no /dev/ttyACM* device found — is the STM32 board plugged in?")
  return ports[0]


class Board:
  def __init__(self, device: str | None = None, baudrate: int = 1_000_000):
    if device is None:
      device = _find_device()
    self.device = device
    self.baudrate = baudrate
    self._open()

  def _open(self) -> None:
    self.port = serial.Serial(None, self.baudrate, timeout=1)
    self.port.rts = False
    self.port.dtr = False
    self.port.setPort(self.device)
    self.port.open()
    # Exclusive lock — prevents a second process from corrupting the serial stream
    try:
      fcntl.flock(self.port.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
      self.port.close()
      raise RuntimeError(f"{self.device} is already locked by another process")
    time.sleep(0.5)

  def reconnect(self) -> None:
    try:
      self.port.close()
    except Exception:
      pass
    time.sleep(0.5)
    self._open()

  def _write(self, func: int, data: bytes | list[int]) -> None:
    buf = bytearray([0xAA, 0x55, func, len(data)])
    buf.extend(data)
    buf.append(_crc8(buf[2:]))
    self.port.write(buf)

  def set_motor_speed(self, speeds: list[tuple[int, float]]) -> None:
    """Set motor speeds. speeds is list of (motor_id, speed) where speed is in pulse/10ms."""
    data = bytearray([0x01, len(speeds)])
    for motor_id, speed in speeds:
      data.extend(struct.pack("<Bf", motor_id - 1, float(speed)))
    self._write(FUNC_MOTOR, data)

  def set_steering(self, pulse: int, servo_id: int = 3, duration_ms: int = 20) -> None:
    """Set PWM servo position. pulse ~500-2500, center=1500."""
    data = struct.pack("<BHBH", 0x01, duration_ms, servo_id, int(pulse))
    # format: sub_cmd, duration_lo, duration_hi, count, [servo_id, pulse_lo, pulse_hi]...
    data = bytearray([0x01, duration_ms & 0xFF, (duration_ms >> 8) & 0xFF, 1])
    data.extend(struct.pack("<BH", servo_id, int(pulse)))
    self._write(FUNC_PWM_SERVO, data)

  def set_buzzer(self, freq: int = 1900, on_time: float = 0.05, off_time: float = 0.01, repeat: int = 1) -> None:
    self._write(FUNC_BUZZER, struct.pack("<HHHH", freq, int(on_time * 1000), int(off_time * 1000), repeat))

  def stop(self) -> None:
    """Stop all motors and center steering."""
    self.set_motor_speed([(1, 0), (2, 0), (3, 0), (4, 0)])
    self.set_steering(1500)

  def close(self) -> None:
    self.stop()
    self.port.close()
