# Thumb runtime state cache

This second opt-in stage keeps PC, instruction budget, CPSR and TLB pointer in
WASM locals in the direct-memory Thumb path. It uses the existing deferred
state-cache barriers: callbacks receive published state, callback changes reload
into locals, and every return publishes the final state. ARM translation leaves
the new PC-cache flag off. The public option and its disabled default are
unchanged; this stage contains no game-specific rule.

The first stage still needed repeated PC stores and runtime-field loads between
instructions and memory guards. Keeping them local lets the host compiler remove
dead PC assignments and repeated loads while preserving the intermediate states
that a callback or partial-budget exit can actually observe.

The frozen v3 archive is based on `2e4f1f0d4` plus source patch
`d992c4497003c1951d6942123ba401fe38e0b47161204538c11b2bce33551f68`.
Static WASM SHA-256:
`7c43bebf1d39fb7e3b0521bfe0af23b82a74addfb508ba1cab00220dc291c407`.
The subsequent `7cba6c1b7` commit records only first-stage timing evidence.

All 177 compiler tests pass. The bounded matrix now includes two additional
Thumb variants with the new path enabled: 477,680 total exact instruction-budget,
register, flag and memory comparisons. The Thumb memory matrix passes 9,222
comparisons, including callbacks that replace the TLB pointer, change endian
mode, reduce the budget, and change PC between transfers of one instruction.
The known crash-repro XFAIL remains unchanged.

All 2,880 v3 WASM memory-fault records match the unchanged native golden records,
including callback-visible state, for byte policies 0/3 and both TLB layouts.
Sky Force control, candidate and interpreter-checked candidate each match the
240-image native reference and 1,703,464 PCM frames exactly. Candidate Snakes
matches the standard 1,600 images / 4,656,051 PCM frames and longer route 360
images / 2,832,756 PCM frames, including exact guest records and audio events.
The frontend/configuration API is unchanged; its first-stage tests are reused.

`THUMB_STATE_EVIDENCE.json` retains provenance, reports and the complete suite
log. An exploratory six-observation serial screen follows correctness, comparing
v2 and v3 with the Thumb option enabled in both. It uses shared audio, physical
GPU rendering without capture, no sampling or detailed counters, and unchanged
limits and scheduling. A single pair per route is not promotion evidence.
Further measurement against the untouched live archive and normal live/audio
acceptance remain required. The realtime Sky Force target is still open.
