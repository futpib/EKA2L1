# Adopt cache-policy specialization after gameplay profiling

The browser launcher, profiler and replay harness now default to
`EKA2L1_HOTPATH=2`. This selects the existing trusted-byte/original-layout
lookup specialization at the outer execution boundary, avoiding repeated
policy selection inside compiled chains. Explicit `EKA2L1_HOTPATH=0` retains
the general path. The other hotpath bits remain off.

The same-binary reversed-order comparison improves measured throughput by
**7.81% in Sky Force combat and 1.95% in Snakes**, with both pairs favoring the
candidate in each game. These are incremental measurements with Thumb direct
memory already enabled and IR policy 17 held fixed. They are a small screen,
not precise speed guarantees for arbitrary play; Sky Force remains below
realtime.

## Why this experiment

The preceding pass's Chrome captures already use the exact newly served binary
and settings: Thumb memory 1, IR 17, hotpath 0. Those control captures are reused;
they were not recollected here. RAM compiled-code lookup accounts for 9.36% of
the Snakes guest worker's sampled span and 2.25% of Sky Force's. Source inspection
shows that lookups still select fixed cache-layout and executable-byte policies.

Fresh candidate captures confirm execution of `find_trusted_original`, replacing
`find_original`. Its self samples are 8.89% in Snakes and 2.46% in Sky Force.
All traces report no data loss. These are self-sample fractions of the selected
worker's span, including waits and collection margins, not process utilization.
The cache bucket remains substantial. The profiles identify the target and
confirm path selection; they do not establish the throughput gain.

The setting also selects a different compiled runner specialization, so V8
optimizes a different function body. The measured gain cannot be attributed
solely to the few removed loads from these function-level samples. In particular,
Sky Force's gain exceeds its separately named RAM-cache self-sample bucket.

The implementation and broad historical correctness coverage already exist in
[the V29 acceptance record](HOTPATH_SPECIALIZATION_RESULTS.md). Its earlier
[short timing screen](HOTPATH_SHORT_SCREEN_RESULTS.md) was inconclusive. Neither
older timing panel is pooled with the current results.

## Serial unprofiled measurements

The fixed order was control/candidate/candidate/control for Snakes, then the
same order for Sky Force. Each observation used a fresh Chromium process on
the physical NVIDIA GPU, shared audio and capture mode 1 (readback and hashing,
without PNG compression). Sampling, Chrome tracing and verification were off;
the binary has custom diagnostics compiled out. No owned build, profiler or
correctness job overlapped the panel. Other host work was not controlled.

| # | Workload | Control seconds | Candidate seconds | Mean throughput change | Paired changes |
| --- | --- | --- | --- | ---: | --- |
| 1 | Sky Force combat | 11.08540, 11.09060 | 10.15710, 10.41170 | +7.81% | +9.14%, +6.52% |
| 2 | Snakes standard | 2.31437, 2.29205 | 2.28868, 2.22981 | +1.95% | +1.12%, +2.79% |

Throughput change is `mean(control seconds) / mean(candidate seconds) - 1`.
Mean durations are 11.08800 versus 10.28440 seconds for Sky Force and 2.30321
versus 2.259245 seconds for Snakes. All eight observations are retained, with
no trimming, substitutions or host-load normalization.

Only `EKA2L1_HOTPATH` changes within each game's commands. Every observation has
identical guest work and presentation boundaries:

- Sky Force: guest microseconds 42,000,001–48,000,000,
  2,171,043,925 instructions and 192 presentations.
- Snakes: guest microseconds 21,000,000–25,000,000,
  644,728,231 instructions and 84 presentations.

Warmup is outside the timed window and retained in the JSON. Sky Force control
warmups are 78.59/82.37 seconds, versus 75.34/75.83 with the specialization.
Snakes warmups are 12.30/12.05 versus 12.05/12.31 seconds.

The production binary is unchanged, SHA-256
`6a7d07b370cd7ce0fd08405c46f52644a3bd8bd8776b536fdd8ac6267842a578`.
Baseline source is `39c559f3b`. This is a policy comparison, not a new-binary
layout comparison or a cumulative measurement of earlier optimizations.

## Validation and adoption

Mapping source/generation, ASID, backing/extent, dependency and lifetime checks
remain. The specialized path is eligible only for trusted executable bytes and
the original cache layout; other modes fall back to the established general
path. Budgets, interrupts, faults, guest scheduling and the existing unsafe-byte
assumption are unchanged.

The current WASM test target was rebuilt and passes 16,384 cache lifecycle
comparisons, 16 active/inactive verifier protection checks, and 288 actual
lookup/runner guard-publication checks. These are focused tests, not a fresh
full compiler or native fault-suite run.

Both fresh game replays use the new shared defaults with the hotpath, Thumb and
IR environment overrides absent. Each matches all 60 reference frame records,
pixels, guest timestamps, instruction counts, PCM and audio events exactly.
References are the existing native-matched runs, not regenerated candidate
references. Compiler-policy tests cover valid overrides, invalid values,
configuration failure, missing/wrong readback and policy-dependent HTML ETags.

The launcher now configures and reads back the hotpath policy; previously it did
not expose that option. The live service at `https://claude-laptop.lan:8188/`
uses source defaults without hotpath, Thumb or IR environment overrides. Real
Chromium/NVIDIA Vulkan checks select both games through the launcher, drive
keyboard input into gameplay/combat, and confirm policy 2, Thumb memory 1,
IR 17, the expected binary, advancing guest time and rendered frames. Both
finish without page errors or emulator aborts; screenshots show active play.
Replay, build and live checks ran after timings, sometimes concurrently. Their
elapsed times are excluded from performance evidence.

Reload the launcher to use the new default. Commands, all timing reports,
profiles, hashes, focused test output, exact comparisons and live readbacks are
retained in [`CACHE_POLICY_DEFAULT_RESULTS.json`](CACHE_POLICY_DEFAULT_RESULTS.json).
Raw captures and screenshots are under `/home/claude/.scratch/eka-profile-cache/`.
