# Fit the game renderer within 1.3 GiB without losing gameplay throughput

The October 21 candidate completes nine full cold/prepared routes under a
**1331 MiB game-tab renderer limit**, with zero OOMs and zero OOM kills. The
three largest established workloads are Asphalt 3, Hooked On and Tetris.
Their measured unpaced gameplay is faster than the previous 128/120 MiB
code-cache default. Cold startup differs by about 1–3 seconds; this is not a
claim that every phase became faster.

The default generated-module budget is now **104 MiB, trimming to 96 MiB**.
Demand compilation still looks ahead four regions. No benchmark cache override
is used in the candidate results. The emulator keeps its CPU worker and guest
state throughout execution; eviction releases cold modules normally.

## Comparable measurements

All runs use Chromium 154.0.8037.97, a fixed 3.0 GHz CPU, independent empty
browser profiles and ROM translation caches, guest audio enabled with browser
output muted, and the checked continuous-unpaced gameplay routes. Both old
and new builds use browser profiles on the SSD capture filesystem.

Earlier fixtures silently put OPFS in a `/tmp` tmpfs profile. That made roughly
136 MiB of filesystem data count as renderer-owned shared memory, versus about
13 MiB with the disk profile. Moving the fixture is a measurement correction,
not an emulator RAM optimization. The old build still OOMs at 1331 MiB with
the corrected disk fixture. Historical result tables remain intact.

The control has a 2048 MiB cap, leaving these peaks uncensored. Candidate peaks
at 1331 MiB are censored by the enforced limit: Linux reclaims filesystem cache
in some runs. `memory.events:max` is nonzero in six of the nine candidate runs;
`oom` and `oom_kill` are zero in every run. This demonstrates completion under
the limit, not an uncapped peak below it or generous spare headroom. Browser,
GPU, spare renderers and the test runner are outside this tab-only budget.

FPS below is the last matching gameplay window. Earlier windows, CPU clocks,
startup stages, cap events and all repeats are in the companion JSON. The FPS
control is the better of the two controls, avoiding inflated gains from the
slower repeat.

| # | Game | Old fresh peak, MiB (two runs) | New fresh peak, MiB (two runs) | Old FPS | New FPS (two runs) |
|---:|---|---:|---:|---:|---:|
| 1 | Asphalt 3 | 1422.1 / 1429.6 | 1321.4 / 1331.0 | 95.70 | 105.64 / 106.65 |
| 2 | Hooked On | 1446.5 / 1505.5 | 1331.0 / 1331.0 | 222.62 | 253.82 / 254.79 |
| 3 | Tetris | 1439.8 / 1417.7 | 1307.3 / 1316.8 | 129.89 | 152.72 / 152.81 |

| # | Fresh startup endpoint | Old seconds (two runs) | New seconds (two runs) |
|---:|---|---:|---:|
| 1 | Asphalt 3 race loaded | 89.71 / 90.89 | 91.03 / 92.21 |
| 2 | Hooked On playable boat | 142.14 / 162.39 | 141.27 / 141.75 |
| 3 | Tetris playable board | 86.94 / 87.03 | 89.64 / 89.32 |

Asphalt's mean cold-start difference is +1.3 seconds and Tetris's is +2.5 seconds
(about 3%). Hooked On matches the faster control. Its slower control is retained
as variability evidence, not used to claim a 20-second gain. Prepared Asphalt
is 57.97 seconds versus 57.56 seconds for the matched control.

| # | Prepared route | Peak, MiB | Final matching gameplay FPS | OOM / OOM kill |
|---:|---|---:|---:|---:|
| 1 | Asphalt 3 | 1331.0 | 104.31 | 0 / 0 |
| 2 | Hooked On | 1331.0 | 250.10 | 0 / 0 |
| 3 | Tetris | 1331.0 | 146.99 | 0 / 0 |

## Changes and attribution

1. **Keep state pruning across watchdog polls.** The bytecode analyzer treated
   `i32.atomic.load` as unknown and rejected pruning for the entire region.
   Recognizing this one instruction restores redundant state-transfer removal;
   unknown/malformed atomics remain conservative. This is committed separately
   as `e78480584` and validated with 8,960 before/after execution comparisons.
2. **Do not touch fresh arena holes.** `multiple_mem_model_process` previously
   zeroed every newly exposed prefix, including untouched holes before distant
   allocations. `sparse_wasm.c` extends the selected SDK's dlmalloc MORECORE hook
   under its existing locking, identifying fresh zero-filled backing. Only the
   recycled prefix is explicitly cleared before exposure. The allocation still
   uses ordinary `free`, allocator reuse, the same stable base and the same
   guest memory fast path. This reduces resident pages, not the 48 MiB virtual
   arena reservation or WASM linear-memory capacity.
3. **Outline ARM memory misses into existing precise compiled recovery.** Scalar
   post-index/writeback/PC forms and eligible whole-span LDM/STM accesses now
   leave before side effects on a failed inline check. The single-instruction
   compiler handles the callback and exact writeback/fault ordering. Removing
   duplicated helper/reload sequences makes both generated WASM and V8's
   representation smaller. Partially completed doubleword transfers and early
   base-in-list writeback keep their original paths. No interpreter is added.
4. **Use 104/96 MiB retention with age-aware eviction.** Reuse priority decays
   with installation epochs when victims are selected, allowing completed
   startup phases to lose priority. This adds no generated guest checks. It
   does not establish a universally optimal budget or decay interval. Merely
   reducing the old cache, or aging it alone, did not meet the speed objective.
5. **Reuse the previous process's resolved mapping list.** Startup CPU sampling
   attributed about 5.4 seconds to MMU callbacks/view rebuilds in the preceding
   candidate. A two-process binding-list cache avoids repeated resolution when
   the global mapping generation still matches. Remaps, permissions and
   lifetime changes invalidate it. The published page table and affine checks
   remain unchanged; there is no extra guest load/store check or second 8 MiB
   page table. This recovered most of the smaller cache's startup penalty.

Earlier native allocation samples identified V8 module wire copies, compiled
code metadata and trap-handler metadata as major consumers. Mapping PSS also
showed large WASM resident arena backing. These are why shrinking emitted code
and avoiding physical arena initialization were prioritized. Native sampled
allocation estimates are not added to mapping PSS: those describe overlapping
ownership and residency, respectively.

## Final build memory profile

A separate fresh Hooked On capture used the exact benchmarked normal binary
with Chrome native/V8 allocation sampling and process mapping attribution.
It reached gameplay without runtime errors. At the last gameplay endpoint,
the renderer was **1164.44 MiB PSS**; mapping samples taken moments later total
1164.29 MiB (0.15 MiB sampling difference). Whole-browser PSS was 1789.49 MiB,
which includes processes deliberately excluded from the game-tab cap.

| # | Renderer mapping owner | Resident PSS, MiB |
|---:|---|---:|
| 1 | WASM linear-memory backing | 448.93 |
| 2 | Chromium PartitionAlloc mappings | 399.76 |
| 3 | V8 executable code and trampolines | 156.69 |
| 4 | Other V8 sandbox mappings | 57.59 |
| 5 | Other V8 mappings | 42.38 |
| 6 | File-backed mappings | 47.33 |
| 7 | Other anonymous/shared mappings | 11.60 |
| 8 | Mapping total, before rounding individual rows | **1164.29** |

V8's `GetAndCopyFirstArgumentAsBytes` is the largest sampled native allocation
site, about 106.9 MiB, followed by `WasmCode` metadata at 35.6 MiB and trap-handler
metadata at 22.8 MiB. These are statistical ownership estimates overlapping the
mapping rows, not additional RAM. JS/V8 heap samples cover seven isolates.
WASM capacity is 1262.94 MiB while its resident backing is 448.93 MiB; capacity
and virtual reservations are not resident allocation totals.

This attribution capture is uncapped and instrumented. Its observed startup
renderer PSS peak was 1421.75 MiB. It is not evidence that an unrestricted,
profiled tab stays below 1.3 GiB, nor is its FPS used for speed comparisons.
The acceptance evidence is the nine separate normal runs under the enforced
cap, including startup, installation and gameplay. Captured append-only V8
code-map totals also include retired/reused addresses and must not be reported
as live native code bytes.

## Verification

- 199 AOT WASM tests passed, including compiled recovery, callback-visible
  writeback/fault ordering, ARM/Thumb PC returns, mapping-cache invalidation,
  concurrent allocation and recycled arena zeroing. The existing unrelated
  `0x8046506E` expected-failure case remains identified by the suite.
- Precompile-only checks passed; 7,086 assertions in 44 native test cases passed;
  seven real Chromium browser tests passed.
- The state-pruning comparison exercised 8,960 complete register/flag/memory and
  callback equivalence cases; 6,720 emitted smaller code.
- The diagnostic allocator fixture exercised 122,920 allocations and verified
  sparse allocation ownership/free, exported JS malloc and resident-page
  attribution, with zero unknown frees. Its wrapper suppresses nested allocator
  recording before a sparse aligned allocation is split, preventing a
  profiler-only double count. These hooks are absent from the normal build.
- Nine independent complete cap routes passed: two fresh source-install runs
  and one prepared-install run for each of the three games. Each identifies
  exactly one game-tab renderer, disables swap, checks gameplay screenshots and
  records all matching FPS windows. Reaching the launcher alone is not a pass.

## Artifacts and reproduction

Candidate WASM SHA-256:
`6a5cc6de8223b777b46c3699891767ee5f8b1d753898418cfbb28ef17f4e481c`.
Candidate JavaScript SHA-256:
`2e76d9486a3de105066f8892c060e134aa614c2aed98e262d7ef45895de1c209`.
Old control WASM SHA-256:
`e725b6ae547bc7e41923731dbad7c8a9d132e4a81074cd690986206e68616032`.

Compact evidence: [MEMORY_CAP_WITHOUT_GAMEPLAY_SLOWDOWN.json](MEMORY_CAP_WITHOUT_GAMEPLAY_SLOWDOWN.json).
Full captures, CPU/native profiles, screenshots, failed experiments and scripts
are outside Git at `/home/claude/.scratch/eka-memory-fast1331/`. The exact
frequency-restoring command is:

```sh
/home/claude/.scratch/eka-memory-fast1331/panel.sh SERVER_URL 1331 \
  fresh:asphalt-3:NEW_LABEL fresh:hooked-on:NEW_LABEL fresh:tetris:NEW_LABEL \
  prepared:asphalt-3:NEW_LABEL prepared:hooked-on:NEW_LABEL prepared:tetris:NEW_LABEL
```

Each label must be unused. `panel.sh` sets the checked route file, correct
renderer client ID and SSD profile location, locks the host to 3.0 GHz for
comparability, and restores the previous CPU frequency bounds on exit.

The sparse allocator profiler fixture can be rebuilt with the tested SDK using
`check-sparse-profiler.py` in the evidence directory. It links the production
`sparse_wasm.c.o` and SDK `sbrk.c.o`, compiles the tracked
`allocation-profiler-fixture.cpp` with `-DEKA2L1_SPARSE_ALLOCATOR_FIXTURE=1` and
`-fno-builtin`, and wraps both the existing builtin allocator APIs and
`eka2l1_sparse_aligned_alloc`. The latter is diagnostic-only; normal binaries
use the same measured allocation path without profiling.
