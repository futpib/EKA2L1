# Compact direct-memory lowering and mapping notifications

All cuts reduce Snakes worker CPU by **8.5% versus cached direct** and **9.6%
versus TLB** in these runs. Sky Force averages **5.1% less worker CPU than cached
direct**, but its observations vary substantially and average **2.8% more than
TLB**. This is not a consistent win over TLB across both games.

This implements the proposed direct-memory cuts on `wasm-port`, starting from
`cde0c5fb6`. The default memory implementation remains the TLB. Select all cuts
in the browser test harness with:

```sh
EKA2L1_MEMORY_IMPL=2 EKA2L1_DIRECT_POLICY=3
```

The new pre-initialization API is `eka2l1_direct_memory_configure(policy)`.
Policies are frozen once guest initialization starts and require direct memory
from boot; delayed activation is rejected. The controls are:

| # | Policy | Behavior |
|---|---:|---|
| 1 | 0 | Original direct implementation |
| 2 | 1 | Previous cached-direct intervention: unchanged generated memory code, view cached by mapping generation/address space, no experimental chain/instruction counters |
| 3 | 2 | Compact generated memory code plus cached view |
| 4 | 3 | Compact generated memory code plus mapping-change notifications and publication at execution boundaries |

Policy 1 reproduces the behavior of research control 3 in
[the earlier attribution experiment](DIRECT_MEMORY_ATTRIBUTION.md), now in the
same binary as the new cuts. This comparison does not reuse historical timings.

## Changes

1. Embed the fixed guest arena start and size in generated ARM/Thumb WASM.
2. Precompute `host_base - guest_base` when preparing a mapping view.
3. Replace the four-field lazy metadata cache with three derived WASM locals:
   address bias, enabled-arena mask and valid descriptor pointer. Load the page
   table pointer only on the fallback path.
4. Establish null-metadata and endian guards at region entry and after helpers,
   using the existing deferred register reload barriers.
5. Omit page-crossing checks when scalar width/alignment proves they cannot
   cross a page. Retain crossing checks for multi-instruction/block spans.
6. Replace per-chain mapping generation/address-space polling with notifications.
   Mapping mutations and address-space switches mark the CPU's view dirty;
   CPU run/step entry and host callback returns publish it before guest execution
   resumes. Unchanged compiled chains just read the published descriptor pointer.

Notifications use weak observer references so retired CPUs cannot leave a
dangling subscriber. Rebuilds are batched until a publication boundary instead
of rebuilding the entire fallback directory for each modified page. Existing
mapping/scheduler ownership still governs mutations; this does not add support
for unsynchronized concurrent remapping.

The hot address calculation is approximately:

```text
# Region entry and helper return:
valid_view = metadata != null and little_endian ? metadata : null
bias = valid_view ? valid_view.bias : unused
mask = valid_view ? valid_view.arena_mask : 0

# Aligned access of WIDTH bytes:
if unsigned(address - ARENA_BEGIN) < (mask & (ARENA_SIZE - WIDTH + 1)):
    host = address + bias
elif valid_view and access_does_not_cross_page:
    host_page = valid_view.pages[address >> 12][read_or_write]
    host = host_page ? host_page + (address & 4095) : null
else:
    host = null

if host:
    access_primary_wasm_memory(host)
else:
    existing_helper_or_deferred_path()
```

The mask is all ones for a usable canonical arena and zero otherwise. This
preserves a single arena range comparison without cloning whole generated
regions. Alignment checks remain where required. The existing experimental
assumption of mapped/permitted accesses inside the arena is unchanged. Aliases
outside the arena use the same C++ backing through the page directory; an
incompatible remapping inside the arena disables its affine shortcut. There
are no synchronization copies or new JS calls in these paths.

## Validation

The Emscripten build succeeds. The complete WASM translator suite passes
**167 tests, zero failures**. The focused `--direct-cuts-only` run repeats
**1,407 memory comparisons for each of policies 1, 2 and 3**, then tests arena
edges, null/endian changes within a generated invocation and publication through
real CPU/MMU entry, address-space switching, system calls, slow-memory and
exception callbacks, permission changes and observer retirement.

Both timed direct policies pass full 60-frame replays in Snakes and Sky Force,
matching the existing native reference exactly for pixels, presentation timing,
guest instruction counts, PCM audio and audio events. These validate the tested
game paths, not every Symbian workload. The LAN deployment is unchanged.

## CPU-time comparison

All configurations use one frozen binary, Chrome 153.0.8010.52 and the stock
5320 device. Each observation starts a fresh browser. The initial campaign runs
TLB, cached direct and all cuts forward, then in reverse order for each game.
An additional forward pass for Sky Force checks the large spread observed in
its first two all-cuts samples; all observations are retained.

Snakes executes 644,728,231 guest instructions and 84 presentations over guest
time 21–25 seconds. Sky Force executes 2,171,043,925 instructions and 192
presentations over 42–48 seconds. Instruction endpoints and presentation journals
match between variants. Sampling, tracing and detailed custom diagnostics are
off. No owned builds, compiler tests, correctness replays or profiles overlap
the timing campaign. There are no mapping-view rebuilds or synchronization
bytes during the measured windows. The removed experimental instruction/chain
counters remain zero; the independent benchmark instruction accounting verifies
identical guest work.

Worker CPU is the scheduler runtime delta of the busiest matched renderer
thread, named `DedicatedWorker` in every observation. Renderer CPU includes
emulation, compilation, graphics and audio. Threadpool CPU sums observed threads
whose names start with `ThreadPool`; it is not a new function-level compiler
profile. CPU samples include the harness's polling margins. CPU time excludes
descheduling but still varies with CPU frequency, caches, JIT decisions and
other host activity. Two or three samples do not establish a narrow confidence
interval.

All 15 timing runs pass the driver checks. CPU values below are seconds;
observations appear in chronological order within each configuration.

| # | Game | Configuration | Worker mean | Worker observations | Renderer mean | Threadpool mean | Host interval mean |
|---|---|---|---:|---|---:|---:|---:|
| 1 | Snakes | TLB | 2.038819 | 2.063902, 2.013736 | 2.450 | 0.256132 | 2.525501 |
| 2 | Snakes | Cached direct | 2.014387 | 1.991972, 2.036803 | 2.810 | 0.631860 | 2.530072 |
| 3 | Snakes | All cuts | 1.844096 | 1.845827, 1.842366 | 2.550 | 0.532331 | 2.273251 |
| 4 | Sky Force | TLB | 9.352855 | 9.101943, 8.965266, 9.991357 | 10.027 | 0.292670 | 10.222278 |
| 5 | Sky Force | Cached direct | 10.132338 | 9.812363, 10.957095, 9.627557 | 10.827 | 0.303211 | 10.972628 |
| 6 | Sky Force | All cuts | 9.613572 | 8.506653, 11.177239, 9.156824 | 10.283 | 0.266583 | 10.471451 |

Snakes improves in both pairs: all cuts use 7.3% / 9.5% less worker CPU than
cached direct and 10.6% / 8.5% less than TLB. Renderer CPU falls 9.3% versus
cached direct, but remains 4.1% above TLB. The observed threadpool cost falls
15.8% versus cached direct and remains approximately twice TLB's. The earlier
Chrome attribution identified WASM compilation as a major source of direct's
extra threadpool work; this campaign does not independently profile that cost.

Sky Force changes versus cached direct are -13.3%, +2.0% and -4.9% worker CPU.
Against TLB they are -6.5%, +24.7% and -8.4%. The middle all-cuts observation
is retained. The initial two-run worker mean was 9.841946 seconds, versus
10.384729 cached direct and 9.033604 TLB; the extra pass does not turn the result
into a consistent TLB win. The source, build hashes, guest work and presentation
journals are identical, but this experiment does not establish the cause of
the timing spread. It would be incorrect to discard the slower observation or
attribute it specifically to the new generated code without more evidence.

The timing comparison measures all six cuts together against cached direct.
It does not assign a speedup to each cut independently. Policy 2 is available
and correctness-tested for a future comparison isolating notification-based
publication from emission changes. The default remains TLB; the other memory
experiments are retained because direct has not demonstrated an overall win.

## Reproduction

Build with the existing Emscripten toolchain and freeze the browser artifacts
into `$BUILD`. Use fresh output directories, stock 5320 assets and the existing
native replay references:

```sh
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j8
node build-wasm/src/tests/aot/test_aot_wasm.js --direct-cuts-only
node build-wasm/src/tests/aot/test_aot_wasm.js

python3 src/tests/benchmark/memory_implementations.py "$OUT/replays" replays \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 2 --direct-policies 1 3 --frames 60
python3 src/tests/benchmark/memory_implementations.py "$OUT/timings" timings \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 0 2 --direct-policies 1 3 --rounds 2
python3 src/tests/benchmark/memory_implementations.py "$OUT/timings-followup" timings \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 0 2 --direct-policies 1 3 \
  --games combat --rounds 1
```

Raw logs, frozen artifacts and individual reports are under
`/home/claude/.scratch/eka-direct-cuts/`. Individual observations, exact commands,
asset hashes and validation provenance are retained in
[the results JSON](DIRECT_MEMORY_CUTS_RESULTS.json). Frozen browser hashes:

```text
eka2l1.js    b947d205b082df89a009926c87a6d7dda68a7988c54dfd7d553bc1dfb7065580
eka2l1.wasm  2a4db572eeaea27f663e004d4e7d67d07d0597cade95ca19c0a3c49c5ffccbbe
```
