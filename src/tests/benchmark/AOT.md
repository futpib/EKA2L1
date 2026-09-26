# Bounded browser AOT

The replay runner accepts `EKA2L1_BENCHMARK_AOT=0` (interpreter reference), `1`
(bounded ROM export blocks), or `2` (exports plus hot ROM blocks). Capture and
virtual guest timing are otherwise identical. AOT is implemented by generating
WASM functions on the executing worker; native runs remain interpreter references.

```sh
cd src/tests/wasm
EKA2L1_BENCHMARK_AOT=2 node benchmark.ts /absolute/assets /absolute/new-output 1000
EKA2L1_BENCHMARK_AOT=2 node profile.ts /absolute/assets /absolute/new-profile 0 0
EKA2L1_BENCHMARK_AOT=2 EKA2L1_AOT_VERIFY=1 node benchmark.ts /absolute/assets /absolute/new-checked-output 20
```

For a serial timing comparison (with concurrent warmup and paired repeats):

```sh
python3 src/tests/benchmark/profile_batch.py --assets /absolute/assets --output /absolute/new-comparison --compare-aot
```

With `EKA2L1_AOT_VERIFY=1`, the optional verifier executes every nonempty compiled block again in a private
interpreter state and memory overlay. It compares registers, NZCV/T and memory,
then aborts at the first divergence. It is a diagnostic mode, not a performance
measurement. Values greater than one check every Nth compiled block (for example,
`EKA2L1_AOT_VERIFY=1024`), using an execution counter, never wall time. This is
sampled differential validation, not an exhaustive per-block pass. Runner reports
record the integer stride. The guest memory changes only once, through compiled execution.
Keep this diagnostic scoped to the deterministic replay's ordinary memory; it is
not intended to duplicate side-effecting MMIO reads or emulate devices twice.

Bounded blocks stop at the remaining instruction budget and return at branches;
there are no recursive sibling calls. Registry keys include ARM/Thumb mode.
Unsupported instructions, including exclusive accesses and long Thumb call
halfwords, fall back to DynCom. This preserves the reference instruction clock
and its scheduling boundaries. Native CPU tests exercise mode separation and
zero-progress fallback; the WASM suite checks compiled execution against DynCom.

Mode 2 samples every 32nd uncompiled dispatch and queues a ROM entry after eight
samples. Compilation is batched (up to 32 functions per module, with a flush every
8,192 uncompiled dispatches). It caps new entries at 4,096 and sampled candidates
at 65,536. Counts depend on guest execution, not host timing. RAM code is excluded;
it needs address-space-aware invalidation before it can be compiled safely.

See [AOT_RESULTS.md](AOT_RESULTS.md) for the measured replay scope, speed and
remaining limits. The machine-readable reports and binary hashes are in
[AOT_EVIDENCE.json](AOT_EVIDENCE.json). Do not infer speed from the number of
compiled functions.

AOT register history and per-module accounting are now disabled by default.
Set `EKA2L1_AOT_DIAGNOSTICS=1` on either browser runner to restore them.
`profile_batch.py --compare-diagnostics` measures hot-ROM mode with bookkeeping
off, on, then off again; instruction budgets and guest behavior are identical.

Mode `3` adds hot RAM code. Each cache entry includes address-space identity,
PC/instruction mode and a distinct compiled version. Only the emitted instruction
prefix is a code dependency; unused translation-window suffix bytes are excluded. Before executing a RAM
block, the runtime resolves the current executable mapping and compares its
backing pointer and exact code bytes with that version. This catches writes
through aliases or host pointers as well as remapping; explicit unmap/IMB hooks
also invalidate overlapping entries. Late module instantiation cannot revive an
obsolete version. RAM blocks stay within one page, span at most 256 code bytes,
and stop at their first store. Versions are capped at 16,384 for bounded storage.
This initial validation assumes the emulator's serialized guest execution;
concurrent external writes to code while a block is running are outside its scope.

Mode `4` adds bounded compiled-successor execution and register/flag locals.
Eligible source windows grow to 512 bytes; RAM still exits at stores. Up to 64
compiled blocks can execute in one runner call, always within the remaining
guest instruction budget. Every successor checks mode, pending interrupts and
RAM code validity; missing or zero-progress blocks return to the interpreter.
Registers/flags are flushed before memory callbacks and exits, and reloaded
after callbacks. The deferred barriers include registers first used later in
the block, so a callback cannot leave a stale cached register. PC stays in the
CPU state throughout. Diagnostics record a whole runner call in this mode.

`profile_batch.py --compare-stages` compares modes 0, 2, 3, 4, 4, 0 serially.
All fixtures use full capture, no CPU sampling, and no per-block verification
or dispatch diagnostics. With `--measure-gate /new/path`, the batch creates
`/new/path.ready` after warmup and waits until `/new/path` exists. This allows
correctness jobs to finish before any timed window is released.
