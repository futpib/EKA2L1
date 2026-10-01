# Conditional-leaf site limit revisit

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
