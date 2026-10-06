# Conditional-leaf runner limit revisit

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

Planned after the frozen tail-prefix timing and diagnostic queues finish. This
changes only the outer compiled-runner cap: 64, 512 and zero (uncapped). The
primary source window stays 512 bytes, leaf bound 16 instructions and inline
site bound eight. Policy 7 and conditional integer leaves remain enabled; all
additional leaf feature bits are zero. No translation eligibility or guest
scheduling change is included.

The frozen tail-prefix archive provides all three same-binary modes. The
untouched delivered conditional-only archive is a separate baseline. Its older
binary matters because runtime-configurable limit machinery and later compiler
changes may affect cost even when their new features are disabled.

## Contract and evidence boundaries

`execute_chain` retains the original guest budget, zero-progress rejection,
stop/interrupt checks and exact successor validation. A cap exit returns to
`InterpreterMainLoop`; positive progress resumes CPU dispatch. It does not by
itself return from `cpu->run`, reschedule the guest or yield to the browser.
Uncapping still publishes/reloads guest state and validates each region.

The archived 173-test suite includes 270 actual-runner cases across caps
0/1/64/512/4096 and budgets through 4,840, covering zero progress, stop, masked
and unmasked interrupts and unavailable successors. This result will be reused
with attribution. Fresh explicitly selected native fault comparisons and exact
standard/longer image/audio replays are required for each changed cap. The
standalone fault probe primarily verifies generated instruction semantics and
configuration selection; it is not evidence of outer-runner cap handling. The
actual-runner tests and complete browser replays provide that coverage.

## Fixed timing plan

Two serial batches per route, eight runs per batch, 32 observations total:

- A: cap 512, cap 64, uncapped, live archive, then reverse that order.
- B: uncapped, live archive, cap 512, cap 64, then reverse that order.

Thus every mode moves between inner and outer positions. The longer route uses
42-60 guest seconds; standard uses 60-78. Each route must retain identical guest
instruction endpoints and presentations across modes. Diagnostics and replay
checking are disabled for timing. Every observation and actual order remains,
including slow or opposing comparisons; passive host telemetry is context, not
an exclusion or normalization rule. No owned builds or diagnostic jobs run
concurrently with timing.

Separately checked censuses will measure runner returns, total compiled calls,
code/dependency checks and guest work. Since this cap does not change the
translator, a code-size change is not expected; observed compiled-function and
heap counts will still be retained. Any promotion requires repeatable gameplay
results and normal/live/audio acceptance. No bound is claimed optimal in advance.

Status: plan only; no new runner measurements or delivery.

Before timing starts, the quiet-host observer also waits for compiler, build and linker processes. This keeps the separate literal-PC candidate build out of runner timing; the frozen runner archive and preplanned orders are unchanged.

## Correctness acceptance

Caps 64 and zero each pass 40,640 explicitly selected native instruction-fault comparisons (81,280 fresh comparisons total), exact 1,600-image standard and 360-image longer replays, including guest records and 4,656,051 / 2,832,756 stereo PCM frames. All replays verify actual limits, feature policy and archive hash. The same archive's 173-test suite, including 270 actual-runner cases, and cap-512 control evidence are reused with attribution. Standalone fault comparisons do not replace actual-runner tests. Timing and any live/audio graduation remain pending.

## Serial runner-limit timing

All 32 observations are retained. Values are mean elapsed seconds for identical guest work within each route; lower is faster. Each batch mirrors its first half, with a two-position rotation in batch B. The untouched live archive is separate from the matching binary control.

| Route/batch | 512 regions matching | 64 regions | Uncapped | Live archive |
| --- | ---: | ---: | ---: | ---: |
| long a | 11.6859 | 11.2894 | 11.2574 | 11.7975 |
| long b | 11.2818 | 12.7374 | 11.6691 | 12.2812 |
| standard a | 11.2317 | 11.6730 | 12.2647 | 11.4369 |
| standard b | 12.1064 | 11.5310 | 11.1660 | 11.7951 |

- long a, cap64: vs control512: +3.51% throughput; half-pairs -0.10% / +7.14%; vs baseline: +4.50% throughput; half-pairs -0.91% / +9.93%.
- long a, uncapped: vs control512: +3.81% throughput; half-pairs -0.85% / +8.58%; vs baseline: +4.80% throughput; half-pairs -1.65% / +11.40%.
- long b, cap64: vs control512: -11.43% throughput; half-pairs -9.26% / -13.53%; vs baseline: -3.58% throughput; half-pairs -2.05% / -5.06%.
- long b, uncapped: vs control512: -3.32% throughput; half-pairs +1.84% / -8.05%; vs baseline: +5.25% throughput; half-pairs +9.92% / +0.96%.
- standard a, cap64: vs control512: -3.78% throughput; half-pairs -1.26% / -6.19%; vs baseline: -2.02% throughput; half-pairs -1.46% / -2.56%.
- standard a, uncapped: vs control512: -8.42% throughput; half-pairs -10.04% / -6.74%; vs baseline: -6.75% throughput; half-pairs -10.23% / -3.13%.
- standard b, cap64: vs control512: +4.99% throughput; half-pairs +10.16% / -0.42%; vs baseline: +2.29% throughput; half-pairs +6.51% / -2.12%.
- standard b, uncapped: vs control512: +8.42% throughput; half-pairs +16.17% / +0.66%; vs baseline: +5.63% throughput; half-pairs +12.31% / -1.06%.

Pairs associate corresponding halves and are not all adjacent. Host observers detect watched competing jobs, not all host activity. Startup includes guest work and is not isolated compilation time. These timings do not establish an optimum outside the tested range. Diagnostic cost counters and any normal/live/audio delivery checks remain separate.

## Conditional-only runner-limit census

The two changed bounds separately pass another 81,280 explicitly selected instrumented native fault comparisons and exact checked standard/longer image/audio replays. Cap 512 reuses the same frozen binary and policy from the preceding tail-prefix control census; it is not a fresh run. Diagnostic elapsed time is excluded from throughput acceptance.

### long

| Counter | 64 regions | 512 regions | Uncapped |
| --- | ---: | ---: | ---: |
| Runner invocations | 3,599,894 | 1,910,747 | 1,897,082 |
| Cap returns | 1,706,092 | 13,666 | 0 |
| Budget returns | 623,516 | 623,516 | 623,516 |
| Zero-progress returns | 1,140,632 | 1,143,911 | 1,143,912 |
| Missing successor returns | 129,654 | 129,654 | 129,654 |
| Compiled invocations | 140,993,871 | 140,994,669 | 140,994,670 |
| Direct-call exits | 43,526,965 | 43,526,965 | 43,526,965 |
| Site-limit exits | 551,863 | 551,863 | 551,863 |
| Leaf-length limit exits | 970,429 | 970,429 | 970,429 |
| Dependency spans requested | 35,293,874 | 35,294,035 | 35,294,035 |
| Dependency bytes requested | 768,013,864 | 768,017,640 | 768,017,640 |
| Primary bytes requested | 9,690,075,866 | 9,690,132,630 | 9,690,132,714 |
| Entry-proof attempts | 17,353,977 | 17,354,245 | 17,354,245 |
| Private entry-proof fallbacks | 733,061 | 733,238 | 733,238 |
| Gap-only fallbacks | 0 | 0 | 0 |

cap 64 vs 512: compiled calls -798 (-0.00%); requested dependency bytes -0.00%.

cap 0 vs 512: compiled calls +1 (+0.00%); requested dependency bytes +0.00%.

### standard

| Counter | 64 regions | 512 regions | Uncapped |
| --- | ---: | ---: | ---: |
| Runner invocations | 3,622,309 | 1,907,427 | 1,897,997 |
| Cap returns | 1,727,271 | 9,430 | 0 |
| Budget returns | 611,615 | 611,615 | 611,615 |
| Zero-progress returns | 1,135,846 | 1,138,805 | 1,138,805 |
| Missing successor returns | 147,577 | 147,577 | 147,577 |
| Compiled invocations | 140,625,080 | 140,626,048 | 140,626,048 |
| Direct-call exits | 44,506,411 | 44,506,411 | 44,506,411 |
| Site-limit exits | 571,365 | 571,365 | 571,365 |
| Leaf-length limit exits | 1,020,658 | 1,020,658 | 1,020,658 |
| Dependency spans requested | 35,765,197 | 35,765,500 | 35,765,500 |
| Dependency bytes requested | 778,282,396 | 778,289,684 | 778,289,684 |
| Primary bytes requested | 9,697,995,198 | 9,698,087,982 | 9,698,087,982 |
| Entry-proof attempts | 17,400,648 | 17,400,966 | 17,400,966 |
| Private entry-proof fallbacks | 741,779 | 741,998 | 741,998 |
| Gap-only fallbacks | 0 | 0 | 0 |

cap 64 vs 512: compiled calls -968 (-0.00%); requested dependency bytes -0.00%.

cap 0 vs 512: compiled calls +0 (+0.00%); requested dependency bytes +0.00%.

Requested byte coverage is not physical memory traffic. All raw exits, proof causes, mapping/guard results and compile counters are retained. Removing a compiled boundary does not remove guest instructions or change guest scheduling.

## Disposition after the conditional-only revisit

cap64: long-a matching +3.51%, live +4.50%; long-b matching -11.43%, live -3.58%; standard-a matching -3.78%, live -2.02%; standard-b matching +4.99%, live +2.29%.

uncapped: long-a matching +3.81%, live +4.80%; long-b matching -3.32%, live +5.25%; standard-a matching -8.42%, live -6.75%; standard-b matching +8.42%, live +5.63%.

Neither changed bound has a repeatable gain across both routes and batches against its matching control. The 512-region default remains a retained bound for this configuration, not an established global optimum. All 32 observations, both timing orders and passive host readings remain, including slow controls and candidates. No new live/audio or deployment acceptance is claimed for either rejected bound.

long, cap 64: +1,689,147 runner returns, -798 compiled invocations, combined primary/dependency requested bytes -0.0006%, cap returns 1,706,092.

long, cap 0: -13,665 runner returns, +1 compiled invocations, combined primary/dependency requested bytes +0.0000%, cap returns 0.

standard, cap 64: +1,714,882 runner returns, -968 compiled invocations, combined primary/dependency requested bytes -0.0010%, cap returns 1,727,271.

standard, cap 0: -9,430 runner returns, +0 compiled invocations, combined primary/dependency requested bytes +0.0000%, cap returns 0.

Compiled invocations are not individual state-load/store counts, and requested bytes are not physical memory traffic. Uncapping retains budget, interrupt, stop, zero-progress and successor-validity checks; it does not fuse compiled regions or change guest scheduling. The frozen conditional-only LAN archive remains served. The separate opt-in literal-PC veneer candidate proceeds to full correctness acceptance next; nothing is pushed or deployed.

The tiny differences in compiled-invocation counts are entirely zero-progress attempts: `calls - zero_progress` is identical at all three caps on each route (139,850,758 longer; 139,487,243 standard). The same differences appear in memory-guard exits. Thus this census finds no reduction in compiled invocations that execute guest instructions from changing the runner cap. This accounting statement does not assign a wall-time cost to each exit.
