# Register-only tail-prefix experiment

Status: opt-in implementation with focused checks; full acceptance and timings pending. No deployment.

The preceding captured code shows that the hotter branch veneer begins with three register moves. Single-instruction feature32 leaves it unfused. New feature64 accepts a nonempty, bounded register-only integer prefix followed by an unconditional ARM branch. It uses the original emitter with conditional leaves enabled and the retained 512/16/8/512 source/leaf/site/runner limits. It does not recognize game addresses.

Guest values remain in WASM locals across the call and prefix. Every operation, false predicate and terminal branch consumes its exact budget; LR keeps the caller return address. The terminal branch takes the ordinary precise exit even when its destination lies inside the caller source window. Destination validation and scheduling are unchanged. The complete prefix becomes an exact code dependency; target bytes are not assumed. SP/LR/PC operands, memory, status transfers, multiply and nested/internal control flow remain excluded. Pure single-instruction veneers retain their separate opt-in feature32.

Focused tests pass 86,400 exact interpreter comparisons across flags, predicates, register shifts, short budgets, positive/negative/self/in-window branch targets and caller stores aliasing primary/dependency bytes. Selection tests cover exclusions and length boundaries. Native CPU/cache tests pass 570 assertions in 33 cases. Native/WASM builds and browser configuration/readback checks pass. A stale frontend negative test initially rejected the newly valid mask64; that failure and correction are retained.

Full compiler and explicit native fault checks are running. Browser replay and any timing follow the currently frozen site-limit diagnostic queue. Performance is unmeasured. No claim is made that removing an entry boundary outweighs the added dependency scan or generated code.

## Compiler and focused fault gates

All 173 instrumented compiler tests pass, including the new 86,400 comparisons; the existing documented XFAIL remains. The new tail-prefix fault fixture passes 5,376 cases each in control, candidate and instrumented candidate modes (16,128 comparisons, with explicit compiler/feature/readback checks). Six missing/wrong/duplicate marker cases are rejected. These focused cases will also occur within the later full matrix; they must not be misreported as distinct coverage.

The planned timing comparison uses control/candidate/live followed by its mirror, then candidate/live/control followed by its mirror, separately on both routes: 24 total observations. All modes retain the same 512/16/8/512 limits. Only feature64 differs in the matching binary; the untouched conditional-only live archive is separate. Passive host telemetry and all samples are retained. Timings start only after full faults and exact native replays pass and all diagnostic/build jobs finish.

## Full correctness acceptance

Both control and candidate pass 46,016 explicitly selected native fault comparisons (92,032 total), including the new prefix fixture. Checked control/candidate and normal candidate standard replays match all1,600 native images, guest records and4,656,051 PCM frames. The checked longer route matches360 images and2,832,756 PCM frames. Actual compiler policy, feature mask, limits and archive hashes are verified. These full matrices include cases also reported by the focused runs; no extra distinct coverage is implied.

No new timing or live/audio acceptance is claimed yet. The previous conditional-only archive stays served.

## Serial gameplay timing

All modes retain policy 7, conditional integer leaves and limits 512/16/8/512. The matching control uses feature 0 and the candidate feature 64 in the same frozen binary. The untouched delivered conditional-only archive is a separate baseline. Diagnostics and interpreter checking are off; guest work and scheduling are identical within each route.

| Route/batch | Matching control | Tail prefixes | Untouched live archive |
| --- | ---: | ---: | ---: |
| long a | 11.1197s | 11.7619s | 11.1770s |
| long b | 11.6982s | 11.3686s | 11.8925s |
| standard a | 11.2337s | 11.0879s | 11.9243s |
| standard b | 12.1341s | 10.9534s | 11.3702s |

Each cell averages two observations. All 24 samples remain, including slow runs. A uses control/candidate/live and its mirror; B uses candidate/live/control and its mirror, moving every mode between positions. Actual per-run orders are retained. Comparisons pair corresponding halves and are not all immediately adjacent. The two-second host observer watches competing test/profile jobs and records passive frequency/thermal context; it cannot exclude all host activity or assign a cause to slow samples. Startup includes guest work and is not an isolated compilation measure.

- long a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: -5.46% throughput, paired -1.52% / -9.10%; versus baseline: -4.97% throughput, paired -0.48% / -9.12%.
- long b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +2.90% throughput, paired +1.39% / +4.33%; versus baseline: +4.61% throughput, paired +15.11% / -5.34%.
- standard a order control-1, candidate-1, baseline-1, baseline-2, candidate-2, control-2: versus control: +1.31% throughput, paired +2.72% / -0.07%; versus baseline: +7.54% throughput, paired +0.54% / +14.46%.
- standard b order candidate-1, baseline-1, control-1, control-2, baseline-2, candidate-2: versus control: +10.78% throughput, paired +4.30% / +17.27%; versus baseline: +3.81% throughput, paired +5.19% / +2.42%.

No automatic default or delivery change follows. Separately checked diagnostics measure dispatch, dependency and guard costs; normal live/audio acceptance is required for any promotion.

## Timing decision

No promotion. Against the matching conditional-only compiler, longer-route throughput changes by -5.46% and +2.90%; standard by +1.31% and +10.78%. Longer-route comparisons with the untouched live archive also reverse (-4.97% / +4.61%). Five of eight matching half-pairs favor the candidate, but the first longer batch loses both and standard A has opposing pairs. Slow candidate and control observations remain, including the closing standard-B control that inflates its mean advantage. All 24 preplanned samples and passive host readings are retained; no watched competing benchmark job was observed. These observations do not identify the cause of timing variability. The feature remains opt-in while separately checked diagnostics measure the structural tradeoff.
