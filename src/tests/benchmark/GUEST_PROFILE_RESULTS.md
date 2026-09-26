# Guest-level interpreter profile: Snakes

This completes the previously missing guest module/instruction breakdown from the
RAM/chained AOT work. Runtime: `5e1c46333d4e563975db87c9c5a1adbd7da5921c`;
`651bf078` changes only a regression-test expectation. No new optimization or
speedup is claimed by these diagnostic runs.

## Scope and validation

Two fresh AOT-mode-4 browser runs cover **21–25 guest seconds**, using deterministic
sampling intervals **1,021** and **1,009**. Each advances **655,867,149 guest
instructions**: **458,913,845 compiled** plus **196,953,304 interpreted**. Each
performs **26,137,201 interpreter decodes**. Exact handler totals are identical
between the runs and reconcile with the existing execution and decoder counters.

All **85 gameplay PNGs and guest frame records in each run match native exactly**.
There are 192,902 / 195,196 execution samples and 25,599 / 25,904 decoding samples.
Neither run drops samples or encounters an unreadable sampled opcode. All sampled
work belongs to process `Snakes[2000730f]0003`. Its executable is `6r45_1b.exe`,
loaded at `0x70000000`, UID3 `0x2000730F`; the package/install and launch records in
`browser.log` establish that this is the Snakes game binary.

These are **instruction-work shares, not wall-time shares**. Module shares are
sampled and approximate; handler counts are exact, including condition-failed
instructions as required by the guest instruction budget. Execution counts exclude
AOT and the optional differential reference interpreter. Decoder counts include
cache-building work even when a decoded instruction is not subsequently executed.
See [GUEST_PROFILING.md](GUEST_PROFILING.md) for collection semantics and commands.

## Game and library attribution

Percentages below use only remaining interpreter execution or its decoding work,
not all guest execution. Ranges show the two sampling intervals, not statistical
confidence intervals.

| Loaded module | Interpreter execution samples | Interpreter decode samples |
| --- | ---: | ---: |
| `6r45_1b.exe` — Snakes | **91.78–91.82%** | **90.91–90.98%** |
| `euser.dll` | 6.15–6.18% | 4.05–4.09% |
| `dfpaeabi.dll` | 1.61–1.62% | 4.36–4.39% |
| `drtaeabi.dll` | 0.38–0.39% | 0.36–0.39% |

Each other module contributes below 0.02% of execution samples. The remaining
interpreter workload is overwhelmingly in game ARM code: only **928,857**
interpreted instructions are Thumb (**0.47%**).

## Exact instruction-handler breakdown

| ARM handler | Interpreted instructions | Share of interpreted work |
| --- | ---: | ---: |
| LDR | 38,615,299 | 19.61% |
| MOV | 38,538,835 | 19.57% |
| **SMLAL** | **36,798,864** | **18.68%** |
| STR | 14,529,167 | 7.38% |
| CMP | 12,191,982 | 6.19% |
| ORR | 11,625,793 | 5.90% |
| B/BL (`bbl`) | 10,209,483 | 5.18% |
| LDM | 5,884,443 | 2.99% |
| BX | 5,436,389 | 2.76% |
| ADD | 4,879,187 | 2.48% |

SMLAL, SMULL and UMULL together account for **38,554,202 interpreted instructions**
(**19.58%**). UMLAL has zero interpreter executions in this window. The ARM AOT
translator explicitly bails out for all four long-multiply forms in
`src/emu/cpu/src/aot/arm_translator.cpp` (`is_long_multiply`, around line 865).
SMLAL also accounts for **5,178,516 decodes**, **19.81%** of decoding work.

Supported instructions appearing in this table do not necessarily lack an AOT
implementation: they can execute in fallback blocks around an unsupported form,
or at an entry that was not selected for compilation. The counts alone do not
establish an AOT miss reason for every PC.

## Concrete hot regions

All addresses below are guest addresses in the loaded Snakes executable; ranges
are end-exclusive. Samples for the same region are aggregated across handlers.

| Region | Share of interpreter execution samples | Observation |
| --- | ---: | --- |
| `0x70063700–0x70063800` | **30.24–30.30%** | Arithmetic region containing repeated SMLAL, loads, shifts and stores |
| `0x70065b00–0x70065b10` | 4.83–4.86% | CMP, conditional MOVs and BX return |
| `0x70065a64–0x70065a74` | 4.05–4.12% | Another CMP/conditional-MOV/BX sequence |
| `0x700002a8–0x700002b4` | 3.78–3.79% | STR, SUBS, BNE store loop |

For example, `0x70063760` contains original ARM opcode `0xe0e54c97` (SMLAL).
The 256-byte region above contributes 18.58–19.23% of decoder samples as well.
These are measured address regions, not recovered function names; their ultimate
gameplay/rendering roles have not been established from symbols.

## Updated next target

**Implement bounded ARM long-multiply support first**, prioritizing SMLAL and
SMULL, with differential tests for signedness, accumulation, register overlap,
condition codes, flags and exact budget exits. Then remeasure this exact window
and inspect whether the region around `0x70063700` stays compiled. This concrete
coverage hole is a better first experiment than beginning with generic region
fusion. A speedup cannot be inferred directly from instruction percentages.

After that, use the measured comparison helpers and store loop as focused
candidates for longer compiled regions, and retain safe lookup/cache improvements
as incremental work. Those optimizations were not implemented in this profiling
follow-up. This result completes attribution; it does not claim realtime gameplay.

## Tests and artifacts

WASM: **123 tests pass**. Native CPU tests pass; the package suite has **83 passes
and seven previously observed failures** (464/471 assertions). New tests cover
sample intervals, separate execution/decoding totals, identities, JSON escaping,
handler labels, and measurement-window boundaries. A three-instruction native
fixture deliberately expects four decodes: the branch target is decoded before
the exhausted-budget exit.

The earlier 1,000-image correctness gate is documented in
[REALTIME_AOT_RESULTS.md](REALTIME_AOT_RESULTS.md); this diagnostic-only follow-up
was checked over its full 85-image profiling window, not a new 1,000-image run.
Both raw diagnostic files remain at the paths and hashes in
[GUEST_PROFILE_EVIDENCE.json](GUEST_PROFILE_EVIDENCE.json), which includes complete
summaries, module/type cross-tabulations, the top 50 PCs, source/binary identities,
frame-manifest hashes and test-log hashes. Instrumentation is off by default.
