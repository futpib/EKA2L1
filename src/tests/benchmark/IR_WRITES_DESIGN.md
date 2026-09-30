# Invariant write proofs consumed by mixed IR

Policy 12 extends policy 11's mixed integer/flag/call IR with the invariant
write proofs already used by the original emitter. It is opt-in. Runtime
selection does not change the delivered policy 7 archive.

The entry scanner proves a span through a register unchanged throughout the
region. It checks mapping permissions, endian state, alignment, span bounds
and physical code aliases. Failed proof enters the original compiled fallback.
The IR receives the same proved host address for a qualifying scalar store,
removing its repeated dynamic guard. Stores and loads remain ordered effects;
this does not cache loaded values or eliminate stores. Other memory operations
retain their guards and precise state reconstruction. A helper/callback ends
the region before later accesses can consume a pointer invalidated by remapping.

Existing validated leaf calls retain real guest PC, LR and instruction counts.
A short budget can return a shorter positive prefix through the private precise
fallback; the production runner must finish the requested budget exactly.
Tests compare that returned prefix with an independent interpreter and original
emission. Code aliases still require an immediate exit after the actual store.

Coverage includes 1,920 call/budget comparisons and 16,896 write-state cases
for policy 12. The production fault probe requires three actual IR proved
writes in its callback-remapping fixture. Its other coverage assertions remain
enabled for policy 12, including integer segments, dynamic guards and wide
products. Test assertions were strengthened after review of the first archive;
only the probe binaries changed in v2. Application, full-suite and fixture
binaries are byte-identical between the two archives.

Performance compares policies 12, 11 and 7 inside one application binary and
the exact served policy 7 archive. Static body size and probe translation times
are not gameplay throughput evidence. Every ordinary timing sample is retained.
