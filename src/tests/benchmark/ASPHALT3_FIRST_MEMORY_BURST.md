# First significant Asphalt 3 startup memory burst

The stock Nokia 5320 Asphalt 3 launch was traced from a fresh Chromium 153
process through the real game-picker asset sequence. The probe sampled Linux
browser-process PSS every 100 ms and captured renderer mappings and Chromium
native allocation stacks when PSS first rose at least 32 MiB across the most
recent sample interval spanning at least 200 ms (typically 200–300 ms).
This is a **burst trigger**, not a claim that one `malloc` allocated that amount.
The browser stack was symbolized with the matching Chromium build-ID debug file.

The first burst after navigation is mostly browser page startup: a fresh blank
HTML control rose 110 MiB, while the unprofiled EKA2L1 page rose 178 MiB over
its first 216 ms. The EKA2L1 page also starts the core WASM decoder and GPU
renderer during that interval. The blank-page result means this is not a useful
Asphalt-specific allocation to remove from the emulator. The first burst after
the core was ready and the game-picker began loading assets occurred during
`/preload/rom` download. In the source-profiled run, whole-browser PSS rose
90.9 MiB over 298 ms; 91.2 MiB of the rise was in renderers, offset slightly
by another process. The ROM-cache preparation and ROM fetch overlap, so the
PSS delta alone cannot separate them.

Native lifetime sampling during that burst attributed an estimated 118.4 MiB
to Chromium `TeeHelper::OnStateChange` and 32.9 MiB to
`FetchDataLoaderAsArrayBuffer`. These are **sampled allocation estimates**,
not resident-memory subdivisions to add to the PSS rise. The second allocation
matches the 34.5 MB derived ROM translation cache read by
[`rom-cache.js`](../../emu/wasm/rom-cache.js). The tee was triggered by
[`ekaDownload`](../wasm/asset-cache.ts): on a cache miss it awaited
`cache.put(url, response.clone())` before returning the original response.
The original tee branch was not being consumed while Cache Storage read the
clone, so Chromium buffered downloaded bytes in the renderer. The next caller
then read those bytes again as a blob or ArrayBuffer. This explains a large
**temporary** launch burst; it does not imply the same bytes remain live at
the race HUD.

The cache miss now writes the original response into Cache Storage, reopens the
stored response, and returns it to the caller. That removes the unconsumed tee
branch while keeping cache hits and offline reuse. If the cache write fails
after consuming the response, `ekaDownload` refetches the asset; if the body
was not consumed, it returns the original response. A real-browser test covers
both failure cases, cache reuse after restart and offline reads.

Two alternating pairs used fresh Chromium profiles, the same private server,
the same release WASM and ROM cache, and no native allocation profiler. All
four launches reached `_gameRunning`. The asset-phase peak is relative to the
first PSS sample after asset loading began; page startup produced different
absolute baselines, so only the within-run growth is compared here.

| # | Download path | First asset burst, MiB | Asset-phase peak growth, MiB | Asset load to game running, s |
|---:|---|---:|---:|---:|
| 1 | Original cloned response A | 62.7 | 111.0 | 8.96 |
| 2 | Reopen cached response A | 50.7 | 58.9 | 8.73 |
| 3 | Reopen cached response B | 48.4 | 84.5 | 8.65 |
| 4 | Original cloned response B | 60.4 | 122.7 | 12.57 |

The paired asset-peak reductions were **52.1 and 38.2 MiB**. The first-burst
reductions were **12.0 and 12.0 MiB**. The startup timings do not establish a
speedup because the two control launches varied by 3.61 seconds. In a separate
source-profiled trial, the tee site fell from 118.4 MiB sampled to zero, while
the ROM-cache ArrayBuffer remained at 32.9 MiB. The actual patched server
likewise reached `_gameRunning` with no tee allocation in the first-burst
sample; its native allocation estimate was 33.5 MiB, almost all the cache
ArrayBuffer. The cache change acts only during asset download, before guest
execution, so it adds no CPU checks to the game loop. The patched private
server also completed the stock Asphalt 3 route to the validated race HUD,
presented 303 frames in its five-second unpaced window (59.78 FPS), and logged
no guest errors. This is a correctness smoke test, not a paired runtime-speed
comparison.

The `.lan` service was restarted after the source change. Its served HTML now
contains the reopened-cache path, its WASM ETag remains
`d4be6fab0c3151f047ba385b630c78b31df935e9477cf16ae8bf1fe49246d426`,
and a fresh HTTPS game-picker launch reached `_gameRunning` without errors.
The private-server race route above provides the stronger gameplay check.

The reproducible probe is
[`memory-burst-probe.mjs`](../wasm/memory-burst-probe.mjs). Its default follows
the custom-file route; `EKA2L1_BURST_MANUAL_LIVE=1` replays the game-picker
asset path with explicit phase markers, and `EKA2L1_BURST_NATIVE_PROFILE=0`
removes the native profiler for paired PSS measurements. The private-server
captures are under `/home/claude/.scratch/eka-burst-paired-20261017-*`;
symbolized first-burst captures are under
`/home/claude/.scratch/eka-first-burst-assets-repeat-20261017` and
`/home/claude/.scratch/eka-first-burst-actual-fix-20261017`; the race capture
is `/home/claude/.scratch/eka-cache-fix-race-20261017`, and the live HTTPS
launch is `/home/claude/.scratch/eka-first-burst-live-deployed-20261017`.
