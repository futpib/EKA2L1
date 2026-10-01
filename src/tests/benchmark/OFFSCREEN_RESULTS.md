# Worker-owned browser graphics

Implementation: `8a790a484` on local `wasm-port`. This follows the upstream merge
baseline in `UPSTREAM_MERGE_RESULTS.md`. Nothing was pushed.

WebGL now belongs to the graphics worker when OffscreenCanvas is available.
The application thread no longer takes the canvas at startup. The frontend
calls `eka2l1_prepare_graphics`, asynchronously polls `eka2l1_graphics_ready`,
then calls `eka2l1_run`. The graphics command loop yields through a worker-local
MessageChannel after presentations so the browser actually updates the visible
canvas. Guest clocks and instruction budgets are unchanged.

Shutdown first joins emulation and drains display hooks, destroys guest services
while their graphics driver remains available, then aborts/joins the graphics
worker. Its pthread cleanup destroys WebGL resources on their owning thread.
Discovery-disabled Bluetooth askers skip libuv teardown when they have no
handles; a new end-to-end shutdown check exposed this missing case.

## Serial performance

Physical NVIDIA Quadro T1000 Max-Q, Chromium 150.0.7871.186; four guest seconds
(21–25s), rendering enabled, readback/capture and diagnostic counters disabled.
Each fixture warmed before measurements. The order was old/new/new/old.

| Trial | Proxied GL baseline | Worker-owned GL |
| --- | ---: | ---: |
| First | 5.16287 host seconds | 4.70895 host seconds |
| Second | 4.96818 | 4.67068 |
| Mean | 5.065525 | 4.689815 |

Observed throughput improvement is 1.080×. The new result is 0.853× realtime,
requiring about 17.2% further throughput. This is a shared-host, two-trial result,
not sustained interactive playability. Both modes execute exactly 617,185,821
guest instructions and 160 presentations in the measured interval. Timing uses
the MessageChannel prototype binary before shutdown-only fixes; its exact hash
is retained in `OFFSCREEN_EVIDENCE.json` separately from the committed build.

Rejected variants are preserved under `.scratch/eka-benchmark`: a blocking
worker rendered matching offscreen pixels but a black visible canvas; a timeout
loop fixed presentation but averaged 5.322245 seconds versus 4.8977 baseline.
The SDK's immediate-loop helper used a Window postMessage signature in a worker
and aborted, so the accepted implementation uses its own MessageChannel.

## Correctness

Both complete browser runs, including one with every 1,024th compiled block
checked against the interpreter, match the merged native reference across all
1,000 distinct gameplay images, guest timestamps/instruction counts, PCM and
audio events. Endpoint: 70.969054 guest seconds, covering 49.937495 seconds of
captured gameplay. This retains the merged baseline, not the pre-merge one.

The benchmark now checks that the visible canvas contains nontrivial colors,
changes during the replay, and that shutdown completes without browser errors.
Both full runs pass those checks. Visible screenshots are saved alongside the
framebuffer captures. All three native CTest targets and seven frontend smoke
tests pass. The unchanged CPU implementation passes all 127 WASM tests; CPU
semantics were not modified in this stage. Audio quality remains deferred.

## Reproduction

Build normally, then use the existing `benchmark.ts` commands with AOT mode 5,
with and without `EKA2L1_AOT_VERIFY=1024`. Compare against the merged native
reference using `compare.py`. For timing use `profile_batch.py --capture-mode 2
--before-aot 5 --after-aot 5 --compare-build ARCHIVE` with `EKA2L1_GPU=hardware`
and `EKA2L1_PROFILE_DETAIL=0`. See `PROFILING.md` for fixture/gate setup.

Raw artifacts: `/home/claude/.scratch/eka-benchmark/offscreen-{build,full,full-checked,tasks-timings}`.
Raw data and hashes: `OFFSCREEN_EVIDENCE.json`.
