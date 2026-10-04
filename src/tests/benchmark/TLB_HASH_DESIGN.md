# Fold higher virtual-page bits into the DynCom data TLB index

Historical experiment, superseded by [the memory-cache comparison](MEMORY_CACHE_RESULTS.md). Production now uses only the original low-bit TLB index; the folded implementation and selection controls have been removed. The description and measurements below record the earlier implementation.

ZERO_LITERAL_RESULTS.md records repeated conflicts between instruction literals
and data at the same low-bit TLB slot. The experimental index is
(page ^ (page >> 9)) & 511, retaining 512 entries and all permission tags.
No guest addresses are special-cased. Extra shifts/XORs and new collisions may
outweigh saved misses; the result is a hypothesis until measured.

A per-TLB immutable choice controls insertion, permission-specific/general lookup
and invalidation. DynCom snapshots its pre-init research setting when constructing
the TLB. Native 12l1r cores keep the original default index because their native
code generator has its own indexing contract. ARM WASM guards emit the same
selected hash for scalar accesses and whole block spans, also serving invariant
proofs and mixed IR. A runner refuses the direct TLB pointer if its instance does
not match the configured emission scheme. Mapping, callbacks, endian handling,
code writes and exact code validation otherwise keep their existing contracts.

Browser API eka2l1_tlb_hash_configure accepts 0/1 only before initialization.
Benchmark/profile controls are explicit and recorded; the serial runner rejects
wrong mode reports. Fault probes receive --tlb-hash=0/1 explicitly, assert DynCom
instance selection and print PROBE_TLB_HASH. The comparator rejects missing/wrong
markers. Environment variables alone do not establish WASM probe selection.

Validation includes high-page conflicts in generated scalar/block accesses and
partial budgets; all compiler tests selectable with explicit --tlb-hash=0/1
and matching manual TLB fixtures; native replacement/permission/dirty/flush tests; rebuilt native
fault comparison including callback remapping; original and candidate checked
image/audio replay; frontend late-configuration rejection. Then serial same-binary
comparisons against mode0 and the exact served archive, initially on the new
longer-snake workload and subsequently the standard heavy scene if promising.

Default remains mode0. No performance claim or deployment is made by this design.
