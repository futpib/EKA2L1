# ARM short-block runtime state cache

The opt-in direct ARM short-block path now keeps PC and runtime fields
(including budget, CPSR endian state and TLB pointer) in locals. Existing
callback barriers publish written state and reload the live values; final
returns publish the exact stopping state. Stop counts and IRQ signals remain
uncached. General register/flag caching existed before this change. No guest
instruction, callback, budget, scheduling rule or mapping check is removed.

All 180 compiler tests pass freshly, including the expanded 84,526-case ARM
transfer/full-state/callback matrix. Callback cases check PC visibility, replace TLB, endian state and budget,
request stops, and raise masked/unmasked IRQ. Production
native comparisons pass all 4,032 ARM fault cases, 1,152 Thumb call cases and
2,880 Thumb memory-fault cases. All three native CTest targets pass.

All seven exact native image/record/PCM replays pass: stationary Sky Force
control/candidate/checked, moving-and-firing Sky Force normal/checked, and both
Snakes routes. Checked invocations force callback memory; normal replay and
the unit/native matrices separately exercise direct accesses.

The candidate was built and frozen while the preceding frozen V11b compiler
suite ran. Source was restored immediately after freezing, before independent
V12 correctness checks. Archives record the exact base, complete source patch
and hashes. Timing waits for every owned build/correctness job to finish; these
concurrent correctness runs supply no performance evidence.

The fixed eight-observation serial screen compares V11b/V12 on four routes,
with opposite orders across routes, so only ARM PC/runtime caching differs.
It starts after the preceding V8/V11b screen finishes. Both select the same
Thumb/direct-ARM policies, mode 3, original limits, physical GPU and shared
audio; capture, sampling and detailed counters are disabled. Single pairs
are exploratory. Reordered confirmation, untouched-live comparisons and
sustained normal play/audio remain required before promotion. Realtime Sky
Force remains the open target; no game-specific rules, deployment or push.
