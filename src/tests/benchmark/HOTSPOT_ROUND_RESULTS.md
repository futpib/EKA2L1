# Profile-driven hotspot round

Completed with **seven runtime optimizations enabled by default** on `wasm-port`, through `5a30297fb`. The measured build is served at `https://claude-laptop.lan:8188/`. Three additional candidates did not earn adoption and were removed from active source; their patches and full observations remain archived.

## Direct combined result

This is a fresh comparison of the original runtime `536ea5af3` against all seven accepted changes together. It does not add or multiply the incremental campaign percentages. Each game executes the same fixed 18-guest-second replay window on both builds. Snakes uses guest seconds 78–96; Sky Force combat uses 42–60.

| # | Game | Worker CPU seconds, before → after | CPU throughput gain | Wall throughput gain | Retired native instructions | Unpaced wall speed, before → after | Faster pairs |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | Snakes | 7.1368 → 6.8226 | +4.60% | +3.64% | -2.63% | 2.09× → 2.16× | 4/4 |
| 2 | Sky Force | 22.1715 → 19.4096 | +14.23% | +12.81% | -13.80% | 0.73× → 0.83× | 4/4 |

All 16 valid observations are included, with 0 invalid attempts. ABBA and BAAB give four launches per build per game. Worker CPU 7 and its reserved sibling 15, measured 3.6 GHz clock, counter and affinity checks remain unchanged. No temperature gates or cooldowns apply. No owned build, profile or correctness job overlapped controlled timing. All 88 final live host-restoration checks pass.

Snakes CPU pairs: +4.05%, +4.09%, +6.08%, +4.21%. These are individual comparisons, not confidence bounds.
Sky Force CPU pairs: +17.06%, +10.02%, +13.87%, +16.19%. These are individual comparisons, not confidence bounds.

See [all combined observations and provenance](HOTSPOT_ROUND_RESULTS.json), [controls](CONTROLLED_BENCHMARKS.md), and [scope](CONTROLLED_REASSESSMENT.md). The fixed-clock, unpaced replay ratios are workload measurements, not a promise of that speed throughout every level.

## Accepted changes

| # | Change | Why it helps | Incremental evidence |
|---:|---|---|---|
| 1 | Static built-in syscall bindings | Avoids owning callable copy/invoke/destruction; mutable callbacks retain snapshots | [Results](STATIC_SVC_BINDINGS_RESULTS.md), `554a92f1e` |
| 2 | Empty exclusive-monitor clearing | Avoids synchronization when there is no reservation to clear | [Results](EMPTY_MONITOR_CLEAR_RESULTS.md), `7a5ec3472` |
| 3 | Generic EUser-pattern list scanning | Keeps scan values and flags local, retaining ordered accesses and exact exits/counts | [Results](LIST_SCAN_RESULTS.md), `b8f65984e` |
| 4 | Private compiled-runner counters | Avoids repeated result-field traffic and repeated registry acquisition | [Results](RUNNER_LOCALS_RESULTS.md), `50c70b115` |
| 5 | Registry-only ROM bounds check | Removes a redundant comparison without lengthening the outer RAM path | [Results](ROM_REGISTRY_RANGE_RESULTS.md), `ec32cd81d` |
| 6 | Packed monitor summary | Publishes reservation state using the existing atomic lock; preserves shared synchronization | [Results](PACKED_MONITOR_RESULTS.md), `17e19c3d5` |
| 7 | Exact-order audio SIMD | Computes four interpolation accumulators together while preserving PCM | [Results](AUDIO_SIMD_RESULTS.md), `5a30297fb` |

Audio SIMD has a +3.03% retained incremental Sky Force mean, but one +8.86% pair inflates it. Its other three pairs are +0.80% to +1.33%; Snakes loses 0.47%. It is retained as a consistent small Sky Force win under the accepted tradeoff, not as proof of a reliable 3% isolated audio saving.

## Rejected follow-ups and stopping point

| # | Candidate | Incremental Sky Force CPU result | Disposition |
|---:|---|---:|---|
| 1 | Combined ROM range rewrite | +0.26%, 2/4 favorable | [Full result](ROM_RANGE_RESULTS.md); [archived patch](ROM_RANGE_EXPERIMENT.patch). Its separate registry-only part subsequently won. |
| 2 | Published-view exclusive reads | +0.14%, 2/4 favorable | [Full result](PUBLISHED_EXCLUSIVE_READ_RESULTS.md); [archived patch](PUBLISHED_EXCLUSIVE_READ_EXPERIMENT.patch). 0.97% fewer native instructions did not buy useful runtime. |
| 3 | CPU-owned monitor locking bypass | +0.93%, 2/4 favorable | [Full result](CONFINED_MONITOR_RESULTS.md); [archived patch](CONFINED_MONITOR_EXPERIMENT.patch). Native lock bypass worked, but timing did not establish a useful gain. |

This reaches diminishing returns for the investigated local changes: several further reductions produce mixed subpercent results, and the final accepted audio change has mostly roughly 1% paired gains. It does not establish a global performance ceiling. Region handoffs, lookup and CPU-state transfer remain the larger architectural opportunity; another small branch deletion is not automatically a win.

## Final profile

Actual gameplay samples are matched to timestamped V8 code versions, covering both C++ runtime functions and generated guest code. The following shares come from separate warmed 42–60-second Sky Force profiles. They describe where remaining time goes, not absolute timing gains; percentages can rise when other code gets faster.

| # | Sampled function | Before | After |
|---:|---|---:|---:|
| 1 | Compiled-region runner (calls, inlined lookup and return work) | 26.16% | 28.46% |
| 2 | Outer interpreter/CPU loop | 11.80% | 11.86% |
| 3 | EUser list scan at 0x8019d818 | 11.56% | 6.36% |
| 4 | C++ syscall dispatch | 4.73% | 3.99% |
| 5 | Standalone compiled lookup | 1.49% | 2.70% |
| 6 | C++ single-precision audio resampler | 1.18% | 0.75% |

The hot game RAM cluster at `0x70008184–0x700082ff` belongs to `skyforce.exe` (UID `0xa020d913`). Its imported call resolves through EUser ordinal 674 to SVC 5, `tick_count`; the caller computes elapsed ticks and often returns early. This is polling/frame-control work. Skipping it would alter guest timing and scheduling. Simple import veneers were already inlined. Cone/Ws32 samples are spread across state transfers, flag work and calls; no separate guest-library rewrite was adopted. The shared runner and syscall improvements apply across callers.

## Correctness and served artifact

Every accepted addition passed both 60-frame exact image, guest-progress, PCM and audio-event replays. Focused independent model and differential tests cover syscall callback mutation, monitor concurrency, full CPU state, short budgets, faults, mapping changes and runner exits. The full WASM suite passed 185 tests with zero failures during the final monitor/read investigation; the subsequent audio-only change additionally passed 7,680 bit-exact arithmetic cases and all 20 real shared-audio-driver fixtures against the scalar build. Per-change reports retain the exact tests rather than claiming that every full suite was repeated after every edit.

The final combined comparison reuses replay evidence only after verifying all six frozen artifact hashes match the already-tested adopted build. The active workspace build matches those hashes too. Both real HTTPS game-picker paths pass gameplay, input and default-policy checks using NVIDIA hardware; both gameplay screenshots were inspected. The existing non-silent browser-audio check still fails for both games, so full live-audio E2E is not claimed. Exact PCM and audio events pass.

Plans, source patches, hashes, all observations, native captures, tests and the completed progress journal remain under `/home/claude/.scratch/eka-hotspot-round`. The [central experiment index](EXPERIMENT_INDEX.md) includes every disposition.
