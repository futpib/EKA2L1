#!/usr/bin/env python3
"""Validate the guest-clock PCM artifact and hash audio plus callback timing."""
import hashlib
import json
from pathlib import Path
import struct
import wave


def audio_record(directory, first_us, last_us):
    directory = Path(directory)
    with wave.open(str(directory / 'audio.wav'), 'rb') as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (2, 2, 48000):
            raise RuntimeError('Expected 48 kHz stereo signed 16-bit PCM')
        count = wav.getnframes()
        if count != last_us * 48000 // 1000000:
            raise RuntimeError(f'Audio duration disagrees with guest clock: {count} frames at {last_us} us')
        pcm = wav.readframes(count)
    events = [json.loads(line) for line in (directory / 'audio.jsonl').read_text().splitlines()]
    if any(a['virtual_us'] > b['virtual_us'] for a, b in zip(events, events[1:])):
        raise RuntimeError('Audio events run backwards')
    if any(e['virtual_us'] > last_us for e in events):
        raise RuntimeError('Audio event after final frame')
    shared = (directory / 'audio-backend.json').exists()
    writes = [e for e in events if e['event'] == 'write_bytes' and e['value']]
    callbacks = [e for e in events if e['event'] == ('render' if shared else 'more_buffer')]
    gameplay = pcm[first_us * 48000 // 1000000 * 4:]
    samples = [s[0] for s in struct.iter_unpack('<h', gameplay)]
    nonzero = sum(s != 0 for s in samples)
    if (not shared and not writes) or not callbacks or not nonzero:
        raise RuntimeError('No active gameplay audio or buffer callbacks')
    return {'sample_frames': count, 'sample_rate': 48000, 'channels': 2,
            'sha256_pcm': hashlib.sha256(pcm).hexdigest(),
            'sha256_events': hashlib.sha256((directory / 'audio.jsonl').read_bytes()).hexdigest(),
            'writes': len(writes), 'callbacks': len(callbacks),
            **({'backend': 'shared-dsp-cubeb-resampler', 'render_callbacks': len(callbacks)} if shared else {}),
            'gameplay_nonzero_samples': nonzero,
            'gameplay_peak': max(abs(s) for s in samples)}


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument('directory', type=Path)
    a = p.parse_args()
    frames = [json.loads(line) for line in (a.directory / 'frames.jsonl').read_text().splitlines()]
    print(json.dumps(audio_record(a.directory, frames[0]['virtual_us'], frames[-1]['virtual_us']), indent=2))
