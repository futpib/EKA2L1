# Read-only firmware storage results

The installed RM-409 device has 8,989 Z-drive files totaling 120,214,537 bytes.
Only 305 files are at least 64 KiB, but they account for 72,154,740 bytes.
The hot firmware skin in the OPFS trace is about 29 KiB, so it remains in RAM.

The browser moves those large Z files to OPFS after device installation on
supported Chromium builds, using the same copy, size check, RAM-path
backup, and symlink protocol already used for `SYM.ROM`. It preserves the
file's mode and millisecond modification time. The manifest records each
offloaded path and its deterministic target; a reload restores the symlink
and verifies that the target exists. Mutable C/E files and small Z files stay
in WasmFS RAM. The operation is resumable after an interrupted pass because
the completion marker is written only when the traversal finishes.

## Browser measurements

Measurements used a private HTTP server on port 8299 and separate Chromium
profiles. The public `.lan` server remained on its control build. These are
single-run comparisons, so small startup differences are noise, not a claimed
speedup. Linear-memory capacity is the WASM buffer high-water mark, not total
browser RSS.

| # | Build and route | RAM layout | Read-only offload | Observation |
|---:|---|---:|---:|---|
| 1 | Final build, warm Snakes first scene | 14.23 s | 14.22 s | Same screenshot |
| 2 | Final build, Snakes gameplay | 945.50 MiB | 876.63 MiB | 160 frames in 10 s on both, about 16.0 FPS |
| 3 | Earlier cache diagnostic, K-Rally first car scene | 871.13 MiB | 785.44 MiB | Same screenshot, 13.05 vs 12.84 s |
| 4 | Earlier cache diagnostic, K-Rally after 10 s of animation | 871.06 MiB | 879.31 MiB | 318 vs 319 frames; later game allocations erase the capacity difference |

The warm offloaded profile reopened, proving that the manifest and symlinks
retain the layout. The browser persistence fixture
checks a native-written firmware file across offload and reload, including
checksum and exact modification time. The normal ROM save/reload, truncation,
rename, deletion, rollback, and tab-exclusion checks still pass.

A fresh Snakes install with firmware offload moved 142,102,132 read-only
bytes (ROM plus firmware) and reached a visible scene in 14.01 seconds. Its
saved manifest has 305 offloaded Z files totaling 72,154,740 bytes and the
completion marker. That first installation still grew WASM memory to
1,035.88 MiB before the offload could release source buffers; the memory
benefit appears on subsequent launches, not during the initial install.

After removing the option, a fresh Chromium profile loaded Snakes without a
storage parameter, moved the same 142,102,132 bytes, and reached its first
visible scene in 14.26 seconds. Reopening that profile reached the scene in
13.92 seconds; actual gameplay presented 162 frames in 10 seconds (16.19 FPS)
and the WASM heap was 878.50 MiB. The browser persistence fixture also passed
with firmware offload enabled by default.

## Storage-only memory attribution

The existing arena-growth benchmark runs a transient session, which bypasses
`EkaDeviceDisk`; its earlier Asphalt 3 memory captures therefore cannot measure
firmware offload. For this check, two isolated persistent Chromium profiles used
the same WASM binary (`b3b320d8...e387ce`) and the same Snakes gameplay route.
The control used the previous opt-in disk script without its URL switch. Both
profiles reached the gameplay board and presented about 16 FPS. The offloaded
profile had 305 Z-drive links covering 72,154,740 bytes (68.81 MiB).

| # | Warm gameplay measure | RAM firmware | Offloaded firmware | Difference |
| ---: | --- | ---: | ---: | ---: |
| 1 | WASM linear-memory capacity | 943.69 MiB | 874.69 MiB | -69.00 MiB |
| 2 | Resident WASM backing, from Linux `smaps` | 599.34 MiB | 526.89 MiB | -72.45 MiB |
| 3 | Ten-second presented frame rate | 16.19 FPS | 15.99 FPS | -0.20 FPS |
| 4 | All-process browser PSS | 1746.17 MiB | 1554.08 MiB | -192.09 MiB |

Only the WASM-memory reduction is securely attributable to the offload in this
pair. Other Chrome mapping categories changed substantially, including 133 MiB
less file-backed PSS, so the 192 MiB whole-browser difference must not be
reported as a firmware saving. The small FPS difference is within run noise.

A separate source-mapped allocator build replayed the same two warm Snakes
routes with zero dropped records, unknown frees or truncated stacks. The
instrumentation changes absolute memory use, so compare its two runs with each
other, not with the production browser figures above:

| # | Live C++ allocator owner | RAM firmware | Offloaded firmware | Difference |
| ---: | --- | ---: | ---: | ---: |
| 1 | WASMFS file storage | 70.63 MiB | 1.82 MiB | -68.81 MiB |
| 2 | Process arenas | 320.00 MiB | 320.00 MiB | 0 |
| 3 | Guest chunk backing | 246.00 MiB | 246.00 MiB | 0 |
| 4 | ROM and mapped files | 66.71 MiB | 66.71 MiB | 0 |
| 5 | All live C++ allocations | 839.05 MiB | 771.02 MiB | -68.03 MiB |

The WASMFS decrease matches the 68.81 MiB of offloaded Z files. Its difference
from the total C++ decrease comes mostly from small allocations, whose stacks
are not unwound. The production pair's resident-page decrease is slightly
larger because the live allocator totals and page residency are different
measures. Raw screenshots, process maps and allocator reports are in
`/home/claude/.scratch/eka-fresh-storage-{current,control}/` and the matching
`eka-fresh-storage-diagnostic-*` directories.

## Cache and full-OPFS boundary

A diagnostic 256 KiB page cache in the OPFS proxy worker served 3,345 K-Rally
read pages and 1,574 size queries from RAM during one ten-second intro sample.
The paired selective-storage run without that cache delivered 319 frames at
31.89 FPS; the run with it also delivered 319 frames at 31.89 FPS. First
visible times were 13.86 and 14.85 seconds respectively. This does not show
a net benefit for the cache in the selective layout, so it is not in the
browser build.

For comparison, a private *full-OPFS* prototype reached a K-Rally screen in
30.79 seconds with the cache versus 52.49 seconds without it. That layout was
rejected because the SDK's OPFS mount failed a direct file-rename test and a
deleted file remained visible through the mount in both cached and uncached
runs. Installed games need those operations on their writable drives.

The OPFS proxy worker already performs browser storage calls asynchronously,
but the current WasmFS caller waits for them. This change does not defer guest
RFs completion. The selected read-only files have no guest write/rename path,
while the high-frequency small reads remain in RAM, so guest-IPC async work
would not address a measured bottleneck in this layout.
