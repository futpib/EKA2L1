# Fault-only IR reconstruction

Policy 3 (`outlined_recipes`) extends policy 2's outlined integer/memory IR.
Pure values live only in intermediate fault snapshots are computed inside
those exits. Values needed by successful memory effects or the final snapshot
remain on the successful path. Policies 0, 1, 2 and configured behavior retain
their prior lowering; experimental build options remain OFF by default.

Reads, writes and mapping guards retain their original order. In particular,
an earlier load must retain its value even when an intervening store overwrites
its source. Recipes use that saved value and never repeat the memory access.
All source values are consumed before architectural snapshot destinations are
written, preserving register swaps and other parallel assignments.

Pure lowering is shared by ordinary values and recipes, including typed i64
products, packing and halves. Each exit memoizes shared expressions in typed
locals. The compiler resets that memoization at each independent exit: one
failure arm cannot rely on assignments in another. This avoids exponential
code growth for expressions such as repeated squaring. It does not reduce
the number of locals automatically, and may increase backend frame or cold
code costs. Smaller successful-path work is a hypothesis, not a speed claim.

New raw compiler tests require actual non-half recipes and compare every
budget with the interpreter. They cover overwritten load sources, wide
products, shared expressions, a 24-operation shared DAG, memory permissions,
alignment, page boundaries and endian fallback. A new production runner
fault mode exercises callbacks after wide arithmetic whose results are
overwritten later. Existing tests expand to the new policy as well.

Measure original emission (0), previous outlined IR (2) and recipes (3) in
one archived application binary, with the served build as a separate anchor.
Use full compiler/native/frontend tests, explicitly rebuilt fault probes and
checked image/audio replay before serial gameplay timing. Preserve all timings.
The two captured busy loop fixtures contain no cold values and therefore
remain byte-identical between policies 2 and 3; do not extrapolate coverage
or a game speedup from the synthetic recipe fixtures.
