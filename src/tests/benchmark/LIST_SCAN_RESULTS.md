# Summarize the hot linked-list scan

Adopted without a new selector. The compiler recognizes an eight-instruction
ARM list-scan structure and register roles at any address. The hot match is
EUser at `0x8019d818`. Node values remain in WASM locals, a complete iteration
adds seven guest instructions at once, and architectural flags are reconstructed
only on observable exits. Memory accesses retain their order and use the existing
direct-memory span proof. There are no per-instruction runtime counters.

Short budgets, unavailable direct memory and unsupported instruction forms retain
precise execution. The summary preserves empty/ready exits, partial-memory
fallbacks, register/flag state, exact instruction counts and backedge IRQ checks.
ROM and game files remain stock.

## Controlled runtime comparison

Control: adopted `7a5ec3472`, including static syscall bindings and the empty
monitor shortcut. These are incremental results; gains from separate reports
must not be added. Four launches per build per game use ABBA then BAAB.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.0701 → 7.0815 | -0.16% | -0.01% | +0.04% | 3/4 |
| 2 | Sky Force | 21.0168 → 19.8995 | +5.61% | +5.11% | -7.75% | 4/4 |

Sky Force pairs range from +4.19% to +7.45%. Snakes is effectively flat.
All 16 valid observations are retained; 0 invalid attempts.
Guest progress, instruction counts and presentation journals match. The worker
uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and
counter checks validate the 3.6 GHz request. No temperature gates or cooldowns
apply. All 88 live host-restoration checks pass.

Candidate wall speeds are Snakes **2.09×** and Sky Force
combat **0.81×** realtime in the fixed 18-guest-second windows.

## Native evidence and correctness

Actual warmed V8 code confirms that gameplay executes the summary. The loop
keeps flags out of its successful backedge and reconstructs them on exits.
The complete function grows from 2,816 to 4,032 native bytes because the precise
fallback remains. Its sample share falls from 2,284/18,630 (12.3%) to
1,300/20,392 (6.4%). Those capture shares establish location and code shape;
adoption uses the separate controlled timing above. Native capture recovers
159 of 160 selected versions.

The full WASM AOT suite passes **184 tests, zero failures**. New coverage
compares **54,720** complete register/flag/memory/count states with the independent
DynCom interpreter: relocated addresses, register renaming, list lengths, ready
nodes, budgets, IRQ/stop, endianness, absent views, alignment and page failures.
Both 60-frame game replays match images, progress, PCM and audio events exactly.

The measured artifact is served at `https://claude-laptop.lan:8188/`; its served
WASM hash matches. Both real game-picker paths pass gameplay, input and defaults
with NVIDIA hardware rendering; saved screenshots were inspected. The existing
non-silent browser-audio failure remains, so full live-audio E2E is not claimed.

See [full observations and evidence](LIST_SCAN_RESULTS.json) and the raw campaign
under `/home/claude/.scratch/eka-hotspot-round/list-scan`.
