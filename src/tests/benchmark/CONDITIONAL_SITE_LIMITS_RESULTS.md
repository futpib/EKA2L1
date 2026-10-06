# Conditional-leaf site limit revisit

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

Prepared after the conditional-integer eligibility expansion. No new measurement is claimed yet. The preceding census recorded 551,863 longer-route and 571,365 standard-route site-limit exits; the earlier restrictive-inliner sweep cannot establish an optimum for this compiler.

This experiment varies only accepted inline call sites: 4, 8 and 16. The source window remains 512 bytes, leaf bound 16 instructions and runner cap 512; guest scheduling is unchanged. Policy 7, conditional leaves, folded TLB and grouped exact scanner remain enabled. All additional leaf feature bits, including branch veneers and prefixes, are zero. These are existing controls in the frozen branch-veneer archive, so no new compiler binary or instruction eligibility is introduced. The exact delivered conditional-only archive is a separate baseline.

Before timing, each changed bound must pass explicitly selected native fault comparisons and exact native standard/longer image/audio replay. The archived 172-test compiler suite already covers zero/one/eight/sixteen site selection, leaf bounds and partial budgets; those prior results will be cited as reused, not fresh tests. Site 8 correctness and matching census from the current archive are also reused with explicit attribution.

The planned timing order for each route is control8/sites4/sites16/baseline followed by its mirror, then sites16/baseline/control8/sites4 followed by its mirror. This moves each mode between the inner and outer positions. All 32 observations, configurations and host-process observer results will be retained; diagnostics are off. Both post-merge routes require identical guest instruction totals and presentations. Timing begins only after the preceding veneer timing and diagnostic/module queues finish.

Afterward, separate counters will measure total compiled calls, site-limit exits, dependency checks and proof coverage. Results may establish only an unresolved range. No default change follows without repeatable timing and normal live/audio/HTTPS acceptance.

Before this study starts timing, the host observer is extended with passive two-second CPU-frequency and thermal-zone samples, plus completed-run count. These cover startup and warmup as well as the measured window. They provide context for variation, not causal proof, normalization or a reason to remove observations. No host governor, fan, scheduling or browser setting is changed. A direct parser/read check succeeds for 16 frequency policies and 14 thermal zones.

## Correctness acceptance

Sites 4 and 16 each pass 40,640 explicitly selected native fault comparisons (81,280 fresh comparisons total), exact 1,600-image standard and 360-image longer replays, with guest records and 4,656,051 / 2,832,756 stereo PCM frames matching native. Every replay reads back its actual limits and feature policy. Site 8 and the 172-test compiler suite reuse previously completed acceptance on this same archive; they are not reported as fresh runs.

## Serial site-limit timing

All 32 observations are retained. Values are mean elapsed seconds for identical guest work within each route; lower is faster. Each batch mirrors its first half, with a two-position rotation in batch B. The untouched live archive is separate from the matching binary control.

| Route/batch | 8 sites matching | 4 sites | 16 sites | Live archive |
| --- | ---: | ---: | ---: | ---: |
| long a | 11.6669 | 12.6167 | 11.9736 | 11.4192 |
| long b | 11.6524 | 11.7457 | 12.0469 | 11.6802 |
| standard a | 11.1096 | 12.3893 | 11.1465 | 11.7896 |
| standard b | 11.2126 | 12.2735 | 11.3231 | 11.9170 |

- long a, sites4: vs control8: -7.53% throughput; half-pairs -14.01% / -0.39%; vs baseline: -9.49% throughput; half-pairs -11.68% / -7.09%.
- long a, sites16: vs control8: -2.56% throughput; half-pairs -10.68% / +6.66%; vs baseline: -4.63% throughput; half-pairs -8.26% / -0.51%.
- long b, sites4: vs control8: -0.79% throughput; half-pairs +5.69% / -6.95%; vs baseline: -0.56% throughput; half-pairs +5.89% / -6.68%.
- long b, sites16: vs control8: -3.27% throughput; half-pairs +7.88% / -12.97%; vs baseline: -3.04% throughput; half-pairs +8.09% / -12.73%.
- standard a, sites4: vs control8: -10.33% throughput; half-pairs -9.82% / -10.83%; vs baseline: -4.84% throughput; half-pairs +1.31% / -10.95%.
- standard a, sites16: vs control8: -0.33% throughput; half-pairs +0.43% / -1.08%; vs baseline: +5.77% throughput; half-pairs +12.83% / -1.22%.
- standard b, sites4: vs control8: -8.64% throughput; half-pairs -1.51% / -14.87%; vs baseline: -2.90% throughput; half-pairs -0.21% / -5.25%.
- standard b, sites16: vs control8: -0.98% throughput; half-pairs -1.14% / -0.81%; vs baseline: +5.25% throughput; half-pairs +0.15% / +10.40%.

Pairs associate corresponding halves and are not all adjacent. Host observers detect watched competing jobs, not all host activity. Startup includes guest work and is not isolated compilation time. These timings do not establish an optimum outside the tested range. Diagnostic cost counters and any normal/live/audio delivery checks remain separate.

## Timing decision

Neither changed bound earns promotion. Four sites loses all four matching-control batch means and seven of eight corresponding-half comparisons; sixteen sites loses all four means and five of eight comparisons. Sixteen's apparent standard-route lead over the older live archive is not a marginal gain over eight in the same binary, and slow live controls contribute to that difference. All slow samples remain. Eight is the useful retained bound in this tested configuration; these observations do not establish a global optimum or settle the separate source, leaf and runner limits.

No watched foreign benchmark job was observed. Passive frequency/temperature ranges and observer costs are retained in `telemetry_summary`, covering startup, warmup and measurement together. They are not assigned to individual measured windows and do not prove a thermal cause for slow runs. No sample is excluded or normalized.

The separately gated instrumented census and module captures continue to measure the structural tradeoff. Diagnostic timings cannot reverse this no-promotion decision.

## Conditional-only site-limit census

The two changed bounds separately pass another 81,280 explicitly selected instrumented native fault comparisons and exact checked standard/longer image/audio replays. Site 8 reuses the same frozen binary and policy from the preceding branch-veneer control census; it is not a fresh run. Diagnostic elapsed time is excluded from throughput acceptance.

### long

| Counter | 4 sites | 8 sites | 16 sites |
| --- | ---: | ---: | ---: |
| Compiled invocations | 148,979,036 | 140,994,669 | 140,864,201 |
| Direct-call exits | 48,561,465 | 43,526,965 | 43,463,362 |
| Site-limit exits | 6,079,692 | 551,863 | 30,443 |
| Leaf-length limit exits | 969,345 | 970,429 | 970,496 |
| Dependency spans requested | 37,345,539 | 35,294,035 | 35,294,470 |
| Dependency bytes requested | 814,503,784 | 768,017,640 | 767,788,176 |
| Primary bytes requested | 9,943,292,216 | 9,690,132,630 | 9,691,138,486 |
| Entry-proof attempts | 19,129,666 | 17,354,245 | 17,323,251 |
| Private entry-proof fallbacks | 737,263 | 733,238 | 733,216 |
| Gap-only fallbacks | 0 | 0 | 0 |

4 sites vs 8: compiled calls +7,984,367 (+5.66%); requested dependency bytes +6.05%.

16 sites vs 8: compiled calls -130,468 (-0.09%); requested dependency bytes -0.03%.

### standard

| Counter | 4 sites | 8 sites | 16 sites |
| --- | ---: | ---: | ---: |
| Compiled invocations | 148,794,685 | 140,626,048 | 140,462,878 |
| Direct-call exits | 49,648,458 | 44,506,411 | 44,423,756 |
| Site-limit exits | 6,236,274 | 571,365 | 29,010 |
| Leaf-length limit exits | 1,019,496 | 1,020,658 | 1,020,706 |
| Dependency spans requested | 38,020,692 | 35,765,500 | 35,747,297 |
| Dependency bytes requested | 831,752,068 | 778,289,684 | 777,783,680 |
| Primary bytes requested | 9,968,819,252 | 9,698,087,982 | 9,695,823,080 |
| Entry-proof attempts | 19,141,189 | 17,400,966 | 17,371,973 |
| Private entry-proof fallbacks | 745,896 | 741,998 | 742,033 |
| Gap-only fallbacks | 0 | 0 | 0 |

4 sites vs 8: compiled calls +8,168,637 (+5.81%); requested dependency bytes +6.87%.

16 sites vs 8: compiled calls -163,170 (-0.12%); requested dependency bytes -0.07%.

Requested byte coverage is not physical memory traffic. All raw exits, proof causes, mapping/guard results and compile counters are retained. Removing a compiled boundary does not remove guest instructions or change guest scheduling.

## Generated-module diagnostics

The metadata-only hook is byte-identical in application WASM to the accepted archive. Both changed bounds pass a hooked, checked 360-image native replay before captures. Site 8 reuses the preceding control capture with identical binary, policy, route and hook. These observations include startup through 60 guest seconds, not just the warmed timing window.

| Metric | 4 sites | 8 sites | 16 sites |
| --- | ---: | ---: | ---: |
| Modules | 534 | 534 | 532 |
| Generated module bytes | 60,398,127 | 62,123,036 | 62,504,588 |
| Function exports including repeated construction | 14,336 | 14,257 | 14,261 |
| Summed synchronous constructor milliseconds | 170.060 | 146.665 | 191.830 |

Synchronous constructor sums are single instrumented observations, not total browser JIT time or repeatable compilation-speed comparisons. Raw module metadata and worker-selection evidence are retained.

## Completion

Four sites adds 7.98/8.17 million compiled invocations (5.66/5.81%) and 6.05/6.87% more requested dependency-byte checks on longer/standard. Sixteen removes only 130,468/163,170 invocations (0.09/0.12%), with near-unchanged dependency coverage. Most relieved site-limit labels become unsupported-callee labels; they are not all fusion opportunities. No gap-only proof fallback occurs. The checked counter/module evidence describes that structural tradeoff; it does not override the no-promotion timing result. The delivered eight-site conditional-only build remains unchanged. The later census/module captures overlapped separate correctness-only tail-prefix work after timing had finished; their wall/constructor times are not comparative performance evidence. All results and failures are local; nothing pushed. Separate source, leaf and runner bounds are still not established optima.
