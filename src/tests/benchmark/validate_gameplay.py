#!/usr/bin/env python3
"""Check the fixed Snakes replay has moving scenes and sustained virtual-time progress.

These quantitative checks supplement inspection of the gameplay video; image
variation alone cannot prove that arbitrary content is gameplay.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
from PIL import Image, ImageChops
from run_native import frame_records

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
p.add_argument('--frames', type=int, default=1000)
a = p.parse_args()
records = frame_records(a.directory, a.frames)
if len(records) < 2:
    raise SystemExit('Need at least two frames')
viewport_hashes = set()
changes = []
previous = None
for i, record in enumerate(records):
    with Image.open(a.directory / f'frame-{i:04d}.png') as image:
        # Exclude the score and lower HUD; require movement in the 3D scene.
        viewport = image.convert('RGB').crop((0, 30, image.width, image.height - 40))
    viewport_hashes.add(hashlib.sha256(viewport.tobytes()).hexdigest())
    if previous is not None:
        r, g, b = ImageChops.difference(previous, viewport).split()
        changed = ImageChops.lighter(ImageChops.lighter(r, g), b).histogram()
        changes.append(1 - changed[0] / (viewport.width * viewport.height))
    previous = viewport

deltas = [b['virtual_us'] - x['virtual_us'] for x, b in zip(records, records[1:])]
seconds = (records[-1]['virtual_us'] - records[0]['virtual_us']) / 1e6
result = {
    'frames': len(records),
    'unique_images': len({r['sha256_rgba'] for r in records}),
    'unique_viewports': len(viewport_hashes),
    'first_virtual_us': records[0]['virtual_us'],
    'last_virtual_us': records[-1]['virtual_us'],
    'virtual_seconds': seconds,
    'game_images_per_virtual_second': (len(records)-1) / seconds if seconds > 0 else 0,
    'median_interval_us': statistics.median(deltas),
    'max_interval_us': max(deltas),
    'min_viewport_changed_fraction': min(changes),
    'median_viewport_changed_fraction': statistics.median(changes),
}
# This fixed replay should produce roughly 21 images/s, with no menu pauses.
result['pass'] = (len(viewport_hashes) >= 0.95 * len(records)
                  and min(changes) >= 0.01
                  and min(deltas) > 0 and max(deltas) <= 100000
                  and 18 <= result['game_images_per_virtual_second'] <= 25)
print(json.dumps(result, indent=2))
raise SystemExit(0 if result['pass'] else 1)
