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
