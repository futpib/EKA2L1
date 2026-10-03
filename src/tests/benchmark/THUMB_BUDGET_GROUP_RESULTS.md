# Thumb arithmetic budget groups

The opt-in direct Thumb path checks the budget once for each consecutive run
of at least two supported arithmetic instructions. These operations cannot
call out, fault, branch, alter mappings or change the instruction budget. If
the whole run fits, it executes without intermediate budget exits. Otherwise
the original per-instruction checks preserve the exact stopping PC, registers
and flags. No proof crosses memory, callbacks, calls or control-flow changes.

The support query uses the same arithmetic decoder without emitting code or
modifying cached state. This avoids a second opcode classifier drifting from
the lowering. The fast and short-budget arms use the same arithmetic lowering.
The fast arm also lets the browser eliminate intermediate flag calculations
whose results are overwritten before any architectural observation. This is a
mechanism hypothesis until measured, not a promised gain. Existing source
windows, runner limits, instruction budgets and guest scheduling are unchanged.

All 180 compiler tests pass freshly. The new 57,728-case oracle compares the
full state and callback observations against the existing non-direct emitter,
covering all flag combinations, carry/shift edge inputs, every short budget,
callbacks that change budget/state, and control-flow terminators. The existing
bounded/native instruction matrices remain part of the full suite. Production
WASM also passes 1,152 native call cases and 2,880 native memory-fault cases.
Those single-instruction probes test unchanged surrounding behavior; the new
multi-instruction matrix and exact replays exercise grouped arithmetic.

All seven exact native image/record/PCM comparisons pass: stationary Sky Force
control/candidate/checked, moving-and-firing Sky Force normal/checked, and both
Snakes routes. Checked invocations force callback memory; normal replay and
existing unit/fault matrices independently cover direct memory.

The fixed eight-observation serial screen compares frozen V8/V9 archives on
four routes, with opposite orders across routes. Both retain the accepted
syscall-allocation change and identical shared policy, physical GPU and shared
audio, original limits, and disabled sampling/capture/detailed counters. The
screen waits for correctness and the separate runner diagnostic to finish.
One pair per route is exploratory; final promotion would need reordered
confirmation, untouched-live controls and normal sustained live/audio checks.
The realtime target remains open. This option remains unserved; nothing pushed.

## Four-route screen

All eight planned observations are retained.

| Route | V8 seconds | V9 seconds | Throughput change | V9 realtime |
| --- | ---: | ---: | ---: | ---: |
| sky | 11.08170 | 10.98880 | +0.85% | 0.546x |
| combat | 10.55300 | 10.88380 | -3.04% | 0.551x |
| standard | 9.93007 | 10.16430 | -2.30% | 1.771x |
| long | 9.82667 | 9.54057 | +3.00% | 1.887x |

Stationary Sky Force and longer Snakes improve slightly while combat Sky Force
and standard Snakes are slower. These single pairs do not establish stable
gains or regressions and do not support promotion. The arithmetic grouping
source is omitted from the next independent experiment, which returns to the
accepted V8 allocation baseline. All implementation and evidence remain in
history. Live is unchanged and the realtime goal remains open.
