# Fuse connected Thumb regions with shared register locals

Adopted: Sky Force improves in all four controlled pairs, with a small Snakes regression. The user explicitly accepts this tradeoff. Per-instruction budget behavior is unchanged by this commit.

Known Thumb branch and direct-call successors now share one WASM function and guest-register locals. Every path retains a compile-time instruction count. Successor-only fields load at their own entry, while inherited values remain in locals. Memory order, callback publication/reloads and stop/IRQ exits are preserved. Cycles, unknown targets, ARM transitions, syscalls and compilation bounds still return to the runner. ROM fusion initially allows eight joined edges, follows another edge below 128 path instructions and 512 emitted instructions, and reads 256-byte successor windows; these bounds are not established optima. There is no private PC dispatcher or WASM call between fused bodies.

## Controlled runtime comparison

Control: `ab67f5aee`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 6.8372 → 6.8998 | -0.91% | -0.43% | -1.33% | 1/4 |
| 2 | Sky Force | 19.4478 → 18.3940 | +5.73% | +5.08% | -9.31% | 4/4 |

Sky Force's four pairs are positive despite a wide timing spread. Snakes loses 0.91% CPU throughput despite executing fewer native instructions, so the report retains that regression. Fewer instructions alone do not guarantee faster execution.

Snakes pairs range from -1.59% to +0.04%; candidate wall speed is **2.16× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from +2.16% to +10.69%; candidate wall speed is **0.87× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

Native instructions fall by 9.31% in Sky Force and 1.33% in Snakes. An independent executed-WASM counter finds 448 lower, 640 equal and 64 higher paths in a 1,152-path fixture matrix; generated-function loads fall from 11,792 to 11,024 and stores from 13,088 to 11,824. Those counts exclude eliminated C++ runner work. Remaining growing paths include exact-budget exits and callbacks. The warmed native profile identifies the matching V8 code versions and function identities; optional guest source metadata was disabled in this timed build, so this capture does not establish instruction-level ARM attribution.

All 185 AOT regression tests pass. Fusion coverage includes 27,648 full-state/memory/callback chain comparisons, 3,264 independent DynCom comparisons, conditional branches, finite loops, direct calls, aliasing, all small budgets and helper-induced register/flag/stop/IRQ changes. Test chains make 43,072 generated calls versus 54,880 in the control. Both games pass exact 60-frame image, guest-progress, audio-event and PCM replays.

The measured artifact is served at `https://claude-laptop.lan:8188/`; its served WASM hash matches. Both real game-picker paths pass gameplay, input and default-policy checks with NVIDIA hardware rendering, and saved gameplay screenshots were inspected. The existing non-silent browser-audio failure remains, so full live-audio E2E is not claimed. Exact PCM replays pass.

See [full observations and evidence](REGION_FUSION_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-region-fusion/thumb-scoped`.
