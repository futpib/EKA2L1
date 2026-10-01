# Browser ARM implementation audit, 2026-09-30

Read-only source comparison. No cross-project speed claim or code transplant.
Pinned sources and hashes are recorded in ARM_BROWSER_SOURCE_AUDIT.json.

## Cloudpilot: cached interpretation with a build-time direct dispatcher

Pinned revision `e886d6cf44658e186951efb4f22907ae9184c028`.
The ARM/Thumb loops in `src/uarm/uarm/CPU.cpp` fetch a decoded instruction,
advance the guest PC, execute its handler and check the slow-path flag each
iteration. `icache.cpp` caches decoded handlers with instruction data and
translation/revision checks. This inspected CPU path is a cached interpreter,
not a runtime ARM-to-WASM block compiler.

A more interesting implementation detail is the production build's Binaryen
postprocessor, [build-jump-table/main.ts](https://github.com/cloudpilot-emu/cloudpilot-emu/blob/e886d6cf44658e186951efb4f22907ae9184c028/src/uarm/tools/build-jump-table/main.ts).
It remaps tagged handler addresses to dense IDs and replaces dispatch functions
with a WASM switch/branch table whose cases call handlers directly. ARM/Thumb
and MMU/MPU variants have distinct dispatchers. `src/uarm/Makefile` applies it
outside development builds. This is not merely putting indirect targets into
one instance. It is a concrete direct-dispatch layout worth a discriminator.

Applicability limit: Cloudpilot knows its interpreter handlers at build time.
EKA2L1 generates guest-region functions dynamically in separate instances.
A similar guest-region dispatcher would require generated clusters with direct
calls, a lookup/validation interface and precise budget/exit contracts. Existing
module-layout fixtures found cheaper isolated direct calls but no dependable
co-location-only gain; they did not establish a whole-game cluster win. A giant
switch over all regions could itself be costly. Do not infer that replacing our
CPU with this interpreter will be faster.

`pace_patch.cpp` finds an exact ROM signature, checks uniqueness and callout
locations, then redirects PACE entry/resume/callout handling to emulator support.
That removes a nested m68k emulation layer in Palm OS. It is system-specific
acceleration, not a generic instruction optimizer. No equivalent nested layer
has been identified for Snakes; do not invent game-specific shortcuts from it.

## QEMU Wasm: TCG lowering, tiering, and an outer dispatch loop

Pinned revision `0ef7b4e2814b231705d8371dd7997f5b72e70baf`.
[tcg/wasm32.c](https://github.com/ktock/qemu-wasm/blob/0ef7b4e2814b231705d8371dd7997f5b72e70baf/tcg/wasm32.c)
contains the browser integration and cold TCI path. Its header sets
`INSTANTIATE_NUM` to 1500. The execution loop uses TCI below the counter threshold,
instantiates generated WASM when eligible, and invokes compiled exports through
function-table indices. Instances import the shared memory and helper functions.
The current code caps live instances at 15000 and accounts for deferred browser
collection. These constants describe this revision, not recommendations for ours.

[tcg-target.c.inc](https://github.com/ktock/qemu-wasm/blob/0ef7b4e2814b231705d8371dd7997f5b72e70baf/tcg/wasm32/tcg-target.c.inc)
emits same-TB backedges as WASM loop branches. Other linked targets update the
context and return to the outer execution loop; the inspected backend does not
turn every inter-TB edge into a direct WASM tail call. It uses direct fast memory
accesses after TLB checks and helpers for the slow path, with resume/unwind state.
TCG virtual-register globals must not be confused with proof that architectural
ARM register values remain live across arbitrary TB boundaries.

This is a substantial reusable semantic frontend/backend ecosystem, but its
source does not remove our identified dispatch problem automatically. A fair
adapter experiment must match instruction accounting, callback-visible state,
code writes/remaps and memory faults before timing. Building and comparing such
an adapter remains future work; this audit has not run QEMU against our workload.

## Decision

Continue to consider a generated, bounded direct-call cluster as a structural
experiment, with exact validation and exits retained. Treat Cloudpilot's switch
as a design reference rather than a drop-in patch. QEMU remains a serious larger
backend comparison, with clear adapter cost. Neither source audit establishes a
performance win. The user's length-dependent slowdown investigation also remains
open; kernel or short-scene results cannot close that request.
