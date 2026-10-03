# Shared immutable-ROM module dispatcher

Opt-in iterative dispatch keeps existing generated ROM blocks in a private function table, using a sparse mode-tagged address map. All original public entry names alias a single dispatcher. No guest calls recurse on the host stack. A map owner outlives the module exports. Dynamic ROM/RAM compilation and cache capacities are unchanged.

The dispatcher checks remaining instruction budget and constituent-block cap, then invokes each unmodified base function with the exact remaining budget. Stops, IRQs, zero progress, pending syscalls and helper callbacks return to the outer runner; RAM/missing successors retain normal mapping/lifetime lookup. Exclusive callbacks mark the boundary without repacking CPSR. Bounded direct ROM calls and module dispatch are mutually exclusive. The default remains off.

Acceptance:

- All 190 compiler tests, including 203 map/slot extent checks, 5760 full-state/budget/cap/mode/callback/SVC composition comparisons and 42 real-runner cap cases.
- Additional native module-dispatch comparisons: 20736 syscall and 17280 exclusive cases. Existing 5632 interworking, 5376 cold-memory, 20736 syscall, 17280 exclusive, 4032 ARM memory, 2880 Thumb memory and 1152 Thumb call cases pass.
- Three native package test targets and seven exact image/frame-record/PCM replays. Actual option readback and post-initialization policy rejection are checked.
- Initial full suite trapped during module staging because the standalone fixture lacked a logging filter. Corrected test-only logging and CPU mem-cache setup; all 190 tests rerun. Production/probe artifacts are byte-identical, so their passing checks are explicitly reused. Failed archive/log retained.

Diagnostic coverage and mechanism (not CPU-time shares):

| Route | Guest instructions | Interpreted | Outer generated invocations | Constituent blocks |
|---|---:|---:|---:|---:|
| sky | 2,142,147,961 | 0 | 185,573,404 | 220,539,887 |
| combat | 2,171,043,925 | 0 | 171,724,287 | 204,000,045 |
| standard | 2,964,235,296 | 0 | 129,434,680 | 129,703,735 |
| long | 3,031,637,220 | 0 | 128,594,358 | 128,844,564 |

Independent total-minus-compiled counts match interpreter census. performance.aot_dispatches retains constituent-block count; guest-profile.compiled_blocks counts outer generated invocations and module calls. Edges aggregate a whole module call in this mode and cannot be interpreted as original single-block exit categories.

The reference eager module grows from 12,194,656 to 12,207,144 bytes (+12,488), with a 723,968-byte map and 6,065 base functions. Static WASM grows from 10,977,152 to 11,019,571 bytes (+42,419). The same-binary panel does not isolate static binary growth. The prototype adds a per-module map, private table and outer-runner specialization. Include static/generated module size, map allocation, startup and both-game speed effects in evaluation. It targets the fresh V26 dispatch samples, not game identities or fixed guest addresses. No speed/realtime or promotion claim. Live remains unchanged.
