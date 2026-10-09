# Keep compiled-runner counters private

Adopted by default without a selector.

The runner keeps its instruction and region counts in unescaped scalar locals and constructs the returned aggregate once at exit. It obtains the persistent compiled-function registry once per chain. Registry contents still reload after each region; registrations, removals, RAM validation, budgets, interrupts and pending syscalls retain their behavior. No new selector or runtime instrumentation is introduced.

## Controlled runtime comparison

Control: `b8f65984e`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.0574 → 6.8511 | +3.01% | +2.67% | -2.49% | 4/4 |
| 2 | Sky Force | 20.8114 → 19.6629 | +5.84% | +5.34% | -2.67% | 4/4 |

Both games improve in all four adjacent pairs, so the change earns adoption. Sky Force gains exceed its retired-instruction reduction; this comparison establishes a runtime win but does not isolate the share due to each removed operation. Every valid slower or faster launch is retained. A direct combined comparison will assess the whole round.

Snakes pairs range from +2.38% to +3.54%; candidate wall speed is **2.15× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from +4.63% to +8.19%; candidate wall speed is **0.82× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

The actual warmed V8 capture recovers all 160 selected versions. execute_chain shrinks from 8,128 to 7,616 native bytes. The control writes instruction and block fields through the result pointer after each region (+0x139d and +0x13a9) and repeats the registry initialization check (+0x145c). The candidate retains private counters around +0x144c–0x1456 and proceeds directly to registry bounds/lookup at +0x150c. Stack spills and the cross-module call machinery remain. This proves the intended code removal; the separate controlled runtime comparison decides adoption.

504 real runner comparisons cover budget, zero progress, stop, masked/unmasked IRQs, missing successors and in-flight registry removal across eight memory/hotpath/byte-policy combinations. Existing inline-boundary, guard-publication, sparse-registry, verifier-protection and frozen-cache checks pass. SVC-return coverage passes 600 eligibility cases, 14,976 full-state comparisons and 480 real-core state/count/callback/IRQ/stop/remap/fault comparisons. Both 60-frame game replays match exact images, progress, PCM and audio events.

The measured artifact is served at `https://claude-laptop.lan:8188/`; its served WASM hash matches. Both real game-picker paths pass gameplay, input and default-policy checks with NVIDIA hardware rendering, and saved gameplay screenshots were inspected. The existing non-silent browser-audio failure remains, so full live-audio E2E is not claimed. Exact PCM replays pass.

See [full observations and evidence](RUNNER_LOCALS_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-hotspot-round/runner-locals`.
