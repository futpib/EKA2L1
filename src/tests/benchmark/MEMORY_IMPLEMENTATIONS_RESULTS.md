# Four guest-memory implementations

Mode 2 has since been replaced by a shared-backing direct-memory experiment.
See [the zero-copy rerun](DIRECT_MEMORY_RESULTS.md) for its implementation and
measurements. The results below are historical and describe the removed copying
prototype at commit `6f03de309`.

## Historical results

**Retain mode 0.** Allocation ranges consistently regressed. The full page table
showed a small Snakes improvement but no consistent Sky Force gain, and consumed
more renderer CPU overall. The identity copying prototype was decisively slower;
this says nothing conclusive about a future shared-backing, zero-copy redesign.

The extended panel ran modes 0, 1, 3, 3, 1, 0 serially for each game. Each Snakes
run executed 644,728,231 guest instructions and 84 presentations; each Sky Force
run executed 2,171,043,925 instructions and 192 presentations. All endpoints and
presentation journals matched within each workload. Values below are means of
two runs. Positive throughput change means faster, using busiest-thread CPU.

| # | Game | Implementation | Thread CPU seconds | Renderer CPU seconds | Wall seconds | Thread throughput vs TLB |
|---|---|---|---:|---:|---:|---:|
| 1 | Snakes | TLB | 1.988281 | 2.380 | 2.30938 | +0.0% |
| 2 | Snakes | Allocation ranges | 2.223865 | 3.010 | 2.53187 | -10.6% |
| 3 | Snakes | Full page table | 1.905750 | 2.510 | 2.20904 | +4.3% |
| 4 | Sky Force | TLB | 9.422375 | 10.110 | 10.14826 | +0.0% |
| 5 | Sky Force | Allocation ranges | 10.782808 | 11.435 | 11.49715 | -12.6% |
| 6 | Sky Force | Full page table | 9.973620 | 10.595 | 10.68665 | -5.5% |

Snakes full-page-table gains were +3.6% and +5.1% in the corresponding forward
and reverse comparisons. Sky Force changes were -11.7% and +0.9%; its TLB
controls varied by about 9.9% in thread CPU time. Two repetitions do not support
a precise general speedup claim. Renderer CPU also includes background compiler,
audio and rendering workers; its increase should not be hidden by reporting only
the busiest thread.

These are complete implementation costs. The experimental views add entry/exit
callbacks and bookkeeping per compiled chain. Sky Force made about 17.1 million
chains during its window, versus about 179 thousand in Snakes, with no view
rebuilds in either window. Boundary bookkeeping is a plausible contributor to
the different results, not an isolated or profiled attribution of the slowdown.

The common short panel ran all four implementations over a nominal 100 ms of
guest gameplay: 16,019,994 instructions/two presentations for Snakes and
30,023,010 instructions/three presentations for Sky Force. This single short
sample quantifies the copying regression; it should not rank small differences.

| # | Game | Implementation | Thread CPU seconds | Renderer CPU seconds | Wall seconds |
|---|---|---|---:|---:|---:|
| 1 | Snakes | TLB | 0.088520 | 0.100 | 0.092915 |
| 2 | Snakes | Allocation ranges | 0.075984 | 0.080 | 0.080190 |
| 3 | Snakes | Identity + copies | 4.242420 | 5.030 | 4.266200 |
| 4 | Snakes | Full page table | 0.056220 | 0.080 | 0.061345 |
| 5 | Sky Force | TLB | 0.178508 | 0.220 | 0.176715 |
| 6 | Sky Force | Allocation ranges | 0.182857 | 0.220 | 0.177460 |
| 7 | Sky Force | Identity + copies | 64.087831 | 65.110 | 64.197200 |
| 8 | Sky Force | Full page table | 0.163157 | 0.210 | 0.162020 |

Identity used 47.9x the TLB thread CPU in Snakes and 359.0x in Sky Force.
During the measured windows it copied 45.6 GB / 1.67 TB into guest memory,
respectively, plus 79 MB / 1.97 GB back to C++ backing. These are decimal bytes;
boot and the initial mapping copy were outside the measured windows.

All individual observations, commands, hashes, correctness comparisons and
counter-quality flags are in [the evidence JSON](MEMORY_IMPLEMENTATIONS_RESULTS.json).
Complete per-thread snapshots and browser logs remain in the local artifact
directory listed below.

All modes are experimental except mode 0, which remains the default. The
alternatives change generated ARM and Thumb memory accesses; interpreter and
fallback accesses still use the existing memory subsystem.

| # | Mode | Implementation | Backing and translation metadata |
|---|---:|---|---|
| 1 | 0 | Existing 512-entry software TLB | Existing C++ backing and tagged TLB entries |
| 2 | 1 | Contiguous allocation ranges | Existing backing; four cached WASM locals and a 16 MiB page-indexed range directory |
| 3 | 2 | Identity-addressed memory, copying prototype | Separate 4 GiB WASM memory; 4 MiB alias directory and 1 MiB dirty-page bitmap |
| 4 | 3 | Full page translation table | Existing backing; 8 MiB table with separate read/write bases per guest page |

Mode 1 caches the containing contiguous guest/backing range. A hit uses range
bounds, permissions and base arithmetic. A miss finds the range by guest page.
Adjacent pages merge only when guest addresses, backing addresses and permissions
are contiguous and identical. This is a general allocation/range implementation,
not a special case for EUser.dll or either game.

Mode 3 removes finite-TLB indexing and tag collisions. It directly indexes a
read/write translation entry by the 20-bit guest page number. Both modes preserve
alignment, endian, permission and fallback behavior, and rebuild the active view
when the address space or mapping generation changes. They share the canonical
backing with C++; no memory mirroring is needed.
The experimental views require 4 KiB guest pages and cache one address space per
CPU; changing address spaces rebuilds that view rather than retaining every
process's table. Rebuild costs are included in measurements.

## What mode 2 does and does not establish

Mode 2 emits loads and stores directly into memory 1 using guest virtual
addresses. CPU state, mapping metadata and the existing C++ runtime remain in
memory 0. Ordinary C++ pointers cannot select memory 1. The prototype therefore
refreshes mutable guest ranges on entry and publishes dirty pages on exit from a
compiled chain or around a C++ memory callback. Bulk copies are WASM-to-WASM
calls, not JavaScript calls. Stores propagate physical aliases immediately so
loads through another guest alias in the same compiled region see the write.

These synchronization costs are included in timing. **This is not a benchmark
of a zero-copy identity-memory architecture.** Such an architecture requires
changing the C++ guest-memory access API and its raw-pointer consumers, or
reworking the common memory layout. Process mappings, aliases and direct host
buffer users also need a coherent representation. The copying prototype cannot
establish whether that larger redesign would win.

Mode 2 deliberately assumes valid mapped accesses and immutable ROM/read-only
contents; it does not reproduce guest permission faults. The ROM loader supplies
the actual immutable backing extent, because this emulator maps ROM as RWX.
Concurrent writes to canonical backing during a compiled chain are unsupported.
Partial-page physical aliases are rejected. These restrictions are additional to
the existing unsafe executable-byte policy; all benchmark modes use policy 3.

The optional identity activation time boots with paired TLB translations, then
swaps function-table entries to their identity translations before measurement.
It does not branch between implementations at each guest load. Replays and
timings report the actual activation time. Identity gameplay validation therefore
does not establish identity-mode correctness throughout boot.

## Reproduction

Select a mode before initialization with `EKA2L1_MEMORY_IMPL=0..3` in either
browser harness, or `eka2l1_memory_impl_configure(mode)` in the WASM API. The
configuration is frozen once the emulator is initialized. The browser API also
exposes mode readback and boundary/copy statistics. Experiments require AOT
regions and reject verifier mode. Mode 2 requires unsafe-code policy 3.

The serial driver records exact arguments, environment, asset/input/binary
hashes and reports; it rejects changing binaries, guest instruction totals or
presentation journals. CPU tracing, sampling and detailed diagnostics are off for
throughput runs. Correctness runs compare pixels, guest timing and PCM/events to
the existing native references.

```sh
python3 src/tests/benchmark/memory_implementations.py /tmp/memory-replays replays \
  --build /path/to/frozen-wasm-build \
  --snakes-assets /path/to/snakes-assets --sky-assets /path/to/sky-assets \
  --reference-root /path/to/native-references --frames 3

python3 src/tests/benchmark/memory_implementations.py /tmp/memory-short timings \
  --build /path/to/frozen-wasm-build \
  --snakes-assets /path/to/snakes-assets --sky-assets /path/to/sky-assets \
  --reference-root /path/to/native-references --window-us 100000 --rounds 1

python3 src/tests/benchmark/memory_implementations.py /tmp/memory-long timings \
  --build /path/to/frozen-wasm-build \
  --snakes-assets /path/to/snakes-assets --sky-assets /path/to/sky-assets \
  --reference-root /path/to/native-references --modes 0 1 3 --rounds 2
```

The frozen build directory must contain `eka2l1.js`, `eka2l1.wasm`,
`eka2l1.data`, `eka2l1.html`, `audio.js` and `audio-worklet.js`. Reference
subdirectories are `replay-standard` and `replay-combat`. Outputs must not exist.
Use `--frames 60 --modes 0 1 3` for full replays; the copying mode is prohibitively
slow over the full Sky Force replay. Short reference prefixes preserve the native
pixels and events and reproduce the audio backend's terminal partial-quantum
silence padding.

The default identity activation lead is 1,000 guest microseconds. The earlier
60-frame Snakes identity replay used a one-second lead. The measured windows begin
at 21 seconds in Snakes and 42 seconds in Sky Force. The short panel advances
100 ms in every mode; extended panels advance four and six seconds respectively.

CPU time means Linux scheduler runtime for the busiest observed renderer thread;
it excludes descheduling but still varies with frequency and cache contention.
Thread identity is not independently attributed by a profiler in these timing
runs. Renderer-process CPU totals and wall time are also retained. Short windows
are unsuitable for resolving small differences; extended repeats assess the
practical alternatives. No owned build, correctness replay or profiler overlaps
the serial timing campaign. Other host activity is uncontrolled.

## Validation and provenance

The focused WASM suite passed 1,397 comparisons covering ARM/Thumb scalar and
block transfers, the EUser queue loop, instruction budgets, unaligned and crossing
accesses, high guest addresses, aliases, callback remapping, address-space changes
and permission metadata. Generated state/memory was compared with the baseline
and an independent DynCom interpreter. Mode 2's deliberately omitted permission
faults are not covered by a claim of equivalence. The existing AOT suite passed
167 tests earlier in this implementation campaign. Six existing CPU-counter and
profiler harness tests passed as well.

On the final browser binary, modes 0, 1 and 3 matched all 60 unique native
reference frames, instruction timestamps and audio for both games. Mode 2 matched
60 Snakes frames after activation at 20 seconds. All four modes then matched
three native frames plus audio in both games, with identity activation at
20.999/41.999 seconds. The full Sky Force identity replay was stopped because
copying made it impractical; it is not reported as a correctness pass.

The short-reference comparison initially failed on baseline audio alone. The
first attempt truncated a longer PCM capture without reproducing the backend's
silence padding for the final partial 10 ms quantum. Correcting the reference
prefix preparation, as specified by `export_clocked_audio`, made all eight prefix
comparisons exact. No pixel, event or audio tolerance was introduced.

The short Sky Force identity measurement completed, but the initial driver
rejected it because one Chrome `ThreadPoolForeg` thread retired. Its renderer
process totals were complete and the busiest worker's lifetime/counters matched.
That completed report was retained after auditing its counters, instructions,
binary hash and presentation journal. The missing-thread record is preserved;
no missing counter is interpreted as zero. The driver now permits recorded pool
thread retirement while rejecting disappearing non-pool threads.

All final gameplay checks and timings use the same Release WASM binary with
custom diagnostics disabled:

- WASM SHA-256: `742a3e3c6908b7dfb13fab770724c08e20f0ca6182ef919fb9a4ab19f4629698`
- Loader SHA-256: `ea62427d9173188b1117b726f341533873562466070e33f780ac1bc42ae908f1`
- Base commit: `444d6e69673dac2baeb3d1ce2810d8c6a1ba5948` plus this change.
- Emscripten 4.0.10; Chromium 153; Intel Core i7-10875H.
- Timing GPU: NVIDIA Quadro T1000 through ANGLE/Vulkan.
- Local artifacts: `/home/claude/.scratch/eka-memory-implementations/`.

Early incomplete copy-all launches and launcher/reporting failures are retained
in that artifact directory and excluded from the result panels. The build and
live LAN server were kept separate; this experiment did not deploy a new server
build or change the production default.
