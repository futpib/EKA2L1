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

## Completed screen and diagnostic profile

All eight planned observations are retained. Each route has one pair.

| Route | V11b seconds | V12 seconds | Throughput change | V12 realtime |
| --- | ---: | ---: | ---: | ---: |
| sky | 11.00060 | 12.15330 | -9.48% | 0.494x |
| combat | 10.21780 | 10.26940 | -0.50% | 0.584x |
| standard | 10.51190 | 9.99253 | +5.20% | 1.801x |
| long | 9.76769 | 9.76460 | +0.03% | 1.843x |

The screen is mixed and does not support promotion. Both archives select the
same direct ARM policy; this isolates state caching, not the combined change
against live. The subsequent normal-browser sampled profile attributes 38.96%
of CPU-worker samples inclusively to generated code and 37.54% as self time to
InterpreterMainLoop, which includes the inlined runner. These percentages
include idle/wait samples and are diagnostic, not timing-panel observations
or a measured interpreter/runner split.

The next registry lookup experiment uses the shared Thumb/V8 policy with
ARM direct memory disabled in both matching binaries. ARM candidates remain
opt-in and unserved. The realtime target is still unmet.
