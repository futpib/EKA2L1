# Shared ROM-region policy screen

Frozen V5, Thumb memory enabled in both policies, original execution limits and
mode 3. The policy applies to all games; no game identity selects it. All eight
preplanned serial observations are retained, with physical GPU, shared audio,
rendering without capture, no sampling or detailed counters, and passive host
readings. All owned correctness jobs finished before timing began.

| Route | ROM regions off (s) | On (s) | Throughput change | On realtime ratio |
| --- | ---: | ---: | ---: | ---: |
| sky | 11.79910 | 12.24750 | -3.66% | 0.490x |
| combat | 10.85800 | 11.73930 | -7.51% | 0.511x |
| standard | 11.21000 | 10.29580 | +8.88% | 1.748x |
| long | 9.75408 | 9.79598 | -0.43% | 1.837x |

These are single pairs, with opposite orders across routes. Both Sky Force
pairs regress; the Snakes pairs are mixed. The screen does not support promotion
and cannot establish a stable global regression or gain. The previous exploratory
ROM improvement did not reproduce here. The realtime target remains unmet.

Correctness passes: 46,016 fresh native ARM fault comparisons with actual mode-3
and feature-128 readback; normal/checked stationary Sky Force with ROM regions;
both Snakes routes with ROM regions; and normal/checked moving-and-firing Sky
Force under both policies. All images, guest records and PCM match native.
The new combat reference is documented in SKY_FORCE_COMBAT_REFERENCE.json.
The frozen archive's existing 179-test suite is reused, not counted as a fresh
run. Checked invocations force callbacks; normal replays cover direct memory.

Live remains unchanged. The prefix-writeback candidate is tested separately in
V6 with ROM regions off, preserving attribution between source and policy changes.
