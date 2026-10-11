# Deadline-aware paced WASM idle waits

Measured and deployed 2026-10-11. Hooked On's interleaved clear-map comparison
improved from **29.23 to 30.91 FPS**, an observed **5.7%** gain, with guest time
remaining at 1x. The production idle wait now uses the nearer of the next timer
deadline and 1 ms. These are short measurements on a shared host, not a guaranteed
gain on every device.

This adopts the follow-up to the [paced deficit profile](HOOKED_PACED_PROFILE.md).
That profile identified excess timer wakeup latency; its fixed 100 us diagnostic
was not adopted as a global polling interval.

## Implementation

When no guest thread is runnable, the paced WASM loop queries the earliest timer
event without advancing guest time or firing callbacks. It subtracts host time
elapsed since the last clock update, then requests a sleep of at most 1,000 us.
An already-due event causes no sleep; no pending event retains the 1 ms wait.
Host scheduling can still delay actual wakeup beyond the requested deadline.

The query and clock advancement share the existing elapsed-time policy: explicit
pauses reset the origin and host gaps over 100 ms are discarded. Guest timer
intervals, the 2 ms watchdog, native Qt execution and unpaced execution are
unchanged. There is no new launch option or instrumentation.

## Interleaved comparison

Normal paced, count-free Chromium 153.0.8010.52, hardware ANGLE/Vulkan on NVIDIA
Quadro T1000 Max-Q, Intel Core i7-10875H. Stock Nokia 5320 RM-409 and the existing
Hooked On 0.74 package. The scene is the Costa Rica map after completing the anchor
introduction and returning from fishing: stationary boat, animated water/clouds,
no help overlay. Start/end screenshots were checked. FPS counts screen
presentations, including repeated images, per host second.

Both sessions were warmed up. Only one emulator ran at a time; the other owned
browser renderers were suspended. The order was baseline A, candidate A, candidate
B, baseline B. No CDP CPU profiler or debugger was attached. CPU runtime comes from
Chrome process counters and Linux per-thread scheduler counters.

| # | Configuration | Frames / host seconds | FPS | Guest / host time | Busiest renderer thread, % of one core | Mean sampled all-core GHz |
|---|---|---:|---:|---:|---:|---:|
| 1 | Baseline A, fixed 1 ms | 594 / 20.23421 | 29.36 | 1.000025 | 25.92 | 2.68 |
| 2 | Candidate A, next deadline | 626 / 20.26661 | 30.89 | 1.000012 | 23.52 | 3.17 |
| 3 | Candidate B, next deadline | 626 / 20.24301 | 30.92 | 0.999976 | 25.86 | 2.39 |
| 4 | Baseline B, fixed 1 ms | 591 / 20.29947 | 29.11 | 1.000019 | 35.02 | 1.52 |

Aggregate rates use total frames divided by total host seconds, giving 29.23495
and 30.90624 FPS. Both candidate windows exceeded both baseline windows. Emulator
compilation was 0.475/1.252 ms in the baseline windows and 0.869/0 ms in the
candidate windows; no recorded page/guest access faults occurred.

The busiest renderer thread was not independently mapped to the CPU worker in
this campaign. Its 24–26% candidate utilization rules out continuous full-core
spinning in these windows, but does not establish a precise CPU or power saving.
CPU frequency and other workloads varied. The frequency column is an average of
once-per-second readings across all cores, not the emulator thread's effective
frequency and not a basis for normalizing FPS.

## Earlier observations and limits

Earlier noninterleaved clear-map windows measured baseline **30.06/29.26 FPS** and
candidate **27.71/22.71 FPS**. Those observations are retained in the JSON. After
the candidate slowdown, cores were observed near 1.3 GHz; later CPU0 reached
3.6 GHz without a power-setting change. Other workloads, including an Android
emulator/build, were left running. Frequency, load and warmup effects were not
isolated, so the exact cause of those lower results remains unresolved. They
prompted the interleaved campaign rather than being discarded as invalid.
The last exploratory window also lost 32 per-thread counter identities; all
interleaved counter collections were complete.

The interleaved result supports adoption alongside the prior one-variable binary
probe, but is not an isolated-host confidence interval. The previous Qt rate of
31.66 FPS and the 100 us diagnostic's 31.71–31.76 FPS are historical context;
neither was remeasured here. This change does not claim to remove all of the gap
or optimize the remaining COTD software-rendering hotspot.

## Regression and deployment checks

| # | Check | Result |
|---|---|---|
| 1 | Native timer tests | 138 assertions in 13 cases passed, including query side effects, earlier event/cancellation, due events, backwards host time, pause and long-stall handling |
| 2 | Hooked On, forced 250 ms renderer stall | Recovery 30.90 FPS, guest time 0.99995x, input received |
| 3 | Hooked On, forced six-second renderer stall | Recovery 30.58 FPS, guest time 1.00007x, input received |
| 4 | Snakes opening level, 15.12 seconds | 16.00 FPS, guest time 1.00011x, active gameplay visually verified |
| 5 | Sky Force stage 1, 15.14 seconds, live port 8188 | 31.97 FPS, guest time 0.99998x, active shooting visually verified |
| 6 | Build and deployed bytes | WASM and native test builds passed; served WASM SHA-256 equals tested candidate |

The largest sliding one-second guest/host ratios after the two forced stalls were
1.00165 and 1.00235, with no catch-up burst. Snakes and Sky Force are candidate
regression checks, not fresh paired baseline comparisons. Their windows included
34.789 ms and 22.479 ms of emulator compilation, respectively. All gameplay
windows retained zero instruction accounting and had no recorded page/guest
access faults.

Audio output was not verified: Chromium reported an audio-device/WebAudio-renderer
error and its AudioContext remained suspended. During a ten-second retry the
emulator's received PCM counter advanced from 12,757,931 to 13,238,887 frames, but
there were no worklet counters to validate playback or underruns. This is a limit
of this run, not an audio-correctness claim.

Commands used:

```sh
cmake --build build-wasm --target eka2l1_wasm -j4
cmake --build build --target ekatests -j4
# From build/src/tests:
./ekatests '*timer*,Paced idle*,Rounded User After*,[timer]'
```

Existing libuv link warnings remained in the WASM build. Module validation passed.
The live service is <https://claude-laptop.lan:8188/>. Its build override points to
`/home/claude/.scratch/eka-idle-deadline-20261011/build`; the game data archive is
byte-identical to the previous deployment. No ROM or game package was committed.

## Evidence

- Source base: `7453d2bc6` on `wasm-port`; executable source before this change
  matches `86a64bdea` (intervening changes were documentation).
- Baseline WASM SHA-256:
  `dec37577f64f2b6addc7d3d1d9e877fe4bcd288bbc0b39022fd4c22236130f63`.
- Candidate/served WASM SHA-256:
  `b727df4739bd8622907eda634286d27957399f445216dc777e1b73e2c254adba`.
- Unchanged data SHA-256:
  `8b7dd0983892bd09e5a12b06788a7a4073a8431d87890ffd2c8519dc1bbaadba`.
- [Numeric results and raw artifact paths](idle-deadline-results.json).
- Raw root: `/home/claude/.scratch/eka-idle-deadline-20261011/`.
  `game.mjs`, `observed-window.mjs`, `stall.mjs` and each run's `steps.log` retain
  launch, scene setup and capture commands. `collect-results.py` validates and
  reduces the raw results. Screenshots and full sample series remain there.
- A partial asset-interception attempt failed before gameplay; the successful
  candidate tests used a complete HTTPS server. That setup failure has no FPS
  result and is separately recorded in the JSON.
