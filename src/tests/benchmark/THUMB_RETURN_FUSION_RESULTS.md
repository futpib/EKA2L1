# Fuse known Thumb return continuations

Not adopted. Snakes is neutral and Sky Force regresses in the aggregate, with only one of four faster pairs. The prototype and its selector are archived and removed from active source.

Track a compile-time return-address stack through fused direct Thumb BL calls. BX LR and POP PC can continue in the same WASM function when the raw runtime return value equals the expected Thumb address. Unexpected targets, ARM returns, cycles, edge limits, stops, IRQs and short budgets retain exits. Guest instruction counts remain static and memory accesses retain their order. The runtime return guard is omitted when no continuation is eligible. Existing exact whole-path entry budgeting is preserved.

## Controlled runtime comparison

Control: `888cec2ae`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 6.8125 → 6.8212 | -0.13% | -0.06% | -0.01% | 2/4 |
| 2 | Sky Force | 17.7325 → 18.1670 | -2.39% | -2.33% | -1.98% | 1/4 |

Snakes changes -0.13% with two faster pairs. Sky Force changes -2.39% with only one faster pair; its pair spread is wide (-13.17% to +6.83%). All valid runs remain in the result. The native instruction reduction does not establish a runtime improvement. Larger generated functions are observed, but cache or branch-prediction effects were not measured, so no specific regression cause is established.

Snakes pairs range from -1.19% to +1.02%; candidate wall speed is **2.16× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from -13.17% to +6.83%; candidate wall speed is **0.88× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

The warmed gameplay capture recovers 160 of 160 selected code versions without sampling loss. Matched native versions of Thumb entries 0x801a1051, 0x801a1059 and 0x801a1063 grow from 7,936 to 9,344, 2,944 to 4,864 and 14,528 to 26,496 bytes as they absorb continuation paths. This is code-size evidence, not a runtime instruction-count claim. Function-level sampling remains dominated by the C++ runner and interpreter. This artifact does not emit guest source metadata; instruction-level guest attribution is not claimed.

The return-focused matrix passes 76,032 exact chain/state/memory/callback comparisons and 5,600 independent DynCom checks, including nested calls, short budgets, helper mutations of LR, stack aliasing, changed return addresses and ARM-mode returns. Generated-call counts fall from 112,128 to 92,256 in this fixture matrix. The independent oracle compares canonical PC and Thumb mode because DynCom canonicalizes POP PC; the variant comparison still checks raw PC exactly. Pending SVC invocation-local counts are normalized to whole-chain counts after validating the protocol. The instrumented subset passes 2,112 paths: 432 execute fewer WASM operations, 1,536 are equal and 144 grow; calls fall from 2,816 to 2,240. Both games pass exact 60-frame images, guest progress, audio events and PCM. The bounded Thumb regression suite passes. The ordinary fusion and whole-path budget matrices each also pass 76,032 exact comparisons and 5,600 independent interpreter checks. No full ARM-suite rerun is claimed for this Thumb-only prototype. After removal, all six adopted artifact hashes reproduce exactly, the focused Thumb suite passes again, and the active LAN service serves the adopted WASM hash.

The prototype is archived and removed from active source. LAN retains the preceding adopted artifact.

See [full observations and evidence](THUMB_RETURN_FUSION_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-region-fusion/thumb-returns`.
