# N-Gage package staging in the browser

The cold N-Gage launcher previously loaded the complete `.n-gage` response into
a JavaScript `ArrayBuffer`, passed it through WasmFS's `writeFile` allocation,
and retained a separate memory-backed guest file until the N-Gage installer
removed it. Asphalt 3's package is 12,107,859 bytes. The new path streams the
response into a temporary OPFS file, links it at `E:/n-gage/game.n-gage`, and
deletes the link and OPFS file when the guest installer finishes. It reuses the
existing OPFS staging helper and retains the memory-backed path when OPFS is
unavailable.

The adopted N-Gage path feeds the browser's OPFS writable stream in at most
64 KiB writes. A direct `pipeTo` trial caused a larger transient renderer
allocation. A WasmFS write trial left an OPFS access handle locked after the
guest installer finished, so it could not safely delete the staged file.
The ROM, RPKG and SIS upload paths retain their existing `pipeTo` method.

## Identical-binary comparison

Both variants used the same WASM binary (SHA-256
`0a4dd901c76763a35b8614fd6b07be73f0c2ed75c3375efcb78e68a8d398c041`),
generated JavaScript, asset server and Chromium 153. Only the generated HTML
launcher differed. Each run used a fresh browser profile and cold Asphalt 3
device. Whole-browser PSS was sampled every 50 ms around package preparation;
"settled" means the sample nearest 150 ms after package preparation finished.

| # | Package staging | WASM capacity before → package ready | Browser PSS before → peak → settled | Package preparation |
| - | - | -: | -: | -: |
| 1 | Original in-memory write | 256.00 → 307.25 MiB | 1151.3 → 1163.5 → 1162.8 MiB | 61 ms |
| 2 | Browser `pipeTo` OPFS trial | 256.00 → 256.00 MiB | 1151.7 → 1197.0 → 1160.8 MiB | 109 ms |
| 3 | Adopted 64 KiB OPFS writes | 256.00 → 256.00 MiB | 1139.7 → 1154.9 → 1150.6 MiB | 153 ms |

The adopted path avoids a 51.25 MiB WASM-capacity increase at package staging.
Its sampled PSS rise was about 15 MiB, close to the original path's 12 MiB;
the different baselines and one run per variant do not establish a physical
RAM saving. WASM capacity is reserved space, not resident memory. The 92 ms
package-preparation difference is small beside the much longer guest
installation and does not establish a reliable startup regression.

The browser smoke suite passed. Fresh Chromium Asphalt 3 and Tetris launches
reached nontrivial screens and completed N-Gage installation without browser
or guest errors. Asphalt 3 reached a nontrivial screen at 23.80 seconds and
finished installation at 47.37 seconds; the guest link and OPFS stage were
both absent afterward. Tetris reached a nontrivial screen at 21.70 seconds
and finished installation at 39.45 seconds. Its saved-device reload skipped
the package download and kept the staging directory empty. Scratch evidence is under
`/home/claude/.scratch/eka-ngage-worker-memory-old.json`,
`/home/claude/.scratch/eka-ngage-browser-64k-memory.json` and
`/home/claude/.scratch/eka-ngage-final64-*`; these are not release assets.
