# Generic ARM word exclusive-operation compiler stage

The matched V8 census found 1.77–1.96% interpreted instructions in Sky Force,
versus 0.15–0.29% in Snakes. Per user steering this stage takes priority over
the unverified module-local ROM dispatcher, whose source is preserved separately.

The opt-in shared compiler emits ARM LDREX/STREX through the existing exclusive
monitor. Selection is by instruction encoding, with all condition codes and
ordinary register operands; byte/halfword/doubleword forms and PC operands remain
unsupported. Each executed operation completes its current instruction and
returns to the existing runner for budget, stop, IRQ and successor validation.
No game, ROM address, monitor algorithm, scheduling policy or execution limit is
special-cased. The original monitor remains responsible for reservation checks,
compare-and-swap, callback failure and other-processor invalidation. Diagnostic
checked replay snapshots the reservation into its independent reference monitor.

`EKA2L1_ARM_EXCLUSIVE=1` selects the experiment in the harness; the runtime API
freezes it before initialization and reports actual selection. Default is off.

Fresh V20e acceptance:

- All 185 compiler tests pass, including 12,000 new decoding/operand selections.
- 17,280 native DynCom/generated-WASM exclusive-operation comparisons pass:
  basic blocks and regions, flag-setting prefixes, predicates, short budgets,
  operand aliasing, missing/wrong/stale reservations, two processors, callback
  stop/register changes/failure, modes 0/3 and both TLB indexing selections.
  Every case checks registers, CPSR, instruction count, callback observations,
  both reservations and every changed byte; the WASM probe asserts all executed
  instructions were compiled. Monitor callbacks retain their existing behavior;
  these are not claims of new architectural fault handling for exclusives.
- Existing 4,032 ARM fault, 2,880 Thumb memory-fault and 1,152 Thumb call cases pass.
- All three native CTest targets pass.
- Seven exact native image, frame-record and PCM replays pass: Sky stationary
  candidate/control plus checked candidate, Sky combat normal/checked, and both
  Snakes routes. Runtime option readback and post-init rejection are checked.

The first build passed a value where the reference memory helper needs a pointer;
its failed logs remain. Initial non-flag-setting probes and game replays passed,
but the stronger flag-prefix probe found 436 callback CPSR mismatches in its first
4,320-case configuration. The native exclusive path does not repack CPSR before
calling the monitor. The new helper now retains that behavior while generated
barriers publish registers and split flags. The failed matrix is retained, and
all acceptance above was freshly rerun after correction; no earlier pass is reused.

A separate same-binary on/off stationary Sky census reproduces the V8 control:
41,886,443 interpreted instructions (1.955%) become 27,894,433 (1.302%), a 33.40%
reduction. Interpreted LDREX/STREX counts fall from 3,497,074 each to zero.
Total guest instructions are unchanged at 2,142,147,961. Outer compiled runner
calls fall from 18,519,076 to 15,021,017, while compiled function invocations rise
from 193,386,711 to 196,881,862. These are diagnostic counts, not CPU-time shares
or speed results. Existing `memory_calls` counters cover AOT TLB helpers and do
not count the new exclusive helper or all interpreter accesses. Most remaining
interpreted instructions are system calls and their return paths.

The fixed sixteen-observation four-route reversed-order speed panel follows
correctness and census completion, with all samples retained. Reduced fallback
does not establish faster execution. Sky Force realtime remains unachieved;
Snakes performance must still be checked. Nothing deployed or pushed; the served
WASM hash remains 91c7d6f6b6df4c335eda2005fd671119a85c1fc57008f8bdf000e49a0825215e.
