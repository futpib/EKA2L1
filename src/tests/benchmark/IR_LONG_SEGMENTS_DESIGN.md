# Bounded longer mixed-IR segments

Policy 14 uses policy 13 integer, condition, flag, memory and inline-leaf
semantics, increasing only the maximum selected segment from 32 to 128 guest
instructions. Existing branch targets, unsupported instructions and conditional
memory/control operations remain boundaries. Original-emitter budget chunks
retain their 32-instruction limit. Policy 7 remains the delivered default.

A larger value graph can eliminate intermediate state publication and reuse
values across the previous artificial boundaries. It can also increase local
pressure, graph-building work, exit reconstruction and short-budget fallback
frequency. No performance benefit follows from reducing the segment count alone.

Translation metadata records maximum selected segment length and total selected
instructions. Inspect the captured busy regions under policies 13 and 14 before
claiming that the new bound changes real coverage. There are no game-address
specializations.

The differential test executes a 133-instruction sequence through both policies,
including conditional flag consumers, register swaps, wide products, ordered
stores/loads and faults at offsets 31, 63, 95 and 127. Every budget from 0 through
134 is compared with the interpreter. A production-runner fixture faults after
111 instructions, requiring exact callback state and continued progress. Existing
fault/remapping, inline-call and exact image/audio matrices remain gates.

Performance compares policies 14, 13 and 7 in one application binary and the
exact served archive. All observations remain in the record. No owned heavy job
may overlap timing or warmup; profiles and correctness runs are separate evidence.
