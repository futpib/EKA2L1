# Fold the private sparse-registry range check

Adopted by default: retain the private registry simplification while preserving the original outer RAM classifier.

Unsigned 32-bit address-minus-base excludes below-base addresses because the private registry rejects wrapped extents at configuration. Only the registry check changes; the outer runtime classifier retains its original early RAM rejection. Sparse layout, ARM/Thumb tags, updates, removals and RAM validation retain their behavior.

## Controlled runtime comparison

Control: `50c70b115`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 6.8314 → 6.8305 | +0.01% | +0.43% | -0.14% | 1/4 |
| 2 | Sky Force | 19.6575 → 19.3727 | +1.47% | +1.38% | -0.73% | 3/4 |

Sky Force improves in three of four pairs with a modest 1.47% mean CPU throughput gain and 0.73% fewer native instructions. Snakes is flat. The final control is relatively slow, but its paired candidate is also slower than earlier candidates; all valid observations remain included. This result earns keeping the smaller part of the previously inconclusive combined rewrite, without claiming its effect generalizes to every workload.

Snakes pairs range from -0.19% to +0.44%; candidate wall speed is **2.16× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from -0.47% to +2.60%; candidate wall speed is **0.83× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

Actual warmed V8 code recovers all 160 selected versions. execute_chain remains 7,616 native bytes after layout/alignment. The outer runtime classifier retains its original lower-bound branch at +0x14f6. The private registry check shrinks to move/subtract/compare/branch at +0x150c through +0x151d; its previous redundant lower-bound comparison and branch are gone. The successful ROM path is shorter, while V8 still lays it out after a forward branch. This proves code removal, not a runtime gain.

320,000 sparse registry and 160,000 ordinary registry oracle comparisons pass, including odd bases, upper-address ranges, invalid extents, null functions, replacements, removals and clears. The 504 real runner policy/mutation checks and existing guard-publication, verifier, frozen-cache and SVC-return tests pass. Both 60-frame game replays match exact images, guest progress, PCM and audio events.

The measured artifact is served at `https://claude-laptop.lan:8188/`; its served WASM hash matches. Both real game-picker paths pass gameplay, input and default-policy checks with NVIDIA hardware rendering, and saved gameplay screenshots were inspected. The existing non-silent browser-audio failure remains, so full live-audio E2E is not claimed. Exact PCM replays pass.

See [full observations and evidence](ROM_REGISTRY_RANGE_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-hotspot-round/rom-registry-range`.
