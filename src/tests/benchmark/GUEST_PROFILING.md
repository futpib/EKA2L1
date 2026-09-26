# Guest interpreter attribution

Set `EKA2L1_GUEST_PROFILE=1021` on `profile.ts` to enable read-only guest diagnostics
inside the existing 21–25-second guest measurement window. Zero (default) disables
these diagnostics. The positive integer is the PC-sampling interval; compare a
second prime interval such as 1009 to expose sensitivity to periodic alignment.

```sh
cd src/tests/wasm
EKA2L1_BENCHMARK_AOT=4 EKA2L1_GUEST_PROFILE=1021 node profile.ts /absolute/assets /absolute/new-profile 0 0
cd ../../..
python3 src/tests/benchmark/summarize_guest_profile.py /absolute/new-profile
```

`guest-profile.json` contains exact dispatch counts by DynCom handler and ARM/Thumb
mode, independently for interpreter execution and cache decoding. Counts include
conditional instructions whose condition fails, matching the guest instruction
budget. Thumb ALU instructions can normalize to ARM handlers (for example, shifts
can appear under `mov`); sampled rows also retain the original opcode, mode and PC.
These are instruction-work counts, not wall-time attribution.

Every Nth instruction in each stream records PC, ASID, original opcode and the
current process and loaded code segment. Ownership is resolved at sampling time,
so process switches, relocation, unloads and reused addresses are not attributed
using a stale end-of-run address map. ROM fallback uses the existing ROM module
map; unresolved ownership is explicitly `<unattributed>`. Opcode sampling uses
executable mapped host bytes and does not perform additional guest memory reads.

The collector caps distinct rows at 131,072 and reports dropped samples. The
summarizer refuses dropped samples, reconciles exact interpreter plus AOT counts
against total guest execution, and independently reconciles decoder counts.
Sampled module shares are approximate; exact handler counts are not sampled.
Interpreter differential-check executions are excluded. The diagnostic affects
host runtime and is never used as a performance timing control.

Measured Snakes results: [GUEST_PROFILE_RESULTS.md](GUEST_PROFILE_RESULTS.md).

The JSON also includes `aot_events`, `aot_samples` and `dropped_aot_samples`.
Events count lookup/compilation attempts during the measured phase, not guest
instructions; chain lookups and subsequent dispatcher lookups may both appear.
Sampled rows distinguish missing, rejected, pending and unmapped RAM entries,
ROM misses, compiled zero-progress exits, candidate thresholds and RAM capacity
limits. Each reason has its own sample cadence; preserve PC/mode and address
space when interpreting rows. An opcode value of zero on a ROM-miss event is an
uncollected field, not evidence of a zero opcode. Resolve it through the original
opcode samples from executed instructions.

`summarize_profile.py` additionally reports `generated_code_inclusive`: CPU
samples with a generated `wasm://` module frame on the stack, including its
callees (such as memory imports). The linked emulator has an HTTP `eka2l1.wasm`
URL. This is a subset of the sample span, not a separate additive timing scope.
Other samples include shared dispatch/lookup, interpreter fallback, services and
waits; they must not all be labeled interpreter work. Use a separate run with
sampling enabled and guest profiling/verification disabled for performance
attribution.
