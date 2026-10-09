# Skip an already-empty exclusive monitor

Adopted without a selector. A conservative atomic summary lets `clear_exclusive`
return when every reservation is already invalid. Reads and snapshot restores
mark the monitor potentially nonempty while holding its existing lock. Only a
complete global clear resets the summary; individual invalidations may leave
it set and cause an unnecessary clear, preserving reservation behavior.

## Controlled runtime result

Control: static syscall bindings, now committed as `554a92f1e`. The candidate
adds only the monitor change to that frozen runtime. Four launches per build
per game use ABBA then BAAB order. All sixteen observations are valid and
retained, including the slower Sky Force pair; there are zero invalid attempts.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.0889 → 7.0953 | -0.09% | +0.29% | +0.00% | 2/4 |
| 2 | Sky Force | 21.8259 → 21.1123 | +3.38% | +3.05% | -0.51% | 3/4 |

Snakes is effectively unchanged. Sky Force's paired range is -1.22% to +7.43%;
higher control timings contribute substantially to the mean, and this small panel
does not establish the same gain on other workloads or V8 versions. The
measured gain and three favorable pairs earn keeping this small change.
A direct combined comparison will assess the whole hotspot round; percentages
from its individual experiments must not be added.

The worker uses CPU 7, sibling 15 is reserved, and the 3.6 GHz request is checked
against measured frequency, throttle, affinity and counters. No temperature
gates or cooldown waits apply. All 88 live host-restoration checks pass.
Guest progress, instructions and presentation journals match. Timing artifacts
have source maps and extra diagnostics disabled.

Candidate wall speeds at that clock are Snakes **2.10× realtime** and Sky Force
combat **0.77×**, using the fixed 18-guest-second windows.

## Native evidence and correctness

The actual warmed native capture recovered all 160 selected code versions.
In `InterpreterMainLoop`, the candidate loads the reservation summary at native
`+0x81d` and jumps over the lock/clear path to `+0xa77` when it is empty. The
control always enters that lock. The atomic acquire at `+0x84c` remains on the
nonempty path. Maintaining the new summary itself emits atomic stores: this
was included in the runtime measurement, not assumed free. The complete outer
body is 165,504 native bytes versus 166,016 in the control; body size alone is
not evidence of a runtime gain.

An independent reservation model and cross-thread clear test pass **254,406
checks** in native and WASM builds. They cover one, two and eight processors,
successful/failed exclusive writes, snapshots, restores, individual/global
clears and synchronized cross-thread invalidation. Both 60-frame game replays
match exact images, instruction progress, PCM and audio events.

The measured frozen artifact is served at `https://claude-laptop.lan:8188/`;
the served WASM hash matches. Both real game-picker paths pass gameplay, input
and default-policy checks with NVIDIA hardware rendering; saved screenshots
were inspected. The pre-existing non-silent browser-audio check still fails
for both games, so full live-audio E2E is not claimed. Exact PCM replays pass.

See [full observations, hashes and evidence](EMPTY_MONITOR_CLEAR_RESULTS.json),
[benchmark controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under
`/home/claude/.scratch/eka-hotspot-round/monitor-clear`.
