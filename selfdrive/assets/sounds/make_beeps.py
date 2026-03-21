import os
import numpy as np
from scipy.io import wavfile

os.chdir(os.path.dirname(os.path.abspath(__file__)))

sr = 48000
max_int16 = 2**15 - 1


# ── Utility functions ──────────────────────────────────────────────────

def harmonic_beep(freq, duration_seconds):
    n_total = int(sr * duration_seconds)
    signal = np.sin(2 * np.pi * freq * np.arange(n_total) / sr)
    x = np.arange(n_total)
    exp_scale = np.exp(-x / 5.5e3)
    return max_int16 * signal * exp_scale


def concat(*signals, gap=0.0):
    parts = []
    silence = np.zeros(int(sr * gap))
    for i, s in enumerate(signals):
        parts.append(s)
        if i < len(signals) - 1 and gap > 0:
            parts.append(silence)
    return np.concatenate(parts)


def save(name, signal):
    signal = signal / np.max(np.abs(signal)) * max_int16
    wavfile.write(name, sr, signal.astype(np.int16))


# ── Original beeps ─────────────────────────────────────────────────────

engage_beep = harmonic_beep(1661.219, 0.5)
wavfile.write("engage.wav", sr, engage_beep.astype(np.int16))
disengage_beep = harmonic_beep(1318.51, 0.5)
wavfile.write("disengage.wav", sr, disengage_beep.astype(np.int16))


# ══════════════════════════════════════════════════════════════════════════
# alert_double_same: Two identical pips at 1400 Hz with tight gap.
# ══════════════════════════════════════════════════════════════════════════

beep1 = harmonic_beep(1400, 0.5)
beep2 = harmonic_beep(1400, 0.5)
# Overlap: second beep starts while first is still decaying
offset = int(sr * 0.15)  # second beep starts 150ms after first
total = np.zeros(len(beep1) + offset)
total[:len(beep1)] += beep1
total[offset:offset + len(beep2)] += beep2
save("prompt_distracted.wav", total)
