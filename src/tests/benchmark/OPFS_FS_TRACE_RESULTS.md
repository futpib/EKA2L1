# Full-OPFS startup and file-I/O trace

Source baseline: `wasm-port` at `94b65346d` (2026-10-16). The browser's
production storage policy keeps installed files on memory-backed
WasmFS and offloads only `SYM.ROM` to OPFS. This study used a **private,
diagnostic full-OPFS build** to understand why moving the entire phone to OPFS
made launches slow. The public `.lan` build was not changed.

## Workload and capture

An installed, saved RM-409 phone with nine installed entries was reopened in a
fresh Chromium process per game. Each process used an independent copy of the
same browser profile. The build mounted all of `/data` on WasmFS OPFS; the
installed firmware and packages were unchanged. The visible-screen criterion
was a nonblank screenshot after the first presentation. It often identified an
N-Gage splash or client screen, **not gameplay**. Times include device reopen,
emulator initialization, and launch. The host was shared, and run-to-run
variance was large; the ranking is a way to choose a trace workload, not a
stable performance league table.

| # | Game | Warm reopen to first nonblank screen |
|---:|---|---:|
| 1 | K-Rally | 56.83 s |
| 2 | Tetris | 56.41 s |
| 3 | Hooked On: Creatures of the Deep | 55.79 s |
| 4 | Asphalt 3 | 47.70 s |
| 5 | Sky Force | 44.64 s |
| 6 | Snakes | 43.24 s |
| 7 | Asphalt 2 | 41.38 s |
| 8 | Super Miners | 38.41 s |

K-Rally was the slowest observation and its trace reached the game's sound
selection screen. Hooked On is within 1.04 seconds and was followed through
its actual game scene, so it provides a paired startup/gameplay trace. The
instrumented K-Rally run reached the first visible screen in 48.74 seconds;
the difference from 56.83 seconds shows the measurement noise. Another Hooked
On reopen was 35.33 seconds after a prior
run measured 52-57 seconds, illustrating how much more replication is needed
before attributing a few seconds to a code change.

The diagnostic `EKA2L1_WASM_FS_TRACE` build recorded every dispatched
file-server IPC: normalized and raw opcode, guest process/thread, PC/LR and
module, sync/async send, path or handle, read position and length, result, and
handler duration. A probe around the generated WasmFS OPFS imports recorded
observed host calls on the active worker(s), including path, duration, and
read position/length. Worker evaluation was not exhaustive, so OPFS call
counts are observed counts, not a proof that no call was missed. The probe and
tracer change timing and are not suitable as throughput benchmarks.

Raw captures are in [`opfs-fs-trace/`](opfs-fs-trace/). Reproduce the summaries
with:

```sh
python3 src/tests/wasm/analyze-fs-trace.py \
  src/tests/benchmark/opfs-fs-trace/hooked-startup-guest.tsv.gz /tmp/hooked-startup-summary.json \
  --opfs-json src/tests/benchmark/opfs-fs-trace/hooked-startup-opfs.json.gz
```

## Slowest observed launch: K-Rally

The K-Rally trace recorded 5,626 guest file-server requests before the first
visible game screen, 5,623 synchronous and three asynchronous. **No further
guest file-server requests** appeared during the 30-second post-screen capture.
Its startup cost centers on repeated file existence/metadata checks, not bulk
file reads.

| # | Guest file-server IPC | Calls | Summed handler time |
|---:|---|---:|---:|
| 1 | `entry` | 2,871 | 4.25 s |
| 2 | `file_open` | 301 | 0.93 s |
| 3 | `file_read` | 398 | 0.52 s |

The game probed `C:/Private/A0000BF6/data.pak` and
`D:/Private/A0000BF6/data.pak` 277 times each; both returned not found. It
then used the existing `E:/Private/A0000BF6/data.pak` for about 276
open/size/read/close cycles. Those three paths alone accumulated 2.94 seconds
in the guest handler. A negative lookup cache and coherent file metadata index
directly address this pattern. They must invalidate misses when files are
created or moved between drives.

The mount took 5.33 seconds. The observed host trace contains 20,760 OPFS
calls during mount, 102,539 between guest-trace start and first visible
screen, and 32,142 afterward. The
last phase is almost entirely repeated `get_size_file` and `get_entries` with
no guest file-server calls, consistent with the logged periodic full-tree save
scan. Across the capture, `get_size_file` ran 90,547 times on 10,051 unique
paths (25.98 seconds summed), `read_blob` 22,660 times (18.03 seconds),
`get_child` 33,691 times (7.59 seconds), and `get_entries` 4,879 times (5.31
seconds). The firmware skin still dominated individual physical reads:
13,800 `read_blob` calls and 10.53 seconds summed. K-Rally's
`E:/Private/A0000BF6/data.pak` caused 5,827 OPFS operations (1.39 seconds
summed). A 32 KiB 4 KiB-page replay cache would have fully served 96.70% of
observed host read calls; that is a potential backend-call avoidance rate, not
a speedup measurement. The replay does not invalidate on writes, so this is an
optimistic bound.

## Paired client/game trace: Hooked On

The Hooked On **client startup** guest trace starts just after OPFS mount and
ends 30 seconds after its first visible splash; the host OPFS probe also covers
the mount. It contains 1,583 guest file-server requests (1,574 synchronous;
nine asynchronous). The handler durations below
are sums of calls, not independent wall-clock phases.

| # | Guest file-server IPC | Calls | Summed handler time | Bytes returned |
|---:|---|---:|---:|---:|
| 1 | `file_read` | 635 | 1,018 ms | 684,072 |
| 2 | `entry` | 80 | 316 ms | 0 |
| 3 | `file_open` | 89 | 311 ms | 0 |
| 4 | `is_file_in_rom` | 151 | 208 ms | 0 |
| 5 | `dir_read_packed` | 23 | 48 ms | 0 |
| 6 | `dir_open` | 98 | 43 ms | 0 |

Every recorded guest PC/LR at the file-server boundary resolves to
`euser.dll`, the client stub. This trace therefore cannot name the ultimate
calling DLL. The process name and file path are stronger attribution here.
Many emulator C++ services also call the VFS directly and never appear as
guest file-server IPC.

| # | Observed WasmFS to OPFS operation | Calls | Summed duration |
|---:|---|---:|---:|
| 1 | `get_size_file` | 93,653 | 26.19 s |
| 2 | `read_blob` | 26,502 | 17.95 s |
| 3 | `get_child` | 40,301 | 9.50 s |
| 4 | `get_entries` | 4,933 | 5.16 s |

These durations overlap between requests and workers. In this SDK,
`get_size_file` invokes `fileHandle.getFile()` each time to ask for its size;
93,653 calls touched only 10,076 unique paths. There were 114 `write_access`
calls to 16 paths in this capture. A coherent in-RAM size index has an unusually
good call-reduction opportunity. `get_child` visited 11,407 unique paths in
40,301 calls; it requires a case-aware directory index with both positive and
negative lookup caching. `get_entries` needs cached directory contents and
precise invalidation on create/delete/rename. The read path needs byte caching,
not just metadata caching.

The most expensive individual path was the firmware skin
`Z:/private/10207114/import/200039f5/200039f5.skn`: 13,800 `read_blob`
calls, about 8.95 seconds of accumulated host-call latency, over an offset
span of only about 29 KiB. `akn_skin_server` opens it through the emulator VFS
and parses it via `ro_file_stream`; this path bypasses the guest RFs IPC
entirely. A small whole-file cache or prefetch before the parser would avoid
the repeated Blob `slice().arrayBuffer()` round trips.

A second capture disabled only the private build's startup/periodic full-tree
snapshot scan and advanced through the N-Gage client to Hooked On's underwater
scene. It recorded 4,802 guest file-server requests, including 3,165
`file_read` calls (4.34 seconds summed) and 200 `entry` calls (674 ms). The
game process `COTD` accounted for 3,112 requests and 4.30 seconds of summed
handler time. The game's `C:/Private/2000AFBC/data0000.pak` caused 4,104
`read_blob` calls, about 3.23 seconds summed, concentrated in 240 distinct
4 KiB pages. The scan-off variant still used full OPFS for all file operations;
it is diagnostic only because its save durability is not established.

A replay of the observed host reads through a 4 KiB page LRU found that a
32 KiB cache would have fully served 94.49% of the startup read calls and
93.67% of the game-path read calls; a 1 MiB cache raised these to 96.70% and
95.98%. These are *potential backend-call avoidance rates*, not measured
speedups. The replay does not invalidate on writes, so these are optimistic
bounds. An 8-page cache is enough for the skin file's observed footprint;
larger game files need bounded pages plus sequential read-ahead. The first
misses, metadata work, allocation cost, and write coherence remain.

## Storage design, ordered by measured leverage

1. Keep a RAM metadata index for sizes and file/directory lookup, reconstructed
   once from the saved manifest. Put it in the WasmFS-facing backend or OPFS
   adapter, before `get_size_file` calls `getFile()` and before directory
   lookups reach OPFS; a guest RFs-only cache would miss direct emulator VFS
   users. Update it at the VFS mutation boundary for
   write, truncate, create, delete, rename, and restore. Pin immutable firmware
   metadata; keep mutable drive metadata versioned across all emulator and I/O
   workers rather than relying on an uncoordinated per-worker JavaScript map.
   A size-only prototype can validate the roughly 90,000-call bucket in both
   traced launches before implementing the rest.
2. Stop inventorying the full mounted tree every ten seconds. The current
   `persistent_disk.cpp` scan walks `/data` and issues `lstat` for every entry;
   the launcher calls it on start and periodically. Maintain a dirty-path and
   rename journal, flush manifest deltas below foreground read priority, and
   keep a bounded occasional reconciliation scan for recovery. The two
   scan-off warm Hooked On runs took 32.29 and 32.27 seconds, versus one nearby
   scan-on repeat at 35.33 seconds. Earlier scan-on runs took 52-57 seconds,
   so the small paired gap is suggestive, not a measured speedup guarantee.
3. Cache tiny read-only files whole (start with the skin file), then add a
   bounded page cache for larger packages. Start the page cache at 256 KiB and
   compare 32 KiB and 1 MiB against the same workload; the replay gain beyond
   256 KiB is small. Read-ahead should follow the observed sequential
   511/1,024-byte `data0000.pak` reads. The ROM can remain on OPFS as a large
   file. Do not copy all installed files into RAM to get
   low latency for a few hot pages.
4. Make cache misses asynchronous with respect to the **emulated CPU**: queue
   an owned file request to a dedicated I/O worker, let the guest scheduler run
   another runnable thread, and post the result back to the emulator thread.
   A guest synchronous `SendReceive` blocks only its calling guest thread;
   guest asynchronous requests return with their `TRequestStatus` pending and
   complete later. Preserve order per open handle, cancellation, session
   teardown, descriptor lifetime, and write visibility. The current HLE
   dispatch invokes `process_accepted_msg()` inline, and `file_read()` performs
   `vfs_file->read_file()` before completing the request. Existing
   `ipc_context::move_to_new()` supplies a way to retain an IPC context, but
   completion must occur on the emulator thread after checking its lifetime.
5. Direct emulator VFS callers such as skin loading need an explicit async
   prefetch boundary before their synchronous parser. Making only the guest
   file-server asynchronous would miss the hottest small-file path. A dedicated
   browser worker may use OPFS sync access handles internally while the CPU
   remains free; the browser restricts those handles to dedicated workers
   ([MDN](https://developer.mozilla.org/en-US/docs/Web/API/FileSystemSyncAccessHandle)).
   Emscripten's [WasmFS API](https://emscripten.org/docs/api_reference/Filesystem-API.html)
   still exposes synchronous calls to C++, so guest-level overlap requires an
   explicit suspend/resume boundary above it.

The guest-visible path would look like this:

```text
file_server_read(message):
    if page_cache.contains(message.file, message.offset, message.length):
        copy_cached_bytes_to_guest_descriptor(message)
        complete(message)
        return
    request = retain_message_and_copy_request_parameters(message)
    io_queue.submit(request)
    return_to_guest_scheduler()  # calling guest thread remains pending

io_worker(request):
    read_missing_pages_from_opfs(request)
    post_result_to_emulator_thread(request)

emulator_thread_on_result(request):
    if request.session_and_thread_still_live():
        install_pages_and_copy_to_guest_descriptor(request)
        complete(request.message)
```

The I/O worker must never write a guest descriptor or complete a kernel
message directly. Read requests for the same open handle must observe writes
and seeks in order. A guest synchronous API can still block that *guest
thread* while the emulator runs another; a guest asynchronous API leaves its
request status pending. This is why moving the existing synchronous WasmFS
call to a different browser worker without changing the HLE completion path
would not by itself create guest-level overlap.

The success gate is matched warm startup-to-*gameplay* latency, foreground
read latency and frame pacing while background persistence runs, memory
footprint, and save/reload correctness. An async queue alone cannot shorten a
serial cold read; the index and cache remove the repeated waits.
