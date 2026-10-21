# Share generated code without a gameplay slowdown

This is a generated-code size cleanup with no material measured performance
regression. It does not establish lower whole-tab RAM usage.

Two compiler deduplications are enabled by default: common continuations in
inlined Thumb leaves, and identical private helper definitions within a module.
The inlining bounds and memory-access fast paths are unchanged. After a prepared
Tetris memory failure, the generated-module trim trigger moves from 48 to 47 MiB;
the 40 MiB target and four-region lookahead remain. The
[raw comparison data](CODE_DEDUP_RESULTS.json) records every window, cap result,
background-load capture and excluded attempt.

## Changes and correctness

1. The existing bounded leaf analysis computes postdominators: instructions
   every path from a given guest PC must reach. Each branch emits only up to its
   earliest common continuation, then emits that continuation once. This adds
   no helper call. The original expanded-path instruction bound still decides
   eligibility. Early returns, flags, prior stores and the precise PC of a failed
   memory access are retained.
2. Module assembly interns private definitions only when their guest name,
   bytecode, all local-type counts and source marks match. Imports and public
   function indices remain unchanged; existing call relocations select the
   shared private definition. The interning table exists only during assembly.

The expanded leaf fixture cohort fell from **40,065 to 33,550 generated body
bytes (16.3%)**. This is a fixture code-size measurement, not a claim of a 16.3%
whole-game or resident-memory reduction. Private-helper tests verify sharing
across parent functions, multi-byte indices and imports, while rejecting merges
across local layouts or guest provenance. **Whole-tab RAM savings are not
established**: endpoint PSS and capped peaks vary in both directions.

Validation: 199 AOT tests, including 3,840 independent interpreter comparisons
for leaf registers/flags/memory; 28 private-helper configurations; source-map
relocation/cache/module checks; and seven real Chromium smoke tests passed.
The existing `0x8046506E` harness expected-failure and diagnostic-only skips
remain explicitly reported by the suite.

## Matched browser comparison

Chromium uses independent empty SSD profiles, no persisted ROM translations,
guest audio enabled with browser output muted, and fixed 3 GHz CPU bounds.
The three N-Gage games use full fresh source installation. Other games use their
ordinary SIS route. Each run caps only the game renderer at 1024 MiB, with zero
swap. No builds or other benchmark routes run concurrently in accepted pairs.
Later pairs record external VM/compiler/browser load every two seconds.
FPS below is the last matching gameplay window; all windows are in JSON.

### Initial compiler-only build, 48/40 MiB

| # | Game | Control FPS | Candidate FPS | Change | Candidate peak, MiB |
| --- | --- | --- | --- | --- | --- |
| 1 | Asphalt 3 | 105.96 | 107.93 | +1.86% | 1024.0 |
| 2 | Hooked On | 250.81 | 250.05 | -0.31% | 1024.0 |
| 3 | Tetris | 148.92 | 151.11 | +1.47% | 1020.5 |
| 4 | Asphalt 2 | 123.75 | 124.60 | +0.69% | 760.6 |
| 5 | Snakes | 77.22 | 76.79 | -0.55% | 691.4 |
| 6 | Sky Force | 253.00 | 265.88 | +5.09% | 684.9 |
| 7 | K-Rally | 158.69 | 173.25 | +9.17% | 708.1 |
| 8 | Super Miners | 297.19 | 298.10 | +0.30% | 698.5 |

### Final default build, 47/40 MiB

| # | Game | Control FPS | Candidate FPS | Change | Candidate peak, MiB |
| --- | --- | --- | --- | --- | --- |
| 1 | Asphalt 3 | 105.96 | 105.63 | -0.32% | 1019.6 |
| 2 | Hooked On | 250.81 | 252.25 | +0.57% | 1024.0 |
| 3 | Tetris | 148.92 | 151.23 | +1.56% | 1019.7 |
| 4 | Asphalt 2 | 123.75 | 123.20 | -0.45% | 760.4 |
| 5 | Snakes | 77.22 | 78.02 | +1.04% | 689.3 |
| 6 | Sky Force | 253.00 | 263.30 | +4.07% | 683.9 |
| 7 | K-Rally | 158.69 | 167.72 | +5.69% | 692.5 |
| 8 | Super Miners | 297.19 | 289.26 | -2.67% | 684.4 |

These short routes support no material throughput regression, not exact
performance equivalence or a precise speedup for faster individual windows.
All selected final routes completed with `oom=0` and `oom_kill=0`. Peaks at
1024 MiB are censored by the cap: they prove capped operation, not an uncapped
peak below 1 GiB or spare headroom.

Time to the first timed gameplay window, including scripted navigation and
warmup (not just the first visible frame):

| # | Game | Control seconds | Final seconds |
| --- | --- | --- | --- |
| 1 | Asphalt 3 | 73.52 | 73.83 |
| 2 | Hooked On | 148.58 | 146.58 |
| 3 | Tetris | 70.78 | 70.58 |
| 4 | Asphalt 2 | 25.20 | 25.20 |
| 5 | Snakes | 25.09 | 25.29 |
| 6 | Sky Force | 21.70 | 22.09 |
| 7 | K-Rally | 36.13 | 36.66 |
| 8 | Super Miners | 19.89 | 20.32 |

## Resident memory

The final gameplay endpoint mapping PSS is shown separately from the cgroup
peak. The small, mixed changes do not establish a whole-tab RAM reduction.
These capped runs also include differing filesystem-cache reclaim.

| # | Game | Control endpoint PSS, MiB | Final endpoint PSS, MiB |
| --- | --- | --- | --- |
| 1 | Asphalt 3 | 881.61 | 878.10 |
| 2 | Hooked On | 917.04 | 913.55 |
| 3 | Tetris | 875.88 | 874.71 |
| 4 | Asphalt 2 | 600.48 | 602.30 |
| 5 | Snakes | 595.93 | 599.33 |
| 6 | Sky Force | 632.91 | 616.44 |
| 7 | K-Rally | 592.41 | 584.93 |
| 8 | Super Miners | 631.46 | 630.87 |

## Prepared Tetris failure and retention adjustment

The compiler-only 48/40 build passed the eight fresh routes and prepared
Asphalt 3 / Hooked On, but **OOMed on prepared Tetris before gameplay**. This
failure is retained as `prepared-tetris-repeat`, with `oom=1`, `oom_kill=1`.
The old control completed the same route. The candidate had one fewer module
trim at the relevant stage. A byte-based retention budget can hold different
sets of functions after emitted code changes; this is a plausible explanation,
not a measured attribution of every resident byte.

On the same candidate binary, a 47/40 benchmark override passed two independent
prepared Tetris runs. The final build bakes in that trigger and uses no override.
The first final run's last window initially appeared 4.1% slower. Screenshot
inspection later showed it had crossed into the results screen. The repeated
control, both override screens and the first live-URL run also ended their third
window outside gameplay. These third windows are **excluded**, even where their
reported FPS was favorable. The earlier interpretation as ordinary timing
variation was incorrect.

The matching middle gameplay window is 152.48 / 153.80 FPS in the two controls
and 152.55 / 154.37 in the final-build repeats. All their start/end screenshots
show the board. The harness now requires the Tetris board at both interval
endpoints before publishing an FPS record. The follow-up live prepared route
stops after the second window. A retrospective check of 66 real screenshots
accepted the board endpoints and rejected all five results/transition endpoints.

| # | Build / run | First / middle / final FPS | First timing, seconds | Cap result |
| --- | --- | --- | --- | --- |
| 1 | `prepared-tetris-control-check` | 153.40 / 152.48 / 149.82 | 54.57 | pass, 1024.0 MiB |
| 2 | `prepared-tetris-control-repeat-b` | 153.60 / 153.80 / excluded (results) | 54.34 | pass, 1024.0 MiB |
| 3 | `prepared47-tetris-screen` | 154.52 / 152.79 / excluded (results) | 53.90 | pass, 1024.0 MiB |
| 4 | `prepared47-tetris-repeat` | 153.56 / 154.39 / excluded (results) | 53.67 | pass, 1024.0 MiB |
| 5 | `prepared-tetris-default47-a` | 152.50 / 152.55 / excluded (results) | 53.89 | pass, 1024.0 MiB |
| 6 | `prepared-tetris-default47-c` | 155.92 / 154.37 / 151.88 | 54.84 | pass, 1024.0 MiB |

## Excluded and diagnostic attempts

One final prepared Tetris attempt (`prepared-tetris-default47-b`) timed out
before guest execution while waiting for OPFS initialization. Its peak was
142.9 MiB, with no OOM and no game asset fetches or FPS measurements. The
launcher remained at `Loading Tetris` with no device disk. This is recorded
as a browser-filesystem startup failure, not a compiler timing result.
The same pre-emulation OPFS stall is documented in the preceding
[reclamation report](GRACEFUL_CODE_RECLAMATION.md) and
[renderer-cap report](RENDERER_CAP_RESULTS.md); this work does not fix it.

The first combined Hooked On capture dropped from 258.5 to 172.8 settled FPS.
An external VM started at 20:52:22 UTC, immediately before that capture's timing
windows. A later leaf-only run overlapped the VM and a large Android build.
Those timings cannot establish a compiler regression. CPU-frequency samples
alone had missed this contention; subsequent runs record external process CPU.

Two follow-up profiling routes accidentally overlapped after stopping a parent
shell left its next benchmark child running. Both were stopped and excluded.
The private comparison harness now uses a lock, and all owned child processes
were checked before restarting. Partial artifacts and qualification notes are
retained rather than counted as passes.

With quiet timing windows, the adjacent initial Hooked On pair was **250.81
versus 250.05 FPS (-0.31%)**, with close agreement across all three windows.
The large drop did not reproduce under quiet conditions. Profiles collected
after timing in the contaminated leaf-only attempt are diagnostic artifacts only.

## Scope and reproduction

Repeated scalar memory proofs and the full ARM proof-failure fallback remain.
Combining those paths changes executed checks or recovery behavior and needs a
separate measured implementation. This change shares common leaf tails and
identical private bodies without adding execution calls.

Artifacts: `/home/claude/.scratch/eka-dedup/`. `panel.sh` invokes
`src/tests/wasm/renderer-memory-limit.mjs` with the checked gameplay routes,
fixes/restores CPU frequency bounds, captures phase mappings and records external
load. `compare.py`, `collect.py` and the JSON retain matching windows and build
hashes. Runtime source maps are enabled; allocation/native profilers are absent
from accepted timing runs. The 4% single-window comparison threshold triggers
investigation; it is not a statistical confidence interval.

Compiler commit: `66daa355fbabc7df4cac85b52f3ea90a6b671f9b`.

Final WASM SHA-256: `af0162b29b63d1c629f7b57c02ca35077c508264b712ccb5832369e16a8fa506`.

Final loader SHA-256: `3cefe612be554de5bf098798e272be3dd50a4e2c5593f350a424e1b35c902de7`.

## Deployment

The immutable release `/home/claude/.local/share/eka2l1/releases/b26148182-af0162b2` is deployed at `https://claude-laptop.lan:8188/`. Both served hashes match the final candidate. The first live prepared Tetris route completed at the 1024 MiB cap with zero OOMs. Its first two gameplay windows were 153.53 / 154.75 FPS; its third window crossed into the results screen and is excluded from gameplay evidence. A follow-up live route with the new start/end board assertions passed under the same cap, with zero OOMs and two verified gameplay windows of 153.38 / 151.56 FPS. Its peak was 1024.0 MiB. CPU frequency bounds were restored afterward.
