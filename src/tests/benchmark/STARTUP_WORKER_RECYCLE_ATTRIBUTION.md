# Fresh N-Gage startup after CPU worker recycling

The browser recycles its CPU Worker after a fresh N-Gage installation to
release V8 code created during the importer. The guest OS and installed files
remain live. This study brackets the cost from importer completion through a
gameplay scene, using the same build with recycling enabled or skipped.

The released WASM SHA-256 was
`fa0634ef6f34e1e50bdf46a54494cfa7672a33267ecba4960764cde940e5701c`.
Only a private copy of the HTML shell carried timestamp and monitor-counter
diagnostics; the released code, compiler policy and `.lan` service were not
changed. Chromium 153 used a fresh profile and guest installation for each
unpaced run, with sound enabled and the same scripted scene. Synchronous V8
module construction falls inside the AOT installation timer; later V8 tiering
does not. Normal host work remained active,
so small differences between individual runs are not precise speed estimates.

| # | Scene | Recycle: completion to scene | Keep worker: completion to scene | Difference | Measured worker handoff | Extra AOT translation + emission + installation | Wall difference left after those timers |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | Tetris board | 42.940 s | 26.206 s | 16.734 s | 0.235 s | 7.877 s | 8.622 s |
| 2 | Tetris board, CDP CPU profile on | 37.518 s | 22.462 s | 15.056 s | 0.235 s | 7.909 s | 6.912 s |
| 3 | Hooked On boat | 134.572 s | 109.845 s | 24.727 s | 0.450 s across two recycles | 11.961 s | 12.316 s |
| 4 | Asphalt 3 warm race | 47.326 s | 35.215 s | 12.111 s | 0.251 s | 16.536 s | -4.676 s |

Hooked On's bracket starts at the game loop, before its early importer recycle;
the other games start at installer completion. The control's bookkeeping-only
handoff is 4–7 ms. AOT counters reset on each real recycle, so the measured
pre-recycle epochs were added to the post-recycle totals. The last column is
an **arithmetic residual, not a measured subsystem**. Asphalt 3's negative
residual is direct evidence that host/run variation and work outside the
timers prevent an exact wall-clock decomposition from these pairs. Each route
advanced approximately the same guest time within its game; page-navigation
times before the bracket varied substantially and are excluded.

The compiled-function counters expose the repeated work:

| # | Game | Recycled worker: functions compiled after importer | Surviving worker: additional functions compiled after importer | Extra translations, approximate |
| ---: | --- | ---: | ---: | ---: |
| 1 | Tetris | 64,964 | 32,836 | 32,128 |
| 2 | Asphalt 3 | 57,206 | 30,127 | 27,079 |
| 3 | Hooked On | 80,818 after its final recycle | 49,384 after importer | 31,434 after the final recycle alone |

Hooked On also compiled 51,591 functions before its *early* importer recycle;
its two resets make the last comparison an incomplete accounting of the total
duplicate work. Function counts are compilation events rather than distinct
guest PCs.

In the profiled Tetris pair, the new CPU Worker accumulated 36.10 sampled
seconds versus 21.04 on the surviving worker. The largest self-time changes
were `dyncom_core::run` 10.63 versus 4.04 seconds (+6.59),
`WebAssembly.Module` 3.48 versus 0.87 (+2.60), and
`state_local_cache::finish` 1.90 versus 0.59 (+1.31). The latter two are
inside the AOT operation timers above; do not add them again. The generated
guest-code sample totals were broadly comparable. CDP names the outer runner,
but its self time includes inlined work and does not isolate a branch, lookup,
or V8 tier transition. Native instruction sampling or a controlled reset-only
variant would be needed to assign that runner difference more finely.

The lifecycle boundary is split by ownership. In
[`main.cpp`](../../emu/wasm/main.cpp), the C++ CPU loop observes the recycle
request at a safe point, calls `discard_worker_compiled_code()`, exits, joins,
and starts a replacement thread. In
[`shell.html`](../../emu/wasm/shell.html), browser JavaScript waits for that
state, removes the Emscripten Worker from its pool, calls `terminateWorker`,
and resumes the CPU thread. Terminating that browser Worker cannot be done by
ordinary C++ thread cleanup. The shell currently also contains a fixed 200 ms
wait; it accounts for most of the measured handoff, not the seconds-long
startup penalty. The important consequence is that
`discard_worker_compiled_code()` clears the function registry and restarts
hot-ROM compilation state. The replacement Worker therefore discovers and
compiles many guest regions again, and V8 creates their WASM modules again.

Raw traces, screenshots and CPU profiles are under
`/home/claude/.scratch/eka-startup-attribution-20261018/` in the
`tetris-{recycle,control}{,-profile}`, `hooked-{recycle,control}` and
`asphalt3-{recycle-r2,control}` directories. The first Asphalt 3 recycle
attempt stalled before emulator launch and is excluded; the successful retry
and control both reached the warm race.
