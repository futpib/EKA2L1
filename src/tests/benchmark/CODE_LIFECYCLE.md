# Mutation-driven compiled-code validation

Configure the experimental path with `-DEKA2L1_WASM_CODE_LIFECYCLE=ON`.
It implies `EKA2L1_WASM_CODE_VERSIONS` for that configuration; both remain off
by default. This extends the physical-backing tracker documented in
[CODE_VALIDITY.md](CODE_VALIDITY.md).

A newly registered code allocation is initially **unwatched**. Loading,
relocation and guest writes before compilation need no version increments.
After the first exact comparison, the cache watches the physical pages of the
compiled region and all inlined dependencies. Guest aliases share those pages.

Writes to watched pages advance their versions and publish a shared atomic
mutation flag. Host-pointer exposure, retirement and allocation reuse also
publish a mutation. At lookup, the CPU consumes pending mutations and advances
one validation epoch, coalescing multiple notifications. A cache entry already
validated in that epoch skips both byte scans and its per-page stamp walk.
After a mutation, it checks stamps; if a relevant stamp changed, it performs an
exact comparison before retaining or rejecting the compiled entry. An unrelated
watched-page write therefore costs a stamp walk, not a byte scan.

Mapping/address-space/mode guards still run on every entry. A mapping-generation
change forces mapping resolution and byte validation independently of the
mutation epoch. Adding an inlined dependency clears prior validation. Stores
into the executing region still cause a precise exit; epoch validation does not
replace within-region code-write guards or instruction-budget checks.

An exposed writable host pointer permanently disables stamp-only validation for
its entire allocation. Future unannounced writes use exact checking. Untracked,
retired, overlapping/reused and overflowed backing also retains that fallback.
A page-version overflow poisons the page; it cannot become unwatched and later
be rearmed. Epoch overflow permanently disables the epoch shortcut. The page
versions and epoch have one writer/consumer, the guest CPU; host exposures only
publish atomic flags. This does not introduce support for concurrent arbitrary
host mutation while guest instructions execute.

The change moves repeated proof checking out of steady compiled entry and
starts write tracking only when code depends on a page. It does not precompute
arbitrary runtime data addresses, remove permissions/endian/alignment checks,
or make all host pointer lifetimes observable. It is general across guest code
and contains no game address/name special cases.

Tests cover lazy watching, write coalescing, generated STRB/STRH/STR/STM stores,
physical aliases, dependency changes, remaps, host escape/retained writes,
allocation reuse, page-version overflow and epoch exhaustion. The native/WASM
fault probe and checked 1,600-image replay remain required acceptance gates;
performance must be measured separately without diagnostic counters or sampling.
