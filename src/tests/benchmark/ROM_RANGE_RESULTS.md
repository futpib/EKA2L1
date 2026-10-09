# Fold ROM range checks

Not adopted: the full ROM range rewrite gives no useful repeatable runtime gain.

The sparse registry owns private bounds and rejects overflowing extents at configuration. Therefore unsigned 32-bit address-minus-base compared with size also excludes below-base addresses. The runtime classifier uses unsigned 64-bit subtraction to preserve its more general bounds contract. This removes repeated lower-bound branches without changing sparse table layout, ARM/Thumb tags, updates, removals or RAM mapping checks.

## Controlled runtime comparison

Control: `50c70b115`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 6.8559 → 6.8543 | +0.02% | -0.44% | +0.19% | 2/4 |
| 2 | Sky Force | 19.7635 → 19.7121 | +0.26% | +0.17% | -0.89% | 2/4 |

Both games are effectively flat and only two of four pairs favor the candidate. Sky Force executes fewer native instructions, but that does not establish a CPU-time improvement. All host-valid observations are retained. A separate registry-only follow-up preserves the old early exit for RAM addresses.

Snakes pairs range from -0.71% to +0.61%; candidate wall speed is **2.15× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from -3.39% to +4.50%; candidate wall speed is **0.82× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

The actual warmed V8 capture recovers all 160 selected versions. execute_chain shrinks from 7,616 to 7,552 native bytes. The hot runtime classification uses one 64-bit subtract/compare/branch at +0x14ad–0x14bf; sparse registry classification uses one 32-bit subtract/compare/branch at +0x14c5–0x14d6. The control separately compares each address with its base first. Both lower-bound branches disappear. The candidate places the successful sparse lookup after a forward branch; static instruction reduction alone cannot decide the runtime result. On a below-ROM RAM successor, the original outer classifier exits after its first comparison; the 64-bit subtraction version performs additional arithmetic and loads before rejecting the ROM path. The whole-path instruction effect is therefore mixed even though the successful ROM path shrinks.

320,000 sparse registry and 160,000 ordinary registry oracle comparisons pass, including odd bases, upper-address ranges, invalid extents, null functions, replacements, removals and clears. The 504 real runner policy/mutation checks and existing guard-publication, verifier, frozen-cache and SVC-return tests pass. Both 60-frame game replays match exact images, guest progress, PCM and audio events.

The prototype is archived and removed from active source. LAN retains the preceding adopted artifact.

See [full observations and evidence](ROM_RANGE_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-hotspot-round/rom-range`.
