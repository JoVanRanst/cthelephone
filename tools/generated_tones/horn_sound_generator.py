import numpy as np
import wave
import struct
from scipy.signal import butter, lfilter

# ----------------------------
# CONFIG
# ----------------------------
SAMPLE_RATE = 48000 # classic telephony rate
BIT_DEPTH = 8       # 8-bit PCM
AMP = 0.6           # base amplitude

# ----------------------------
# SIGNAL GENERATORS
# ----------------------------
def sine_wave(freq, duration, sr=SAMPLE_RATE, amp=AMP):
    t = np.linspace(0, duration, int(sr * duration), endpoint=False)
    return amp * np.sin(2 * np.pi * freq * t)

def silence(duration, sr=SAMPLE_RATE):
    return np.zeros(int(sr * duration))

def apply_envelope(signal, attack=0.01, release=0.01):
    n = len(signal)
    env = np.ones(n)

    attack_len = int(attack * SAMPLE_RATE)
    release_len = int(release * SAMPLE_RATE)

    if attack_len > 0:
        env[:attack_len] = np.linspace(0, 1, attack_len)
    if release_len > 0:
        env[-release_len:] = np.linspace(1, 0, release_len)

    return signal * env

# ----------------------------
# TELEPHONY EFFECTS
# ----------------------------
def bandpass_filter(signal, low=300, high=3400, sr=SAMPLE_RATE):
    nyq = 0.5 * sr
    low /= nyq
    high /= nyq
    b, a = butter(4, [low, high], btype='band')
    return lfilter(b, a, signal)

def add_noise(signal, level=0.02):
    noise = np.random.normal(0, level, len(signal))
    return signal + noise

def add_hum(signal, freq=50, level=0.01):
    hum = sine_wave(freq, len(signal)/SAMPLE_RATE, amp=level)
    return signal + hum

def degrade(signal):
    signal = bandpass_filter(signal)
    signal = add_noise(signal)
    signal = add_hum(signal)
    return signal

# ----------------------------
# WAV EXPORT (8-bit PCM)
# ----------------------------
def write_wav(filename, signal, sr=SAMPLE_RATE):
    # Normalize
    signal = signal / np.max(np.abs(signal))

    # Convert to 8-bit unsigned PCM (0–255)
    signal_8bit = np.int16(signal * 127)
    signal_8bit = (signal_8bit + np.int16(128)).astype(np.uint8)

    with wave.open(filename, 'w') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(1)  # 8-bit
        wf.setframerate(sr)
        wf.writeframes(signal_8bit.tobytes())

# ----------------------------
# TONE DEFINITIONS (EU / Belgium)
# ----------------------------

# 1. Dial Tone (continuous 425 Hz)
def generate_dial_tone(duration=5):
    tone = sine_wave(425, duration)
    tone = apply_envelope(tone)
    return degrade(tone)

# 2. Rotary Phone Ring (1s ON / 4s OFF)
def generate_ring(duration=10):
    pattern = []
    elapsed = 0

    while elapsed < duration:
        on = apply_envelope(sine_wave(425, 1.0))
        off = silence(4.0)
        pattern.append(on)
        pattern.append(off)
        elapsed += 5.0

    signal = np.concatenate(pattern)[:int(SAMPLE_RATE * duration)]
    return degrade(signal)

# 3. Connection Tone (Busy tone: 0.5s ON / 0.5s OFF)
def generate_busy(duration=5):
    pattern = []
    elapsed = 0

    while elapsed < duration:
        on = apply_envelope(sine_wave(425, 0.5))
        off = silence(0.5)
        pattern.append(on)
        pattern.append(off)
        elapsed += 1.0

    signal = np.concatenate(pattern)[:int(SAMPLE_RATE * duration)]
    return degrade(signal)

# ----------------------------
# MAIN
# ----------------------------
if __name__ == "__main__":
    dial = generate_dial_tone()
    ring = generate_ring()
    busy = generate_busy()

    write_wav("dial_tone_be.wav", dial)
    write_wav("rotary_ring_be.wav", ring)
    write_wav("busy_tone_be.wav", busy)

    print("Generated: dial_tone_be.wav, rotary_ring_be.wav, busy_tone_be.wav")