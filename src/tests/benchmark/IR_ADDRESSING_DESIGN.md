# Narrow and scaled memory in mixed IR

This opt-in extension keeps values across ordinary byte/halfword accesses and
word/byte accesses with register offsets shifted by an immediate amount. It also
accepts conservative PC-relative loads. The existing integer shift semantics
supply LSL, LSR32, ASR32, ROR and RRX address values; carry remains architectural
input. PC reads use the instruction's pipeline PC+8. Writeback occurs only after
a successful memory effect, with the pre-instruction state retained at exits.

Loads of all widths remain ordered effects, including signed byte and halfword
loads. Stores truncate to their architectural width. No load is CSE'd or moved,
and no memory operation is elided. Exclusive, doubleword, unprivileged,
conflicting writeback, PC-destination and unsupported encodings retain fallback.
The whole-region entry-proof backend retains its earlier memory subset.

The shared span-proof helper now takes an optional alignment argument, default4
for all original callers. IR byte/halfword guards use alignment1/2 and their
actual width. A successful TLB proof establishes the permitted ordinary page's
host base. Later cached accesses still check their own alignment and span;
read/write proofs remain separate. Aligned scalar accesses fit in one page,
and multiword spans keep an explicit end bound. Endian and zero-tag checks,
physical code-overlap detection and wrapping-end guards remain in place.

Snapshots and exact instruction accounting are unchanged. No helper runs inside
a successful graph. An unsafe access reconstructs the pre-instruction state
and returns before effects; the original runner performs precise continuation.
No game addresses select the optimization. Captured busy loops motivated these
ordinary memory forms, not per-game specialization.

Validation includes all widths, signed extension, scaled/rotated offsets,
PC-relative reads, pre/post and positive/negative writeback, code aliases,
permissions, endian state, page edges and safe partial budgets. A mixed-width
chain verifies that narrow cached proofs do not bypass later alignment/span
checks. The `--ir-addressing` production fault mode requires IR guard selection
and compares real callbacks with native. Full compiler/native/frontend tests,
checked replay and serial gameplay measurements gate any promotion.
