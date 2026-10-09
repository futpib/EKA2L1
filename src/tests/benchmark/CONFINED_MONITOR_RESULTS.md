# Confine browser reservation state to its CPU worker

Not adopted: CPU ownership removes monitor atomics but does not produce a useful repeatable runtime gain.

The browser system opts into explicit thread-confined monitor ownership. Monitor-state locking is bypassed under that immutable contract; the default shared monitor retains synchronization. The CPU worker is the only runtime owner, verifier access occurs synchronously on that worker, and shutdown joins it before destruction. Guest-memory compare-and-swap remains atomic because external memory users still exist. Ownership may move only while no monitor call is in flight.

## Controlled runtime comparison

Control: `17e19c3d5`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 6.8459 → 6.8522 | -0.09% | -0.42% | -0.01% | 2/4 |
| 2 | Sky Force | 19.3806 → 19.2021 | +0.93% | +0.88% | -0.23% | 2/4 |

Sky Force gains 0.93% on average with only two of four pairs favorable. The reversed-order panel is unfavorable; all observations remain included. Snakes is flat. The smaller executed native path does not establish a speedup. This result applies to this ownership-check implementation, not every possible thread-confined design.

Snakes pairs range from -0.83% to +0.41%; candidate wall speed is **2.15× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from -3.09% to +5.63%; candidate wall speed is **0.84× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

The actual warmed V8 capture recovers all 160 selected versions. InterpreterMainLoop shrinks from 166,080 to 164,800 native bytes. In the hot LDREX path the immutable ownership check at +0x1be41 through +0x1be53 jumps to the ordinary reservation store at +0x1bea6, bypassing the shared lock exchange at +0x1be97; the bypass branch is sampled during gameplay and the lock loop has no samples there. The shared synchronization path remains present for other monitor instances. Guest-memory lock cmpxchg remains in the STREX path. This proves the intended executed lock bypass; separate controlled timing decides adoption.

498,877 independent reservation state, concurrency and handoff checks pass in native and WASM builds. Both ownership modes cover 1/2/8 processor sequences; shared mode retains competing-thread and cross-thread clear tests; confined mode adds ordered thread handoff. Runner coverage includes 504 policy/budget/IRQ/stop/mutation cases and 56 inline boundaries. SVC-return coverage includes 600 eligibility, 14,976 full-state and 480 real-core cases. Memory checks cover 816 ARM/Thumb comparisons plus CPU entry, ASID, syscall, callback, permission and observer lifetimes. Both exact 60-frame image/progress/PCM/audio-event replays pass. Source ownership evidence is retained in PLAN.md; browser shutdown joins its CPU worker before system destruction.

The prototype is archived and removed from active source. LAN retains the preceding adopted artifact.

See [full observations and evidence](CONFINED_MONITOR_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-hotspot-round/monitor-confined`.
