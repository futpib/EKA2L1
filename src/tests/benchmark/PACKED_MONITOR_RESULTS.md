# Publish reservation state with the existing monitor lock

Adopted by default: publish reservation state through the existing monitor lock.

The monitor publishes its conservative nonempty summary in bit 1 of the existing atomic lock byte. Bit 0 remains the lock. The lock owner maintains the summary as an ordinary private Boolean; its release publishes both states together. Acquisition marks the held lock conservatively nonempty. Empty clears still return without acquiring the lock, and marking or clearing a reservation no longer needs a second atomic store.

## Controlled runtime comparison

Control: `ec32cd81d`. This measures the isolated addition to that frozen runtime. These percentages are incremental and must not be added to gains in other reports.

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.0002 → 7.0075 | -0.10% | -0.33% | -0.00% | 1/4 |
| 2 | Sky Force | 20.6293 → 19.8686 | +3.83% | +3.60% | +0.04% | 4/4 |

Sky Force improves in all four pairs, from +2.34% to +5.70%, with a +3.83% mean CPU throughput gain. Snakes is effectively flat at -0.10% and three of its four pairs are slower. The nearly unchanged native instruction count demonstrates why operation latency matters: the candidate removes atomic stores while adding a few ordinary instructions. The comparison measures the full implementation tradeoff, not a separately isolated cost for each atomic.

Snakes pairs range from -1.80% to +2.90%; candidate wall speed is **2.12× realtime** in the fixed 18-guest-second window.
Sky Force pairs range from +2.34% to +5.70%; candidate wall speed is **0.81× realtime** in the fixed 18-guest-second window.

All 16 valid observations are retained; 0 invalid attempts. Four launches per build per game use ABBA then BAAB. Guest progress, instruction counts and presentation journals match. The worker uses CPU 7 with sibling 15 reserved. Measured clock, throttle, affinity and counter checks validate the 3.6 GHz request. No temperature gates or cooldowns apply. All 88 live host-restoration checks pass.

## Native evidence and correctness

Actual warmed V8 capture recovers all 160 selected versions. InterpreterMainLoop grows from 165,504 to 166,080 native bytes: publishing the summary adds a load/shift at monitor unlock sites. The hot LDREX reservation path replaces its second byte xchg (control +0x1bfb9) with an ordinary byte store, while acquisition/release remain atomic. The full clear similarly avoids the separate summary atomic. This trades a few ordinary instructions for fewer atomic operations; only the controlled runtime comparison determines whether it pays.

254,407 independent reservation-state and concurrency checks pass in both native and actual WASM builds. Coverage includes 1/2/8 processor models, clear/restore/failure behavior, synchronized cross-thread clearing, and two competing threads each completing 3,000 increments with yields and intervening global clears. Exact 60-frame image, guest-progress, PCM and audio-event replays pass for both games.

The measured artifact is served at `https://claude-laptop.lan:8188/`; its served WASM hash matches. Both real game-picker paths pass gameplay, input and default-policy checks with NVIDIA hardware rendering, and saved gameplay screenshots were inspected. The existing non-silent browser-audio failure remains, so full live-audio E2E is not claimed. Exact PCM replays pass.

See [full observations and evidence](PACKED_MONITOR_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and the raw campaign under `/home/claude/.scratch/eka-hotspot-round/monitor-packed`.
