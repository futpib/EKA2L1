# Complete division helper summary

Adopted on `wasm-port`: **Snakes CPU throughput improves 5.80%**, with all four pairs faster. Sky Force averages **+0.21%** with 1/4 faster pairs; this does not establish a Sky Force gain.

## Runtime measurement

| # | Game | Worker CPU seconds, control → candidate | CPU throughput | Wall throughput | Native instructions | Paired CPU range | Faster pairs |
|---:|---|---:|---:|---:|---:|---|---:|
| 1 | Snakes | 7.3963 → 6.9911 | +5.80% | +4.85% | -6.06% | +4.79% to +6.23% | 4/4 |
| 2 | Sky Force | 24.8445 → 24.7921 | +0.21% | +0.16% | -0.00% | -1.11% to +2.37% | 1/4 |

All 16 observations are valid; 0 invalid attempts are retained.
Four fresh launches per build per game run in ABBA then BAAB order against
`ff89d6335`, with the same current defaults and frozen harness. Snakes runs from
78–96 guest seconds and Sky Force combat from 42–60. Guest instruction counts,
virtual endpoints and presentation journals match. Timing uses uninstrumented
frozen runtime artifacts, without sampling, tracing or verification.

The worker runs on CPU 7, its sibling 15 is reserved, and support work runs on
other CPUs. Requested clock is 3.6 GHz; measured frequency is about 3.592 GHz.
Clock, throttle, affinity and counter checks pass. There are no temperature
gates or cooldowns. All 88 live restoration checks pass after restoring temporary
clock, placement, platform-profile and charging settings. See the
[method](CONTROLLED_BENCHMARKS.md) and [complete evidence](DIVISION_HELPER_RESULTS.json).

The repeated Snakes gain is accompanied by fewer retired instructions. This is
a result for the measured routes and current configuration; historical gains
are not additive.

## Scope and observable state

This recognizes one complete signed divmod algorithm by all 89 ARM instructions,
at any address. Only the displacement of its external divide-by-zero branch may
vary. It is not tied to a ROM address or game identity, and has no result cache
or runtime experiment selector. Interior entries retain ordinary translation.

The accelerated path requires a nonzero divisor, quotient magnitude at least
256, sufficient guest instruction budget, and no pending exit or unmasked IRQ.
Other calls execute the original generated body. The guards precede all guest
state writes. This includes division by zero and short budgets; unsigned
magnitude division also handles `INT_MIN / -1` without a WASM signed-divide trap.

The arithmetic depends on incoming r0/r1 and returns through LR. Unrelated
registers are neither loaded nor compared nor stored. Packed status is read for
the IRQ guard and remains unchanged. All intermediate scratch/flag calculations
disappear. Only final r0/r1, r2/r3/r12,
NZCV and return PC/T are produced. A dynamic LR means no caller continuation
has been proved to overwrite the final scratch outputs before reading them;
this implementation does not infer dead outputs from an ABI convention.

For magnitudes `n`, `d`, quotient `q` and remainder `rem`, the final scratch
outputs simplify to `r2 = q >> 1`, a sign mask in r3, and
`r12 = ((rem + (q & 3) * d) >> 1) - d`, with 32-bit wrapping arithmetic.
N is the XOR of input signs, Z its inverse, C the numerator sign, and V is zero on
the selected large-quotient domain. Initial flags are irrelevant to this path.

The scheduler reads the returned instruction count. The exact complete-call
count is computed from three magnitude boundaries, without replaying the digit
loop or retaining intermediate instruction counters:

```text
count = 64
      + 24 * (q >= 2^14)
      + 28 * (q >= 2^20)
      + 20 * (q >= 2^26)
      - (numerator_negative OR divisor_negative)
```

The threshold predicates use `(n >> threshold) >= d`; the full budget is thus
proved before division or guest state changes. The optimization adds no guest
memory access, host helper call, or JS/WASM crossing. The common original call
already completes in one compiled region, so this change removes division
machinery, not caller/return dispatches. Call-site liveness and connection are
outside this implementation.

## V8 and executed work

A fresh baseline Snakes profile attributes 5.51% of selected worker samples to
this helper. That diagnostic function attribution is not a hardware CPU share
or a predicted speedup. The captured live-game module has exactly the same
normalized helper body as the independently exported baseline fixture.

| # | Common call: `0x40000000 / 0x101e`, guest count 88 | Before | After |
|---:|---|---:|---:|
| 1 | Executed WASM operations | 2,590 | 181 |
| 2 | Executed captured V8 native instructions | 807 | 110 |
| 3 | Native calls on that path | 0 | 0 |

WASM counts use the shared offline counter and cost ledger. Native counts come
from executing actual Chromium 153.0.8010.52 / V8 15.3.76.13 TurboFan machine code
in Unicorn x86-64, with a synthetic instance, stack and CPU state. Both native
paths return count 88 and match every CPU-state word. They are specified fixture
path counts, not native hardware cycles or game path frequencies. Game timings
use normal tiering and contain none of these counters or profiling flags.

The unchanged fallback remains in the module: whole-function static WASM size
increases from 3,295 to 3,476 operations, and native code from 11,608 to 12,008
bytes. Small calls and failed guards pay extra checks. In 76,728 deliberately
enumerated baseline/candidate invocations, 2,816 shrink and 73,912 grow; all
returned state and counts match. These fixtures heavily enumerate short budgets
and pending exits and are not gameplay hit rates. This is not an all-path
instruction reduction.

## Correctness and reproduction

The independently emulated ARM algorithm matches 11,772 nonzero full-call
contracts. The compiler regression fixture makes 155,664 comparisons against
DynCom across signs, boundary quotients, random operands, zero divisors, short
budgets, relocated entry addresses, TLB/direct backends and entry-budget modes.
Every signature word is checked for mutation rejection, except the deliberately
variable zero-divisor branch displacement.

The offline baseline/candidate matrix additionally covers initial flags,
pending/blocked interrupts, exits, ARM/Thumb returns and every CPU-state word.
Existing entry-budget variants (17,280), division sequences (21,504), precise
instruction counts (34,560), loop budgets (24,192) and exact-code checks pass.
Both games pass 60-frame exact comparisons against retained references,
including images, frame records, guest instruction progress and PCM audio.

```sh
cmake --build build-wasm --target test_aot_wasm eka2l1_wasm -j4
node build-wasm/src/tests/aot/test_aot_wasm.js --division-helper-only
node build-wasm/src/tests/aot/test_aot_wasm.js --emit-division-helper-probe > candidate.log
node src/tests/wasm/division-helper-counts.mjs baseline.log candidate.log counts.json
```

The baseline probe uses the same exporter with the pre-change translator.
The evidence directory is `/home/claude/.scratch/eka-division-helper`.
The companion JSON retains plans, all timing observations, artifact hashes,
contract/cost results, native code and path audit, replay evidence and restoration.

## Live-game confirmation

A separate candidate profile reduces the helper's sampled share from 5.51% to 1.42%. The actual captured candidate module exactly matches the independently inspected helper body. This diagnostic capture is separate from the controlled timing campaign.

The measured artifact is served at `https://claude-laptop.lan:8188/`; its fetched
WASM hash matches the frozen candidate. Both real game-picker launches advance
frames, consume keyboard/touch input and retain expected defaults on the hardware
NVIDIA renderer. Gameplay screenshots were inspected for both games.

Full live-audio E2E still reports the previously reproduced host audio-device
failure. The only remaining game-picker failures are the two non-silent-browser-
audio checks. Exact PCM replay comparisons pass; physical browser audio output
is not claimed verified.
