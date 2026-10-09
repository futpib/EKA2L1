# Reuse the published page view for exclusive reads

Not adopted: the published-view exclusive-read shortcut has no useful repeatable runtime gain.

The exclusive-read callbacks use the CPU published read-page table when it is present, current, readable and page-contained. They bypass MMU discovery, page walking and TLB republishing. Logging, missing or dirty views, non-readable/unmapped pages, page crossings and ordinary TLB cores retain the original callback path. The guest value and callback success/failure semantics are preserved; writes are unchanged.

## Controlled runtime comparison

Control: `17e19c3d5`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 6.8690 → 6.8421 | +0.39% | +0.21% | -0.02% | 3/4 |
| 2 | Sky Force | 19.5810 → 19.5541 | +0.14% | +0.20% | -0.97% | 2/4 |

Sky Force retires 0.97% fewer native instructions, but its CPU throughput improves only 0.14% with two of four pairs faster. Snakes improves 0.39%, a small result. The full correctness and native-path evidence is retained; it establishes a working shortcut, not a useful runtime improvement. This is a local diminishing-return result for this particular replacement.

Snakes pairs range from -0.12% to +0.99%; candidate wall speed is **2.16× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from -2.09% to +1.52%; candidate wall speed is **0.82× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

The actual warmed V8 capture recovers all 160 selected versions. InterpreterMainLoop shrinks from 166,080 to 164,800 native bytes. Its LDREX callback now checks the published view at +0x1c400 through +0x1c430, resolves the read page and loads directly at +0x1c44d through +0x1c47d, then jumps past MMU discovery and read/TLB publication. The direct data load at +0x1c479 has gameplay samples; the fallback starts at +0x1c486. This confirms an executed fast path, without claiming fallback never occurs. The old read path includes the MMU search, page-directory walk and TLB publication.

1,248 differential read/MMU comparisons pass across the multiple and flexible memory models; 336 eligible calls prove the fast path by leaving a deliberately emptied TLB unfilled. Coverage includes 8/16/32/64-bit widths, aligned/unaligned/page-edge accesses, ASID switches, remaps/aliases, non-readable and unmapped pages, missing/dirty views, logging, and a fresh TLB core beside a direct core. The monitor concurrency model passes 254,407 checks. The full runtime suite passes 185 tests with zero failures; existing diagnostic skips and expected failures are unchanged. Both exact 60-frame image/progress/PCM/audio-event replays pass. The initial fixture setup was corrected to compare committed bytes, since flexible and multiple models use different commit return units.

The prototype is archived and removed from active source. LAN retains the preceding adopted artifact.

See [full observations and evidence](PUBLISHED_EXCLUSIVE_READ_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-hotspot-round/exclusive-direct-read`.
