# Startup follow-up: compiler admission and mapping work

## Scope

This campaign continues from `47bab3744`, the published result of the
[startup bookkeeping campaign](STARTUP_BOOKKEEPING_RESULTS.md). It starts with
the longest automatic loading stage, Hooked On's fresh N-Gage installation,
then checks Asphalt 3, Tetris and the other seven launcher games.

Normal startup measurements use Chromium 153.0.8010.52, an i7-10875H and NVIDIA
T1000 ANGLE/Vulkan. Each run starts with a fresh browser, HTTP cache and guest
filesystem; the server's ROM/game download cache is warm. Internet/IPFS
retrieval is excluded. Measurements are serial, without simultaneous builds
or test suites. Ordinary background host work remains enabled.

First presentation and installation completion are separate metrics. For
N-Gage games, completion is receipt of the successful installer-exit log,
checked against the **Installation complete** screen. It does not include
subsequent language selection, menus or loading a gameplay scene. For the
other seven games, the headline metric is the first counted presentation,
which can still be black. Logos are not game-ready frames.

Review found that the screenshot color heuristic included HTML touch controls
and the FPS counter layered above the canvas. That could falsely classify a
black guest frame as nontrivial. The harness now hides only those overlays
while capturing, restores them afterward, and fails if a non-N-Gage game
never produces a nontrivial image. Earlier `nontrivial` fields are not trusted
as guest-image evidence; first-presentation counters and installer-exit
milestones are separate and unaffected. Sky Force receives a fresh paired
visible-startup check with the corrected harness. That check finds its title
artwork at 18.15 seconds on the control and 17.39 seconds on the adopted build
(two-second screenshot sampling resolution).

## Changes

1. **Source provenance:** copying source marks used to scan the entire mark
   vector for each emitted range. Binary search now selects the relevant
   interval. Generated instructions and provenance contents are preserved.
2. **Page-table enumeration:** resolve the first and last metadata entries
   before walking contiguous page-table storage. Preserve the old multiple
   memory model's directory boundary inside the table at `0x60000000` by
   falling back to individual lookups when the endpoints are not contiguous.
3. **ROM candidate admission:** eager preparation and runtime discovery shared
   a 65,536-entry admission limit. Preparing loaded images filled that table,
   preventing genuinely hot decompression and hashing routines from being
   considered. Runtime-origin candidates now have their own admission count.
   The eight-observation threshold, one attempt per immutable entry and
   **4,096-region runtime compilation quota are unchanged**.

A two-list mapping-enumeration cache was also tried, including a correction to
construct its capturing `std::function` once rather than allocating on every
chain entry. It improved installation timing but failed the unpaced gameplay
gate and was removed. Its measurements belong to rejected prototypes, not the
adopted result.

Guest clocks, watchdog policy, executable-code assumptions, permissions,
aliasing and eager compilation scheduling are unchanged. No user-facing
experiment flags or game/firmware binaries are added.

## Attribution

A native sampling capture of the published Asphalt 3 build identified
source-map construction and memory-view rebuilding alongside real interpreter
work. Its 49,760 CPU-worker userspace samples include 28,171 without an exact
instruction position and 5,048 in JIT code without a matching snapshot. These
gaps are retained; inclusive function totals are not exact costs of individual
checks. Streaming the JIT log readers made the 2.6 GiB capture readable without
Node's maximum-string-length failure.

A separate temporary fallback-dispatch sampler found 58,931 of 118,495 sampled
dispatches in EZLib.dll and 45,136 in hash.dll. That is **88% of sampled fallback
dispatches, not 88% of CPU time**. ROM headers and directory records identify the
libraries. Diagnostic logging then showed hot PCs absent from the candidate
map when it reached 65,536 entries, even with only 2,480–3,433 regions compiled
against the 4,096 quota. This establishes an admission problem rather than
evidence that a larger compiled-code quota is needed.

Diagnostic builds and profiles are excluded from startup timing comparisons.
All temporary PC samplers and admission logging were removed.

## Experiments rejected

| # | Variant | Observation | Decision |
| ---: | --- | --- | --- |
| 1 | Raise hot compilation quota to 16,384 without admission fix | Hooked On 102.96 s, Asphalt 3 59.85 s | No improvement over the smaller quota's preceding build |
| 2 | Reuse the view's enumeration output buffer through a changed resolver API | Hooked On 90.95 then 105.39 s; preceding build 98.40 and 103.64 s | Initial gain did not repeat; reverted |
| 3 | Raise quota to 16,384 after admission fix | Hooked On 87.57 s, Asphalt 3 54.36 s; 4,096 quota 82.58–88.36 and 53.89 s | Extra compiled functions did not improve the two slowest games |
| 4 | Retain four mapping lists | Hooked On 85.05 s, Asphalt 3 56.11 s, Tetris 44.12 s | No repeatable benefit over the two-list prototype; neither cache is adopted |
| 5 | Retain two mapping lists, including persistent resolver correction | Matched unobstructed Hooked On boat scene: 182.94 and 204.30 FPS; control 238.28; three-change build without cache 238.01 | Reject on gameplay evidence; the precise cause of the remaining runtime loss was not established |

## Adopted build: installation results

The selected build contains changes 1–3 only. Its final rebuild is byte-identical
to the measured `admission-build` artifact:
`c0103c19db43678dcbe2c53ff41fb70f2a15e6955a444833c01fd0e09d244cb5`.

| # | Game | Published `47bab3744`, seconds | Adopted build, seconds |
| ---: | --- | --- | --- |
| 1 | Hooked On | 101.25, 105.46, 116.91 | 82.58, 85.00, 88.36 |
| 2 | Asphalt 3 | 62.77, 65.88, 64.73 | 53.89, 50.56 |
| 3 | Tetris | 47.43 | 47.00 |

These are small sequential samples, not confidence intervals. The first two
Hooked On controls come from the preceding round's sparse-clear and final
public runs; the 116.91-second control was repeated during this round. Hooked
On and Asphalt 3 improve; Tetris is essentially unchanged. The rejected cache's
52-second Asphalt 3 and 76-second Hooked On measurements are not the deployed
result. None of these numbers include Internet/IPFS cold-download time or
subsequent game menus.

The other seven games' first-presentation checks are below. They are not
game-ready timings and do not establish startup wins for every game:

| # | Game | Previous first presentation, seconds | Adopted first presentation, seconds |
| ---: | --- | ---: | ---: |
| 1 | K-Rally | 9.00 | 10.23 |
| 2 | Snakes | 7.94 | 8.43 |
| 3 | Super Miners | 7.84 | 7.33 |
| 4 | Asphalt 2 | 7.88 | 9.52 |
| 5 | Sky Force | 7.52 | 6.76 |
| 6 | Pathway to Glory | 4.31 | 4.14 |
| 7 | Explode Arena | 4.89 | 5.48 |

All eventually show the expected initial game/loading art in the reviewed
screenshots; Sky Force uses the corrected longer visible-content check noted
above. No browser or guest panic is reported in the adopted launch runs.

The first adopted Hooked On run records 8.77 seconds of ARM translation,
0.57 seconds of module emission and 1.33 seconds of module installation. The
corresponding Asphalt 3 figures are 9.31, 0.59 and 1.35 seconds. These counters
are cumulative measured operations, not proof that all of their time could
be subtracted from wall startup by shipping a package.

## Unpaced gameplay regression screen

Compare the adopted artifact with the unchanged public `47bab3744` build, in
Chromium, with a fresh installation each time. Guest audio is enabled (browser
output is muted by the harness). Instruction totals remain zero; all timed
samples run continuously in unpaced route phase zero. No profiler, build or
test suite runs during the measurements. Screenshots at both ends confirm
gameplay. These are short presentation-rate screens, not deterministic
exact-state replays, CPU-only throughput or a new Qt comparison.

Every completed control and adopted-build sample from the final routes is
listed below, in run order within each build:

| # | Game / scene | Previous WASM FPS | Adopted WASM FPS | Window |
| ---: | --- | --- | --- | --- |
| 1 | Hooked On, stationary Costa Rica boat after anchor tutorial | 238.28, 231.05 | 238.01 | 10 s |
| 2 | Tetris, level-one Marathon | 127.59 | 142.17 | 5 s |
| 3 | K-Rally, tutorial track | 153.45 | 194.79 | 3 s |
| 4 | Snakes, opening level / score 200 | 100.53 | 99.39 | 5 s |
| 5 | Super Miners, level-one standing scene | 294.59, 172.09, 348.45 | 214.42, 281.26, 339.87 | 5 s |
| 6 | Asphalt 2, Golf at San Francisco barrier | 134.22, 135.57 | 101.89, 133.50, 140.30 | 10 s |
| 7 | Sky Force, stage-one combat | 323.71 | 318.69 | 2 s |
| 8 | Pathway to Glory, Pantelleria battlefield | 143.62 | 143.00 | 5 s |
| 9 | Explode Arena, first arena | 286.14 | 291.49 | 3 s |
| 10 | Asphalt 3, coastal race / sound YES | 146.72 | 163.07 | 5 s |

No consistent gameplay regression was found. This does **not** establish a
tight statistical bound on small regressions or rare slow runs. Super Miners
varies widely even on the unchanged control. Asphalt 2's first candidate run
was low; its next two matched/exceeded controls, and the earlier build without
the admission fix also produced a 101.95 FPS sample. The cause of these low
runs was not established. They are retained, not discarded or reclassified as
failed harness runs. Snakes, Sky Force and Pathway are within about 0.4–1.6% in
their single pairs; those small differences are not precise speedup estimates.

Dynamic gameplay can travel farther in a faster run; Tetris piece sequences
and Pathway's selected soldier also differ. The screen rules out menus, help
overlays, panics and gross persistent losses, but does not prove equal guest
work instruction by instruction. No audible-output or full-game-completion
claim is made by these muted-browser throughput measurements.

## Validation

The full WASM suite passes **188 tests**. Native CTest passes **3/3** targets.
The page-walk oracle covers both memory models, old/new layouts, the local/global
boundary, aliases, permissions, page removal and address-space switches.
The rejected cache additionally passed five-address-space eviction tests, but
correctness alone did not pass its performance gate. Existing callback and
mapping-observer lifetime cases also pass.
ROM admission tests cover eager flooding, runtime capacity, promotion and reset.

The source-map test compares 10,000 randomized ranges with the old scan and
retains relocation/pruning fixtures. During review, its earlier Release run
was found to have assertions disabled by `NDEBUG`. The test was corrected,
rebuilt and rerun successfully with assertions active; the earlier nominal
pass is not correctness evidence.

## Evidence and reproduction

Raw evidence is retained under
`/home/claude/.scratch/eka-startup-next-20261012/`. Frozen previews keep every
candidate separate from the unchanged public control while comparisons run.
Normal startup uses:

```sh
node src/tests/wasm/startup.mjs URL NEW_OUTPUT GAME 240
```

Unpaced runtime checks use `runtime.mjs` and `routes.json` in the evidence
directory, with screenshots before and after each timed window. Menu-only
runs and guest panics are rejected. A nonzero presentation count alone is
insufficient to accept a gameplay measurement.

Asphalt 3's old fixed-delay input route sometimes remained in language menus.
Those menu FPS values were discarded. Subsequent attempts also encountered
N-Gage `-1000002` launch failures on both the unchanged control and candidates,
and USER 44 on candidate attempts. Earlier audio-work evidence already records
intermittent N-Gage startup failures; this campaign does not establish their
cause. Shorter input steps and explicit screenshot/HUD checks produced real
races with sound enabled. Successful retries do not erase the failures.

Hooked On's introductory help is another important confound. The moving-boat
pair measured 288.40 versus 181.53 FPS, with the control reaching a cheaper help
screen earlier. Keeping the boat stationary did not prevent automatic tutorial
help: the prototype's 10-second trace moved from 160–194 FPS to about 550 FPS
when that appeared. These aggregates are retained but cannot establish steady
gameplay throughput or attribute the entire difference to the mapping cache.
The focused comparison completes the anchor action, enters fishing and returns
to the map before timing. Both ends of the window must show the unobstructed
boat scene; closing the help dialog alone is insufficient.

## Deployment and stopping point

The adopted artifact is now served at `https://claude-laptop.lan:8188/`.
The downloaded WASM hash matches the final rebuild and measured artifact.
A fresh public Hooked On launch completes installation in **79.72 seconds**;
the completion screen and absence of guest errors were checked. Public Snakes
gameplay measures **99.02 FPS**, matching its earlier pair. Policy readback
confirms watchdog 2,000 microseconds, immutable-code mode 3 and memory mode 2.
All 13 private previews from this campaign are stopped; evidence is retained.

The work stops at the user's requested round boundary. This is not a claim
that all possible startup improvements have been exhausted. The three runtime
changes, profiling-reader fix and launch-check correction are committed locally
on `wasm-port`; nothing is pushed. Full scalar results and raw evidence paths
are in [the JSON results](STARTUP_FOLLOWUP_RESULTS.json).

## Prepared-image options (not implemented)

These are proposals, not measured improvements or shipped features.

| # | Artifact | Work it could remove | Cost and limitation |
| ---: | --- | --- | --- |
| 1 | Preinstalled device/game filesystem | Device extraction, SIS setup and repeated N-Gage import | Build and verify one clean installed filesystem per device/game version; download size may exceed compressed installers |
| 2 | Pretranslated ARM-to-WASM modules | EKA2L1 translation and module emission | Key by exact ROM/game bytes, relocation layout, compiler ABI and policy; retain runtime discovery for uncovered code |
| 3 | Shared device image plus per-game installed files and translations | Combines the first two | Most promising target; more packaging/versioning work, with shared ROM content deduplicated across games |
| 4 | Running-emulator checkpoint | Could bypass boot and menus as well | Requires a real portable checkpoint/restore design for kernel state, pointers, threads, timers and browser resources; copying linear memory alone is insufficient |

Start with option 1, then add immutable-ROM translations from option 2 if
translation remains significant. Preserve the current optimizing compiler
policy: this shifts work out of startup rather than weakening runtime code.
Use a clean writable copy/overlay per session so the existing fresh-game
behavior is preserved. A local persistent installation cache is a useful
alternative for repeat visitors, but cannot accelerate a new user's first
installation without also shipping an image.

Evidence: `src/emu/wasm/shell.html:startEmulator` initializes `/data`, installs
the device and SIS, then places `game.n-gage` on drive E for the guest importer.
`asset-cache.ts` caches downloads, not that installed filesystem.
`src/emu/system/src/aot_setup.cpp:initialize` translates the initial ROM module
at runtime; `aot_runtime.cpp:js_instantiate_aot_module` creates a browser WASM
module and binds its exports to the current worker's function table. Those
indices and RAM cache bindings must be reconstructed when loading saved code.
No existing complete browser checkpoint path was found in the system/WASM
frontend sources.

[Emscripten supports separately packaged preload data](https://emscripten.org/docs/porting/files/packaging_files.html).
Shipping WASM still leaves browser compilation to native code. V8 documents
[its compilation pipeline and browser-managed native-code cache](https://v8.dev/docs/wasm-compilation-pipeline);
that cache is not a portable native image to distribute across browsers and
CPU architectures. A downloaded WASM package could also enable browser-managed
caching, but its benefit must be measured on the deployed loading path.
