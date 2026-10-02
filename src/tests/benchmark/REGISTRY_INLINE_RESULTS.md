# Registry positive-hit inline layout

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
