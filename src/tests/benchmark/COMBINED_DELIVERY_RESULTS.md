# Combined write-span and budget proofs: exact archive delivered

The delivered candidate selects policy 7 in the deferred-counts archive. That
policy uses original instruction lowering, invariant read/write proofs, and
precise short-budget private fallbacks. Deferred counts and general IR are not
selected. The separate deferred-count experiment did not establish a useful
marginal gain. All experimental policies remain off in ordinary defaults.

Exact archive: `/home/claude/.scratch/eka-benchmark/deferred-counts-candidate`.
Source base `a0aaebf372e4dfcb957e469a32e04c512ba46c5c` plus its archived patch;
implementation committed as 026ffc82d. WASM SHA-256:
`dcceea4c30d544b55af11b79d403cabb342b2e5474c7afa41a84ecd6b216464b`.
JS SHA-256:
`e5f9165e91ad0f62f16b28025d6916e5e32bbca3e48e0e17ca170dddaa1fa041`.

## Serial throughput

All runs use the same 18 guest seconds (78–96), 3,975,618,624 instructions,
676 presentations, shared audio and physical GPU. No sampling or owned heavy
work overlaps either warmup or measurement. All twenty trial observations,
including deferred-count and same-binary read controls, remain in
DEFERRED_COUNTS_EVIDENCE.json and are copied into the delivery evidence.

| Batch | Selected policy 7 | Previous served | Throughput change |
| --- | ---: | ---: | ---: |
| A | 12.6169s | 13.23405s | +4.89% |
| B | 12.6285s | 13.75225s | +8.90% |
| C | 12.7918s | 14.12755s | +10.44% |

Pooled means are 12.67907s versus 13.70462s, +8.09% throughput. Slow served
outliers (14.2268s and 15.0373s) inflate that percentage; it is not a promised
8% gain. The separate read-only policy in this same application binary averages
13.2408s in C, giving the combined policy a 3.51% lead there. Both adjacent
read/combined pairs favor the combination. Selected candidate samples span
12.5934–12.9869s, about 1.386–1.429x realtime.

Earlier archived policy-7 negative results remain in COMBINED_PROOFS_RESULTS.md.
These new measurements establish a result for this exact archive on this host;
they do not prove why the older archive's results varied or attribute every
improvement to one source change. Deferred counts themselves remain opt-in.

## Correctness and live acceptance

All 151 compiler tests pass. The selected policy passes all 7,712 explicitly
rebuilt native fault comparisons; the alternative policy 8 also passes the
same matrix (15,424 total). Three native targets and nine frontend checks pass.
Both selected-policy normal and interpreter-checked replays match native exactly
across 1,600 images, guest records and 4,919,249 stereo PCM frames. Existing
crash-harness XFAIL and native-identical movement-heuristic limitations remain
separate from exact equality.

Manual and automatic startup each pass a two-minute live/audio route, including
keyboard/touch, visible gameplay, mobile layout and shutdown. Both sustain
realtime without additional gameplay underruns or dropped audio. Startup audio
recovery remains. Exact lag, audio and resource observations are preserved in
COMBINED_DELIVERY_EVIDENCE.json. The manual desktop/mobile screenshots were also
visually inspected and show active gameplay and controls fitting the viewport.

## HTTPS delivery

The exact archive is now served at https://claude-laptop.lan:8188/ with policy 7
and eager regions disabled. Downloaded JS/WASM hashes match the archive above.
The actual HTTPS launcher passes gesture sound, measured mute/unmute, keyboard,
touch, mobile layout and shutdown, with no page/request/HTTP errors. Requested
and applied policy are checked in the real page.

Maximum sampled lag is 37.72ms (manual) and 49.43ms (automatic), with realtime
ratios 1.00012 and 0.99981. This is not a latency improvement claim. Startup
worklet underruns are six/five; both measured gameplay windows add zero
underruns or drops. Normal and checked replay PCM hashes match native.

Nothing pushed. The next IR/read-proof prototype is separate, uncommitted and
unvalidated; it is not part of the served archive. Ongoing optimization continues.
