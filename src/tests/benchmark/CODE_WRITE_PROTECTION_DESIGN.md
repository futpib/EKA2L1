# Watched-code write protection experiment

The existing code-lifecycle experiment still emits physical-page tracking work
for every direct generated store. This opt-in experiment moves that work to TLB
insertion and a check before every generated-function entry. It is not a promise
of a speedup: the extra callback traffic and TLB scans may exceed the saved work.

`EKA2L1_WASM_CODE_WRITE_PROTECTION` implies the existing versions/lifecycle build
options. Runtime selection is frozen before CPU initialization. The delivered
exact-byte build remains separate. Invariant span/IR research paths disabled by
code versions remain disabled; comparisons use explicitly selected policy 0.

An exact snapshot first marks physical backing pages as watched. A monotonically
increasing watch generation forces each CPU's own TLB to remove writable tags
whose host spans overlap watched pages before entering generated code. Refills
apply the same restriction, including guest aliases and partially overlapping
host spans. Read/execute tags and the MMU's real permissions remain intact.
Generation exhaustion permanently forces rescanning; it cannot reuse a lease.

Generated direct stores then omit their page-version barrier. Writes to watched
pages take existing precise helper/interpreter paths, whose real memory callbacks
retain the version and mutation notification. STRH already takes a helper path;
it need not have the same zero-instruction exit as a deferred scalar/block store.
Physical code-write exits within regions, instruction budgets, remap guards and
interrupt behavior remain. Host pointer escapes continue to force exact scans.

Focused tests exercise both linear/folded cache indexing, aliases, refills,
remaps, partial host spans, watch-generation exhaustion and pointer escapes.
Denied direct stores leave registers/memory/count unchanged at the original PC;
the halfword helper sees the precise pre-store state, changes exactly two bytes,
invalidates the watched stamp and exits after one instruction.

The ordinary fault matrix additionally verifies explicit runtime mode markers
and exact callback/register/memory equality against native interpretation. Its
fixtures are not all watched code pages; do not describe every matrix case as a
code-protection test. Checked game replay and the focused watched-page tests
provide separate evidence. Timing must follow correctness and run serially
against the same binary with protection disabled and the exact served archive.
