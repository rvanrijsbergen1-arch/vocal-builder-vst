"""Synthetic 30 s stereo test 'song' (no copyrighted material): a sung-like vowel melody on the
left/centre plus a bass/drum-like accompaniment. Only used to exercise lengths, alignment and export."""
import sys
import numpy as np
import soundfile as sf

sr = 44100
t = np.arange(sr * 30) / sr
notes = 220 * 2 ** (np.array([0, 2, 4, 5, 7, 5, 4, 2]) / 12)
f0 = notes[(t // 0.5).astype(int) % len(notes)] * (1 + 0.01 * np.sin(2 * np.pi * 5 * t))
phase = 2 * np.pi * np.cumsum(f0) / sr
voice = sum(np.sin(k * phase) / k for k in range(1, 12)) * (np.sin(np.pi * (t % 0.5) / 0.5) ** 0.5) * 0.2
bass = 0.25 * np.sin(2 * np.pi * 55 * t) * (t % 0.5 < 0.25)
kick = 0.5 * np.exp(-30 * (t % 0.5)) * np.sin(2 * np.pi * 60 * t)
mix = np.stack([voice + bass + kick, voice * 0.9 + bass + kick], 1).astype(np.float32)
sf.write(sys.argv[1], mix / np.abs(mix).max() * 0.8, sr, subtype="PCM_24")
