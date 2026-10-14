# Contiguous process arena prefix growth

This replaces the segmented implementation at `24a6c7560`. The existing
`EKA2L1_ARENA_GROWTH=1` flag selects the new implementation and stays **off by
default**. The [segmented measurements](SEGMENTED_ARENA_GROWTH_RESULTS.md) are
preserved, but its bank tables, backing segments and additional scratch locals
have been removed from executable code.

## Implementation and limits

Each process owns one contiguous 64 MiB reservation in the primary WASM memory.
The experiment uses aligned allocation without clearing the whole reservation;
it initializes a prefix starting at 4 MiB, doubling when a new guest chunk needs
more room, up to 64 MiB. It never moves existing bytes or retained native
pointers. Native TLS objects, thread metadata and MMF audio pointers therefore
keep their addresses. Process destruction frees the reservation.

The cold allocation path implements the proposed structure:

```text
if requested chunk fits the initialized prefix:
    return reservation_base + guest_offset
elif requested chunk fits the 64 MiB maximum:
    zero the newly added prefix
    publish its larger limit
    return reservation_base + guest_offset
else:
    use separately backed guest mappings
```

Growth happens when the guest allocates a chunk, before its native host pointer
is returned, rather than treating an arbitrary ARM read/write as an allocation.
Every legitimate arena-backed chunk already fits the prefix by the time it is
executed. Generated memory accesses use one cached limit and bias; misses use
the existing canonical page table and fault/helper paths. Conflicting aliases
disable the affine shortcut exactly as in the default implementation.
MMU-view preparation caches a conservative start limit for a 64-byte access,
covering scalars, the specialized EUser list scanner and ARM register transfers.
Those hot accesses need only an unsigned 32-bit comparison and the cached bias.
Function entry merely reloads the existing three derived locals, with no new
limit arithmetic. Smaller accesses in the final 63 bytes use canonical page
translation. Larger entry-proved spans retain a widened whole-span proof so
arithmetic cannot wrap. There are no bank lookups or added scratch locals.
The per-access widening and per-function adjustment tried in intermediate
contiguous prototypes have both been removed from ordinary accesses.

**This reduces initialization and potentially physical browser RAM, not the
address reservation.** It still consumes 64 MiB of WASM allocator space per
process and does not increase the number of processes that can fit in the
WASM address-space maximum. WASM linear memory cannot be shrunk by this feature.
The published stats distinguish `arena_bytes` (reservation bytes) from
`arena_initialized_bytes` (sum of initialized prefixes); `arena_growths` counts
prefix initialization/expansion events, including initial activation.
Initialized bytes are not a measurement of resident RAM; PSS is measured
separately. Default mode reports the full reservation as initialized.

Emscripten's actual anonymous-mmap implementation in the installed SDK
(`system/lib/libc/emscripten_mmap.c`) uses aligned allocation followed by
`memset` of the entire mapping. The experiment avoids that eager clearing.
Fresh guest chunks continue to clear their own reservation, preserving zero
initialization and clean chunk reuse. Host protection calls are already no-ops
in this WASM backend; canonical guest mapping permissions and existing unsafe
mode requirements remain unchanged.

## Validation and measurement method

The compiler suite passes **190 tests with zero failures**, including prefix
growth to the maximum, stable native `unordered_map` objects and pointers,
process isolation, contiguous ARM/Thumb loads/stores and cross-chunk block
transfers, conflicting aliases, out-of-window allocation and destruction
accounting. Native CPU tests pass **915 assertions in 37 cases**. Launcher
configuration/cache/failure-propagation tests pass.
The previously documented crash-reproduction fixture remains a separately
labeled XFAIL; it is not a new passing test.

Browser comparisons use the **same frozen candidate bundle** with the flag
explicitly 0 or 1. Runs are serial, fresh browsers and temporary guest disks,
with normal installation and the existing gameplay routes. Order alternates
between off/on and on/off across games. Diagnostic memory-access instrumentation
and instruction accounting are off. Guest audio remains enabled; browser output
is muted. Eight games reach gameplay, while the N-Gage client exercises Home
and My Games in a bounded guest-time route rather than an FPS benchmark.
Asphalt 3 is a stationary race at 0 km/h with sound enabled, not moving-car
throughput. Final K-Rally captures start at 0 mph and reach 50 mph under held
acceleration without steering; the final on-run leaves the road. Earlier
prototype captures remained stationary. Hooked On measures the boat map after
dismissing tutorial fishing. These routes exercise gameplay but do not establish
bit-identical guest work across two different host-time windows.

Game windows run continuously and unpaced, with no route pauses inside timing.
FPS is emulator presentations per host second. Late emulator compilation is
recorded separately. Whole-Chromium process-tree PSS is sampled after each window;
shared pages are not double-counted as summed RSS. These are runtime samples,
not captured startup memory peaks. No forced GC or host-frequency locking is
used. Short-window differences are observations rather than precise guarantees.
Screenshots and runtime error collections gate valid captures.

## Final measurements and disposition

**Physical-memory savings are consistent; the throughput effect is not
established.** Keep the experiment off by default. The single frozen final
WASM artifact has SHA-256
`d0a8217c6472512970895312e96a9b8df6b3193031f835ab27669ced26f7a469`.
Its runtime-source patch against `24a6c7560` has SHA-256
`3e290af682fa531c80ca894b90e520a103c076bc68f7d6748910687dccbada9f`.
All eight games have successful captures in both modes, and the N-Gage client
reaches Home and My Games in both modes. Successful captures record no runtime
errors, zero instruction accounting, and the requested frozen arena policy.
This is gameplay smoke coverage, not an exact full-state/image/PCM replay.

These are **observed FPS differences, not attributed speedups**. Host clocks
and workloads vary. Hooked On FPS combines its three consecutive windows by
total frames / total host seconds; its PSS column uses the final window.
All other game rows use one window. Arena counters are the final sample.

| # | Game | Off FPS | On FPS | Observed change | Browser PSS off / on (MiB) | Reserved arenas (MiB), both | Initialized off / on (MiB) |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | Snakes | 74.61 | 98.71 | +32.30% | 2327.0 / 2107.2 | 320 | 320 / 64 |
| 2 | Sky Force | 252.57 | 311.53 | +23.34% | 2264.5 / 2082.4 | 320 | 320 / 80 |
| 3 | K-Rally | 198.31 | 192.70 | -2.83% | 2442.7 / 2233.0 | 320 | 320 / 80 |
| 4 | Asphalt 2 | 135.39 | 133.89 | -1.11% | 2399.1 / 2180.9 | 320 | 320 / 80 |
| 5 | Hooked On | 139.57 | 177.84 | +27.42% | 4513.7 / 3872.1 | 832 | 832 / 192 |
| 6 | Asphalt 3 | 59.31 | 83.95 | +41.54% | 3969.0 / 3299.8 | 832 | 832 / 192 |
| 7 | Tetris | 68.71 | 114.96 | +67.30% | 3987.8 / 3439.9 | 832 | 832 / 224 |
| 8 | Super Miners | 315.92 | 261.47 | -17.24% | 2319.7 / 2093.0 | 320 | 320 / 80 |

N-Gage's bounded ten-guest-second window takes 0.269 / 0.282 host seconds
off/on; it is not an FPS benchmark. Browser PSS is 3021.7 / 2645.5 MiB,
reservation is 768 MiB in both modes, and initialized prefixes are 768 / 160
MiB. WASM capacity happens to be 1809.3 / 1745.2 MiB in those captures; this is
allocator/layout variation, not elimination of the per-process reservation.
For the five-arena games capacity is mostly identical (1194 or 1202 MiB);
Super Miners' first on capture is 1204 MiB versus 1194 MiB off. Exact capacities
and allocator bytes are retained in JSON.

None of the successful final game/client windows, including the repeats,
expands an arena. The prefix-growth work occurs during setup. Late emulator
compilation remains inside the FPS windows: primary totals are Snakes 95 / 101,
Sky Force 11 / 25, K-Rally 98 / 122, Asphalt 2 100 / 84, and Super Miners 0 / 0
functions off/on. Remaining counts and translation/emission/installation times
are retained per window in JSON rather than treated as guest execution time.

The conspicuous Super Miners loss and Tetris gain were repeated in reverse
order with the same artifact and fresh guest disks:

| # | Game | Repeat off FPS | Repeat on FPS | Observed change | Browser PSS saved (MiB) |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Super Miners | 69.66 | 87.27 | +25.29% | 258.8 |
| 2 | Tetris | 138.82 | 138.21 | -0.44% | 675.8 |

The reversal is not evidence of a Super Miners speedup. Its repeat off-run's
median CPU-clock endpoint snapshots are 1.4 / 1.4 GHz, versus 3.2 / 2.6 GHz on;
its primary endpoints are 3.6 / 3.9 GHz off and 3.0 / 2.9 GHz on. Tetris's primary
endpoints are 2.1 / 2.5 GHz off and 3.6 / 4.0 GHz on; repeat endpoints are
3.6 / 2.9 GHz off and 3.6 / 2.5 GHz on. These are snapshots across CPU cores,
not interval-weighted measurements of the guest worker. They cannot support
frequency-normalized speedup estimates. The host also runs unrelated QEMU/Java
work, recorded outside timing; no host settings or other sessions were changed.
The measurements therefore neither prove a runtime speedup nor guarantee no
regression. Across the primary captures browser PSS is consistently lower by
182–669 MiB; both repeats retain the memory reduction.

## Retained failures and intermediate prototypes

The first final growth-enabled Asphalt 3 launch crashes after accepting English,
with a null-pointer read at guest PC `0x74e09eb8`, address `0x8`, followed by
KERN-EXEC 3. It produces no valid timing window. The unchanged final artifact
and route succeed on a fresh retry, reaching the stationary racing HUD with
sound enabled and no recorded runtime errors. The rejected launch remains in
`asphalt-3-on-final/failure.json`; the valid timing is in
`asphalt-3-on-final-retry/instant-play-stationary-sound-on.json`.
Its cause remains unresolved; a successful retry does not erase the failure or
establish launch reliability. Earlier control captures also experienced an
Asphalt startup panic, but that is not proof this failure is unrelated to growth.

Earlier contiguous prototypes are retained separately in the same evidence
directory and compact JSON. The `*-a` prototype widens each access proof;
`*-b` caches adjusted limits but computes them at function entry. `*-c` captures
repeat selected `b` workloads and its corrected Asphalt route. Apparent K-Rally
and Asphalt 2 gains reverse in later pairs. Their percentages are not gains of
the final runtime. The final implementation moves limit derivation to MMU-view
rebuilds, uses one conservative 64-byte bound for ordinary generated accesses,
and exact 4/8-byte bounds for fused syscall returns.

The first full-suite checks of that final bound exposed a manual syscall-return
fixture that constructed only the old mask. Its independent expected register,
fault and progress assertions are preserved; the fixture now initializes the
new derived return limits, as production view preparation does. The corrected
full suite passes 190 tests. Earlier failed test logs and failed Asphalt attempts
remain under the evidence directory. The old segmented report/JSON are preserved
under `SEGMENTED_ARENA_GROWTH_RESULTS.*`; its implementation is removed.

## Reproduction and evidence

Build with `EKA2L1_MEMORY_ACCESS_PROFILE=OFF` using the existing WASM configuration:

```sh
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j4
node build-wasm/src/tests/aot/test_aot_wasm.js
node src/tests/wasm/compiler-policy.test.ts
```

Start identical private HTTPS `serve.ts` instances against a frozen bundle,
with `EKA2L1_ARENA_GROWTH=0` and `1` respectively. Then run routes serially:

```sh
node src/tests/wasm/arena-growth-benchmark.mjs snakes /tmp/arena-off-run OFF_URL
node src/tests/wasm/arena-growth-benchmark.mjs snakes /tmp/arena-on-run ON_URL
```

Compact measurements are in [ARENA_GROWTH_RESULTS.json](ARENA_GROWTH_RESULTS.json).
Full logs, rejected attempts, screenshots and the frozen bundle remain under
`/home/claude/.scratch/eka-contiguous-growth-20261014`. The public port 8188
bundle and default-off policy are unchanged by this experiment.
