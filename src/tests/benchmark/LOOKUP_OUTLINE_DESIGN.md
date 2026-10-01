# Outlined code-cache recovery experiment

The delivered grouped-scanner archive is the fixed external control. The earlier
code-cache census recorded 98.672% recent-entry hits and almost entirely unchanged
mapping generations. Those observations motivate a smaller common path; they do
not establish recoverable time or a performance gain.

The opt-in lookup checks the complete recent key and live flag, nonzero mapping
generation, and mapping-source identity before exact primary/dependency byte
validation. Container search and mapping refresh enter the preceding lookup
implementation, moved out of the header. A byte mismatch goes directly to an
invalidation helper: it cannot enter another validation attempt and revive the
version after its bytes have changed back. Legacy zero-generation cores retain
per-entry mapping resolution. Address spaces, invalidation counters, backing
identity, extents and inlined-code dependencies retain the existing contracts.

The binary default remains the original lookup. The explicit pre-initialization
`eka2l1_code_lookup_configure(0/1)` control selects the experiment. Fault probes
accept `--code-lookup=0/1`; comparisons reject absent or incorrect markers.
Browser replay/profile tools carry and record the same explicit selection.
The grouped byte scanner (mode 2), compiler policy 7 and folded TLB (mode 1) stay
fixed in the intended matching-binary comparison.

A new WASM regression exercises both layouts through repeated hits, every selected
primary/dependency mutation boundary, rejected-version resurrection attempts,
recent-slot collisions, mapping-source replacement with an equal generation,
identical-byte remaps, unmapping, narrowed extents, ASID changes, dependency
invalidation and legacy/zero generations. Existing native mapping/dependency
regressions also execute under both layouts.

Static inspection of the application finds no standalone common `find` function;
the ordinary compiled-lookup function is 311 WASM bytes. Cold `find_original` is
1,001 bytes, rejection is 157, and `bytes_match` is 1,008. Moving the implementation
also allowed the compiler to group byte comparisons differently: the preceding
archive retained a standalone scanner. The two selectable modes in the candidate
share this layout, while the exact served archive controls for its combined cost.
These are WASM body sizes, not V8 machine code, inlining traces or speed evidence.
The implementation leaves code-version/lifecycle alternatives off in this build.

Full acceptance and ordinary serial timing are pending. The experiment is not
selected by the LAN launcher. Preserve all timing samples and confirm any gain
before considering delivery.
