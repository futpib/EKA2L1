# Reclaim generated code without restarting the CPU worker

The CPU worker does not have to be destroyed for V8 to collect generated code.
Our table exports were keeping a large compilation cache live. The replacement
reclaims cold modules at a CPU boundary and leaves the worker, guest state, and
hot compiled code intact. It also frees retired C++ cache records.

## Ownership and reclamation

1. Emscripten's table, mirror, and abort wrappers retain callable exports. A
   live export keeps its module's code alive. Clearing an unrelated JavaScript
   variable cannot reclaim that module.
2. `module_residency` tracks every installed module, its wire-byte size, and
   the table slots that still belong to it. Replacement transfers ownership;
   removing the last slot retires the module. Module IDs and table slots are
   reused.
3. Above 32 MiB of live source-module bytes, the runtime selects cold whole
   modules until at most 24 MiB remains. This is a cache budget, **not** a
   bound on physical V8 or renderer memory. Recency and an aging reuse score
   protect repeatedly used code from one-use startup scans. Usage stores run
   only while compilation is active; after 20 ms without installation, the
   normal inner runner has no usage stores.
4. At a boundary with no generated frames on the stack, all selected ROM and
   RAM registry owners are retired, table references are removed, and pending
   merge bodies are pruned. Shared RAM functions survive until their last
   owner retires. A later miss recompiles normally; it does not interpret.
5. Retired RAM records are erased from a stable-node map. Monotonic version
   IDs reject late module attachments without preserving an ever-growing
   deque of tombstones. Freed C++ storage returns to the existing allocator
   for reuse. WASM linear-memory capacity need not shrink for this to work.
   The guest-address index points directly to live records; dispatch does not
   perform an additional version-map lookup to recover them.

ROM admission markers must also be cleared on eviction. Otherwise a retired
ROM PC is treated as already compiled and falls into the single-instruction
fallback. Pending ROM merge bodies must be removed too: retaining an old
body and recompiling the same PC produces duplicate exports and failed
consolidation. Both cases have executable WASM regression coverage.

There is no production forced-GC call, CPU-worker restart, per-game recycling
threshold, or guest instruction counter in this implementation. The existing
optional cleanup of unused pool workers remains separate from CPU execution.

## Corrected 40 MiB prepared-game comparisons

The 40 MiB build below uses normal collection throughout these runs. Chromium
154, fresh browser profiles, prepared installations, guest audio enabled,
unpaced execution, and a fixed 3.0 GHz host clock are shared with the controls.
The control is `df5c37567` with CPU recycling suppressed. Its higher cap leaves
its peak uncensored. Candidate cap events (`max`, `oom`, `oom_kill`) are all
zero; each route has one CPU-worker start and no instantiation failures.

| # | Game | Control tab peak, MiB | Candidate tab peak, MiB | Control FPS | Candidate FPS |
|---:|---|---:|---:|---|---|
| 1 | Asphalt 3, normal racing window | 1654.8 | 1288.4 / 1245.3 | 84.25 | 83.53 / 86.10 |
| 2 | Tetris, three consecutive windows | 1644.5 | 1244.8 | 131.67 / 127.18 / 118.38 | 136.86 / 133.90 / 130.75 |
| 3 | Hooked On, three consecutive windows | 1640.9 | 1223.3 | 185.00 / 194.31 / 218.83 | 198.07 / 206.82 / 220.03 |

Asphalt validates the racing HUD at both endpoints. Tetris's last screenshot
still shows active Marathon play; Hooked On's last screenshot shows the Costa
Rica boat without tutorial overlays. These small samples support retaining
gameplay throughput; they do not establish a general speedup. Tetris and
Hooked On vary across consecutive windows even in the control.

Evidence: `current40-prepared-{asphalt-3,tetris,hooked-on}`,
`current40-prepared2-asphalt-3`, `current40-control-asphalt-3`,
`settled-control-tetris`, and `cross-control-hooked-on`, under
`/home/claude/.scratch/eka-graceful-lifetime/`. `checked-gameplay-routes-single.json`
and `current40-panel.sh` record the corrected route and invocation.

## Initial prepared Asphalt comparison (FPS later rejected)

Chromium 154, fresh browser profiles, guest sound enabled, the same prepared
installation and extended stationary-race route, sequential control/candidate/
candidate/control runs at 3.0 GHz. The control is `df5c37567` with CPU recycling
suppressed so it provides the throughput reference without the old restart
cost. Its cap is 2,048 MiB to avoid censoring its peak. Candidates use 1,331 MiB.
Only the uniquely verified game-tab renderer is capped, with no swap.

The first corrected cache build, before direct address indexing, had WASM SHA-256:
`d2c039d86169c71ab30ba7ceecc6e3537d3f3649ec6fa40b8e6853d6b4f384c8`.
Control WASM SHA-256:
`7a0400e2134c8ecc0174eefc71e09eb1b482b836aa1213d513f7721eeaac5ca8`.

| # | Run | Tab peak, MiB | Unpaced FPS | Race entry, s |
|---:|---|---:|---:|---:|
| 1 | Unlimited control 1 | 1624.3 | 103.86 | 52.90 |
| 2 | Graceful reclamation 1 | 1312.4 | 106.51 | 80.72 |
| 3 | Graceful reclamation 2 | 1309.3 | 107.09 | 81.42 |
| 4 | Unlimited control 2 | 1651.3 | 103.24 | 52.53 |

All four completed the then-current racing-HUD assertion with zero limit-hit/OOM
events, zero instantiation failures, and one CPU-worker start. That assertion
checked only the start of the interval. Endpoint screenshots subsequently
showed the trial purchase screen, so these FPS columns mix racing and menu
work and **cannot establish racing throughput or a performance win**.
Startup still costs about 28 seconds versus retaining unlimited code. The
previous recycling default measured 96.7 seconds and 101.53 FPS in its fixed-
clock panel, recorded in `RENDERER_CAP_RESULTS.md`; that is historical evidence,
not a new simultaneous comparison.

The benchmark now requires the racing HUD at both interval endpoints and
publishes an FPS report only after both checks pass. The earlier scratch
route's 60-million-guest-microsecond warmup allowed the trial timer to expire
inside the measurement. Accepted comparisons use the repository's normal
5-million-guest-microsecond warmup on both builds. The attempted longer
`headroom40-freshquiet-asphalt-3` route exposed the purchase screen when its
extra interval started; its reported initial 104.72 FPS is also rejected as
a racing-throughput claim. This correction does not change the recorded
cgroup peaks or prove that old native allocations were still live.

The corrected candidate also passed a run peaking at 1278.2 MiB, but its FPS
was excluded: a concurrent VM workload reduced actual CPU frequency to
1.5–1.6 GHz. One repeat timed out while OPFS initialization was still pending,
before any emulation/AOT work; it is a route failure, not a passing cap run.
Earlier cache prototypes had stale ROM admission/merge bookkeeping and are
not accepted performance evidence.

In subsequent prepared samples, the same build measured Tetris at 126.73
versus 129.33 FPS and Hooked On at 211.30 versus 218.83 FPS. The metadata
change had introduced a second hash lookup on address-cache misses. Direct
address indexing removes that added operation while preserving reclamation;
the final build is validated separately below.

Raw evidence is under `/home/claude/.scratch/eka-graceful-lifetime/`:
`fixed-control-clean-{1,2}`, `fixed-clean56-{3,4}`, and the excluded
`fixed-clean56-{1,2}`. Each cap directory includes `result.json`,
`memory-samples.json`, the route log, gameplay screenshots, and timing/FPS
reports. `clean-fixed-panel.sh` records the exact environment and restores
host frequency limits on exit.

## Direct-address-index candidate and validation method

The 56 MiB direct-address-index candidate has WASM SHA-256
`1f3fbc4406b618d17c46cdd2b7fff32865db9b75cb071882be769916280bc52e`
and JavaScript SHA-256
`8fc75b90d9a327c42dfe2f0422648d2d4653d00b2c8c6a7598c09288800d3386`.
The native CPU suite passed 6,919 assertions across 40 test cases; the AOT
WASM suite passed 197 tests. The additional `--precompile-only` suite passed
ROM/RAM eviction, table-slot reuse, pending-ROM merge cleanup, and >16K-entry
capacity checks. The browser suite passed with system Chromium.

`module-residency-probe.mjs` copies this build to a private directory and
instruments generated-module installation and table release. It retains
only weak instance/export references and numerical bookkeeping. After the
normal gameplay route, it pauses the guest and requests two diagnostic GCs.
Dead-module instances and *all* their exported function wrappers must become
unreachable. Checking instances alone is insufficient: a table function can
retain native module code even when its JavaScript `Instance` wrapper dies.

The cap/FPS runs use the uninstrumented build and do not request GC. Their
renderer cgroup peak includes code, WASM memory, and other renderer charges;
it is not the live-wire-byte cache size. The GC probe's process PSS before
and after is separate evidence, and its delta must not be treated as an
exact allocation-site attribution.

Two samples are excluded from throughput acceptance:
`direct-prepared-tetris` completed its standard 129.33 FPS window, then an
optional diagnostic command incorrectly tried to step a running guest;
`final-prepared-tetris` completed the whole route and passed the cap, but
actual CPU frequency fell from 1.7 to 1.3 GHz despite the requested 3.0 GHz.
Neither is silently counted as a clean performance comparison.

The 56 MiB budget was **not accepted for deployment**. Despite passing prepared
Asphalt twice at 1310.9/1310.2 MiB and fresh routes for all three heavy games,
`final2-fresh-asphalt-3` was OOM-killed while loading game resources after
installation: 1331 MiB peak, 134 limit-hit events, six OOM events and one kill.
The last eviction retained 41.9 MiB of live source-module bytes. This is direct
evidence that a live-code budget must leave space for transient allocations
and collection latency; successful earlier routes do not erase this failure.

## Collection audit

`collection-final` used a private instrumented copy of the 56 MiB build,
the full fresh Asphalt installation, gameplay, and then a paused guest with
two diagnostic GC requests. It found 131,209 retired modules, **zero**
reachable retired instances, and **zero** reachable retired exported
functions. The 347 still-owned modules had 54,712,596 live source bytes.
There was one CPU-worker start.

This establishes that table retirement does not leave our retired callable
objects rooted in that worker. It does not assign individual native pages to
modules. Game-renderer PSS was 989.4 MiB before the diagnostic GC interval and
990.7 MiB afterward: there was no extra resident-memory drop to claim from
those forced collections. Normal collection during execution, allocator reuse,
and retained allocator pages must be distinguished from JavaScript reachability.
The diagnostic's 1326.4 MiB cgroup peak used a 2048 MiB limit; it is not a
passing production 1331 MiB trial. Its FPS is not performance evidence.

The raw weak-reference counts and per-process snapshots are in
`collection-final/route/module-collection.json` under the evidence directory.

The intermediate 40 MiB build has WASM SHA-256
`38d9329b1edd6733518dcd8a363e2867bafc195dad16f5769cf4fbf7e40317af`
and JavaScript SHA-256
`337188f36c0bf0a49e643ec398ee552f7fab7cbe97835660e42ce633ebe39c82`.
Its 197-test WASM suite, precompile-only suite and browser suite also passed.

`collection-headroom40` repeats the audit on that build and records weak
references **before** requesting GC. Of 116,115 retired modules and 457,870
retired exports, normal GC had already collected 115,950 instances and
457,549 exports. The diagnostic GC cleared the remaining 165 instances and
321 exports. All 246 still-owned modules remained reachable, accounting for
37,264,734 source bytes. Renderer PSS changed only from 887.0 to 886.6 MiB
in the forced-GC interval. Thus almost all observed retirement was already
handled by normal collection; there is no production dependency on forced GC.
This diagnostic used a 2048 MiB cap and is separate from production cap/FPS
validation.

## Cold-route headroom and final budget

The 40 MiB budget also failed a repeat and was not deployed.
`current40-fresh-asphalt-3` hit 1331 MiB during N-Gage client initialization,
before gameplay, with 27 limit-hit events, one OOM event and one kill. The
last sweep had retained 29.9 MiB of live module source. Earlier successful
40 MiB runs do not erase this failure: `headroom40-prepared-asphalt-3` passed
its complete extended route at 1323.8 MiB, only 7.2 MiB below the cap. Its
FPS is rejected because that old route crossed the trial-expiry screen.

The final budget is therefore tested at 32 MiB, trimming to 24 MiB. The
source-byte budget cannot bound browser memory directly; V8's native
allocations and collection timing require additional headroom.

The uncapped diagnostic repeat `current40-fresh-peak` did not reproduce the
1331 MiB failure: it completed at 1255.3 MiB under a 2048 MiB limit. Its first
900-to-1100 MiB threshold snapshots grew primarily in PartitionAlloc
(307.1 to 421.3 MiB PSS) and executable mappings (193.8 to 275.1 MiB), with
anonymous mappings at 361.5 to 365.4 MiB. A later 1200 MiB snapshot had
371.0 MiB PartitionAlloc, 224.9 MiB executable and 544.7 MiB anonymous PSS.
These mapping classes are not allocation-site attribution, and the later
anonymous growth must not be hidden by focusing only on the first interval.
This run overlapped a build and is diagnostic evidence, not a performance
comparison or a production-cap pass.

## 32 MiB validation

The tested runtime has WASM SHA-256
`76d0a98a8e367c3b96ba3d9db04f4e793448d17f2440612014dc62085b3b8720`
and JavaScript SHA-256
`7dcaa9e83f7b2baa7447caa6413721bfa63f17254db674f062a702a274d5ac03`.
Its 197-test AOT WASM suite, precompile-only suite, and browser suite
(`PUPPETEER_EXECUTABLE_PATH=/usr/bin/chromium npm test`) passed. The native
6,919-assertion result above still applies: the subsequent budget changes
affect only the Emscripten implementation.

`final32-panel.sh` pins the host to 3.0 GHz and restores its prior frequency
limits on exit. All candidate cap runs use 1331 MiB, no swap, normal GC and
fresh browser profiles. The uncensored fresh control uses 3072 MiB and the
same old runtime hash recorded above, with CPU recycling suppressed.
`checked-gameplay-routes.json` adds a three-second second Asphalt racing
window after the normal five-second window; both endpoints of each are
validated. The first fresh candidate ran only the first window; its repeat
and following runs include both.

All eight candidate runs completed their full routes with zero
`memory.events.max`, `oom`, and `oom_kill`, exactly one CPU-worker start, and
no module-instantiation failures. Served WASM hashes and measured host
frequency were checked in every FPS report.

| # | Route | Tab peaks, MiB | Minimum headroom, MiB | Gameplay evidence |
|---:|---|---|---:|---|
| 1 | Fresh Asphalt 3, two runs | 1267.9 / 1223.4 | 63.1 | Racing HUD at every FPS endpoint |
| 2 | Prepared Asphalt 3, two runs | 1257.4 / 1231.2 | 73.6 | Two racing windows per run |
| 3 | Fresh Tetris | 1140.2 | 190.8 | Three active Marathon windows |
| 4 | Prepared Tetris | 1232.3 | 98.7 | Three active Marathon windows |
| 5 | Fresh Hooked On | 1201.9 | 129.1 | Three Costa Rica boat windows |
| 6 | Prepared Hooked On | 1284.1 | 46.9 | Three Costa Rica boat windows |

The uncensored fresh Asphalt control peaked at 1858.5 MiB. Its first/later
windows were 83.60 / 93.23 FPS; the fresh candidates were 80.73 and
81.53 / 96.51 FPS. The opening candidate windows were 2.5–3.4% slower and
compiled 266/173 functions versus 19 in the control. The later candidate
window compiled seven functions versus one in the control and was 3.5%
faster. These samples do not establish a sustained throughput regression
or a general speedup; they do show that startup recompilation is not free.

| # | Prepared game | Unbounded control FPS | Candidate FPS |
|---:|---|---|---|
| 1 | Asphalt 3, opening five-second window | 84.25 / 81.23 | 83.17 / 82.55 |
| 2 | Tetris, successive five-second windows | 131.67 / 127.18 / 118.38 | 141.10 / 133.10 / 131.56 |
| 3 | Hooked On, successive ten-second windows | 185.00 / 194.31 / 218.83 | 199.90 / 210.23 / 221.97 |

The two prepared Asphalt opening windows average 82.86 FPS versus 82.74
for the two controls, effectively equal in these samples. The tail control
(`final32-tail-control-asphalt-3`) peaked at 1634.1 MiB and measured 90.30
FPS in its later window; the two later candidate windows measured
96.00 / 92.90 FPS. Fresh
Tetris measured 133.14 / 132.94 / 128.37, and fresh Hooked On measured
200.35 / 210.23 / 221.35 FPS. Final screenshots were inspected: Tetris was
still playing Marathon and Hooked On still showed the boat without overlays.

**Startup limitation:** the scripted prepared Asphalt race entry took
118.18 / 116.91 seconds versus 52.30 / 52.81 seconds with the unlimited-code controls.
The historical restart policy took about 96.7 seconds at the same requested
3.0 GHz. Fresh candidate entry took 182.96 / 185.15 seconds versus 78.32 in
the current fresh unlimited-code control. These are the benchmark's scripted
race-entry times, not minimum time to first frame. Reclaiming code in the
same worker is safe and the measured routes fit; the small cache's repeated
startup compilation remains a cost, and this is not a startup-speed win.

Raw final evidence: `final32-{fresh,fresh2,prepared,prepared2}-asphalt-3`,
`final32-freshcontrol-asphalt-3`, `final32-tail-control-asphalt-3`,
`final32-{fresh,prepared}-tetris`, and
`final32-{fresh,prepared}-hooked-on` under the evidence directory above.
`summarize-final32.py` checks build hashes, 3.0 GHz median frequency, one
CPU-worker start, zero limit/OOM events and zero runtime errors for these
passing runs. Only these three heavy games were tested in the cap panel;
the budget is empirical headroom for these measurements, not a hard bound
on total browser memory in every workload.
