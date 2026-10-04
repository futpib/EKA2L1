# Direct shared-backing memory experiment

Mode 2 (`EKA2L1_MEMORY_IMPL=2`) now shares canonical guest backing with C++.
The copying implementation has been removed. The experiment is enabled from
boot; mode 0 remains the default.

## CPU-time results

**Direct did not win. Retain TLB as the default and keep all four modes.**
For identical guest work, direct used **4.3% more busiest-thread CPU in Snakes**
and **17.4% more in Sky Force**. Renderer CPU and wall time also increased.

Each game ran serially in TLB / direct / direct / TLB order. Values are means
of two runs per mode. Snakes executed 644,728,231 instructions and 84
presentations over guest time 21–25 seconds; Sky Force executed 2,171,043,925
instructions and 192 presentations over 42–48 seconds. Endpoints and complete
presentation journals matched within each game.

| # | Game | Mode | Busiest-thread CPU seconds | Renderer CPU seconds | Wall seconds |
|---|---|---|---:|---:|---:|
| 1 | Snakes | TLB | 1.982158 | 2.365 | 2.287280 |
| 2 | Snakes | Direct | 2.066413 | 2.880 | 2.388235 |
| 3 | Sky Force | TLB | 9.320874 | 9.980 | 10.050795 |
| 4 | Sky Force | Direct | 10.946164 | 11.640 | 11.662200 |

The paired thread-CPU changes were +0.5% / +8.0% in Snakes and
+18.3% / +16.5% in Sky Force (positive means more CPU for the same work).
These two repetitions do not establish a precise general effect size, but
they do not justify adopting direct or removing the other experiments.
Renderer CPU increased 21.8% / 16.6%; wall time increased 4.4% / 16.0%.

Thread CPU is Linux scheduler runtime for the busiest matched renderer
thread, named `DedicatedWorker` in every observation. It is not independently
attributed to emulation by a V8 profile in this timing campaign. Renderer
CPU includes emulation, rendering, audio and compilation. CPU samples
bracket resume and observed completion, so they include polling margins.
All renderer totals were complete and all busiest-thread observations
matched; per-thread status and churn are retained in the evidence JSON.

There were zero view rebuilds and zero synchronization bytes during every
measured window. Direct made 178,416 compiled chains in Snakes and
17,110,139 in Sky Force. These timings include the complete implementation,
including its per-chain callbacks and counters; they do not isolate the
cost of address arithmetic from fallback logic, generated-code quality or
runtime bookkeeping. Both games had five live arenas (320 MiB) at the
timing snapshot, plus the 8 MiB fallback table.

This replaces the earlier copying result with an actual zero-copy
measurement. It does not prove that every possible direct-memory layout
or lowering would lose.

## Implementation

Each process using the multiple memory model lazily allocates a 64 MiB arena
for guest addresses `0x00400000..0x043fffff`. Local chunks receive storage in
that arena when created. Both ordinary C++ guest pointers and generated ARM /
Thumb WASM use that same allocation in the runtime's primary shared memory:

```text
if whole_access_fits_process_arena(address, width):
    host = arena_host + (address - arena_guest_begin)
else:
    host = permitted_page_backing(address) + page_offset(address)
load_or_store_primary_wasm_memory(host)
```

The generated code caches arena metadata in WASM locals within a region and
invalidates that cache after a callback. Direct accesses retain span bounds,
alignment and endian checks. There is no page/TLB lookup inside the arena.
Outside it, an 8 MiB read/write page directory resolves canonical backing.
Physical aliases therefore remain coherent without replicating stores.

Mapping-generation or address-space changes rebuild the view. A conflicting
external mapping anywhere in the arena disables its affine shortcut, leaving
the page-directory fallback available. Flexible-model devices currently use
the fallback. This is a bounded direct-memory implementation, not universal
identity addressing across the guest's full 4 GiB address space. The 64 MiB
window size is an experimental choice, not a measured optimum.

There is no second WASM memory, synchronization copy, dirty bitmap or alias
publication loop. Chunk creation clears new/reused storage; that is allocation
initialization, not synchronization with another copy of guest memory. C++
callbacks access the same bytes immediately, without JS memory marshalling.
Per-chain experimental callbacks and counters remain part of measured costs.

As with the replaced unsafe experiment, accesses inside the arena assume valid
mapped data and permissions. Holes and protection faults inside it are not
faithfully emulated. Mode 2 requires unsafe-code mode 3. Each live arena costs
64 MiB of WASM linear address space, in addition to fallback metadata. Failure
to obtain an arena leaves ordinary allocation and translation available.

## Correctness

- 1,407 focused comparisons passed: ARM/Thumb registers, flags, memory, short
  budgets, crossing accesses, high-address aliases, callback remapping,
  permission metadata and address-space transitions.
- Tests use actual process-owned arenas and deliberately clear their page-table
  fallback entries. A generated store is observed by a C++ callback, the callback
  writes a new value, and generated code reads it during the same invocation,
  with no enter/leave synchronization. Separate processes at equal guest
  addresses stay isolated. Chunk reuse clears old data; deleting one process
  leaves another's bytes intact.
- The full compiler suite reported 167 passed, zero failed. Diagnostic-only
  checks remain skipped in this diagnostics-free release build.
- TLB and direct memory each passed 60-frame Snakes and Sky Force replays against
  their existing native references, including all pixels, presentation timing,
  PCM audio, audio events and exact guest instruction counts. Snakes ended at
  3,012,361,007 instructions and Sky Force at 15,827,750,304 instructions.
- All direct runs report zero `bytes_in` and `bytes_out`; the copy routines no
  longer exist. `direct_rebuilds` confirms that arenas were enabled. The final
  `direct_pages` value describes a mapping snapshot, not a measured access-hit
  rate. `arena_bytes` returns to zero after game shutdown.

## Reproduction and evidence

Run from the repository root using the existing stock 5320 assets:

```sh
python3 src/tests/benchmark/memory_implementations.py "$OUT/replays" replays \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 0 2 --frames 60
python3 src/tests/benchmark/memory_implementations.py "$OUT/timings" timings \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 0 2 --rounds 2
```

The driver defaults to enabling direct memory from boot. Its optional delayed
activation switch remains available, but is not used for these measurements.
Profiling and tracing are off during timing. Replays use software rendering;
timings use the existing hardware-GPU configuration. No owned build, replay or
compiler test runs overlap the timing campaign. Background host activity and
CPU-frequency variation are not eliminated by measuring CPU time.

Artifacts are in `/home/claude/.scratch/eka-direct-memory/`. The committed
[evidence JSON](DIRECT_MEMORY_RESULTS.json) contains individual observations,
commands, hashes and correctness results. The [earlier comparison](MEMORY_IMPLEMENTATIONS_RESULTS.md)
preserves historical measurements of the removed copying prototype.

Measured with Chrome 153.0.8010.52. Frozen build hashes:

```text
WASM    60697603b47d210de038eeed0cac91966ec210ae7dc4e64dbc7926ae4a4f3224
Loader  bd8299a8d03577a05f6c67a1bc4c097dbf7ffc9964db7356c37ff17d99a58f82
```
