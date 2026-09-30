# Selecting exact comparators at snapshot construction

The preceding size-switch experiment failed its gameplay comparison. Mode 4 now
chooses the comparator once when constructing each cache entry or dependency,
rather than branching over size on every lookup. Short word-sized snapshots use
fixed C++ functions for 0–64 bytes; other lengths use the grouped scanner. Their
snapshot lengths remain fixed for that cache version. No expected bytes are
embedded, no new modules are generated, and guest instructions are unchanged.

Lookup still resolves/checks mapping generations, address space and entry liveness
before reading backing memory. Every primary and dependency byte participates in
exact equality whenever the original path required it. A mismatch still kills
that version. This tested build has code versions/lifecycle and protected writes
disabled. The original-emitter compiler policy 7 and folded TLB remain selected.
The default mode is unchanged; this research path requires explicit mode 4.

Each snapshot gains a function pointer; the hot path adds an indirect call.
Binary inspection finds all 17 specialized functions, with 851 combined WASM
body bytes. This is bytecode size and coverage, not native machine-code cost.
Those costs can outweigh selecting the size once. Therefore the real-game tests
compare mode 4 with grouped mode 2 in one binary AND the untouched merged archive.
No speedup follows merely from the prior isolated fixed-size microbenchmark.

## Correctness

All 162 compiler tests, 32 native CPU cases (531 assertions) and frontend controls
pass. Cache tests cover both lookup layouts and grouped/stored comparator modes,
28-byte specialized and 128-byte fallback snapshots, primary/dependency mutations,
terminal mismatch rejection, cache collisions, remaps, unmapping, extent shrink,
address spaces, replacement generation sources and explicit invalidation. A
coverage assertion requires the short entry to hold a specialized function and
the long entry to hold the generic fallback. The exact-byte matrix also invokes
the selected functions over all its lengths, alignments and mutations.

The separate actual-WASM memory-boundary harness passes 232,143 comparisons for
mode 4 across 55 lengths, including zero and both buffers ending at memory end.
This helper selects the target for each test call; the cache tests separately
verify the stored selection and its mapping/rejection contract. Raw helper hashes
and the complete archived helper are retained.

Rebuilt fault probes explicitly select and verify mode 2/4, policy 7 and folded
TLB. All 13,760 cases per mode match native exactly, 27,520 total. Both checked
1,600-image browser replays match the merged native reference, guest records,
4,656,051 stereo PCM frames and audio events. The mode-4 longer-route replay also
matches all 360 images and audio. Concurrent correctness runs are not timing
measurements. No unchecked-replay or live/audio acceptance is claimed yet.

## Status

Correctness passes; serial observations are recorded below. The comparison uses the new merged
workload: longer route 42–60 seconds, with the validated 60–78-second standard
holdout available if warranted. The full original route's native-matched restart
and failed uninterrupted-gameplay threshold remain documented in the merge report.
No push or deployment. Live Snakes remains on the separately verified older archive.

Raw artifacts are /home/claude/.scratch/eka-benchmark/stored-comparator-*.
STORED_COMPARATOR_EVIDENCE.json retains binary/source hashes, complete logs,
explicit policy evidence, comparisons and reports. The immutable source archive
records the base commit and implementation patch before this checkpoint.

## Gameplay measurements

Fresh browsers run serially, including warmup, with hardware NVIDIA graphics,
shared audio, rendering on and capture/profiling/counters off. Batch A order:
grouped, stored, baseline, baseline, stored, grouped. A reordered batch B was prepared but not run: this completed batch
already gives no support for promotion. Grouped/stored use scanner2/4 inside the same
binary; baseline is the untouched merged archive with scanner2. Every sample
and outlier is retained. Guest instruction endpoints and presentations agree
exactly across all six observations within each scene/batch. These are comparisons
within the new upstream workload, not speed claims against the old live archive.

| Scene/batch | Grouped mean s | Stored mean s | Merged baseline mean s | Throughput vs grouped | Throughput vs baseline |
| --- | ---: | ---: | ---: | ---: | ---: |
| long-a | 13.15640 | 14.01910 | 12.73460 | -6.15% | -9.16% |

All individual timings, exact modes, binary hashes and complete browser reports
are retained in STORED_COMPARATOR_EVIDENCE.json. No outliers are removed or normalized.

## Decision

Rejected for delivery. Both stored-comparator observations are slower than their
adjacent matching controls and the untouched merged controls. The mean is
14.01910 seconds versus 13.15640 and 12.73460 respectively. This batch does not
isolate the cause or establish a precise regression size; it provides no evidence
that selecting an indirect comparator once improves gameplay. No further timing
or live acceptance is warranted for this candidate. Mode 4 remains opt-in, and
the live archive is unchanged. All six observations remain in the evidence.
