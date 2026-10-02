# Total shared improvement versus untouched live

All planned serial observations are retained, with original limits, mode 3,
literal feature 128, hardware GPU, shared audio, no capture, no sampling and
no detailed counters. Guest instruction and presentation totals match within
each route. Each archive is frozen; recorded harness HEAD is not build provenance.

| Route | First pair | Reversed pair | Candidate realtime |
| --- | ---: | ---: | ---: |
| sky | +43.88% | +41.64% | 0.521–0.532x |
| combat | +37.06% | +43.27% | 0.544–0.549x |
| standard | +3.17% | +1.69% | 1.587–1.628x |
| long | +0.52% | +0.61% | 1.635–1.645x |

Both Sky Force native replay gates pass freshly for the untouched live archive;
V8 correctness acceptance is explicitly reused from its frozen archive. The
old live build has no Thumb/ARM/ROM-leaf experiment APIs. The harness leaves
those variables unset; -1/null records absence, not a fake policy readback.
V8 explicitly selects Thumb memory 1; eager ROM remains 0 for both.

All eight matching pairs favor V8, but two pairs per route do not prove zero
regression in every workload. Sky Force is still only 0.52–0.55x realtime.
Both Snakes routes retain more than 1.58x unpaced headroom. This is a total
change comparison, not normal sustained audio/UI acceptance. The realtime
goal remains unmet; no deployment or push. The served WASM remains unchanged.

The first panel script failed Python syntax compilation before any execution;
its source is retained as compare_live_v8_initial_syntax_failure.py. The
corrected script produced the fixed plan before its first sample.
