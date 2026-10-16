# Selective OPFS storage for installed ROMs

The browser launcher now moves the installed `SYM.ROM` copy to the browser's
Origin Private File System after device and SIS installation. The emulator's
other files stay on memory-backed WasmFS. Chromium stores a small symlink marker
in the IndexedDB device manifest and removes the old IndexedDB ROM revision;
Firefox keeps the existing IndexedDB path because the pinned SDK's OPFS backend
stalled on reopening a file after reload. Existing IndexedDB devices migrate
on their next launch. The ROM copy measured here is 69,947,392 bytes.

An initial attempt to mount all of `/data` on OPFS was rejected. Thousands of
small firmware files made a fresh Snakes launch take 111.86 seconds to its
first presentation and a warm launch 19.23 seconds, compared with 5.62 and
4.97 seconds in the then-current control. WasmFS's OPFS backend also cannot
rename directories, requiring a slow copying fallback during installation.
The selective design avoids that directory and small-file workload.

## Matched selective comparison

Both sides below used the same scratch WASM binary, stock firmware and Snakes
package on Chromium. The control changed only `offloadRom()` to return zero.
Each variant used a fresh browser profile and a cold launch followed by a
saved-device reload. The first-presentation measurements are before confirmed
gameplay and should not be interpreted as gameplay FPS.

| # | Fresh-profile pair | In-memory ROM warm first presentation | OPFS ROM warm first presentation | Warm WASM heap capacity at 10 presentations |
| - | - | -: | -: | - |
| 1 | First pair | 6.67 s | 8.98 s | 442.50 vs 382.69 MiB |
| 2 | Instrumented repeat | 6.92 s | 7.77 s | 442.50 vs 382.63 MiB |

The approximately 60 MiB lower heap capacity on warm reload is consistent
with not restoring the 66.7 MiB ROM copy into WasmFS. Heap capacity is not
process resident memory; Chromium PSS was noisy and does not establish an
equivalent physical-memory saving. The 0.85 to 2.31 second warm-start cost is
real in these pairs, but the spread warns against treating either number as a
stable estimate. The instrumented repeat spent 2.34 seconds restoring the OPFS
device and 2.11 seconds restoring the control; most of that pair's remaining
delay arose between emulator initialization and launch, while preparing
graphics and mapping the ROM.

A separate paced test advanced through the Snakes menus, inspected screenshots
of the actual playfield and then measured ten seconds of gameplay. The matched
control ran at 16.28 presentations/s and the OPFS variant at 15.98. Both
advanced guest time at approximately real time and translated one new compiled
function in the measurement window. One run per variant is insufficient to
resolve a roughly 2% throughput difference; this establishes no large runtime
regression. The OPFS copy happens before gameplay, not on guest memory access.

The standalone storage test passed in Chromium with a 5 MiB ROM fixture:
content checksum survived a reload, the saved manifest referenced OPFS, and
the previous IndexedDB ROM revision was collected. An injected link failure
preserved the original ROM; the next device open removed the unreferenced
OPFS copy. The test also passed in Firefox with its memory-backed/IndexedDB
fallback.

The real Chromium Snakes launcher reached gameplay and saved/reloaded a device
with the 69,947,392-byte ROM offloaded. The public `.lan` control build was not
changed for this test.

Evidence used during development is under `/tmp/eka-opfs-selective-*` and
`/tmp/eka-opfs-gameplay-*`; these are local scratch captures, not release
artifacts. The comparisons isolate the storage policy in a scratch build but
still share a busy host, so startup and small FPS differences require a larger
campaign before making precise performance claims.
