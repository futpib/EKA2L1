# Runtime literal-PC veneer fusion

The tail-prefix census still finds frequent calls to an unconditional `LDR PC,[PC,#imm]` veneer: the leading instance accounts for 5.13/5.39 million longer/standard rejected calls. This is a motivation and upper coverage clue, not a speed estimate. The implementation recognizes the instruction encoding generically; no guest address is special-cased.

Feature 128 admits one unconditional immediate, pre-indexed word load to PC from PC without writeback. It preserves the runtime literal read, ordinary TLB/helper path, callback-visible state, interworking bit, instruction count and precise indirect exit. The loaded destination is not constant-folded. Only the four instruction bytes become a code dependency; neither literal contents nor target code are assumed. The caller's return address is retained. Missing mappings and failed accesses still use existing precise callbacks. The existing emitter performs the load, so this changes eligibility rather than introducing a new memory lowering.

Policy 7 and conditional integer leaves stay enabled. Source/leaf/site/runner limits stay 512/16/8/512; all other extra eligibility features are disabled. The new bit defaults off. Guest scheduling is unchanged.

## Focused verification

- All 174 instrumented compiler tests pass, including 15,360 dedicated interpreter comparisons. The documented pre-existing crash-repro harness XFAIL remains in the log.
- The dedicated test also passes alone. It reuses translated bytes across changed literal destinations and mapped backing pages, checks short budgets, both cache indices, positive/negative literal offsets, ARM/Thumb targets, flags, registers, memory and callback-visible PC/LR. It rejects conditional, byte, post-indexed, writeback, register-indexed and non-PC-base forms. The standalone and full-suite counts overlap.
- Control, candidate and instrumented candidate each match 5,376 independent native fault cases (16,128 comparisons, with repeated coverage across modes). Full callback events, state, memory, instruction count and explicit feature/limit selection match. Both endian modes and repaired, failed, stopped and unresolved-retry callbacks are included. The optional fixture leaves existing archived default matrices unchanged.
- Native/cache tests pass 570 assertions in 33 cases. Launcher tests verify feature 128 configuration/readback, rejected markers and feature-dependent ETags; the parser permits exactly 0–255.
- Native and WASM builds pass. Existing linker warnings remain in the archived logs; they are not represented as clean builds without warnings.

The immutable archive and source patch, binary hashes, scripts, raw fault output hashes and test logs are retained in the evidence. No full browser replay or performance result is claimed at this checkpoint.

## Planned acceptance and timing

After the runner-cap study completes, run full control/candidate native fault matrices (46,016 each, including the optional fixture), checked control/candidate and normal candidate standard image/audio replays, and the checked longer route. Missing or wrong policy readback must fail. Then collect exactly 24 serial observations over both routes with forward and rotated/reversed ordering, comparing feature 128 with feature 0 in the same binary and the untouched served archive. All samples and passive host readings remain. Builds and diagnostics stay outside timing.

A separately checked census will measure compiled calls, indirect exits, dependency spans/bytes, primary byte coverage, entry proofs and code/interval-gap guards. Generated-module size and construction costs will be measured separately. Any apparent throughput gain must survive normal/live/audio and actual HTTPS/cache-upgrade acceptance before delivery. Nothing is pushed or deployed by this experiment.

The preserved completion queue now includes metadata-only generated-module captures after the exit census, with an exact native checked replay of the hooked loader first. Disjoint control/candidate correctness matrices can run concurrently; all must finish before replays and serial timing. No timing order or frozen binary changes. Final disposition records every sample and requires manual live/audio review if comparisons consistently favor the candidate; it never deploys automatically. These are queued procedures, not completed results.

## Full correctness acceptance

Both control and candidate pass 46,016 explicitly selected native fault comparisons (92,032 total), including the runtime literal-load fixture. Checked control/candidate and normal candidate standard replays match all 1,600 native images, guest records and 4,656,051 PCM frames. The checked longer route matches 360 images and 2,832,756 PCM frames. Actual compiler policy, feature mask, limits and archive hashes are verified. These full matrices include cases also reported by the focused runs; no extra distinct coverage is implied.

No new timing or live/audio acceptance is claimed yet. The previous conditional-only archive stays served.

## Serial gameplay timing

All modes retain policy 7, conditional integer leaves and limits 512/16/8/512. The matching control uses feature 0 and the candidate feature 128 in the same frozen binary. The untouched delivered conditional-only archive is a separate baseline. Diagnostics and interpreter checking are off; guest work and scheduling are identical within each route.

| Route/batch | Matching control | Literal PC veneers | Untouched live archive |
| --- | ---: | ---: | ---: |
| long a | 11.8977s | 11.9295s | 11.2860s |
| long b | 11.6942s | 11.1486s | 11.3666s |
| standard a | 11.1696s | 11.3590s | 11.3153s |
| standard b | 11.6703s | 11.0264s | 11.2378s |

Each cell averages two observations. All 24 samples remain, including slow runs. A uses control/candidate/live and its mirror; B uses candidate/live/control and its mirror, moving every mode between positions. Actual per-run orders are retained. Comparisons pair corresponding halves and are not all immediately adjacent. The two-second host observer watches competing test/profile jobs and records passive frequency/thermal context; it cannot exclude all host activity or assign a cause to slow samples. Startup includes guest work and is not an isolated compilation measure.

- long a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -0.27% throughput, paired -1.05% / +0.43%; versus baseline: -5.39% throughput, paired +1.55% / -11.61%.
- long b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +4.89% throughput, paired +0.65% / +9.25%; versus baseline: +1.95% throughput, paired -1.25% / +5.25%.
- standard a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -1.67% throughput, paired +1.52% / -4.65%; versus baseline: -0.38% throughput, paired +1.69% / -2.33%.
- standard b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +5.84% throughput, paired +1.10% / +10.57%; versus baseline: +1.92% throughput, paired +1.91% / +1.92%.

No automatic default or delivery change follows. Separately checked diagnostics measure dispatch, dependency and guard costs; normal live/audio acceptance is required for any promotion.
