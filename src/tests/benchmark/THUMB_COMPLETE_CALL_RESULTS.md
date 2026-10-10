# Completing short Thumb calls inside their caller

**Retained and enabled by default.** Short, read-only Thumb helpers whose paths
all return through unchanged LR now continue inside their caller's WASM
function. Guest registers and flags stay in shared locals across that return.
The compiler recognizes ordinary instructions and control flow; it contains no
EUser or game-specific address or pattern.

The repeated EUser boolean-helper workload improves **14.10–16.54% CPU throughput**
across two accepted panels and retires **17.12% fewer native instructions**. These are fixed-work helper
measurements, not whole-game gains. The game screen is effectively flat:
Sky Force +0.10% and Snakes +0.38% observed CPU throughput, with mixed pair
directions and unequal gameplay work. The local runtime gain, correctness
checks, and verified elimination of the boundary in warmed gameplay support
retaining this bounded extension. They do not establish a game-wide percentage.

## What changes

Previously, partial fusion could execute the helper in the caller's region,
then publish CPU state and return to the C++ dispatcher at the helper's `BX LR`.
The continuation required a lookup, indirect WASM call and state reload.

```text
before:
    caller prefix
    LR = continuation
    execute helper, including conditional branches
    PC = LR
    publish CPU state; return to dispatcher
    lookup continuation; indirect WASM call; reload CPU state
    caller continuation

after, when every memory read succeeds directly:
    caller prefix
    LR = continuation
    execute helper in shared locals
    branch to continuation within this WASM function
    caller continuation using the same locals

on a read miss:
    publish state at the original load; return to dispatcher
    ordinary compiled/interpreted execution handles that load
```

This has no JS crossing and no runtime return-target comparison or instruction
accumulator. The existing call-half stop/interrupt boundary remains. A selected
leaf contains no callbacks, stores, nested calls, backward branches, or SP/LR
writes, so nothing inside it can change its known return destination. Direct
loads retain mapping, permission and page-boundary behavior. Observable exits
publish architectural state. Unequal branch lengths are allowed by the current
count-free runtime.

Selection requires the direct-memory backend and existing region fusion.
Supported instructions are the bounded Thumb ALU subset, immediate/high-register
MOV without SP/LR/PC writes, scalar immediate/literal/SP-relative reads, forward
branches and `BX LR`. Every path must terminate that way. Expanded paths count
against the existing 32-instruction leaf bound, eight-site bound and 512-instruction
fusion bound. Unsupported leaves retain their previous translation.

## Runtime measurements

The fixed-work fixture relocates the real seven-instruction boolean helper at
ROM `0x801b9a48` and its caller's MOVS/BL/CMP pattern into a repetition loop.
Each observation runs ten million identical calls through the real compiler,
registry and C++ runner, using the direct-memory page table rather than the
arena shortcut. This isolates the eliminated return/dispatch boundary; it does
not reproduce the entire EUser scheduler.

| # | Fixed helper workload | Control | Complete fusion |
|---|---|---:|---:|
| 1 | Mean CPU seconds | 0.310700 | 0.272292 |
| 2 | Mean retired native instructions | 2,220,239,148 | 1,840,241,506 |
| 3 | Faster adjacent CPU pairs | — | 4/4 |
| 4 | Optimized loop-root code bytes | 1,664 | 832 |

Chrome 153 / V8 15.3, ordinary tiering, ABBA/BAAB, isolated CPU 7 and sibling
15, requested 2.4 GHz and measured 2394.39–2394.41 MHz. Both relevant TurboFan
roots were installed before the first timing window. Counters ran throughout
each measurement. Debugger attachment happens after timing. No temperature
gate was used. Host frequency, affinity and platform settings were restored.

A final-build repeat after compiler-only cleanup again improves all four pairs:
CPU means 0.316690 / 0.271734 seconds (**+16.54% throughput**), native instructions
2,220,236,706 / 1,840,242,517 (**-17.12%**), measured 2394.41–2394.43 MHz.
Both roots are again tiered before timing, with the same 1,664/832-byte sizes.
The exported control and candidate guest modules are byte-identical between
the two panels; the surrounding emulator/test WASM differs after the cleanup.
Both accepted panels are retained rather than selecting the larger gain.

Both games have four clock-valid ABBA observations, with reviewed start/end
screenshots showing active gameplay. Sky Force covers guest seconds 58–70;
Snakes covers 74–86. Each has one faster and one slower adjacent CPU pair.

| # | Gameplay window | CPU throughput change | Wall throughput change | Native instruction change |
|---|---|---:|---:|---:|
| 1 | Sky Force | +0.10% | -0.49% | +0.13% |
| 2 | Snakes | +0.38% | +0.25% | +0.36% |

These tiny deltas are inconclusive. Sky Force presentations are 377/378/380/379;
the first control finishes at score 775 and the others at 825, all stage 4% with
three lives. Snakes presentations are 158/156/155/151, all score 200, with
different positions. They are guest-time windows, not identical-work workloads.

## Actual warmed native code

Sky Force's enclosing root `f_2149191761` (`0x801a1051`) was captured from
ordinary warmed gameplay, with matching JIT code/version bytes before debugger
attachment. Candidate code ID 199983, PID 282578; previous control ID 200225,
PID 189872. At the selected helper return, the candidate's successful page-table
path loads at native offset `0x818` and joins its arena path at `0x87f`:

```asm
; candidate: helper result and caller continuation, same function
0x818: mov  r8d, [rbx+r8]      ; translated guest load
        ...                    ; move loaded value; join at 0x87f
0x87f: test r11d, r11d         ; helper comparison
0x882: jne  0x890
0x888: xor  r12d, r12d         ; return 0
0x88b: jmp  0x896
0x890: mov  r12d, 1            ; return 1
0x896: mov  r15d, r12d         ; caller comparison / flags begin
        ...
0x8a5: test r12d, r12d
0x8a8: jne  0x1aac            ; caller's conditional branch
```

There is no `RET`, CPU-state publication, lookup, or indirect call between the
successful load and the caller's comparison. The miss path still publishes
`PC=0x801b9a4a` and exits before executing the original load.

**Code-size tradeoff:** this actual enclosing root grows from 7,936 to 14,400
native bytes because it now contains the caller continuation and its other
paths. The smaller synthetic loop root does not imply every game function gets
smaller. This can increase instruction-cache pressure; the current whole-game
screen does not establish its net effect.

## Correctness and limits

All **183 compiler tests pass**. The new fixture adds 4,800 complete-chain
state/memory/callback comparisons and 600 independent interpreter comparisons.
It covers all Thumb condition codes, direct and missing mappings, both memory
backends, flag dependencies, LR reads, callback mutations, stop/interrupt exits,
and rejected stores/LR writes/cycles. Selected test sites: 44. Compiled calls
across the fixture decrease from 8,952 to 7,608.

Instrumented operation counting reports 216 shorter paths, 336 unchanged and
48 longer. Missing mappings incur an extra bailout/dispatcher entry before the
ordinary load helper; the first recorded growth cases add 67 counted WASM operations.
This is not a claim that every possible path improves. Counts are diagnostic
fixtures only and are absent from timing and production.

The first focused attempt exposed missing literal/code bytes in the test memory;
the fixture was corrected before acceptance. An initial browser benchmark had
incorrect Thumb registry export keys, then a second attempt failed the tiering
check: a fused long loop had not received enough entries to trigger TurboFan.
Both attempts are retained and excluded from accepted timing. The driver now
warms short entries as well as full workloads and verifies both root installations.

Raw artifacts and all attempts are retained under
`/home/claude/.scratch/eka-thumb-complete-20261010`. The accompanying
[JSON report](THUMB_COMPLETE_CALL_RESULTS.json) records observations, hashes,
native evidence, scene review and host restoration. The native capture uses a
scratch harness adjustment to skip idle workers with no WASM scripts; the
native byte/version checks remain intact. No profiler counters are added to
normal gameplay.

The final artifact is served on `https://claude-laptop.lan:8188/`; its downloaded
WASM hash matches the frozen final build. The game timing and native capture
above used the pre-cleanup snapshot, while the final helper panel uses the
final test build. Exact artifact hashes distinguish them in the JSON report.
Both games also pass the final served launcher's paced gameplay and keyboard
input checks with NVIDIA Vulkan rendering. Reviewed Sky Force screenshots
advance from score 0/stage 0% to 1,625/stage 7%, with three lives; Snakes remains
in active gameplay at score 200. These checks are muted and do not test audio.

## Reproduction

`thumb_complete_calls` is a compile-time policy selected before translation,
enabled by default. The fixture compiles both variants in one browser process.

```sh
cmake --build build-wasm --target test_aot_wasm eka2l1_wasm --parallel 6
node build-wasm/src/tests/aot/test_aot_wasm.js --thumb-complete-only
node build-wasm/src/tests/aot/test_aot_wasm.js --thumb-complete-counts
node build-wasm/src/tests/aot/test_aot_wasm.js
task_root=$(mktemp -d /tmp/eka-thumb-complete.XXXXXX)
sudo -n systemd-run --quiet --scope --slice=ekabench.slice \
  python3 src/tests/benchmark/fixed_frequency.py --khz 2400000 \
  --state "$task_root/host.json" --platform-profile performance \
  --isolate-cpus 7,15 -- \
  node src/tests/benchmark/stack_returns/micro.mjs \
  build-wasm/src/tests/aot "$task_root/rows" 7 2400 2304 \
  thumb_complete_benchmark 10000000
```

CPU IDs and reference frequency describe this laptop; adjust to another host's
topology. The fixed-clock game commands and inputs are preserved with the raw
observations. Compilation and debugger export are excluded from timing.
