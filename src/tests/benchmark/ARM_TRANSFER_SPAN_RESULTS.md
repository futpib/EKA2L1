# ARM short-block register-transfer spans

The opt-in ARM short-block path can prove an entire multi-register transfer
within one aligned, permitted, little-endian page. Successful proof removes
repeated per-word page checks. Otherwise the original per-access path retains
precise fault/callback state and ordering. All register transfers and writeback
remain; instruction budgets, stops, interrupts, mappings and guest clocks are
unchanged. Modes 0/1/2 keep callback stores and tracking. Mode 3 permits direct
stores under the already accepted immutable-code policy. No game selection.

All 180 compiler tests pass freshly. The expanded 84,526-case ARM matrix covers
all four addressing directions/index choices, legal writeback forms, high
registers/PC, base-in-list without writeback, wider lists, short budgets,
permissions, endian state, both TLB layouts and page boundaries. Existing
callback tests cover full-state visibility, remapping, budgets, stops and IRQ.

The first expanded matrix exposed overflow in the test memory's addr+width
bounds checks. Decrementing near zero reached 0xfffffffc, whose addition
wrapped and admitted an invalid host access. Subtraction-based bounds checks
and guarded byte/code-copy access correct the fixture. The failed suite is
retained. Only the test harness changed for V11b; byte identity is recorded for
all production emulator and native/WASM fault-probe artifacts.

The identical production artifacts pass 4,032 native ARM fault comparisons,
1,152 Thumb call comparisons, 2,880 Thumb memory-fault comparisons, all three
native CTest targets, and seven exact native image/record/PCM replays: stationary
Sky Force control/candidate/checked, moving-and-firing Sky Force normal/checked,
and both Snakes routes. These V11 results are explicitly reused for V11b because
production artifacts are identical. Checked invocations force callbacks;
normal replay, unit matrices and native faults cover direct memory separately.

The retry wrapper initially expected a different CTest success sentence.
Its assertion/log is retained; an independent completion check verifies the
actual native exit status, three passed targets, all seven comparisons, the
fresh full compiler suite and artifact hashes without rerunning passed tests.

The fixed eight-observation screen compares V8 with the aggregate V11b ARM
short-memory/span candidate on four routes. It does not isolate the span's
incremental speed effect from V10b. Both use the same Thumb policy, mode 3,
original limits, physical GPU and shared audio; capture, sampling and detailed
counters are disabled. Single pairs are exploratory. Reordered confirmation,
untouched-live comparisons and sustained normal play/audio remain necessary
before promotion. Realtime Sky Force remains unachieved; nothing deployed or
pushed and the ARM option remains disabled by default.

## Four-route screen

All eight planned observations are retained. The watcher was restarted before
any sample to wait for the independent V12 correctness queue; the initial
plan is retained and the sample order did not change.

| Route | V8 seconds | V11b seconds | Throughput change | V11b realtime |
| --- | ---: | ---: | ---: | ---: |
| sky | 10.71730 | 11.22930 | -4.56% | 0.534x |
| combat | 12.37990 | 10.29270 | +20.28% | 0.583x |
| standard | 10.19690 | 9.89805 | +3.02% | 1.819x |
| long | 9.64743 | 9.65574 | -0.09% | 1.864x |

Stationary Sky Force is slower, the combat pair has a slow 12.38-second control,
standard Snakes improves modestly and longer Snakes is almost flat. The large
combat percentage is not a stable gain estimate. These single pairs do not
support promotion; Sky Force remains only about 0.53-0.58x realtime. The next
screen separately compares this archive against the ARM state-cache candidate.
No samples were removed. Live remains unchanged.
