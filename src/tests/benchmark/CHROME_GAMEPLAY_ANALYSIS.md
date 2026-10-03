# Chrome gameplay profile: Snakes and Sky Force

Fresh serial captures on 2026-10-03, source `59afd0346699915961165d243d273f81d246a9bb`,
stock Nokia 5320 assets, Chromium 153, hardware NVIDIA Vulkan, diagnostics compiled
out. Snakes covers 21–25 guest seconds; Sky Force covers moving/firing at 42–48
guest seconds. Both completed with 33 isolate profiles and no trace data loss.
The WASM SHA-256 is
`903d422d2a54e03152cb3803efb9d246dd4f7ca7fd77e743ef5473514b191f46`.
Reports record a dirty worktree; the binary identity is fixed by this hash.

Full commands, settings, asset hashes, per-function weights and trace checks are
in [the evidence record](CHROME_GAMEPLAY_ANALYSIS.json). Raw captures and labelled
profiles are in `/home/claude/.scratch/eka-chrome-analysis/{standard,combat}`.
These are diagnostic captures, not speedup measurements.

## Where the samples land

Percentages below are **self samples divided by that guest worker's sampled
span**, including waits and profiler start/stop margins: 2.625 seconds for
Snakes, 13.903 seconds for Sky Force. They are not whole-process CPU utilization.
Same-named stack nodes are aggregated. Self time includes inlined work, so an
outer-loop or runner bucket must not all be labelled interpretation or lookup.

| # | Sampled self time | Snakes | Sky Force |
|---|---|---:|---:|
| 1 | Generated ARM translations | 45.8% | 39.3% |
| 2 | Compiled-chain runner (`execute_chain`) | 16.1% | 16.8% |
| 3 | RAM code-cache lookup (`find_original`) | 10.7% | 2.2% |
| 4 | ROM registry lookup | 1.1% | 10.1% |
| 5 | Outer CPU loop (`InterpreterMainLoop`) | 3.5% | 9.5% |
| 6 | Condition-variable wait | 12.7% | 5.0% |

Runner plus lookup functions, including the smaller `lookup_compiled` bucket,
total 28.1% and 30.3%. This is a substantial target for investigation, not a
promise that another dispatch change will remove that time. Sky Force also has
4.7% in the syscall-handler lambda installed by `set_epoc_version`; that symbol
does **not** mean the emulator repeatedly changes the OS version.

## Which ARM code?

The existing generated names yield an ARM/Thumb region entry. Parsing this
capture's exact ROM then yields the DLL and code offset; export ordinals or
independently checked disassembly can identify functions.

| # | Game / generated entry | ROM attribution | Self samples |
|---|---|---|---:|
| 1 | Sky Force: `f_2149177368` → `0x8019d818` | `EUser.dll+0x29b0`, active-object selection inside `CActiveScheduler::DoRunL` | 11.46% |
| 2 | Snakes: `f_2149128552` → `0x80191968` | `DRTAEABI.dll+0x1b60`, export 135, `__aeabi_idivmod` | 3.82% |
| 3 | Sky Force: `f_2149218301` → Thumb `0x801a77fc` | `EUser.dll+0xc994`, export 617, `User::RequestComplete` | 1.23% |

The Sky Force hot entry was identified by disassembling the actual ROM and
matching the queue traversal and virtual `RunL` dispatch to Symbian's published
[ARM implementation of `DoRunL`](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/kernel/eka/euser/epoc/arm/uc_utl.cia#L1159).
Its role, simplified, is:

```text
wait for a request completion
walk active objects in priority order
find an active object whose request is no longer pending
clear its active flags and call its RunL handler
repeat while the scheduler level remains active
```

The 11.46% belongs to the generated region beginning in this code, not a specific
ARM instruction or a count of queue traversals. It does not establish spurious
wakeups, excessive queue length, or incorrect guest scheduling. Inlined guest
callees can contribute to a containing region's samples.

The other names use exact ROM export addresses and published
[DRTAEABI ordinals](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/kernel/eka/compsupp/eabi/drtaeabiu.def#L136)
and [EUser ordinals](https://github.com/SymbianSource/oss.FCL.sf.os.kernelhwsrv/blob/master/kernel/eka/eabi/euseru.def#L618).
The offline mapper records definition-file hashes; it never names internal
functions by the nearest export.

Across all sampled ROM regions, **EUser accounts for 23.94% of Sky Force's
worker span**, Ws32 2.47%, and Cone 2.36%. In Snakes, DRTAEABI accounts for 5.01%
and EUser 2.65%. Unresolved RAM regions account for 37.49% of Snakes and 10.55%
of Sky Force. Their guest PCs and compiled versions are known; assigning an
executable name requires process/ASID and load-lifetime metadata. An address
alone is not enough to call them game code.

## What the timeline rules down

Host measurement markers span 2.508 seconds for Snakes and 13.801 seconds for
Sky Force. Duration totals below include events starting within the markers,
clipped at the end marker. They are not a disjoint breakdown of elapsed time.

| # | Trace event | Snakes | Sky Force |
|---|---|---:|---:|
| 1 | `wasm.CompileLazy` | 56.2 ms / 344 events | 61.9 ms / 310 events |
| 2 | `wasm.SyncCompile` | 4.4 ms / 13 events | 29.1 ms / 160 events |
| 3 | `wasm.SyncInstantiate` | 0.7 ms / 13 events | 5.3 ms / 160 events |
| 4 | `MajorGC` | none | 13.3 ms / 1 event |
| 5 | Background `wasm.TopTierCompilation` | 412.6 ms / 96 events | 207.5 ms / 54 events |

The tiering durations are background thread work, not guest pauses. Nested
compilation wrappers and nested GC events must not be added again. These
captures do not point to GC or synchronous compilation as the primary gameplay
bottleneck. They do not characterize startup or rule out compiler contention.
Visible JS-to-WASM wrapper self samples are about 0.7% and 1.1%; this is not a
complete measurement of every boundary cost.

## Precision and validation

For the Snakes division region every trace sample column is `13378`, versus
entry column `13377`; for the Sky Force scheduler region every column is `7821`,
versus entry `7820`. Both Chrome's internal trace sampler and the CDP inspector
sampler exhibit this. They provide function-entry positions here, not varying
instruction positions. The trace samplers corroborate the hot functions inside
the host markers; they must not be summed as independent execution time.

Trace profiles are joined by `(pid, profile id)`: `Profile` and `ProfileChunk`
can have different thread IDs. `source: Internal` and `source: Inspector`
distinguish the two samplers. Sample times come from `startTime` plus cumulative
`timeDeltas`, not the timestamp at which a chunk was emitted.

We can therefore map samples to **generated region → ARM entry → ROM DLL →
selected function**, but cannot assign these samples to individual ARM
instructions. That would need native instruction sampling with
[V8 JIT metadata](https://v8.dev/docs/linux-perf), plus emitter provenance mapping
WASM offsets to guest PCs, including inlining. Static JIT disassembly by itself
does not provide sampled instruction timing.

The new [offline mapper](map_chrome_guest.py) was run against both real captures:
2,850 executable ROM images parsed; known division and request-completion
exports resolved; RAM versions remained unresolved. Module totals plus
unresolved samples equal generated self time. Reverting only the added function
labels produces profiles identical to the raw inputs, including all samples,
stack relationships, timestamps and module URLs. A mismatched ROM is rejected.
The legacy summarizer still counts 33 isolates with both labelled copies present.

The next focused investigations are ROM registry lookup and the generated
active-object selection region in Sky Force, and RAM cache lookup in Snakes.
Inspect their emitted code and test one generic change at a time with an
unprofiled exact replay. The evidence is specific enough to choose these paths;
it does not yet prove which change will pay off.
