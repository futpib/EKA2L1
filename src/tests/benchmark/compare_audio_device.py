#!/usr/bin/env python3
"""Align a captured 48 kHz stereo S16LE monitor recording to a source WAV.

Capture with parec --latency-msec=10 to avoid truncating its buffered tail.
Only leading device/startup latency is removed; no time warp or gain correction.
"""
import argparse
import json
import wave
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('source')
p.add_argument('capture')
a = p.parse_args()
with wave.open(a.source, 'rb') as w:
    assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (2, 2, 48000)
    x = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, 2).astype(float)
y = np.fromfile(a.capture, '<i2').reshape(-1, 2).astype(float)
n = 1 << int(len(y) + len(x) - 2).bit_length()
c = np.fft.irfft(np.fft.rfft(y[:, 0], n) * np.fft.rfft(x[::-1, 0], n), n)
start = int(np.argmax(c)) - (len(x) - 1)
assert start >= 0 and start + len(x) <= len(y), 'Recording truncated'
z = y[start:start + len(x)]
d = z - x
print(json.dumps({'offset_frames': start, 'source_frames': len(x), 'recorded_frames': len(y),
                 'rms_error': float(np.sqrt(np.mean(d*d))), 'max_error': float(abs(d).max()),
                 'exact_sample_fraction': float(np.mean(d == 0)),
                 'gain': float(np.sum(x*z) / np.sum(x*x)),
                 'correlation': float(np.corrcoef(x.ravel(), z.ravel())[0, 1])}, indent=2))
assert np.array_equal(x, z), 'Device output differs from the source PCM'
