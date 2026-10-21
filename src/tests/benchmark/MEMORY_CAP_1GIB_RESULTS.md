# Fit the browser game renderer within 1 GiB

All eight supported games complete their checked gameplay routes under a **1024 MiB game-tab renderer cap**, with zero swap and zero OOMs. The final build passed **16 complete cap routes**, including two independent cold source-install runs and one prepared-install run for each of the three heaviest games. The measured unpaced windows show no material throughput regression; cold startup is faster. This is a capped-operation result, not an uncapped-peak claim.

## Comparable measurements

Fresh controls for the three heavy games and K-Rally use the preceding deployed `6a5cc6de` build with a 2048 MiB ceiling. The other four controls come from the same-day [eight-game sweep](ALL_GAME_MEMORY_RANKING_20261021.json) on that same build at 4096 MiB. Both sets use fixed 3 GHz CPU bounds, independent empty SSD profiles, no persisted ROM translations and the same checked unpaced routes. No builds or other benchmark cases run concurrently.

The table shows both private-server cold repeats. The separate deployed-URL run is recorded below. FPS is the final matching gameplay window; every preceding window and its compiler work is retained in the [companion JSON](MEMORY_CAP_1GIB_RESULTS.json).

| # | Game | Old peak, MiB | New peak, MiB | Old FPS | New FPS |
| --- | --- | --- | --- | --- | --- |
| 1 | Asphalt 3 | 1316.6 | 1024.0 / 1024.0 | 104.65 | 102.03 / 107.32 |
| 2 | Hooked On | 1385.4 | 1024.0 / 1024.0 | 251.88 | 253.76 / 256.90 |
| 3 | Tetris | 1309.1 | 1024.0 / 1014.0 | 153.05 | 151.55 / 150.45 |
| 4 | Asphalt 2 | 955.4 | 754.6 | 124.26 | 124.60 |
| 5 | Snakes | 868.8 | 682.3 | 77.58 | 79.39 |
| 6 | Sky Force | 812.6 | 680.1 | 253.63 | 266.90 |
| 7 | K-Rally | 838.5 | 693.9 / 715.7 | 154.22 | 155.08 / 167.62 |
| 8 | Super Miners | 823.6 | 677.8 | 289.37 | 300.32 |

K-Rally ranged from 154.2 to 161.0 FPS across the old controls and 155.1 to 167.6 across new runs; this short route varies enough that a precise speedup would be misleading. Asphalt settled repeats bracket the fresh control. Tetris has mixed differences across its three windows (including a faster middle window), so no exact speedup or statistical equivalence is claimed.

| # | Cold startup endpoint | Old seconds | New seconds, two runs |
| --- | --- | --- | --- |
| 1 | Asphalt 3 race | 92.62 | 72.59 / 71.90 |
| 2 | Hooked On boat | 141.58 | 117.17 / 117.54 |
| 3 | Tetris board | 89.29 | 68.54 / 68.44 |

Prepared images use independent fresh browser profiles too; they contain installed files, not compiled V8 code. The private fixture initially lacked its prepared-image directory and was corrected before the runs below. That setup failure is recorded separately in the JSON.

| # | Prepared route | Peak, MiB | FPS | OOM / kill |
| --- | --- | --- | --- | --- |
| 1 | Asphalt 3 | 1024.0 | 107.05 | 0 / 0 |
| 2 | Hooked On | 1024.0 | 260.25 | 0 / 0 |
| 3 | Tetris | 1024.0 | 149.40 | 0 / 0 |

The final candidate uses a 48 MiB generated-module retention budget, trimming
to 40 MiB. Four-region demand-compilation lookahead is unchanged. The budget
counts generated WASM module bytes; it is not a claim that all compiled code
occupies only 48 MiB of physical memory.

## What changed

1. Thumb ROM memory misses now leave the region before the failing instruction's
   side effects and use the existing precise single-instruction compiler. RAM
   regions already used this path. The previous ROM path duplicated callback,
   state-publication and reload sequences in many regions. Scalar accesses and
   eligible whole-span PUSH/POP/LDM/STM accesses use the same recovery contract.
   Callback, fault, PC and writeback behavior remain covered by differential
   tests. Browser execution remains interpreter-free.
2. Thumb exits share one register-state writeback sequence, using the mechanism
   already used by ARM translation. Early exits need the entry values of fields
   written by another path. Only those fields are additionally initialized;
   initializing every cached field measurably slowed Hooked On and was rejected.
   Loop-carried locals retain their existing initialization requirements.
3. Smaller generated code allows 48/40 MiB retention without the startup penalty
   observed when merely shrinking the old code cache. Eviction preserves the
   worker and guest state, drops cold module references, and allows ordinary V8
   collection. No new guest instruction accounting or memory-access checks were
   introduced.

The separate ARM shifter cleanup in `70af9ef33` avoids calculating carry-out for
instructions that do not consume it. It passed correctness tests but its isolated
Asphalt memory peak did not improve; it is not credited for meeting the cap.

## Profiling evidence

The initial normal-build Asphalt capture used Chromium native allocation samples,
V8 code maps and process mappings. Major sampled allocation sites included V8's
`GetAndCopyFirstArgumentAsBytes` (96.4 MiB), `WasmCode` metadata (33.0 MiB), and
trap-handler metadata (22.0 MiB). These statistical ownership estimates overlap
resident mapping totals and must not be added to them. Inspector-related string
storage was also visible and was treated as diagnostic overhead.

About 87% of cumulative generated guest-native bytes in that capture were ROM
Thumb entries. The append-only code map includes retired/reused addresses: this
is an emission distribution, not live executable residency. It directed the
source inspection toward Thumb ROM lowering rather than more allocator tuning.

A separate allocator build attributed resident whole pages to process arenas,
ROM backing, WasmFS, services, guest chunks and CPU/compiler storage. That capture
had complete large-allocation unwind coverage, but 82.3 MiB of small live payload
was below its 64 KiB stack threshold. The native/V8 capture lost final sampling
for three closed temporary workers, so its saved attribution is explicitly
partial. Neither diagnostic run supplies performance numbers or cap acceptance.

The final build's uninstrumented timing routes capture phase `smaps` separately.
Those current mappings provide the before/after resident comparison below; no
native allocation sampler or Debugger is enabled in the cap/FPS runs.

## Rejected experiments

- Shrinking the old cache to 64/60 MiB still peaked at 1275 MiB and delayed
  Asphalt's race to 114.4 seconds. Its 48/40 MiB run OOMed at 1024 MiB.
- A post-emission local compaction pass lowered the peak to 1310 MiB but made
  emission take 6.9 seconds instead of about 1.1 seconds. It was removed.
- Disabling demand-module merging reached 1597 MiB and reduced settled Asphalt
  throughput to 96.8 FPS. It was removed.
- ROM recovery alone lowered Asphalt to 1218 MiB and sped startup, but Hooked On
  still OOMed with a 48/40 MiB cache under the 1 GiB cap.
- The first shared-exit implementation initialized every cached field. Hooked On
  fell to 236 FPS. Restricting entry initialization to shared-exit writeback fields
  restored roughly 250 FPS. The slower implementation is not in the final build.
- The combined compiler change with 64/60 MiB retention reached Asphalt gameplay
  but then OOMed during the first timing window. Reaching the race was not counted
  as a pass.

## Validation and reproduction

- 199 AOT tests passed; the existing unrelated expected-failure case remains
  explicitly identified by that suite.
- 1,296 focused interpreter/compiled comparisons cover full state, flags and
  memory across RAM continuation and ROM recovery, scalar and multiword accesses,
  endian/alignment/page misses and early exits.
- Source relocation/cache-pruning/module-metadata checks and all seven real
  Chromium browser smoke tests passed on the final build.
- Every acceptance run uses one selected game-tab renderer, zero swap, an
  independent empty SSD browser profile, empty persisted ROM translations, and
  guest audio enabled with browser output muted. CPU frequency bounds are fixed
  at 3 GHz and restored afterward. The three large games use public launcher
  source installation; the five SIS games use the normal ROM/RPKG/SIS upload
  route. Prepared routes are identified separately.

The cap applies to the game renderer, including charged filesystem cache. Browser,
GPU, spare renderers and test tooling remain outside it. Peaks of exactly 1024 MiB
are censored by the cap and can include Linux filesystem-cache reclaim. Zero OOM
under this cap proves capped operation, not an uncapped peak below 1 GiB or ample
spare headroom. FPS is measured on the existing continuous unpaced gameplay routes;
it is not a claim about every level, browser, or device.

Full captures, screenshots, failed experiments, profiles and frequency-restoring
scripts are in `/home/claude/.scratch/eka-memory-fast1024/`.

## Final-build resident attribution

At the same Asphalt settled gameplay endpoint, the normal control and candidate mappings sum as follows. These are PSS snapshots, not the cgroup peak and not live allocator bytes. The large reductions are in Chromium allocator and executable mappings; resident WASM backing changes much less.

| # | Mapping owner | Old PSS, MiB | New PSS, MiB |
| --- | --- | --- | --- |
| 1 | WASM linear memory backing | 413.28 | 405.61 |
| 2 | Chromium PartitionAlloc mappings | 393.45 | 256.99 |
| 3 | V8 executable code and trampolines | 187.04 | 91.15 |
| 4 | Other V8 sandbox mappings | 76.92 | 65.62 |
| 5 | File-backed mappings | 72.37 | 72.77 |
| 6 | Other V8 mappings | 20.43 | 24.03 |
| 7 | Other anonymous or shared mappings | 10.27 | 10.25 |
| 8 | Total | 1173.77 | 926.42 |

The three biggest native allocation sites above explain why generated-module size matters to both allocator and executable memory. The mapping reduction cannot be assigned byte-for-byte to those statistical samples. Tetris endpoint PSS similarly falls from 1100.10 to 877.38 MiB in the first matched pair.

## Build and reproduction

Runtime commit: `e237dd50893fac91cc2a38f2e1b0648c33a3e6d0`.

Candidate WASM SHA-256: `d0c73c9ef488b72839af9c25525ed8b4dcd54bfaaa8d76d2e95fdb2b2d3b86b9`.

Candidate JavaScript SHA-256: `ecd7af44b535d06454aef12932072c9cbf5bfc31e5bdbe6c0aff213a54e8fadf`.

Control WASM SHA-256: `6a5cc6de8223b777b46c3699891767ee5f8b1d753898418cfbb28ef17f4e481c`.

```sh
/home/claude/.scratch/eka-memory-fast1024/panel.sh SERVER_URL 1024 \
  fresh:asphalt-3:NEW_LABEL fresh:hooked-on:NEW_LABEL fresh:tetris:NEW_LABEL
```

Use `stock:GAME:NEW_LABEL` for SIS games and `prepared:GAME:NEW_LABEL` for the three N-Gage prepared images. Final defaults use no cache override. The script invokes `renderer-memory-limit.mjs`, fixes CPU bounds, and restores them on exit. Full raw paths and all cap events are retained in the JSON.

## Public deployment check

The exact tested JS/WASM hashes were verified at `https://claude-laptop.lan:8188/` after deployment. A further independent fresh Asphalt 3 source-install route passed under 1024 MiB with a **1001.40 MiB peak**, `memory.events.max=0`, `oom=0`, and `oom_kill=0`. Its two unpaced windows were 91.15 and 101.38 FPS; this additional short settled window is about 3.1% below the fresh control, within the observed spread rather than evidence of an exact speed match. The gameplay endpoint was inspected.

The immutable deployed release is `/home/claude/.local/share/eka2l1/releases/e237dd508-d0c73c9e/`. The service is active, CPU frequency bounds were restored, and temporary private servers were stopped.
