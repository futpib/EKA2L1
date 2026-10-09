# Remove unused WASM loops from bounded Thumb regions

Not adopted. Both games have positive means but mixed pair results; the measured spread does not establish a repeatable runtime gain. The prototype and its selector are removed from active runtime source.

The prototype removes an outer WASM block/loop and unused PC-index initialization from bounded Thumb emission. These bodies return at backedges and fusion refuses cycles, so the loop structure is unnecessary. Budget proofs, memory access order and callback behavior are unchanged.

## Controlled runtime comparison

Control: `b57a12bbe`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 6.8169 → 6.7719 | +0.66% | +0.69% | -0.13% | 2/4 |
| 2 | Sky Force | 17.5495 → 17.4505 | +0.57% | +0.49% | -0.49% | 3/4 |

Snakes has two faster pairs, including a +3.40% pair that raises its mean. Sky Force has three faster pairs but loses 1.62% in the final pair. Every valid observation is retained. Real native checks disappear, but this small instruction reduction has not earned graduation.

Snakes pairs range from -0.74% to +3.40%; candidate wall speed is **2.17× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from -1.62% to +2.14%; candidate wall speed is **0.91× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

The warmed Sky Force caller at 0x801a1044 shrinks from 8,256 to 8,128 native bytes and loses both native checks at its former loop headers. Retired native instructions fall 0.13% in Snakes and 0.49% in Sky Force. The independent executed-WASM matrix has 1,120 lower, 224 equal and zero higher paths; only constant/local setup changes in its explicit operation categories. The native capture has no sampling losses or metadata adapter errors and recovers 158 of 160 requested snapshots, including the audited caller. This build lacks optional guest-source metadata, so the new capture establishes function identities and native code differences, not instruction-level ARM attribution.

The prototype passes 32,256 state/memory/callback comparisons, 3,808 independent interpreter comparisons, and both exact 60-frame image/progress/audio-event/PCM replays. The focused suite covers 99,792 bounded Thumb cases, 5,632 register exchanges, 2,310 direct-memory cases, 2,688 transfer spans, 35,280 ROM veneers, 194,560 ROM syscalls, 288 call boundaries, 600 SVC hints, 14,976 SVC state cases and 480 real SVC-return loops. A full suite was deliberately stopped after its earlier prefix passed, then replaced by this targeted run; no full-suite completion is claimed for this revision. After removing the prototype, all six adopted artifact hashes reproduce exactly and the focused Thumb suite passes again. Its reusable test selector is retained.

The prototype is archived and removed from active source. LAN retains the preceding adopted artifact.

See [full observations and evidence](THUMB_LINEAR_REGION_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-region-fusion/thumb-linear`.
