# One budget proof for an inlined Thumb helper

Not adopted. The tested prototype is archived and removed from active source.

This follows the rejected [per-instruction budget version](THUMB_ROM_LEAF_RESULTS.md).
The generic immutable Thumb helper inliner now checks the longest possible
callee path once before entering it. Short budgets take the original
call-side exit. Accepted helpers contain no callbacks, writes or loops,
so the successful proof covers every instruction on every callee path.
Guest counts remain compile-time constants; no runtime accumulator is added.

## Controlled gameplay result

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.0693 → 7.0800 | -0.15% | -0.34% | -0.01% | 2/4 |
| 2 | Sky Force | 21.8031 → 21.9960 | -0.88% | -0.90% | -1.29% | 1/4 |

The smaller native body does not earn adoption: Sky Force loses 0.88% CPU throughput, with three of four pairs slower, despite 1.29% fewer retired native instructions. Snakes is effectively flat (-0.15%, mixed pairs). Together with the first rejected variant, this establishes a local stopping point for these helper-inlining follow-ups, not an overall emulator performance ceiling. The existing defaults are retained.

Both variants are compared directly with the adopted `536ea5af3` runtime.
The candidate source base `4c12257b4` adds only the first attempt’s report.
The first rejected prototype is not the timing control for this follow-up.
Do not subtract or add percentages from the two independent campaigns.

Four fresh launches per build per game use ABBA then BAAB order. Windows
are Snakes 78–96 guest seconds and Sky Force combat 42–60. Normal timing
artifacts have source maps, sampling and extra diagnostics disabled.
Guest progress, instruction totals and presentation journals match.
Every valid observation remains included, including slower pairs.

The campaign contains 16 valid observations and 0 retained invalid attempts.
The clock request is 3.6 GHz; CPU 7 runs the worker and sibling 15 is reserved.
Clock, throttle, affinity and counter checks apply. There are no temperature
gates or cooldown waits. All 88 host-restoration checks pass.
See [method](CONTROLLED_BENCHMARKS.md) and [full evidence](THUMB_LEAF_BUDGET_RESULTS.json).

## What survived V8

Actual warmed gameplay captures show caller `0x801a0f96`, including the
boolean helper at `0x801b9a48`, shrinking from 1,472 native bytes in the
first prototype to 896 bytes in this follow-up. Comparisons of the remaining
budget against 3, 4, 5, 6, 7, 8 and 9 become one comparison against 10.
The earlier call-half checks remain. Its combined guest return counts are
still 9 and 10. In the production control, the 448-byte caller dispatches
the separate 1,280-byte helper. Thus the simpler code is real; gameplay
timings, rather than its size or instruction count, decide adoption.

All 160 selected native versions were recovered without snapshot errors,
sampling errors or lost samples. The diagnostic captures are not controlled
timing comparisons and do not provide new source-line attribution. The JSON
includes code-version identities, hashes, native comparisons and samples.

## Behavior and verification

Eligibility still uses instruction bytes at any immutable ROM address:
supported register operations, direct reads and forward branches ending in
`BX LR`, within the existing 32-instruction expanded-path bound and eight-site
limit. Unsupported effects, nested calls, loops and incomplete windows keep
the original dispatch. A failed direct-memory proof resumes the precise
guest load without making a helper call inside the inlined region.
Equal-length paths can continue inside the caller; unequal-length paths
return their separate static counts. Exact-budget returns preserve the raw
BX PC before the next caller instruction is allowed to normalize it.

The final artifact passes 45,760 independent interpreter state/flags/budget/
memory comparisons, 336 raw-PC/IRQ/stop/backend boundaries and ten rejected
shapes. Existing ROM veneer and call-boundary tests add 34,560 and 288
comparisons. Both 60-frame replays exactly match reference images, guest
progress, PCM audio and audio events. Timings and native capture use that
same frozen artifact. The prior full suite is reported with the first
prototype and is not presented as a full-suite run of this revision.

The measured loser and test hooks are removed. The public LAN service
continues serving the previous adopted build throughout the experiment.

At 3.6 GHz the candidate wall speeds are Snakes **2.09×** realtime
and Sky Force combat **0.74×**.
The contemporaneous production-control speeds are **2.10×** and **0.74×**, respectively.

Raw builds, exact source patch, plans, commands, all observations, tests and
captures are archived under `/home/claude/.scratch/eka-thumb-leaf-budget`.

## Restored production artifact

After removing the prototype, the normal WASM build was rebuilt. All six
served-artifact hashes exactly match the adopted control. A trusted HTTPS
download of the live WASM matches too:

`569672b938375de68829c5dce9c8d14f8e6c92f9f62bf0469be85042945119e0`

CPU and AOT-test source match the pre-experiment defaults. The LAN service
retained its PID throughout both campaigns and remains active with automatic
restart enabled. No new deployment or fresh live-audio acceptance is claimed
for this identical previously verified artifact. Both rejected variants,
their exact patches and all valid results remain archived.
