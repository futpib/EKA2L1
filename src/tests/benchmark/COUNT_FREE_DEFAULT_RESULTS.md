# Browser execution without instruction accounting

Browser play now always uses watchdog execution. The counted compiler path,
entry-budget wrapper and precise fallback, emitted counters and budget checks,
SVC instruction totals, counted chain runners, instruction-count verifier,
configuration APIs, and game-picker toggle have been removed. Legacy
`?counting=on` links cannot restore accounting. The 512-region chain cap is gone;
static compiler size limits remain.

A separate worker requests yields every 2 ms by default. Generated code polls
where control flow is not statically proved finite; finite segments finish
without instruction accounting. Paced play samples elapsed host time at scheduler
boundaries. Unpaced execution advances virtual CPU slices at yields and skips
idle waits. Explicit pauses do not advance guest time.

Native CPU backends retain their scheduling budgets. Debugger single-step still
executes one instruction through the interpreter. Neither is a selectable counted
browser-play mode. The obsolete instruction-bounded standalone WASM CPU/fault
and matched-kernel harnesses are retired; native tools remain buildable.

## Verification

The [machine-readable results](COUNT_FREE_DEFAULT_RESULTS.json) retain the served
WASM hash, browser/GPU identity, policies, clock and compilation measurements,
and local evidence paths. The deployed bundle was checked against the `.lan`
response hash. Both games were launched through the real picker in fresh Chrome
sessions, serially, with hardware Vulkan rendering and sound muted. Start/end
screenshots were visually checked for gameplay, and keyboard input was consumed.
Old counting bookmarks, absence of the selector and retired exports, advancing
watchdog requests, and instruction totals staying zero are asserted by
`src/tests/wasm/paced-gameplay.ts`.

| # | Game | Presentations / host second | Guest seconds / host second | Instruction total |
|---|---|---:|---:|---:|
| 1 | Snakes | 15.99 | 1.00006 | 0 |
| 2 | Sky Force | 31.98 | 1.00000 | 0 |

Each gameplay measurement lasted 15 seconds after loading and menu navigation.
These are paced playability checks, not an uncapped throughput comparison.
Compilation within the measured windows remains included: approximately 6.2 ms
for Snakes and 5.8 ms for Sky Force across translation, emission and installation.

The compiler suite passed 181 tests with zero failures. Coverage includes ARM and
Thumb register/flag/memory effects against the interpreter, aliases and mapping
changes, syscall returns, finite-loop proofs and external atomic interruption.
The precompilation and watchdog standalone checks passed. Frontend API and policy
tests passed. Native CPU tests passed 497 assertions in 33 cases; compiler probe,
fault probe and native matched-kernel targets also built.

Unpaced Snakes captured 20 gameplay frames spanning guest timestamps 42–43.08 seconds,
with zero instruction totals. Its host capture duration is not a speedup claim:
this clock no longer produces the old instruction-clock replay schedule.
Historical timing and exact native/browser replay reports remain historical.

The old counted input route stopped at the menu in the initial unpaced smoke
check. Browser capture/profiling now default to the existing count-free input
route and a 42-second warmup; the native route remains unchanged.
The profile harness also completed its new default 42–46 second window: 66
presentations, zero initial/final instruction totals, with sampling disabled.
