# Adopt guarded Thumb memory after profiling normal play

The browser launcher now enables the existing guarded direct Thumb memory path.
On the current policy-17 binary, the serial Sky Force comparison improves
throughput by **37.82%**. Snakes has opposing pairs and does not establish a gain
or zero regression. Its mean changes by -2.46%, with realtime headroom in every
observation. This is an adoption of an existing compiler implementation, not a
new code-generation optimization or a claim that Sky Force now runs in realtime.

The investigation found a configuration mismatch: earlier profiling recipes
explicitly enabled Thumb memory, while the normal launcher inherited the C++
default of off. The launcher, `profile.ts` and `benchmark.ts` now share defaults
for Thumb memory (1) and IR policy (17). Explicit environment overrides remain
available, including `EKA2L1_THUMB_MEMORY=0` for the callback control. Other
service-specific settings still need to be matched explicitly.

## Profile evidence

Fresh Chrome sampling of both modes used the same stock 5320 assets, policy 17,
input and frozen runtime. The callback mode matches the previously served
Thumb setting. All four traces report no data loss.

| # | Workload | Callback memory helper self samples | Direct memory helper self samples |
| --- | --- | ---: | ---: |
| 1 | Sky Force combat, guest seconds 42–48 | 17.20% | 0.28% |
| 2 | Snakes standard, guest seconds 21–25 | 0.81% | 0.00% sampled |

These percentages sum `raw_read*`/`raw_write*` self samples over the selected
guest worker's sampled span, including waits and capture margins. They are not
whole-process utilization or speedup estimates. A zero sample count does not
prove that a helper never executes.

The Sky Force stacks attribute these helper samples to many generated Thumb
regions. Among the largest callers are EUser.dll entries at `0x801a77fc`,
`0x801a1034`, `0x801a781c`, and Ws32.dll at `0x804b16d0`. These are region-entry
and ROM-range attributions, not exact sampled ARM instructions or independently
identified internal functions. The evidence JSON retains caller weights.

The existing direct path avoids WASM-to-WASM memory helper calls and the
associated register-cache publication/reload on successful accesses. It retains
permission/TLB, page, alignment and endian checks, plus the original callback
fallback. Direct stores require the already-adopted unsafe executable-byte
mode 3; enabling this option adds no new code-mutation assumption. See
[implementation and historical correctness coverage](THUMB_MEMORY_DESIGN.md).

## Unprofiled same-binary comparison

The fixed order was callback/direct/direct/callback for Sky Force, then the
same order for Snakes. Each observation used a fresh browser, hardware NVIDIA
rendering, shared audio, capture mode 1 (readback/hash, no PNG compression),
sampling off, tracing off, verification off and a diagnostics-free build.
No owned build, profiler or correctness job overlapped the timings. Other host
work was not controlled. Every result is retained; no samples were discarded.

| # | Workload | Callback seconds, both observations | Direct seconds, both observations | Mean throughput change | Paired changes |
| --- | --- | --- | --- | ---: | --- |
| 1 | Sky Force | 14.86390, 14.53610 | 10.79120, 10.54070 | +37.82% | +37.74%, +37.90% |
| 2 | Snakes | 2.40916, 2.28564 | 2.25054, 2.56248 | -2.46% | +7.05%, -10.80% |

Throughput change is `mean(callback seconds) / mean(direct seconds) - 1` for
identical guest work. Sky Force's mean duration falls from 14.70000 to 10.66595
seconds, a 27.44% reduction, for roughly six guest seconds. This still falls
short of realtime. Snakes' mean duration is 2.34740 versus 2.40651 seconds for
four guest seconds; the short, noisy panel does not resolve a small effect.

All four observations per game have identical presentation boundaries and
instruction totals:

- Sky Force: guest microseconds 42,000,001–48,000,000,
  2,171,043,925 instructions and 192 presentations.
- Snakes: guest microseconds 21,000,000–25,000,000,
  644,728,231 instructions and 84 presentations.

Only `EKA2L1_THUMB_MEMORY` changes between each game's commands. IR policy 17,
unsafe mode 3, feature 128, lookup 0, comparison 2, TLB hash 1 and the other
recorded settings remain fixed. Warmup is outside the measured window and is
retained in the JSON: Sky Force takes 105.71/110.26 seconds with callbacks and
78.10/74.83 seconds with direct accesses. Profiled wall times are not used in
this throughput comparison. The older v2 timing panel is not pooled with it.

## Correctness and deployment

The WASM binary is unchanged:
`6a7d07b370cd7ce0fd08405c46f52644a3bd8bd8776b536fdd8ac6267842a578`.
It is the same runtime used for the prior loop-budget adoption. Its existing
policy-17/Thumb-1 replays match all 60 frames, guest records and audio in both
games against the native-matched references. These exact-binary results are
reused, not presented as newly rerun compiler or fault suites.

A fresh Snakes replay with both Thumb and IR environment overrides absent
verifies the new shared defaults. All 60 frame records and 1,143,727 stereo PCM
frames match the reference exactly. The compiler-policy tests also pass,
covering explicit off/on overrides, invalid values, configuration rejection and
runtime readback.

The live `https://claude-laptop.lan:8188/` service was restarted using the same
runtime, without either Thumb or IR environment override. Hardware-accelerated
Chromium selected each game through the actual launcher and drove keyboard
input to Snakes gameplay and Sky Force combat. Both report Thumb memory 1 and
IR policy 17, the expected binary, advancing guest time and presentations,
nontrivial rendered canvases, and no page errors or emulator aborts. A browser
attempt immediately after restart hit connection-refused before the server
finished loading assets; its log is retained separately from the successful
checks. Live checks and replay elapsed times are correctness evidence only.

Reload the launcher to use the new default. There is no per-game policy and no
universal compatibility or speed guarantee. The small Snakes timing panel is
the principal unresolved performance limitation.

Full commands, settings, hashes, profile summaries, all eight timing reports,
correctness comparisons and live readbacks are in
[`THUMB_MEMORY_DEFAULT_RESULTS.json`](THUMB_MEMORY_DEFAULT_RESULTS.json).
Raw captures and screenshots are retained locally under
`/home/claude/.scratch/eka-profile-next/`.
