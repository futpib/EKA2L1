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
