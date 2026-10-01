# Private precise fallbacks for IR segments

Experimental EKA2L1_WASM_IR_OUTLINE (OFF by default) moves each selected IR
segment's original short-budget body into a private function with the existing
(state pointer) -> instruction count ABI. Successful graph execution retains
its memory guards, snapshots and instruction accounting. The fallback remains
the original compiler, with every IR path disabled to prevent recursive outlines.

The short-budget arm cancels the enclosing first-instruction charge, publishes
the segment PC, and flushes the current architectural cache. It temporarily
supplies the callee with the remaining budget. The callee consumes that remainder
or returns precise early progress; the parent adds its preceding instruction
count and restores its original budget directly. It returns without spilling
stale caller registers over the callee's updated state. No following parent
guest instruction executes on this arm. Physical code bounds remain the parent's
validated interval, including inlined dependencies.

The module builder supports multiple private AOT callees after all public
functions, preserving sibling/export indices and keeping private definitions
unexported. It rejects nested outlines, invalid offsets and duplicate patches.
Fixed five-byte call operands are relocated after architectural cache layout
and deferred barrier sizes are known. Existing single whole-region outlines
remain supported. Typed i64 local-index fixups still precede barrier insertion.

Skipping original inlined segment bodies preserves instruction counts and uses
the final flattened instruction's source offset for the region end; inlined
leaf instructions share their caller offset. Segment selection forbids interior
branch labels/control operations. Generic pending wide state is materialized
before graph/fallback selection.

Validation requires private-segment selection, exact budgets after returns,
multiple callers/private targets and indices beyond 127. Production fault mode
--ir-short executes four instructions from a five-instruction translation, with
the faulting access inside its private short-budget helper. Native comparisons
cover callback state, partial transfers and continuation. Full compiler tests,
rebuilt probes and exact native image/audio replay precede serial gameplay.

This changes code layout, not instruction semantics. Smaller hot functions may
come with larger complete modules and more compilation work. Neither size nor
correctness establishes a throughput improvement. All research options stay
disabled by default pending measured acceptance.

The new short-budget probe initially differed in 106 policy-2 cases. The
native reference harness invoked a further Step after the exception callback
requested stop, while the single WASM Run correctly stopped. The harness now
honors that request across its Step loop. All 7104 fault comparisons then
match; the initial logs are preserved. This correction changes the probe, not
the compiler, and app/full-suite binaries remain byte-identical.
