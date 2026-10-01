# Conditional-leaf runner limit revisit

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
