# Persistent browser devices

The launcher now restores a saved virtual device before emulator initialization.
Applications, game saves, C/E drives, device definitions, registries and OS
files persist in the browser's IndexedDB. Games using the same firmware share
one device; different ROM/RPKG hashes select separate devices. Emulator updates
do not change that identity. **Load my own files** uses the uploaded firmware's
identity or the stock saved device when no new firmware is supplied.

Installed package hashes prevent automatic reinstallation. A prepared image
seeds an empty device once. When a phone already exists, selecting Hooked On
uses its ordinary importer if needed, rather than restoring an image over
other applications and saves.

## Storage and execution

Normal file access retains the existing in-memory WasmFS backend. A separate
native worker flushes libc buffers and inventories `/data`; saving does not
add work to guest instructions, emulated memory stores or file reads.
Filesystem write revisions identify actual C++ writes, including same-size
changes with preserved timestamps. The SDK's bulk JavaScript write API also
notifies the tracker. Metadata checks include inode, size, mode and modification
time. Writes during capture are retried. Recently modified timestamps receive
an additional check to cover the clock's millisecond granularity.

The normal launcher saves asynchronously every ten seconds and when the page
becomes hidden. **Save device** requests a fresh save after any in-flight save;
**Play** shuts down the emulator, closes its handles and commits before
navigating to another game. N-Gage saves begin after a successful installer
thread exit, detected by a native kernel callback, not by parsing log output.
Cancellation during installation leaves the preceding saved phone intact.

File revisions are written in bounded batches with strict IndexedDB transaction
durability. Only after every new file has committed does a transaction publish
the new complete manifest. Readers use exactly the file revisions referenced
there. A failed save retains the preceding manifest; unreachable staged files
are removed. Restore checks referenced files before garbage collection and
preserves permissions and modification times. A Web Lock prevents two tabs from
writing the same phone.

The SDK's `FS.readFile` retains a native allocation. Production capture uses
bounded `FS.read` chunks, which release those temporary allocations, rather
than leaking a second device's worth of contents into the 2 GiB WASM heap.
Unchanged saves perform no file-data or manifest writes. This original
measurement used an entirely in-memory filesystem. The Chromium launcher in
this source revision [moves the installed ROM copy to OPFS](OPFS_ROM_RESULTS.md);
other files retain the measured in-memory path.

## Verification

The standalone browser fixture uses the production capture/storage code and
the same threaded WasmFS, with real C++ stdio and POSIX operations. It passes in
Chromium and Firefox:

- Native writes and open libc-buffered writes survive reload.
- Empty files and a multi-chunk 5 MiB file restore exactly.
- Same-size C++ writes survive even when their old timestamp is restored.
- JavaScript bulk writes, renames, deletion and truncation survive reload.
- Modification times survive restoration.
- An injected manifest-commit failure leaves the previous saved bytes intact.
- Firmware profiles are isolated, and a second writer tab is rejected.

The test fixture's initial 64 KiB stack was too small for its deliberately large
stdio buffers; it uses a 1 MiB checked stack now. The production emulator retains
its existing 4 MiB stack. An early fixture server wrongly injected emulator
launcher policy into a standalone filesystem page; its dedicated loopback
server serves the real fixture without that unrelated wrapper. These setup
failures are excluded from performance observations.

Hardware-rendered Chromium launcher checks use a reusable browser profile on a
private HTTPS LAN port. A complete browser restart restores Snakes and skips
firmware/SIS installation. The actual **Play** control switches Snakes -> Sky
Force -> Snakes. Both installed-package identities remain present, the same
device ID is selected, and saved witness bytes survive both switches. Network
capture records only the newly selected Sky Force SIS, with no firmware fetch
and no Snakes reinstall on return. Kernel shutdown also persists cached central
repository changes before the final disk save.

These launcher checks are separate from the file fixture. Their screenshots
and reports are retained; title/client presentations are not called gameplay.

Hooked On's ordinary importer was then run on the existing Snakes/Sky Force
phone. The native completion callback enabled saving, with 10,585 entries in
the saved manifest. A complete browser restart restored 9,941 files and skipped
the runtime SIS and importer. This checks adding N-Gage to an existing device,
separately from seeding an empty one.

## Gameplay and public endpoint

Short Chromium screens use the same final runtime artifact, renderer, assets,
continuous unpaced routes and scene definitions. Instruction totals stay zero;
the persistence variants execute two background scans in each twenty-second
window. No builds, profiles or test suites overlap the timed windows.

| # | Scene / comparison | Temporary or autosave-off FPS | Persistent or autosave-on FPS | Window seconds |
| ---: | --- | ---: | ---: | --- |
| 1 | Hooked On, stationary tutorial-dismissed Costa Rica boat, separate fresh browsers | 257.44 | 247.50 | 20.23 / 20.23 |
| 2 | Snakes, opening level, separate fresh browsers | 99.59 | 97.97 | 20.14 / 20.14 |
| 3 | Hooked On, same running emulator, autosave disabled then enabled | 255.12 | 266.58 | 20.21 / 20.21 |

The first comparisons are lower by 3.86% and 1.62%; the same-instance comparison
reverses direction. These variable presentation-rate captures do not establish
a repeatable FPS loss or gain. Saving adds real background work; they do not
prove zero overhead, CPU-only throughput or unchanged performance on every
game/device. Compilation within each window is only a few milliseconds. The
initial same-instance route used a browser global in Node and failed before
timing; that attempt is retained and excluded.

The final runtime is served by `eka2l1-lan.service` at
`https://claude-laptop.lan:8188/`. Downloaded WASM SHA-256 matches
`3b76c850048ba47ba27c00e2759d0558b3b31bba4fbf591afdb21b2a2905de4e`.
A fresh normal paced launcher uses the image to seed an empty phone, reaches
the actual Costa Rica boat in 65.13 seconds, then measures 31.95 FPS and
approximately one guest second per host second over ten seconds. Movement and
animation are checked, with no guest/page errors. Shutdown commits the phone.
A new browser process on the same profile restores it and presents the N-Gage
client, with no image, firmware, SIS or game-container request. That restarted
client check is not another gameplay-startup timing. The first public attempt
raced the daemon restart and failed to connect before runtime initialization.

The [measurement summary](PERSISTENT_DEVICE_RESULTS.json) retains the accepted
runtime screens, network observations, public paced endpoint and build identity.
All three existing native CTest suites pass; they are a regression smoke, not
coverage of the browser storage protocol, which the two-browser fixture checks.

## Scope and reproduction

This persists disk state, not guest RAM or running processes. Reload boots the
saved phone and reconstructs the CPU and generated modules. An abrupt tab kill
can lose writes since the last completed save; games also control when their
in-memory progress is written to files. The explicit save/switch paths wait for
completion. Storage is local to the browser and origin, not shared between
devices or Chrome/Firefox. Clearing site data removes it. The launcher requests
persistent-storage protection, which the browser may decline; quota failures
are visible and do not replace a previously committed phone.

Normal launches are persistent. The cold-start harness sets
`window.ekaTransientSession` before page scripts run, and scripted routes remain
temporary unless the test explicitly mounts a disk. This preserves fresh-disk
benchmark semantics without a normal-launch experiment switch.

```sh
cmake --build build-wasm --target eka2l1_wasm test_device_disk -j4
node src/tests/wasm/persistent-disk.test.mjs
EKA_TEST_BROWSER=firefox node src/tests/wasm/persistent-disk.test.mjs
```

Raw browser profiles, fixture/build logs, screenshots, network observations and
runtime checks are in `/home/claude/.scratch/eka-persistent-20261013`. Game,
firmware and saved-device binaries are outside Git.

Platform references: [IndexedDB transactions and local storage](https://developer.mozilla.org/en-US/docs/Web/API/IndexedDB_API),
[Web Locks](https://developer.mozilla.org/en-US/docs/Web/API/Web_Locks_API),
[persistent-storage requests](https://developer.mozilla.org/en-US/docs/Web/API/StorageManager/persist).
