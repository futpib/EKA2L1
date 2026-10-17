# Direct arena residency in Asphalt 3

This follow-up used an isolated Chromium 153 browser and the stock Nokia 5320
device to reach the Asphalt 3 racing HUD. The diagnostic build recorded the
guest process, published arena capacity, committed guest chunk bytes, live C++
allocation pointer, and Linux pagemap residency at the same paused route point.
The capture is at
`/home/claude/.scratch/eka-memory-20261017-current/asphalt-3-arena-lineage/`;
`arena-residency.json` is reproducible with
[`summarize_arena_residency.py`](summarize_arena_residency.py). The original
endpoint profile and full-browser totals are in
[`ASPHALT3_CURRENT_MEMORY_PROFILE.md`](ASPHALT3_CURRENT_MEMORY_PROFILE.md).
The lineage build's WASM SHA-256 is
`fbaf24020a63445beefc9901012f12b31a17d38933f2c037b436762059fb37b6`.
The allocator captured 4,398 releases of at least 1 MiB within its 8,192-entry
history, with no dropped allocations, unknown frees, or truncated stacks.

The 13 live arenas reserve **624 MiB of WASM address space**, but only
**330.87 MiB of their pages are resident** in this run. The emulator has
explicitly cleared and published **160 MiB** of contiguous prefixes. Only
**9.86 MiB** of direct guest chunks are currently committed; this does not
describe earlier writes or every address the unsafe direct fast path may read.
The **170.87 MiB resident tail** lies outside the published prefixes, so it
cannot be explained by the current `initialize_direct_range` clearing alone.

| # | Guest process | Published, MiB | Guest committed, MiB | Resident prefix, MiB | Resident tail, MiB | Resident total, MiB |
|---:|---|---:|---:|---:|---:|---:|
| 1 | ngiplay0x20003b78 | 32 | 0.46 | 32.00 | 16.00 | 48.00 |
| 2 | AknIconSrv | 4 | 0.13 | 4.00 | 44.00 | 48.00 |
| 3 | ecomserver | 8 | 0.68 | 8.00 | 40.00 | 48.00 |
| 4 | cdlserver | 4 | 0.02 | 4.00 | 44.00 | 48.00 |
| 5 | playserver | 16 | 0.41 | 16.00 | 20.16 | 36.16 |
| 6 | baksrvs | 4 | 0.03 | 4.00 | 2.00 | 6.00 |
| 7 | gtmdserver | 8 | 0.05 | 8.00 | 2.00 | 10.00 |
| 8 | NAFServiceManagerSrvr_V3 | 32 | 0.15 | 32.00 | 0.00 | 32.00 |
| 9 | TZSERVER | 8 | 0.02 | 8.00 | 0.00 | 8.00 |
| 10 | wmdrmpkserver | 4 | 0.02 | 4.00 | 0.00 | 4.00 |
| 11 | DownloadMgrServer | 4 | 0.04 | 4.00 | 1.14 | 5.14 |
| 12 | CookieServer | 4 | 0.02 | 4.00 | 0.00 | 4.00 |
| 13 | Asphalt3 | 32 | 7.82 | 32.00 | 1.56 | 33.56 |
| 14 | **Total** | **160** | **9.86** | **160.00** | **170.87** | **330.87** |

The first five arenas contain **164.16 MiB**, or 96% of the resident tail.
Their complete 48 MiB address ranges overlap earlier large allocations that
were freed before each arena was allocated. The latest *tracked large* owners
of those ranges were 191.36 MiB in one `wasmfs::MemoryDataFile::write`
allocation group and 48.64 MiB in another. The overlapping allocations were
released before the arenas appeared; the first group also has other live file
buffers at the race capture. The group stacks include the WasmFS file-write path
used while uploading and installing the ROM, RPKG, and game. This directly
ties the arena addresses to former WasmFS buffers; the release trace does not
track later sub-1-MiB writes, so it cannot prove the exact last writer of
every page. It does explain why charging all resident tail pages to the
current guest process would be misleading.

The ordinary arena policy reserves one stable 48 MiB backing allocation per
process and grows the published prefix geometrically (4, 8, 16, 32, 48 MiB)
in [`process.cpp`](../../emu/mem/src/model/multiple/process.cpp). The
generated fast path reads within the published prefix without a page-table
lookup in [`memory_emission.h`](../../emu/cpu/include/cpu/aot/memory_emission.h).
Zeroing that prefix gives untouched addresses deterministic contents under
the current policy. On this Emscripten SDK, `mprotect` reports success
without changing page protection and `madvise` is ignored, so guest decommit
does not return these WASM pages to the host. Chromium 153 on this machine
does not expose `WebAssembly.Memory.prototype.discard`.

## Fine-growth experiment

A temporary build rounded each required prefix up to 1 MiB instead of
doubling it. It still reserved 624 MiB and reached the same racing HUD without
guest errors. The experiment was reverted after comparison:

| # | Diagnostic route | Prefix, MiB | Resident arena pages, MiB | Whole WASM mapping PSS, MiB | Five-second race FPS |
|---:|---|---:|---:|---:|---:|
| 1 | Baseline, first run | 160 | 342 | 886.00 | 70.15 |
| 2 | Fine growth | 121 | 318 | 884.00 | 32.94 |
| 3 | Baseline, repeat | 160 | 340 | 882.98 | 47.75 |

The finer policy saved 39 MiB of explicit zeroing and about 22–24 MiB of
pages *inside arena allocations*, but the full WASM mapping did not fall
measurably across these runs. Freed, resident WasmFS pages are still part of that
mapping even when the allocator no longer assigns them to an arena. The FPS
samples varied with host load, frequency, and ROM-cache warming; they do not
establish runtime parity for fine growth. It also produced 118 arena growth
events by the race versus 34 for baseline. There is no demonstrated net RAM
benefit to justify graduating it.

The next useful memory experiment is at the **temporary WasmFS file-write
source**: reduce or avoid whole-file copies of the ROM/RPKG/SIS during
installation, then compare whole WASM and browser PSS with the same gameplay
and throughput gates. Reassigning already-dirty pages between arena and
allocator accounting cannot reclaim them.
