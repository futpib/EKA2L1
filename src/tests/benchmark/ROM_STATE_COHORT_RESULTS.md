# Bounded shared Thumb state retention

Opt-in ROM dispatch mode 2 composes up to 16 connected eager Thumb regions, capped at 64 KiB of original bodies per group. Direct emitted calls, local branches and resume entries form the graph; no game identities, profiled address rules or guest scheduling changes select groups. ARM and unsupported emitted forms retain ordinary functions. Default mode 0 remains unchanged.

Guest R0-R14 and split flags stay in WASM locals within the group. Each helper publishes and reloads the complete group register union, including fields used by a later constituent. Scratch locals reset on each constituent entry. PC/runtime fields retain per-region publication. Every constituent counts toward the original instruction/region limits; callbacks, pending SVC, zero progress, stop/IRQ and successors outside the group return to the outer runner. No host recursion; immutable map lifetime is retained with the exported module.

Acceptance:

- Fresh full 191-test compiler suite, including 3988 group state/budget/callback comparisons, invalid slots, group sizes 2/3/16, entry aliases, direct-call halfword exits and 42 actual runner caps.
- New production/native composition probes: 2880 Thumb memory-fault and 1920 direct-call cases. The latter includes return instructions and short budgets. All existing ARM/Thumb/interworking/syscall/exclusive probes pass; syscall boundary probes select cohort mode explicitly. ARM exclusive probes use the conservative ARM fallback in this mode.
- Three native test targets and seven exact native image/frame-record/PCM replays, using the normal speed policy (first-use, compiled SVC, exclusive and compiled memory-miss options off; Thumb memory on). Actual mode-2 readback and rejection after initialization pass.
- Initial harness validation rejected mode 2 before browser startup; corrected validation, original logs retained. The initial grouping omitted direct-call target edges and retained only about 1.8% of stationary Sky Force constituent calls internally. Its source and census are retained; revised call-edge build has fresh acceptance. The initial normal-policy control also exposed a copied post-replay assertion expecting policy 3 instead of selected 0; its saved exact comparison/readback passed, the traceback is retained and the assertion was corrected without changing the emulator.

Separate diagnostic census (not CPU-time shares or timing evidence):

| Route | Guest instructions | Interpreted | Outer generated calls | Constituent blocks | Internal boundary fraction |
|---|---:|---:|---:|---:|---:|
| sky | 2,142,147,961 | 41,884,080 | 179,399,283 | 193,385,263 | 7.232% |
| combat | 2,171,043,925 | 38,438,799 | 166,035,268 | 178,944,673 | 7.214% |
| standard | 2,964,235,296 | 8,250,266 | 130,303,998 | 130,364,088 | 0.046% |
| long | 3,031,637,220 | 4,550,824 | 129,547,976 | 129,603,713 | 0.043% |

The internal fraction is a decomposition within this candidate, not a claimed counterfactual speedup. Independent total-minus-compiled counts agree exactly. Coverage-only policies remain disabled for the upcoming timing comparison. Compare repeated four-route throughput and startup cost before promotion; these correctness and mechanism results do not establish realtime. Nothing deployed or pushed.
