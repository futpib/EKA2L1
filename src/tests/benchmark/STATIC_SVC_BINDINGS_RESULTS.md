# Static bindings for built-in syscalls

Adopted under the existing defaults without a selector. Built-in syscall
registrations now carry a static bridge whose handler is known at C++ compile
time. Their invocation avoids copying and destroying an owning `std::function`
and removes its captured handler indirection. Stateful callbacks retain an
owning snapshot, including callbacks that erase or replace their registration.
Kernel locking, syscall lookup and return conventions are unchanged.

## Controlled runtime result

Control: adopted runtime `536ea5af3`. Four launches per build per game used
ABBA/BAAB order. The first Sky Force result had a wide paired spread, so a
second, prospectively recorded eight-launch Sky Force confirmation was run.
All observations from both batches are retained and weighted equally.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.1045 → 7.0910 | +0.19% | -0.28% | -0.01% | 2/4 |
| 2 | Sky Force, both batches | 21.9455 → 21.4050 | +2.53% | +2.25% | -2.17% | 7/8 |

Sky Force's initial batch was +2.23% (3/4 faster pairs); confirmation was
+2.82% (4/4). The full paired range remains -6.40% to +9.77%: neither the slow
candidate nor the slow control was excluded. Snakes is effectively unchanged;
no Snakes speedup is established. This does not establish identical gains on
other games, V8 versions or hosts.

There are 24 valid observations and zero invalid attempts. Guest instructions,
progress and presentation journals match. The worker uses CPU 7, sibling 15 is
reserved, and the fixed-clock request is 3.6 GHz, with measured clock, affinity,
throttle and counter checks. There are no temperature gates or cooldowns.
Both batches pass all 88 live host-restoration checks. Timing builds have
source maps and extra diagnostics disabled.

At this clock, candidate wall speeds are Snakes **2.09× realtime** and Sky
Force combat **0.76×**, using 18 guest seconds divided by measured wall time.
These are incremental results, not percentages to add to earlier optimizations.

## Native evidence and correctness

Actual warmed V8 code takes the static binding branch directly to its bridge
call. It bypasses the owning callable's copy, generic invocation and destruction.
The complete `call_svc` native body grows from 4,608 to 4,672 bytes because the
stateful path remains. Whole-body size is not the cost of the executed path.
The capture recovered 159/160 selected code versions with zero snapshot errors;
it is code-shape evidence, not a separate controlled timing claim.

The registry/return-convention test passes **106,921 checks** in both native and
WASM builds. Coverage includes self-erasing stateful callbacks, nested static
registration and registry clearing. Both 60-frame game replays match reference
images, guest progress, PCM and audio events exactly.

The measured frozen build is served at `https://claude-laptop.lan:8188/`;
the served WASM hash matches. Both games pass real gameplay, input and default-
policy checks using NVIDIA hardware rendering, and their saved screenshots
were inspected. The existing non-silent browser-audio check still fails for
both games; full live-audio E2E is not claimed. Exact PCM replays pass.

See [full observations, settings and hashes](STATIC_SVC_BINDINGS_RESULTS.json)
and [benchmark controls](CONTROLLED_BENCHMARKS.md). Raw patches, builds, logs,
profiles and replay evidence are under
`/home/claude/.scratch/eka-hotspot-round/static-svc`.
