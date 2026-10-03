# One-page Thumb register-transfer proofs

The opt-in Thumb-memory path now proves an entire narrow PUSH, POP, LDM or STM
transfer when at least two words fit in one permitted, aligned, little-endian
page. The proof is local to that single instruction. A failed proof retains the
existing per-access guards and callbacks; it is never reused after a callback.
Stores still require the existing mode-3 policy. No game, address or ROM identity
selects the fast path. Budgets, scheduling, source windows and exception ordering
remain unchanged.

All 179 compiler tests pass. The new 5,376-case matrix compares every runtime
state word and complete guest memory with the original callback path across
register masks, permissions, page crossings, endian modes, budgets and both TLB
layouts. Existing callback tests include mapping, register, PC, endian and budget
changes. All 1,152 actual-runner/native call cases and 2,880 native memory-fault
comparisons pass as well.

Sky Force normal control/candidate and checked candidate match all 240 native
images, guest records and 1,703,464 PCM frames. Candidate Snakes matches the
standard 1,600-image/4,656,051-PCM and longer 360-image/2,832,756-PCM references.
Selected checked invocations force memory callbacks; normal replays, unit tests
and actual fault probes separately exercise the direct paths.

This stage is based on the frozen v4 source, including its unpromoted state-cache
and long-call stages. It remains disabled by default and unserved. The fixed
six-observation v4/v5 screen runs serially after correctness acceptance, with
Thumb memory enabled in both, original limits, shared audio, physical GPU,
rendering without capture and no sampling or detailed counters. One pair per
route is exploratory. Full comparison with the untouched live archive and
normal live/audio acceptance remain necessary; the realtime target stays open.

## Exploratory speed screen

All six observations are retained. Sky Force is 12.4366s control versus
11.9793s candidate for six guest seconds (+3.82% throughput, 0.501x realtime).
Standard Snakes is 12.2531s versus 10.046s (+21.97%); longer is 11.5844s versus
9.85194s (+17.58%). Both Snakes controls are slower than the immediately preceding
call-stage screen, so those large percentages must not be presented as stable
feature gains. One pair per route is exploratory; all guest instruction and
presentation counts match within each route. No zero-loss or promotion claim
follows. The next shared ROM-region comparison uses this untouched archive.
A separate moving-and-firing Sky Force holdout is being established for broader
acceptance, with its measurements kept distinct from the stationary baseline.
