# Memory semantics and connected-path experiments

**Outcome:** fault correctness is repaired; all three speed prototypes are removed
after failing the timing acceptance criteria. No extra headroom is established.

Work starts at `9b3b50732` on local `wasm-port`. No guest-clock scaling,
instruction skipping or relaxed code-byte validation is used.

## Fault prerequisite

`8baca09a4` repairs the three mechanisms identified by the previous probe:
permission-specific DynCom data-TLB lookup, endian conversion for data read
hits (including block-transfer cursors), and full i32 WASM store-import arguments
narrowed inside C++. The zero permission-tag sentinel never grants page-zero
access through the new permission-specific helper. Native regression tests also
exercise cursor endian conversion and denied block writes.

The actual-core callback probe improves from 309/480 to **480/480 exact matches**.
Native and WASM stdout are byte-identical, including exception-visible state,
callback order and every changed memory byte. This is the documented callback
fixture; it does not claim all ARM hardware-abort behavior is covered.

The fault-fixed build passes the checked 1,600-image replay through 102.484363
guest seconds / 16,261,337,499 instructions, with all guest and audio records
matching the preserved native reference. All three native targets and 132 WASM
tests pass. An initial native regression test put the very large ARMul_State on
the stack and crashed; moving the fixture to the heap fixed that test setup.

## Prototype 1: compact generated memory path

A cached page key has an impossible sentinel, so a key match proves the cached
base is valid as well as checking page/alignment. Failed lookups branch through
the existing helper and leave the result block; valid accesses do not repeat a
base-validity test or a final HOST test. Helpers invalidate both cached keys.
Permission, endian, alignment and code-alias exits remain in place.

This changes emitted control flow, not the number of guest instructions. The
existing page cache is retained; no guest-PC whitelist or flat-memory assumption
is introduced. The prototype passes all 480 fault cases, 132 WASM tests and the
checked 1,600-image native replay. It was committed as `5500e86af`, then removed after confirmation failed.

Serial old/new/new/old heavy-window trials take **17.8686 / 17.5204s** for the
fault-fixed baseline and **16.4770 / 16.6064s** for compact memory, for 18 guest
seconds. This is **1.0697x throughput** by means, or **1.0882x realtime**.
Both candidate trials beat both controls. However, the later original-versus-final
confirmation does not repeat that gain: original 18.3571 / 18.0297s versus candidate
20.4184 / 33.5830s. The large slow candidate run is retained; no verified cause
justifies excluding it. A fresh fault-fixed-versus-candidate comparison follows
below. The initial 7% pair alone must not be advertised as a reliable speedup.

The fresh isolation repeat again uses the fault-fixed baseline on both sides:
**19.7639 / 18.8555s** before versus **17.5868 / 17.9613s** with compact memory,
**1.0864x throughput** by means. This reproduces the gain relative to the corrected baseline. However, a second
original-versus-candidate confirmation (including the page-zero follow-up) again
fails: original **17.6219 / 17.9764s**, candidate **16.6942 / 35.2669s**. Two
confirmation batches now contain a very slow candidate run while their original
controls stay near 18 seconds. A causal explanation is not established, and all
runs remain in the evidence. This variability fails the acceptance criterion.
The compact emitter is removed, preserving only its page-zero permission guard.
`compact_memory_experiment.patch` applies on `8baca09a4` to reproduce the prototype.

## Prototype 2: connect callee loops to their caller

The restricted callee resolver additionally accepts internal branches and loops,
with no nested calls or writes to LR/PC before its final return. The caller and
callee run in a single generated function with shared register/flag locals.
Flattened instruction positions and a separate namespace for every call site
prevent repeated calls to the same callee from sharing the wrong branch label.
Each included source range remains an exact byte/mapping dependency. Code-write
aliases, helper exits, interrupts at backedges and every instruction budget remain
checked. This goes beyond enlarging the existing outer runner chain.

The new differential cases cover repeated far calls, forward/interior/backward
branches, loops, partial budgets, memory fallback and stores into included code:
2,560 exact comparisons in this group, versus 1,280 previously. All 132 WASM tests,
all 480 production fault cases and the checked 1,600-image replay pass.

However the speed gain does not repeat:

| Batch | Compact memory only, seconds | Plus connected callees, seconds |
|---|---|---|
| First | 19.4248 / 16.2170 | 15.4888 / 18.4888 |
| Repeat | 17.0334 / 16.1644 | 18.6018 / 15.6133 |

The first pair suggests improvement; the second regresses by its mean. Ranges
strongly overlap. The connected compiler changes and their specific tests are
removed from production, with the exact experiment retained in
`connected_loop_experiment.patch` (apply on `5500e86af`). This evidence does not
justify the added compiler complexity. It also does not prove all connected-path
designs unhelpful.

## Fallback prototype: guarded successor candidates

A RAM region records its last validated successor. A matching candidate avoids
indexing/searching the shared recent-entry cache; it still checks live version,
address-space/PC/mode identity, mapping generation/source and exact primary and
dependency bytes. A miss takes the existing lookup path. Stable entry pointers
are owned by the cache; the runner does not carry them across configuration
lifetimes. Native regressions include invalidation, replacement, remaps,
unmapping, dependencies, mode and process changes.

The successor candidate passes all three native targets, 132 WASM tests, the
480-case fault probe and checked 1,600-image replay. Serial heavy-window results
are **16.6262 / 17.2246s** without it and **16.8877 / 16.4129s** with it. The small
mean difference and overlapping ranges do not establish a useful gain. It is
removed from production; `guarded_successor_experiment.patch` preserves the
implementation and regressions, applicable on `5500e86af`.

## Final retained build

Only correctness changes remain: the permission/endian/import fixes and rejection
of zero permission tags in generated regular accesses and block transfers. All
three performance prototypes are removed; their patches and measurements remain.
Validity generations remain OFF, and exact code/dependency byte checks remain.

Source review found that generated block transfers could mistake a zero permission
tag for page zero when a colliding TLB entry retained a backing pointer. The
follow-up routes this case through the checked helper and adds collision fixtures
to regular-load and block-transfer guard tests (32 and 64 subcases respectively).
That guard fix was committed as `860bf3eed`; the compact-control-flow rollback
retains both guards in `ab9c6c3ea`.

The intermediate compact-memory live test passed the upload/Start, keyboard/touch,
changing display, narrow-layout, blur-release and shutdown checks, but advanced
only **0.978566x realtime** over **120.7804 host seconds**. It is not evidence of
new sustained headroom. The final retained build is verified separately below.

The retained build passes **132 WASM tests**, including the expanded collision
fixtures, **all three native targets**, **seven frontend checks**, and **480/480
fault cases**. Its checked **1,600-image replay** matches native exactly through
102.484363 guest seconds, including pixels, guest timestamps/instruction counts,
audio output/events and sampled per-region interpreter checking.

The retained build's isolated two-minute manual-upload live test advances
**120.822973 guest seconds in 120.532780 host seconds** (**1.002408x realtime**).
Keyboard/touch, held input, blur release, changing gameplay, narrow layout, upload
cleanup and shutdown pass. Queue delivery is 0.80–7.19 ms; this is not display
latency. Maximum sampled temporary lag is **1.598 seconds**, followed by catch-up.
The average does not prove additional headroom or eliminate brief slowdowns.

The raw folders named `speed-final-timing` and `speed-kept-timing` are failed
compact-memory confirmation trials, despite their provisional names. They are
**not timings of the final correctness-only retained build**. Its measurement in
this investigation is the separately identified `speed-retained-live` test.

Final served WASM SHA-256:
`e0c7bbd367e32fa6a6cf61983ea2bba2ec080fabe5a000e67c1df234e4118c62`.


**No reliable new whole-game speedup is established. The 1.25x heavy-window target
is not achieved.** The negative experiments narrow the next investigation; they
do not prove that every connected-path or memory data-flow design is ineffective.
Sound quality remains deferred. Nothing is pushed.

## Method and limits

All performance trials use the physical Quadro T1000 through Chromium 153 with
process-local matching NVIDIA libraries (`~/.scratch/eka-benchmark/validity-gpu.env`).
Rendering stays enabled, capture/readback and detailed counters disabled. Each
batch warms four isolated fixtures, pauses at 78 guest seconds, and measures
old/new/new/old serially to 96 seconds, with correctness/build work finished.
Every trial executes 3,975,200,506 guest instructions and 676 presentations.
Two trials per variant and this shared host do not establish a universal speedup.
The kernels and replay are Snakes-focused; no second-game result is claimed.

Reproduce timing with an archived before build containing all four frontend files:

```sh
source ~/.scratch/eka-benchmark/validity-gpu.env
EKA2L1_GPU=hardware EKA2L1_PROFILE_DETAIL=0 python3 src/tests/benchmark/profile_batch.py \
  --assets ASSETS --output NEW_OUTPUT --compare-build BEFORE_BUILD \
  --before-aot 5 --after-aot 5 --capture-mode 2 \
  --start-us 78000000 --end-us 96000000
```

Fault verification (generate `native-fault.log` with the native probe documented
in `CPU_DISPATCH_AND_FAULT_RESULTS.md`) and exact replay:

```sh
node build-wasm/src/tests/aot/eka_cpu_fault_wasm.js > wasm-fault.log
python3 src/tests/benchmark/compare_cpu_faults.py native-fault.log wasm-fault.log fault.json --require-equal
EKA2L1_GPU=hardware EKA2L1_BENCHMARK_AOT=5 EKA2L1_AOT_VERIFY=1024 \
  node src/tests/wasm/benchmark.ts ASSETS NEW_REPLAY 1600 src/tests/benchmark/snakes.input 21000000
python3 src/tests/benchmark/compare.py NATIVE_REFERENCE NEW_REPLAY
```

`MEMORY_AND_CONNECTED_EVIDENCE.json` embeds raw timing records and correctness
summaries, with hashes/paths for binaries, test logs and saved experiments.

