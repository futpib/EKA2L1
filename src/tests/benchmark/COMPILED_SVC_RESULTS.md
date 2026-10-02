# Shared ARM/Thumb syscall compilation stage

The opt-in translator emits the predicate, PC advance and a typed pending trap. The outer execution loop invokes the existing kernel handler using the original cumulative guest instruction count. It retains budget subtraction, packed flag publication and reload, and exclusive reservation clearing after the callback. Unchanged-PC fallthrough resumes compiled lookup without adding an IRQ check; changed PC and page boundaries use ordinary dispatch. Mapping/lifetime lookup remains in place. No game, address or syscall number is special-cased.

One-instruction outer quanta retain the interpreter to preserve DynCom cached SINGLE_STEP behavior. An unusual mode change without PC change resumes the original decoded stream through a correctness fallback. Compiling SVC removes guest interpreter dispatch; it does not remove or accelerate the kernel work by definition.

Selection: `EKA2L1_COMPILED_SVC=1`; default off. The runtime selection freezes before initialization and the harness verifies readback and post-initialization rejection. Checked replay observes the reference trap with a no-op kernel handler and compares pre-kernel state/reservations, avoiding duplicated kernel side effects. The real native probe tests actual callback effects and continuation.

Fresh V22d acceptance:

- All 186 compiler tests, including 9,216 new predicate/budget/page/source-extent checks.
- 20,736 native DynCom/generated-WASM syscall comparisons, covering ARM/Thumb, blocks/regions, predicates, prefixes, repeated traps, page ends, short quanta, callback stops/budget changes/PC changes/IRQ/flags/mode changes and memory changes. Compares registers, CPSR, counts, callback observations, reservation addresses and the fixture data byte. Correctness-fallback cases are not counted as fully compiled.
- 17,280 existing exclusive-operation comparisons; 4,032 ARM fault cases, 2,880 Thumb memory-fault cases and 1,152 Thumb call cases. Existing non-SVC probes exercise the default-disabled runtime; selected SVC runtime is exercised by the new production probe and selected game replays.
- Three native CTest targets and seven exact native image/frame-record/PCM replays, including normal/checked Sky stationary and combat, both Snakes routes and the matching stationary control.

Failures retained: an overly early budget guard changed an existing decode-profile count (3 instead of 4); guarding only compiled lookup preserves original interpreter decode behavior. The new mode-change fixture initially omitted ordinary memory callbacks and aborted; the fixture was corrected. The strengthened first 5,184-case comparison then found 216 ARM-region budget-one mismatches, fixed by excluding the unexecuted SVC from its fallback count. Self-review found missing Thumb SVC source extent; it is corrected and asserted. Earlier incomplete suites are not reused.

Current instruction census (diagnostic, not speed evidence):

| Route | Guest instructions | Interpreted | Share |
|---|---:|---:|---:|
| sky | 2,142,147,961 | 939,417 | 0.043854% |
| combat | 2,171,043,925 | 515,285 | 0.023734% |
| standard | 2,964,235,296 | 8,086,270 | 0.272794% |
| long | 3,031,637,220 | 4,133,782 | 0.136355% |

All counts independently match total minus compiled instructions. These profiles overlap correctness work and their wall times are excluded from speed conclusions. Zero interpretation and realtime remain separate targets; neither is claimed here. The fixed sixteen-observation same-binary reversed-order panel follows. No deployment or push.
