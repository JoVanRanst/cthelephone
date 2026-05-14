import numpy as np
import scipy.io.wavfile

fs = 48000
duration = 2.0  # seconds of ring
t = np.linspace(0, duration, int(fs*duration), endpoint=False)
ring = 0.5 * (np.sin(2*np.pi*400*t) + np.sin(2*np.pi*450*t))
# Scale to 8-bit unsigned
ring = ((ring + 1) * 127.5).astype(np.uint8)
scipy.io.wavfile.write('phone_ring.wav', fs, ring)