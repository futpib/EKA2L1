# Registry positive-hit inline layout

This is the historical V13 experiment. The later
[complete cache-hit dispatch change](REGISTRY_CHAIN_INLINE_RESULTS.md) also
keeps the surrounding lookup inside the runner, passes a current-default
comparison, and is now adopted. Its results are separate from this panel.

The existing 4096-slot positive registry cache hit is visible in the header
and always inlined under GCC/Clang. Its unordered-map fallback stays out of
line. Keys, hashing, registration, replacement, invalidation and miss behavior
are unchanged. This registry serves ROM entries; RAM mapping and lifetime
validation remains in validated_code_cache.

All 181 compiler tests pass freshly, including 160,000 randomized lifecycle
comparisons against an independent ordered-map oracle. Repeated lookups cover
collisions, replacement, unregister, clear, null functions and address extremes.
All three native CTest targets pass. Native/WASM comparisons pass 4,032 ARM
fault cases, 1,152 Thumb call cases and 2,880 Thumb memory-fault cases.

Six exact native image/record/PCM replays pass: stationary and moving/firing
Sky Force normal/checked, plus both established Snakes routes. These all select
Thumb memory 1 and ARM memory 0. The separate ARM probes still cover the opt-in
ARM implementation. No game-specific behavior is introduced.

The fixed serial panel has 16 observations: two opposite-order pairs per route
against the frozen V12 archive, with ARM memory disabled in both. Only registry
lookup layout differs. Original limits, mode 3, hardware GPU and shared audio
remain; capture, detailed counters and sampling are off. This is not yet an
untouched-live comparison or sustained realtime acceptance. The goal is open.

The archive wrapper mistakenly labels its own archive as baseline_archive;
that original field is retained and annotated in the evidence. The recorded
base commit and full source patch identify the actual source without ambiguity.
Nothing is deployed or pushed.

## Sixteen-observation panel

All preplanned observations and passive host readings are retained.

| Route | First pair | Reversed pair | Mean throughput change | Candidate realtime range |
| --- | ---: | ---: | ---: | ---: |
| sky | +9.60% | +2.42% | +6.03% | 0.556–0.562x |
| combat | -15.02% | +0.45% | -7.49% | 0.505–0.532x |
| standard | -4.50% | +4.70% | +0.08% | 1.666–1.683x |
| long | +21.43% | +3.24% | +11.49% | 1.464–1.764x |

Each route has only two matching pairs. Inspect their directions and raw times;
the mean is not a universal gain estimate. This compares V12 and V13 (ARM memory disabled in both) archives,
not the untouched live archive. It does not establish zero Snakes loss or
sustained realtime Sky Force, and is insufficient for deployment.

## Selection

The implementation is omitted from the next candidate. Stationary Sky Force
and longer Snakes favor it in both pairs, but combat and standard Snakes
reverse direction. The 12.39/12.70-second longer controls and 12.30-second
closing candidate remain in the panel; the larger mean is not a stable gain
estimate. Candidate Sky Force is only 0.505–0.562x realtime. Restoring the
previous registry implementation keeps this uncertain change out of the next
independent experiment; the lifecycle test and all evidence remain.
