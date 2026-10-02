# Thumb cache writeback at earlier exits

The opt-in bounded Thumb path now publishes only the cached fields written
before each callback or return. This is valid for its acyclic instruction
sequence: internal forward and backward targets are disabled. Reloads retain
the complete final cache layout so callback changes to later operands remain
visible. ARM lowering retains its existing behavior. Caches beyond 64 fields
fall back to conservative publication. No guest instruction, budget, interrupt,
callback, mapping check or scheduling step is removed.

All 179 compiler tests pass, including the existing full-state callback,
register-transfer and long-call boundary matrices. All 1,152 native call cases
and 2,880 native memory-fault cases pass. Fresh normal control/candidate and
checked Sky Force replays, both established Snakes routes, and normal/checked
moving-and-firing Sky Force replays match native images, records and PCM exactly.
The combat reference contains 240 images and 2,375,493 stereo PCM frames.
Selected checked invocations force memory callbacks; normal replays and fault
probes independently cover direct paths.

This archive was built from 3e088bf9c plus the recorded two-file source patch.
The intervening combat-reference commit changes only input and evidence.
The option remains disabled by default and unserved. Speed measurement follows
the separate frozen-V5 ROM-policy panel, with no overlapping owned build,
correctness job or profiler. The realtime target remains open.
