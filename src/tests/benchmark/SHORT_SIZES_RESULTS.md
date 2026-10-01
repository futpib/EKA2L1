# Fixed-size short code comparison experiment

Mode 3 specializes word-sized spans from zero through 64 bytes using bounded C++
templates. Other sizes retain the grouped 64-byte scanner and exact vector/scalar
tails. Both buffers are read; no expected bytes are embedded and no per-entry
validator is generated. Every primary/dependency byte and existing mapping,
address-space and live-entry check remains. Mode 0 remains the configured default;
the delivered archive still explicitly selects mode 2 and is unchanged.

The preceding isolated discriminator supported testing fixed sizes but rejected
constants for larger spans. This source change has a cost of its own: the complete
multi-mode comparator is now an out-of-line 1,284-byte WASM function; the merged
baseline has no separately named comparator in the inspected name section.
The cache find_original body grows from 1,001 to 1,166 bytes. Neither byte count
establishes machine-code cost or a gameplay benefit. Measurements must compare
mode 2/mode 3 in the same binary AND the untouched merged archive.

## Correctness

All 162 WASM compiler tests, 32 native CPU cases and frontend configuration/startup
checks pass. The exact-byte test now covers all four modes, every-byte mutations,
independent alignments, vector/scalar tails and paired mismatches. A separate
browser boundary harness passes 232,143 checks across 55 lengths, including inputs
ending at the actual WASM memory boundary and zero-length spans, with production
mode 3 explicitly selected. Two initial frontend-test failures exposed old mode
allowlists and a hardcoded ETag count; those test/control paths are corrected.
The captured failing log is retained, not described as a runtime defect.

Rebuilt native/WASM fault probes explicitly verify policy 7, folded TLB and scanner
mode 2/3. All 13,760 comparisons per mode match exactly, 27,520 total. Both checked
standard browser replays match the merged native reference for all 1,600 images,
guest records, 4,656,051 stereo PCM frames and audio events. Candidate mode 3 also
matches the 360-image longer route exactly. These gates run concurrently; their
elapsed times are not performance evidence. No new unchecked-replay or live/audio
acceptance is claimed here.

The merge changes the workload. Its full old script includes a native-matched
level restart near 83 seconds and retains the failed uninterrupted-gameplay check.
For any standard-scene holdout, a predeclared 60–78-second window of that unchanged
route contains 381 consecutive native images and passes the unchanged gameplay
thresholds: 21.17 changing images per guest second, 62.685 ms maximum gap, 20.83% minimum
viewport change. The source frame indices and complete full-route failure remain
recorded; this does not relabel the entire original script as uninterrupted play.
The already verified longer-route timing window remains 42–60 seconds.

## Status

Implementation correctness passes. Serial gameplay observations are recorded below.
No push or deployment.
Raw logs, immutable candidate and captures are under
/home/claude/.scratch/eka-benchmark/short-sizes-*; hashes and all acceptance evidence
are in SHORT_SIZES_EVIDENCE.json. Source controls accept mode 3 only before guest
startup and verify requested modes in the browser/fault measurement harnesses.

## Gameplay measurements

Fresh browsers run serially, including warmup, with hardware NVIDIA graphics,
shared audio, rendering on and capture/profiling/counters off. Batch A order:
grouped, fixed, baseline, baseline, fixed, grouped. Batch B: fixed, baseline,
grouped, grouped, baseline, fixed. Grouped/fixed use scanner2/3 inside the same
binary; baseline is the untouched merged archive with scanner2. Every sample
and outlier is retained. Guest instruction endpoints and presentations agree
exactly across all six observations within each scene/batch. These are comparisons
within the new upstream workload, not speed claims against the old live archive.

| Scene/batch | Grouped mean s | Fixed mean s | Merged baseline mean s | Throughput vs grouped | Throughput vs baseline |
| --- | ---: | ---: | ---: | ---: | ---: |
| long-a | 13.11185 | 13.94720 | 12.29685 | -5.99% | -11.83% |

All individual timings, exact modes, binary hashes and complete browser reports
are retained in SHORT_SIZES_EVIDENCE.json. No outliers are removed or normalized.

## Decision after the first batch

No promotion. The fixed-size path averages 13.9472 seconds against 13.11185 for
the same-binary grouped control and 12.29685 for the untouched merged archive.
Both comparisons against the merged baseline favor baseline; the two grouped
comparisons disagree because the closing grouped control is also slow. All six
observations are retained. This does not establish a precise causal regression,
but it provides no useful application gain from the isolated fixed-size result.
The experiment remains opt-in and unserved.

The new switch still selects a size on every comparison. The next discriminator
will select a comparator once when constructing a cache snapshot, then retain
exact bytes and mapping checks on every lookup. Indirect-call and entry-size
costs may erase that advantage; it is a separate hypothesis, not a promised fix.
