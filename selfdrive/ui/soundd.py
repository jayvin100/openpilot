import json
import math
import numpy as np
import os
import time
import wave


from cereal import car, messaging
from openpilot.common.basedir import BASEDIR
from openpilot.common.filter_simple import FirstOrderFilter
from openpilot.common.realtime import Ratekeeper
from openpilot.common.utils import retry
from openpilot.common.swaglog import cloudlog

from openpilot.system import micd
from openpilot.system.hardware import HARDWARE

SAMPLE_RATE = 48000
SAMPLE_BUFFER = 4096 # (approx 100ms)
MAX_VOLUME = 1.0
MIN_VOLUME = 0.1
SELFDRIVE_STATE_TIMEOUT = 5 # 5 seconds
FILTER_DT = 1. / (micd.SAMPLE_RATE / micd.FFT_SAMPLES)

AMBIENT_DB = 24 # DB where MIN_VOLUME is applied
DB_SCALE = 30 # AMBIENT_DB + DB_SCALE is where MAX_VOLUME is applied

VOLUME_BASE = 20
if HARDWARE.get_device_type() == "tizi":
  AMBIENT_DB = 30
  VOLUME_BASE = 10

AudibleAlert = car.CarControl.HUDControl.AudibleAlert

SYNTH_WAVE_SINE = 0
SYNTH_WAVE_SQUARE = 1
SYNTH_WAVE_SAW = 2
SYNTH_WAVE_TRIANGLE = 3

SYNTH_STATE_PATH = "/tmp/sound_playground_state.json"


sound_list: dict[int, tuple[str, int | None, float]] = {
  # AudibleAlert, file name, play count (none for infinite)
  AudibleAlert.engage: ("engage.wav", 1, MAX_VOLUME),
  AudibleAlert.disengage: ("disengage.wav", 1, MAX_VOLUME),
  AudibleAlert.refuse: ("refuse.wav", 1, MAX_VOLUME),

  AudibleAlert.prompt: ("prompt.wav", 1, MAX_VOLUME),
  AudibleAlert.promptRepeat: ("prompt.wav", None, MAX_VOLUME),
  AudibleAlert.promptDistracted: ("prompt_distracted.wav", None, MAX_VOLUME),

  AudibleAlert.warningSoft: ("warning_soft.wav", None, MAX_VOLUME),
  AudibleAlert.warningImmediate: ("warning_immediate.wav", None, MAX_VOLUME),
}
if HARDWARE.get_device_type() == "tizi":
  sound_list.update({
    AudibleAlert.engage: ("engage_tizi.wav", 1, MAX_VOLUME),
    AudibleAlert.disengage: ("disengage_tizi.wav", 1, MAX_VOLUME),
  })

def check_selfdrive_timeout_alert(sm):
  ss_missing = time.monotonic() - sm.recv_time['selfdriveState']

  if ss_missing > SELFDRIVE_STATE_TIMEOUT:
    if sm['selfdriveState'].enabled and (ss_missing - SELFDRIVE_STATE_TIMEOUT) < 10:
      return True

  return False


class Soundd:
  def __init__(self):
    self.load_sounds()

    self.current_alert = AudibleAlert.none
    self.current_volume = MIN_VOLUME
    self.current_sound_frame = 0

    self.selfdrive_timeout_alert = False

    self.spl_filter_weighted = FirstOrderFilter(0, 2.5, FILTER_DT, initialized=False)

    # Real-time synth for speaker tuning UI
    self.synth_phase = 0.0
    self.synth_freq_hz = 440.0
    self.synth_gain = 1.0
    self.synth_volume = 1.0
    self.synth_waveform = SYNTH_WAVE_SINE
    self.synth_play = False
    self.synth_envelope = 0.0
    self.synth_state_mtime: float | None = None

  def load_sounds(self):
    self.loaded_sounds: dict[int, np.ndarray] = {}

    # Load all sounds
    for sound in sound_list:
      filename, play_count, volume = sound_list[sound]

      with wave.open(BASEDIR + "/selfdrive/assets/sounds/" + filename, 'r') as wavefile:
        assert wavefile.getnchannels() == 1
        assert wavefile.getsampwidth() == 2
        assert wavefile.getframerate() == SAMPLE_RATE

        length = wavefile.getnframes()
        self.loaded_sounds[sound] = np.frombuffer(wavefile.readframes(length), dtype=np.int16).astype(np.float32) / (2**16/2)

  def get_sound_data(self, frames): # get "frames" worth of data from the current alert sound, looping when required
    if self.current_alert != AudibleAlert.none:
      ret = np.zeros(frames, dtype=np.float32)
      num_loops = sound_list[self.current_alert][1]
      sound_data = self.loaded_sounds[self.current_alert]
      written_frames = 0

      current_sound_frame = self.current_sound_frame % len(sound_data)
      loops = self.current_sound_frame // len(sound_data)

      while written_frames < frames and (num_loops is None or loops < num_loops):
        available_frames = sound_data.shape[0] - current_sound_frame
        frames_to_write = min(available_frames, frames - written_frames)
        ret[written_frames:written_frames+frames_to_write] = sound_data[current_sound_frame:current_sound_frame+frames_to_write]
        written_frames += frames_to_write
        self.current_sound_frame += frames_to_write

      return ret * self.current_volume

    return self.get_synth_data(frames)

  def get_synth_data(self, frames):
    target_gain = 1.0 if self.synth_play else 0.0
    if (target_gain <= 0.0 or self.synth_volume <= 0.0) and self.synth_envelope <= 1e-4:
      self.synth_envelope = 0.0
      return np.zeros(frames, dtype=np.float32)

    phase_inc = 2.0 * math.pi * self.synth_freq_hz / SAMPLE_RATE
    phases = self.synth_phase + phase_inc * np.arange(frames, dtype=np.float32)
    self.synth_phase = float((phases[-1] + phase_inc) % (2.0 * math.pi))

    if self.synth_waveform == SYNTH_WAVE_SQUARE:
      wave = np.where(np.sin(phases) >= 0.0, 1.0, -1.0).astype(np.float32)
    elif self.synth_waveform == SYNTH_WAVE_SAW:
      phase_unit = np.mod(phases / (2.0 * math.pi), 1.0).astype(np.float32)
      wave = (2.0 * phase_unit - 1.0).astype(np.float32)
    elif self.synth_waveform == SYNTH_WAVE_TRIANGLE:
      phase_unit = np.mod(phases / (2.0 * math.pi), 1.0).astype(np.float32)
      wave = (1.0 - 4.0 * np.abs(phase_unit - 0.5)).astype(np.float32)
    else:
      wave = np.sin(phases).astype(np.float32)

    ramp = np.linspace(self.synth_envelope, target_gain, frames, dtype=np.float32)
    self.synth_envelope = float(target_gain)
    return wave * ramp * self.synth_volume

  def update_synth_params(self):
    try:
      mtime = os.path.getmtime(SYNTH_STATE_PATH)
    except OSError:
      self.synth_play = False
      self.synth_state_mtime = None
      return

    if self.synth_state_mtime is not None and mtime == self.synth_state_mtime:
      return

    try:
      with open(SYNTH_STATE_PATH) as f:
        state = json.load(f)
      if not isinstance(state, dict):
        return
    except (OSError, ValueError, TypeError):
      return

    self.synth_state_mtime = mtime
    self.synth_freq_hz = float(np.clip(float(state.get("frequency_hz", self.synth_freq_hz)), 20.0, 12000.0))
    self.synth_gain = 1.0
    self.synth_volume = float(np.clip(float(state.get("volume", self.synth_volume)), 0.0, 1.0))
    self.synth_waveform = int(np.clip(int(state.get("waveform", self.synth_waveform)), SYNTH_WAVE_SINE, SYNTH_WAVE_TRIANGLE))
    self.synth_play = bool(state.get("play", False))

  def callback(self, data_out: np.ndarray, frames: int, time, status) -> None:
    if status:
      cloudlog.warning(f"soundd stream over/underflow: {status}")
    data_out[:frames, 0] = self.get_sound_data(frames)

  def update_alert(self, new_alert):
    current_alert_played_once = self.current_alert == AudibleAlert.none or self.current_sound_frame > len(self.loaded_sounds[self.current_alert])
    if self.current_alert != new_alert and (new_alert != AudibleAlert.none or current_alert_played_once):
      self.current_alert = new_alert
      self.current_sound_frame = 0

  def get_audible_alert(self, sm):
    if sm.updated['selfdriveState']:
      new_alert = sm['selfdriveState'].alertSound.raw
      self.update_alert(new_alert)
    elif check_selfdrive_timeout_alert(sm):
      self.update_alert(AudibleAlert.warningImmediate)
      self.selfdrive_timeout_alert = True
    elif self.selfdrive_timeout_alert:
      self.update_alert(AudibleAlert.none)
      self.selfdrive_timeout_alert = False

  def calculate_volume(self, weighted_db):
    volume = ((weighted_db - AMBIENT_DB) / DB_SCALE) * (MAX_VOLUME - MIN_VOLUME) + MIN_VOLUME
    return math.pow(VOLUME_BASE, (np.clip(volume, MIN_VOLUME, MAX_VOLUME) - 1))

  @retry(attempts=10, delay=3)
  def get_stream(self, sd):
    # reload sounddevice to reinitialize portaudio
    sd._terminate()
    sd._initialize()
    return sd.OutputStream(channels=1, samplerate=SAMPLE_RATE, callback=self.callback, blocksize=SAMPLE_BUFFER)

  def soundd_thread(self):
    # sounddevice must be imported after forking processes
    import sounddevice as sd

    sm = messaging.SubMaster(['selfdriveState', 'soundPressure'])

    with self.get_stream(sd) as stream:
      rk = Ratekeeper(20)

      cloudlog.info(f"soundd stream started: {stream.samplerate=} {stream.channels=} {stream.dtype=} {stream.device=}, {stream.blocksize=}")
      while True:
        sm.update(0)
        self.update_synth_params()

        if sm.updated['soundPressure'] and self.current_alert == AudibleAlert.none: # only update volume filter when not playing alert
          self.spl_filter_weighted.update(sm["soundPressure"].soundPressureWeightedDb)
          self.current_volume = self.calculate_volume(float(self.spl_filter_weighted.x))

        self.get_audible_alert(sm)

        rk.keep_time()

        assert stream.active


def main():
  s = Soundd()
  s.soundd_thread()


if __name__ == "__main__":
  main()
