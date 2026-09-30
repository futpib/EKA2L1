# Mixed IR with invariant read proofs

Research policy 9 combines policy 4's read-only region-entry proofs with policy
3's outlined mixed IR and memoized cold exit recipes. Defaults are unchanged.
It does not select write-span proofs, budget chunks or deferred chunk counts.

The previous mixed IR receives an empty proof map, so its memory operations
repeat dynamic page guards even where the original emitter can retain a proved
host span. This policy passes selected invariant read addresses to the graph.
A scalar load whose PC has an entry proof uses that host base and displacement;
other memory operations retain their dynamic guards and precise snapshots.

Loads remain ordered effects, including loads whose result becomes dead. Stores
still retain permissions, alignment, mapping and code-alias guards. A proved
read observes preceding writes normally; reconstruction uses saved earlier
load results, not fresh loads. The graph remains bounded by existing condition,
flag, control-flow and entry-label restrictions. Register roots of an entry
proof cannot be written anywhere in the region, including inlined leaves.

Entry proof failure runs the original compiled function before effects. A later
unproved-memory guard failure reconstructs the exact intermediate state and
returns before the interpreter handles the access. A callback can change a
mapping, so subsequent execution enters a new function proof. Short budgets
use the existing private original compiler and return immediately afterward.

Metadata ir_proved_reads counts IR read nodes actually consuming entry host
proofs. Regression coverage requires selection in unconditional fixtures and in
the callback-remapping production fixture. Extra matrix cases cover register
swaps, shared cold expressions, stores overwriting earlier source memory and
subsequent reads. The independent interpreter validates the exact returned
prefix; function stopping points can differ at a faulting store. Existing
policy-4 progress comparisons remain unchanged.

Full compiler/native/frontend tests, both rebuilt fault matrices, exact native
image/audio replays and serial same-binary controls are required. Neither size
nor reduced guard counts establish a speedup. The candidate passes the recorded
correctness gates, but the completed gameplay batch did not establish a gain.
It remains opt-in and unserved.
See INVARIANT_IR_RESULTS.md and INVARIANT_IR_EVIDENCE.json.
