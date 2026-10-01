# Invariant write-span experiment

Policy 5 extends policy 4's conservative region-wide register write-set analysis
with immediate pre-indexed word stores without writeback. A selected address
root must remain unchanged on every reachable path, including conditional
instructions and inlined leaves. At least four selected accesses are required.
There are no game addresses or stack-register special cases.

Read and write spans remain separate permission groups. Before guest effects,
each span must be aligned, fit one permitted TLB page, have valid backing, and
use the supported endian mode. A write span must additionally have a
non-wrapping physical exclusive end and not overlap the entire current-code
guard. The runtime expands that guard for inlined dependencies. A failed proof
calls the original private compiler before any guest instruction executes.

The proof only replaces address/permission/code-alias checks. Actual loads and
stores retain their conditions and order. Per-instruction counts, budget checks,
interrupt checks, deferred memory exits and unknown-store code guards remain.
Returning helpers set the region exit flag, preventing a later use of a host
pointer invalidated by a callback. No general IR segments are selected.

Policy 4 remains the delivered read-only control. Policies 4 and 5 can be
selected before initialization within identical application bytes. The new
policy remains opt-in; ordinary defaults and LAN selection are unchanged.

## Validation design

The full compiler suite adds store loops, conditional stores, interleaved loads
and stores, and rejected roots changed by unconditional/conditional arithmetic
or multiplication. The matrix includes permission denial, endian modes,
page/wrap/zero boundaries, current-code writes, short budgets and interrupts.
Ordinary cases compare precise original-compiler progress, state and memory,
and independently run the interpreter for that same guest instruction count.
Physical-alias cases compare the original emitter and require immediate exit
when a store changes code; the identity-mapped interpreter fixture cannot
represent those aliases and is not claimed as their independent oracle.

The production runner's new --invariant-write-remap probe faults through an
unproved root between an initial proved read and later stores. The exception
callback replaces the proved root's backing. Native and WASM must match all
callback records, final state and memory. Additional assertions require stores
to reach the new backing and leave the original backing unchanged. Both repaired
and unrepaired/retry/stop callbacks, initial read-only/read-write mappings and
endian modes are exercised.

Correctness gates and serial same-binary timing results belong in the results
report. Static coverage and generated size do not establish a speedup.
