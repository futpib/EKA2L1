# Guard-interval publication experiment

The delivered mode-3 compiler emits no reads of the two current-code interval
fields, but its runtime still writes them at every successful compiled lookup.
This off-by-default experiment adds `EKA2L1_OMIT_GUARD_PUBLICATION=1`, selected
before CPU initialization with exact readback. It chooses a templated path once
per outer runner. Modes 0/1 always publish; modes 2/3 may omit publication.
Mapping, lifetime, ASID, permissions, fault behavior, budgets, interrupts and
guest scheduling are unchanged. No generated store gets another mode test.

The audit covers startup ROM exports, hot ROM/RAM, Thumb, IR and diagnostic
readers. All production interval readers are under the existing guard policy.
The option remains disabled pending measurement; source size and extra runner
specializations are potential added costs, not presumed free.

## Initial verification

WASM and native builds pass. The first build command named a nonexistent target
`eka2l1`; that log is retained separately from the successful `eka2l1_wasm` build.
Forty-eight focused checks exercise actual lookup/runner calls, sentinel fields,
all four executable-byte policies, both option values, normal/detailed paths and
partial budgets. Modes 0/1 retain publication even when the option is selected.
Browser policy checks reject missing/rejected configuration and absent/wrong
readback. Full compiler, fault and exact native replay gates are pending.

The native instruction fault probe primarily exercises emitted operations and
memory callbacks; it does not replace the dedicated real-runner publication
test or browser replays. Timing follows the 24-observation plan only after
acceptance. Current live mode 3, literal feature 128 and lookup 0 are unchanged.
