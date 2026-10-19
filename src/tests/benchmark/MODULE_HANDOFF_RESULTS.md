# Generated module handoff across CPU workers

The browser page can retain `WebAssembly.Module` objects after a CPU Worker
exits and send them to its replacement. A dedicated keeper Worker is not
required for this operation. The replacement must still instantiate each
module against its own imports and install the new exports in its WASM table;
functions and instances cannot be reused across that boundary.

`src/tests/wasm/module-handoff-fixture.mjs` exercises this in Chrome 154 with
593 captured, real Snakes AOT modules from a prior compiler census. Their
source binaries total 58,913,031 bytes and export 14,872 functions. The first
Worker compiles and instantiates them, sends its module objects to the page,
then terminates. A replacement Worker instantiates clones of those objects.
A third Worker compiles the same bytes from scratch. Each memory observation
follows an explicit browser GC. The capture is from an older emulator build;
it measures browser mechanics, not the current game's compile mix.

| # | Chrome run | Reuse round trip | Fresh round trip | Fresh compilation only |
| ---: | --- | ---: | ---: | ---: |
| 1 | First 593-module run | 27.9 ms | 167.1 ms | 92.6 ms |
| 2 | Second 593-module run | 34.3 ms | 160.5 ms | 94.6 ms |
| 3 | Final 593-module run | 27.4 ms | 184.1 ms | 93.5 ms |

The final run's renderer PSS rose from 312,649 KiB before compilation to
375,574 KiB after the first Worker exited while the page held the modules.
After the later Workers exited, dropping only the page's module references
and requesting GC reduced renderer PSS from 376,950 to 311,628 KiB. The page
still held the source bytes at that point. Dropping those bytes and collecting
again reduced it to 249,176 KiB. These are process-level observations, not
an ownership accounting of every byte.

This fixture deliberately does not execute guest functions or measure V8's
later optimization tiers. It also omits guest-PC-to-module metadata and the
emulator's ARM translation and WASM emission work. Preserving module objects
alone would therefore remove only part of the measured 12–25 second N-Gage
recycle penalty in `STARTUP_WORKER_RECYCLE_ATTRIBUTION.md`. Safe emulator reuse
would need to cache immutable ROM functions with their guest keys; current hot
batches can contain mutable RAM functions, so indiscriminate module retention
would be incorrect. A cache size bound and a full fresh-install/gameplay run
under the renderer memory cap are required before promoting such a cache.
Deleting an entry removes the page's reference but does not force V8 to
reclaim it immediately.

Reproduce the browser handoff with a captured module directory:

```sh
cd src/tests/wasm
node module-handoff-fixture.mjs /path/to/modules /tmp/module-handoff.json 593
```
