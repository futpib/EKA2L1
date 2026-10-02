# Bounded Thumb long-call fusion

This opt-in shared compiler experiment joins a valid ARMv5/v6 BL or BLX pair
within the existing source window. It preserves both separately budgeted
halfwords: intermediate LR and suffix PC are published if the budget ends,
a stop is requested, or an unmasked interrupt is pending. The stop check reads
the full 64-bit counter. BLX retains its ARM-mode switch and target alignment;
undefined odd BLX and truncated encodings retain the previous fallback.
No address, game or ROM identity selects the optimization.

The existing Thumb-memory option selects this stage; it remains disabled by
default and unserved. The source archive is based on e31495074 plus the recorded
patch. This stage includes the preceding unpromoted state cache, whose separate
speed screen did not establish a gain. Retaining that stage in a final combined
change still requires measurement.

All 178 compiler tests pass, including 520,016 bounded comparisons, 9,222
Thumb memory/callback checks and 288 exact full-state call-boundary cases.
The production WASM runner matches all 1,152 native DynCom call cases and
2,880 native memory-fault comparisons. Native DynCom is used for calls because
Dynarmic Step treats the architectural call as one instruction and cannot expose
the emulator's midpoint. The memory-fault goldens use native Dynarmic stepping.

Sky Force normal control, normal candidate and checked candidate each match
240 native images, guest records and 1,703,464 PCM frames. Candidate Snakes
matches the standard 1,600-image/4,656,051-PCM and longer 360-image/2,832,756-PCM
references. The selected checked invocations force memory callbacks; normal
replays, unit tests and actual runner probes separately cover direct memory.

A preplanned six-observation serial screen compares the frozen v3 and v4
archives with Thumb memory enabled in both, unchanged limits and scheduling,
physical GPU rendering without capture, shared audio and no sampling or detailed
counters. One pair per route is exploratory, not promotion evidence. A later
comparison with the untouched live archive and normal live/audio acceptance
remain necessary. Sky Force realtime with preserved Snakes realtime remains open.
