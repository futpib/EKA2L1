# Compiled runner specialization and rejected address experiments

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The retained runner candidate improves real-Snakes throughput by **22.9%** and
**8.8%** in two separate balanced batches. Both candidates beat both controls
in each batch. The two generated-code experiments do not earn promotion.
Implementation commit: `286e28605`. The live and LAN gates pass; the verified
runner build is now served at https://claude-laptop.lan:8188/.

Baseline: `16d22d07f`, with deferred memory misses and compact memory guards
already graduated. This experiment responds to the request for more optimization;
small repeated gains are eligible. The public launcher stayed on the verified
baseline while candidates were measured.

## Fresh profile

A 5 ms sampling run of the exact archived baseline covers real Snakes guest
seconds 78–96, with physical NVIDIA graphics, rendering enabled and capture and
detailed counters disabled. Chromium is 153.0.8010.52. The guest worker has
15.422 sampled seconds: generated regions and their callees account for 7.471s
(48.44%). Self samples include cache lookup/mapping guards 1.654s (10.73%),
code-byte comparison 1.578s (10.23%), lookup wrapper 0.976s (6.33%), and
InterpreterMainLoop 2.074s (13.45%). The latter includes the inlined compiled
runner, not just interpreter fallback. The page is 99.46% idle.

These are sampled categories, not estimates of recoverable wall time. The
profile is not used as a speed trial; comparisons below disable sampling.

## Candidates

1. **Cached address displacement.** The existing per-region page cache stores
   `host_page - guest_page` instead of the host-page address. After the unchanged
   page/alignment-key check, a repeated access becomes `displacement + address`
   rather than `host_page + (address & 4095)`. Calculation uses WASM i32 modular
   arithmetic, including high guest addresses. The displacement is established
   only after the existing permission, endian, alignment and nonzero-base guards.
   Helpers still invalidate cached page keys. Block-transfer span handling is
   unchanged. This shifts an operation from each hit to each page-cache miss.

2. **Deferred-read exit analysis, on top of 1.** An eligible scalar read either
   completes directly without setting AOT_EXIT, or returns before the instruction
   on a miss. It has no returning helper/callback. Therefore its straight-line
   successor does not need another AOT_EXIT check. Its instruction-budget check
   remains. Stores, returning helpers, branch joins and loop entries retain exit
   checks. This uses the semantics of the recently graduated deferred path;
   it does not remove permission checks or assume successful accesses.

Both mechanisms are generic. There are no Snakes names or address whitelists.
Neither replaces code-byte/dependency validation or changes the guest clock.

## Correctness gates

Both candidates pass 134 WASM tests, including 72 added exact state/memory/budget
checks for high virtual pages, aliasing, switching pages and separate read/write
caches. Existing tests include 402,976 budget/state/memory comparisons,
code-write aliases, callback state, permissions, endian, page boundaries and
672 pre-instruction deferred-exit checks. Each candidate also passes all 672
native/WASM fault comparisons (including callback state/order and memory bytes).

Both checked 1,600-image replays match native exactly, including guest records,
PCM and audio events, through 102.484363 guest seconds / 16,261,337,499 guest
instructions. All three native targets and seven frontend checks pass.

## Timing protocol

One browser runs at a time, including warmup. Real gameplay window: 78–96 guest
seconds, physical NVIDIA GPU, rendering on, capture/profiling/counters off.
All observations are retained. This is a shared host; no separate investigation
of unrelated host load is performed. Baseline/displacement/combined followed by
the reverse order isolates the address change and the additional read-exit change.
Only a candidate with a useful initial result proceeds to confirmation.

## First batch: compiler candidates rejected

| Variant | Host seconds for 18 guest seconds | Mean |
| --- | --- | --- |
| Baseline | 15.3467 / 15.5067 | 15.4267 |
| Address displacement | 15.0949 / 15.8452 | 15.47005 |
| Displacement + deferred-read exit analysis | 17.5355 / 15.3487 | 16.4421 |

All runs execute 3,975,200,506 guest instructions and 676 presentations.
Neither candidate establishes a gain: the address-only mean is 0.28% slower,
and the combined mean is 6.58% slower. The source simplifications do not predict
better browser machine code. Both are removed from active production source;
their patches and raw observations are retained. The added address/alias tests
remain useful independently of the displacement implementation.

## Third candidate: runner specialization

The fresh profile still shows substantial compiled dispatch work. The third
candidate uses a template-selected runner without optional diagnostic branches
when detailed counters are disabled. Selection occurs once per chain. Diagnostic
configuration is fixed before guest threads start; the diagnostic runner retains
phase-dependent counting and guest profiling. The verifier retains its original
callback checks. The fixed address of the owning core's embedded TLB array is
calculated once per chain, while memory instructions continue reading current
TLB entries. Remaps, exact code/dependency validation, guest budget accounting,
interrupt checks and code-write exits remain unchanged.

This is separate from the rejected compiler changes above. It passes 134 WASM
tests, all 672 fault comparisons, all three native targets, and both normal and
verifier-enabled 1,600-image exact native replays. A counters-enabled control
matches 80 native images/guest records, with its 40,798,677 compiled blocks,
6,843,907 interpreted instructions and 2,732 decodes reconciling with their
independent totals. These checks exercise the distinct diagnostic and ordinary
runner selections. The final decision and deployment are recorded below.



### Runner first timing batch

| Build | Host seconds for 18 guest seconds | Mean |
| --- | --- | --- |
| Graduated baseline | 17.7189 / 16.1890 | 16.95395 |
| Specialized runner | 13.7921 / 13.8011 | 13.79660 |

Both candidates beat both controls. The observed throughput increase is 22.88%,
reaching 1.3047x realtime in this batch. All four runs have identical guest
instruction totals and presentation counts. This is the combined effect of the
runner specialization, fixed TLB address reuse and the toolchain's resulting
code generation; it does not assign a percentage to each source edit. The separate
confirmation and live acceptance results follow below.


### Runner confirmation

| Build | Host seconds for 18 guest seconds | Mean |
| --- | --- | --- |
| Graduated baseline | 16.4000 / 15.5686 | 15.98430 |
| Specialized runner | 15.3669 / 14.0129 | 14.68990 |

Both candidates again beat both controls. Mean throughput improves **8.81%**,
reaching **1.2253x realtime**. Thus the two batches place this scene at about
1.23–1.30x realtime; they do not establish a fixed 23% gain or consistently
exceed the 1.25x milestone. All eight runs perform identical guest instruction
work and presentations. Host-load variability remains; all observations are
included, and no other owned timing/build/correctness jobs overlap the trials.

The previously graduated optimization percentages must not be added or multiplied
with these estimates. No cross-game or mobile performance claim is made.

## Reproduction and artifacts

The candidate is the source at `286e28605`; the before archive is
`~/.scratch/eka-benchmark/graduation-compact-build`, and the candidate archive is
`~/.scratch/eka-benchmark/runner-specialization-build`. Use
`serial_build_comparison.py ASSETS BEFORE_ARCHIVE AFTER_ARCHIVE NEW_OUTPUT`
with the same process-local GPU environment as prior investigations. It runs
78–96 guest seconds, old/new/new/old, one browser at a time including warmup.
`serial_variants.py` reproduces the six-run compiler experiment.

`address_displacement_experiment.patch` and `address_read_exit_experiment.patch`
are independent patches against the graduated translator: the second includes
both rejected compiler changes, and must not be applied on top of the first.
Both pass `git apply --check` against the retained translator. They are research
artifacts, not active options. The added high-address/alias regression test is
retained on the ordinary memory path independently of the rejected changes.


## Acceptance and deployment

**Graduate the specialized runner.** It has repeated whole-game gains with
all correctness gates passing. The generated-code experiments remain removed.
This does not establish Qt parity or a universal gain outside the tested game,
scene, desktop and browser.

The two-minute manual upload/Start live run advances **120.532136 guest seconds**
in **120.535949 host seconds** (0.999968x paced).
Maximum sampled lag is **0.010778s**. Keyboard/touch, blur release, layout,
presentation and shutdown checks pass; intermediate screenshots show gameplay.
The darkest sample still contains the game grid/objects, not a wholly black
canvas. Queue delivery ranges 0.92–4.30 ms;
that is not end-to-end display latency. This single paced run is acceptance
evidence, not the unpaced throughput estimate and not a guarantee of no stutter.
All seven frontend smoke checks pass too.

The actual trusted HTTPS launcher passes secure-context/cross-origin isolation,
automatic startup, keyboard/touch, narrow layout and shutdown. All 12 added
canvas samples have nontrivial content, with no page/request/HTTP errors. No
separate remote LAN device was tested. Sound remains off.

The served archive is `~/.scratch/eka-benchmark/runner-specialization-build`.
Its loader and WASM match the current build output byte for byte. The served
WASM was independently downloaded over verified TLS and hashes to
`783d8a6b0c88a4d2599dd97c44c44a5a059cdf2b0dcc1de68d127409e5800671`.
Reload https://claude-laptop.lan:8188/ to load it. All work stays on local
`wasm-port` in the single `~/code/EKA2L1` checkout; nothing was pushed.

Raw trial reports, hashes, gate results, live samples and deployment checks are
in `RUNNER_SPECIALIZATION_EVIDENCE.json`. The larger raw profile and screenshot
artifacts remain under `~/.scratch/eka-benchmark/` at the paths recorded there.
