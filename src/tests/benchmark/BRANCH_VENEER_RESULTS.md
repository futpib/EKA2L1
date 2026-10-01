# Single-branch callee fusion

The preserve-inner census still rejects 4,870,478 calls at one unconditional branch veneer and 901,809 at another in the longer route. This experiment admits the generic one-instruction ARM AL B shape as feature bit 32 under original-emitter policy 7 and conditional-leaf mode. It does not special-case guest addresses. Prefix bits 8/16 and broader eligibility bits 1/2/4 remain off.

The caller BL and callee B both execute and consume their exact instruction budgets. LR retains the original caller return address. Guest values remain in locals across that entry boundary, then the B uses the existing precise exit to its actual target, even when that target lies inside the primary window. The target is not resolved or assumed during translation; normal dispatch/code validation follows. Only the four veneer bytes become an additional exact dependency. Caller continuation discovery, code aliases, callback-visible state and ordered effects retain their existing paths. All limits remain 512 source bytes / 16 leaf instructions / 8 sites / 512 runner regions, with no guest scheduling change.

The expected benefit is one fewer compiled entry per eligible call. Additional dependency checks, enlarged protected intervals and generated code can outweigh that saving; separate counters and measurements are required. No performance claim follows from eligibility.

## Correctness

All 172 instrumented compiler tests pass, including the existing documented harness XFAIL. The new focused test makes 12,800 exact comparisons across both TLB indices, positive/negative/self/in-window targets, conditional caller paths, partial budgets, flags, LR, memory and caller/veneer aliases. Native tests pass 570 assertions in 33 cases, including four-byte and eight-byte dependency mutation/remapping.

Explicit feature modes 0 and 32 each match 40,640 native fault cases (81,280 total). The new 5,376-case fixture per mode checks actual four-byte fusion selection followed by faulting memory instructions and precise callbacks at the branch destination. Both checked standard replays and the normal candidate replay match all 1,600 images, guest records and 4,656,051 stereo PCM frames; the 360-image longer route also matches native and 2,832,756 PCM frames. Missing, wrong and duplicate feature/census markers are rejected.

Gameplay timing and any live/audio graduation remain pending. The served conditional-only archive is unchanged.

Instrumentation separately passes another 40,640 explicitly selected native fault comparisons and exact checked standard/longer image/audio replays before any diagnostic census. These checks do not contribute timing evidence.

## Serial gameplay timing

All modes retain policy 7, conditional integer leaves and limits 512/16/8/512. The matching control uses feature 0 and the candidate feature 32 in the same frozen binary. The untouched delivered conditional-only archive is a separate baseline. Diagnostics and interpreter checking are off; guest work and scheduling are identical within each route.

| Route/batch | Matching control | Branch veneers | Untouched live archive |
| --- | ---: | ---: | ---: |
| long a | 11.2172s | 11.7393s | 11.5434s |
| long b | 12.2895s | 11.1334s | 11.3569s |
| long c | 11.6708s | 12.0110s | 11.2270s |
| standard a | 11.2713s | 11.6730s | 11.5705s |
| standard b | 11.8113s | 11.2562s | 11.2990s |
| standard c | 11.8538s | 11.1581s | 11.1697s |

Each cell averages two observations. All 36 samples remain, including slow runs. Each batch mirrors its first half. Both A batches and long B keep the candidate in the middle. After slow closing candidates in A, an ordering edit raced with the running queue: long B had already loaded the original order, while standard B loaded the amended candidate-first order before restoration. The actual per-run orders are retained below. Separately labelled adaptive batch C puts the candidate at the endpoints on both routes. This additional position check was not part of the original four-batch plan. Comparisons pair corresponding halves and are not all immediately adjacent. The two-second host observer found no overlapping watched test/profile job; it does not exclude all host activity. Startup is recorded but includes guest work and is not an isolated compilation measure.

- long a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -4.45% throughput, paired +1.90% / -10.14%; versus baseline: -1.67% throughput, paired +0.65% / -3.75%.
- long b order baseline-1, candidate-1, control-1, control-2, candidate-2, baseline-2: versus control: +10.38% throughput, paired +9.70% / +11.07%; versus baseline: +2.01% throughput, paired +1.71% / +2.31%.
- long c order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: -2.83% throughput, paired +10.01% / -14.00%; versus baseline: -6.53% throughput, paired +0.96% / -13.04%.
- standard a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -3.44% throughput, paired +1.67% / -8.05%; versus baseline: -0.88% throughput, paired +3.83% / -5.13%.
- standard b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +4.93% throughput, paired -1.08% / +11.07%; versus baseline: +0.38% throughput, paired +0.51% / +0.25%.
- standard c order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +6.23% throughput, paired +0.33% / +12.10%; versus baseline: +0.10% throughput, paired +0.18% / +0.03%.

No automatic default or delivery change follows. Separately checked diagnostics measure dispatch, dependency and guard costs; normal live/audio acceptance is required for any promotion.

## Timing decision

No promotion. Longer-route throughput versus the untouched live archive changes by -1.67%, +2.01% and -6.53% across A/B/C; standard changes by -0.88%, +0.38% and +0.10%. Matching-control signs also reverse, and several slow controls inflate apparent gains. The adaptive endpoint batches do not repair the inconsistency. All 36 observations and the ordering race remain visible. The independently checked census and module measurements explain costs; they cannot replace this timing result.
