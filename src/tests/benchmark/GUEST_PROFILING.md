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
