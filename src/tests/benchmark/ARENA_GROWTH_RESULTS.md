# Stable process arena growth

Measured 2026-10-14 on `wasm-port`, source parent `40c0d4962`, Chromium
153.0.8010.52, i7-10875H, hardware ANGLE/Vulkan, stock Nokia 5320. The experiment
is selected with `EKA2L1_ARENA_GROWTH=1` and remains **off by default**. Normal
launch still allocates the existing 64 MiB process arenas.

## Implementation

Logical capacity starts at 4 MiB and doubles when a new local chunk needs a
larger window, up to the existing 64 MiB address window beginning at
`0x00400000`. Allocations are driven by whole chunk reservations, not app names
or a profile-derived promise that later addresses will never be used. Addresses
outside this window retain separate backing and canonical page translation.

Growth adds a stable backing segment; it does not move existing buffers or copy
live data. A local chunk can contain a native `thread_local_data` object with an
`unordered_map`, and thread metadata and MMF audio code retain native pointers
into local chunks. Moving their bytes would not preserve these objects and
pointers. Old segments therefore remain allocated until process destruction;
unused tails can make physical backing larger than the logical sizing oracle
in [the preceding access distribution](MEMORY_ACCESS_DISTRIBUTION_RESULTS.md).

Each process publishes 64 one-MiB banks with a host bias and contiguous backing
end. The active MMU view copies these on mapping/ASID changes, clears banks with
conflicting physical mappings, and clips whole-span proofs at backing or alias
boundaries. Such accesses use the existing canonical page table. ARM/Thumb
scalar and block-transfer emission, and fused SVC returns, all use this view.
A first-segment arithmetic path avoids bank loads when the whole access fits
that segment. Selection happens before code generation; normal launch retains
its existing emitter. No write-synchronization copies are introduced.

The flag requires memory implementation 2 and unsafe code mode 3. The launcher
validates and reads back the choice; the API rejects changes after guest
initialization. Stats expose allocated arena bytes, cumulative segment
allocations (`arena_growths`) and current live segments (`arena_segments`).

## Method

Runs use fresh Chromium processes and temporary guest filesystems. Installation
is performed normally, including full N-Gage containers, rather than restoring
prepared images. Each game reaches the existing gameplay route; Hooked On's
three ten-second windows exclude its tutorial overlay, and Asphalt 3 must show
the racing HUD. The client reaches Home/My Games; its idle display is not a game
FPS benchmark. Browser audio output is muted while guest audio remains enabled.

The steady windows are unpaced, continuous execution with no route pauses.
Instruction accounting and the diagnostic memory-access probe are disabled.
Frames are emulator presentations per host second. The harness records late
compilation separately and verifies zero guest instruction totals. It samples
C++ allocator usage, WASM linear-memory capacity and whole-Chromium process-tree
PSS after the timing window; shared mappings are not double-counted as summed
RSS. No forced GC is used. Arena backing, browser PSS and WASM capacity measure
different things; these are runtime observations, not sampled startup peaks.

Adjacent baseline/candidate runs are serial. Host frequency and browser tiering
are not locked, so short-window values should not be treated as precise
percentage guarantees. Screenshots and error collections validate the scenes;
results are not a survey of every level or long-running session.

## Measurements and decision

**Retain the flag as an off-by-default experiment.** Runtime arena backing and
browser memory fall, but the primary gameplay comparisons lose about 17–40%
throughput. This does not meet the requirement of little performance impact.

| # | App | Arena backing off / on (MiB) | Browser PSS off / on (MiB) | Unpaced FPS off / on | FPS change |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Snakes | 320 / 65 | 2335 / 2130 | 98.5 / 80.2 | -18.6% |
| 2 | Sky Force | 320 / 81 | 2262 / 2084 | 322.4 / 244.6 | -24.2% |
| 3 | K-Rally | 320 / 81 | 2413 / 2260 | 184.2 / 137.4 | -25.4% |
| 4 | Asphalt 2 | 320 / 81 | 2398 / 2208 | 126.6 / 105.2 | -16.9% |
| 5 | Hooked On | 832 / 196 | 4646 / 4199 | 234.6 / 179.3 | -23.6% |
| 6 | Asphalt 3 | 832 / 199 | 4040 / 3563 | 113.4 / 67.9 | -40.1% |
| 7 | Tetris | 832 / 230 | 4009 / 3601 | 147.5 / 116.9 | -20.7% |
| 8 | Super Miners | 320 / 81 | 2340 / 2146 | 308.6 / 234.3 | -24.1% |
| 9 | N-Gage client | 768 / 164 | 3249 / 2752 | N/A | N/A |

The primary controls are the original frozen build. Hooked On combines three
ten-second windows and uses the largest of their post-window PSS samples.
Asphalt 3 shows the race HUD with audio enabled and Up held, but both captures
remain at **0 km/h**; label this as a stationary race view, not driving.
The N-Gage client column reports memory after a bounded guest-time navigation
route; its idle presentation rate is not a game throughput metric.

Every accepted measured window has **zero segment allocations**. Late emulator
translation/emission/installation totals stay below 30 ms per game window.
The cost persists after growth has stopped. Extra segmented address resolution
and generated-function setup are the likely cause; this campaign does not
provide native instruction-level attribution between those costs.

The initial bank-only prototype also lost about 17–26% in its first four game
comparisons. The added first-segment arithmetic path did not restore near-zero
overhead. Later prototype timings overlapped builds and are screening evidence,
not precision comparisons. Only the final implementation is selectable.

A reverse-order Snakes repeat measured 99.4 FPS for the original control and
72.7 FPS with growth. Short-window exact percentages vary, but the negative
direction repeats.

### New build with growth disabled

| # | Game / attempt | Original build FPS | New build, flag 0 FPS | Change |
| ---: | --- | ---: | ---: | ---: |
| 1 | snakes / c | 99.4 | 74.4 | -25.1% |
| 2 | snakes / d | 98.3 | 97.3 | -1.0% |
| 3 | sky-force / d | 314.6 | 319.0 | +1.4% |

The first default-off Snakes observation was slow and remains recorded.
Its repeat and the Sky Force check match their controls within about 1.5%.
These observations do not establish a systematic default-off slowdown, and
also do not establish a statistical zero-regression guarantee. Public port
8188 retains its previous frozen bundle and default memory policy.

### Rejected attempts and exact artifacts

The initial Asphalt 3 controls remained at language selection, and one Hooked
On candidate stayed in tutorial fishing. Scene gates rejected these before a
valid timing report; bounded menu retries produced the accepted pair. Earlier
client attempts incorrectly used a continuous idle deadline; the client now
uses bounded guest-time steps. All failure reports are preserved in the JSON.

Baseline WASM SHA-256:
`1c40e453036b095c10abd59a74e8b4568adb828db7d4a33afedb8eff851dc43f`.

Candidate WASM SHA-256:
`4a9e81247a34681aa81832a6cee828a3602a76a2b68e32261bed61d845e5beed`.

The runtime source patch is retained in the raw artifact directory as
`final-runtime-source.patch`, SHA-256
`edd8e872637f29ac65bb1d9e35618b7f68930be2ad40fceab802964bd546606e`.

## Correctness and reproduction

- WASM compiler suite: **190 passed, zero failed**, including stable native
  objects/pointers, process isolation, actual ARM/Thumb generated loads/stores,
  conflicting aliases and block transfers crossing different segment backings.
  The existing independently labeled crash XFAIL remains unchanged.
- Native CPU suite: **915 assertions in 37 cases**, all passing.
- Launcher tests cover default-off selection, invalid settings, incompatible
  backends, configuration failures, readback and policy-dependent asset caching.

Use the existing WASM build configuration with `EKA2L1_MEMORY_ACCESS_PROFILE=OFF`:

```sh
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j4
node build-wasm/src/tests/aot/test_aot_wasm.js
node src/tests/wasm/compiler-policy.test.ts
```

Start private HTTPS servers using `src/tests/wasm/serve.ts`, identical TLS,
assets and frozen build bundles; set `EKA2L1_ARENA_GROWTH=1` only on the
experimental server. Run each route once per server, serially:

```sh
node src/tests/wasm/arena-growth-benchmark.mjs snakes /tmp/arena-off-run BASELINE_URL
node src/tests/wasm/arena-growth-benchmark.mjs snakes /tmp/arena-on-run EXPERIMENT_URL
```

The harness accepts `EKA2L1_ASSET_DIR` and `EKA2L1_CHROMIUM`. Routes include all
eight supported S60 3rd games and the N-Gage client. Compact measurements are in
[ARENA_GROWTH_RESULTS.json](ARENA_GROWTH_RESULTS.json); full browser logs,
screenshots, timing windows, frozen bundles and rejected attempts remain under
`/home/claude/.scratch/eka-arena-growth-20261014`.
