# Prove read spans through unchanged registers across regions

Research policy 4 selects original emission with invariant read-span proofs;
it does not enable integer IR segments. Normal/configured behavior is unchanged.
The policy requires bounded regions, register caching, deferred memory exits
and exact code checking. It is disabled with code-version tracking.

A conservative write-set scan covers all reachable instructions, including
inlined leaves and conditional writes. Unknown/status-transfer encodings reject
the proof. Long and short multiply destinations, scalar loads, block-load masks
and base writeback all invalidate a candidate root. Doubleword memory encodings
are excluded explicitly. Candidate accesses are immediate, pre-indexed word
loads with no writeback or PC destination, through a never-written entry
register. At least four accesses are required; each permission group must fit
one bounded page span. No register or guest address is whitelisted.

Entry checks validate mapping, permission, alignment, span and endian state
using the existing block-transfer guard. Failure calls an original compiled
fallback before any guest effect, so a conditionally skipped load cannot
raise an early fault. A successful proof retains the host base; actual loads
still execute in order and observe intervening stores. Stores and all other
accesses keep their existing guards and code-alias handling.

Per-instruction budgets, control-flow and interrupts remain in the original
emitter. A proof does not require a full-region budget. Existing slow helpers
set AOT_EXIT; the next instruction or control join must exit before reusing
a host pointer invalidated by a callback. Exiting and reentering a compiled
function performs a new proof. The fallback is private and calls return
without stale outer-cache writeback.

The new matrix compares both the original compiler's exact progress and the
interpreter's resulting state/memory. It covers loops, conditional loads,
overwritten data, base mutation by arithmetic/conditional writes/multiply,
permissions, endian state, alignment, boundaries, code aliases, short budgets
and pending interrupts. Production region-span fault fixtures require proof
selection and check callback state against native. Full-suite and exact game
replay acceptance precede timing.

In the two captured busy loops, nine and eleven reads select the proof. The
hot bodies shrink from 19,852/12,083 to 18,780/10,862 bytes; complete modules
retain original fallbacks and grow. Those static observations do not establish
dynamic coverage or a gameplay gain. Compare policies 0 and 4 in one archived
application binary, with eager regions explicitly off and shared audio on.
