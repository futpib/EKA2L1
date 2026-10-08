# Fixed syscall dispatch and same-thread context reuse

Adopted together on `wasm-port`, without an experiment selector. This is a
measured tradeoff in favor of Sky Force, consistent with the requested priority.
Snakes does regress; fewer retired instructions do not establish a runtime win.

## Combined runtime result

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Paired CPU range | Faster pairs |
|---:|---|---:|---:|---:|---:|---|---:|
| 1 | Snakes | 6.9897 → 7.0967 | -1.51% | -1.28% | -1.26% | -2.68% to -0.89% | 0/4 |
| 2 | Sky Force | 24.9573 → 24.2195 | +3.05% | +2.14% | -2.46% | -0.32% to +5.15% | 3/4 |

The smaller syscall-only candidate gives the following separate result against
the same baseline:

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Paired CPU range | Faster pairs |
|---:|---|---:|---:|---:|---:|---|---:|
| 1 | Snakes | 6.9949 → 7.1639 | -2.36% | -1.83% | -0.02% | -2.96% to -1.86% | 0/4 |
| 2 | Sky Force | 25.3087 → 25.0298 | +1.11% | +0.97% | -0.61% | +0.54% to +1.42% | 4/4 |

The combined candidate provides a larger Sky Force gain and a smaller Snakes
loss in these measurements. The comparisons have independent fresh controls;
their percentage difference is not a separately measured component speedup.
Context-only, bulk-copy-only and reload-elision-only variants were not timed.
The adopted combination has a known Snakes cost, not a claimed two-game win.
The hardware cause of the Snakes loss is not isolated here. It remains present
at the controlled measured clock and is not explained away as clock variation.

## Timing method and validity

Both comparisons use baseline `584d70a92`, identical frozen harness/defaults,
and four fresh launches per build per game in ABBA then BAAB order. Snakes is
measured over 78–96 guest seconds and Sky Force combat over 42–60. Guest instruction
counts, endpoints and presentation journals match. Both timing artifacts omit
the temporary census, sampling, tracing and verification.

There are 32 valid observations and 2 retained invalid attempts across the two comparisons.

Both campaigns' final Sky Force candidate attempts encountered unrelated C++
builds. Their measured clocks fell to 2.26/2.34 GHz and the reserved sibling
was busy; the syscall-only attempt also recorded a throttle-counter increase.
Both failed the predeclared rules and are retained with their errors. Only each
affected observation was retried after its interfering build finished. Every valid slow run,
including the slightly negative reversed-order Sky Force pair, remains included.

The worker runs on CPU 7, sibling 15 is reserved, and support work runs elsewhere.
Requested frequency is 3.6 GHz; valid windows measure about 3.592 GHz. There are
no temperature gates or cooldowns. Both campaigns pass all 88 live restoration
checks after temporary clock, placement, platform-profile and charging settings
are restored. See [method](CONTROLLED_BENCHMARKS.md) and
[complete evidence](KERNEL_DISPATCH_RESULTS.json). Historical gains are not additive.

## Measured target

The census covers 18 guest seconds per game, matching the later timing windows.
It is a separate diagnostic build with counters active only during the selected
window; none of those counters or exports is in the measured or shipped build.

| # | Game | Syscalls | Scheduler switches | Same non-null thread | Share of switches |
|---:|---|---:|---:|---:|---:|
| 1 | Snakes | 56,906 | 887,499 | 884,919 | 99.71% |
| 2 | Sky Force | 37,833,091 | 5,130,329 | 5,055,871 | 98.55% |

Sky Force executes about 9.4 million calls each to request wait, active scheduler,
request signal and tick count. Both games also make a small number of rare HLE
calls, which retain the hash-map fallback. Neither capture observes a same-thread
process mismatch or an uninitialized MMU. The remaining switches enter/leave idle
or remain idle; they do not qualify for live-context reuse.

In the baseline Chrome profile, the syscall wrapper has 6.09% self samples in
Sky Force and 0.04% in Snakes. It includes inlined dispatch and other work, so it
is an upper bound for this target, not an estimate of removable hash-lookup cost.
Scheduler switch_context inclusive samples are 1.79% and 0.93%, respectively.
Sampling and diagnostic timings are not the game-performance result.

## Implementation and observable state

The kernel installs the OS-specific SVC return convention once. EKA1 still uses
the handler's final LR and flags; 9.1 ROM stubs still return through r12 before
dispatch, except ordinal 0xff; other versions dispatch directly. CPU access order,
ARM/Thumb return selection and all unaffected flags are retained.

A 512-pointer table covers slow executive ordinals 0..255 and fast executive
ordinals 0x800000..0x8000ff. An owning unordered map retains uncommon ordinals.
Node pointers survive rehash, registration retains first-insertion precedence,
and erase/clear invalidate the table entries. Trampoline 0xff, the kernel lock,
dynamic logging and a callable snapshot survive. The snapshot owns captures even
when a service changes registrations during its invocation. The production API
currently inserts and clears entries; the registry also tests erasure explicitly.

The scheduler keeps the saved snapshot, time accounting, timeslices, ready state
and reference counts current. Debugger and scripting readers make the saved
snapshot observable, so this change does not discard the save. Only a reschedule
to the same non-null thread, with an initialized MMU, unchanged process and a
reference count that excludes intervening destruction, may reuse live CPU state.
That path cannot invoke process-switch callbacks between saving and reusing it.

The CPU interface defaults reuse_context to the original load operation. DynCom
skips the identical reload only when the primary core's ASID cache is active;
embedded interpreters still invalidate their instruction cache through load_context.
Actual thread/process changes and all idle transitions retain normal loads. Other
CPU backends retain their load side effects. Diagnostic context_loads now counts
actual loads, excluding skipped reloads.

DynCom also copies its integer and floating-point register arrays by array
assignment. All 16 GPRs, 64 VFP words, CPSR, FPSCR and thread-local register values
remain in the snapshot. None of these changes has a runtime experiment flag.

## Actual V8 lowering

The inspected artifacts use Chromium 153.0.8010.52 / V8 15.3.76.13 TurboFan.
In the baseline common syscall path, bucket selection retains integer division
and hash-node traversal. The new common path computes a bounded table index,
loads one pointer and bypasses that work. Uncommon ordinals retain the old lookup.
The new fixed-version wrappers call a shared outlined call_svc body; comparing
wrapper size alone would misleadingly count outlined work as removed.

The original floating-point context copy retains a scalar loop in native code.
Array assignment produces a bulk memory-copy operation for those 256 bytes and
paired 64-bit moves for the integer registers. The bulk copy has bounds checks and
an out-of-line native helper, whose internals are not included in the function's
static instruction count. This report therefore does not treat static code-size
reduction as an executed-instruction speedup. The eligible reuse_context path
returns after its ASID check, without copying state back into the core.

The measured game comparison combines dispatch, reload elision and bulk copies.
It does not establish a separate percentage for each component. The syscall-only comparison above provides a second measured candidate;
its frozen artifacts and patch are retained.

## Correctness and reproduction

The standalone registry/return test passes 106,913 checks in both native and WASM
builds. It forces map rehash, checks namespace boundaries and version override
precedence, and exercises self-erasing callbacks, nested registration and capture
lifetime. Its EKA1/9.1/direct return matrix checks CPU access order and state visible
to the service across flags, ROM membership and ARM/Thumb targets.

The context test passes 2,048 randomized complete integer/VFP/status/TLS
round trips with ASID caching both enabled and disabled. It also verifies that
an embedded interpreter sees modified code after context reuse. Both games pass
60-frame exact comparisons of images, frame records, guest instruction progress
and PCM audio against the retained references.

```sh
cmake --build build-wasm --target test_svc_dispatch test_aot_wasm eka2l1_wasm -j4
node build-wasm/src/tests/aot/test_svc_dispatch.js
node build-wasm/src/tests/aot/test_aot_wasm.js --context-reuse-only
node build-wasm/src/tests/aot/test_aot_wasm.js
```

Evidence is retained under `/home/claude/.scratch/eka-kernel-dispatch`, including
baseline/candidate native disassembly, census build and patch, syscall-only build,
replay commands, frozen artifacts, plans, all timing attempts and restoration
records. The companion JSON embeds the timing observations and hashes.

The complete AOT suite reports 178 passed and zero unexpected failures. The
existing isolated `0x8046506E` fixture remains an expected failure because its
harness omits sibling-call translation. Diagnostics-only cases remain skipped
in this diagnostics-free build. The new context and SVC tests pass independently.

## Live deployment

The exact measured combined artifact is served at
`https://claude-laptop.lan:8188/`; its fetched WASM hash matches the frozen build.
Both games advance frames, consume keyboard/touch input and retain expected
policies in the real game picker on the hardware NVIDIA renderer. Gameplay
screenshots are inspected separately.

The existing host audio-device problem still prevents full live-audio E2E from passing. The only remaining game-picker failures are non-silent browser audio. Exact PCM replays pass; physical browser audio is not claimed verified.

## Remaining lead

The dispatch still copies a `std::function` for each invocation. Built-in
`bridge` wrappers capture only a function pointer; a separate immutable/static
binding representation could bypass the callable copy while retaining the
current owning snapshot for callbacks with state. Its removable CPU cost has
not been measured. This is recorded as a lead, not included in this adoption.
