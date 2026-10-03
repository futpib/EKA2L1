# Bounded first-use ROM compilation with slot reuse

Opt-in first-use policy 3 compiles RAM and ROM entries immediately, replacing dynamically compiled ROM entries in FIFO order within the existing 4096-entry capacity. The eager registry is not owned by this cache. Existing policy 0 remains the default, policy 1 retains the failed no-replacement experiment, and policy 2 provides the matching RAM-only first-use control. No game or guest address selects behavior.

Each reclaimable ROM module has one public export. Replacement occurs only after generated execution has returned to the outer dispatcher. Registry removal checks ownership before unregistering, and Emscripten function slots are released on their owning worker. Thread-local cleanup handles worker exit; reset clears the current worker cache. Unsupported entries retain bounded rejection tracking and correctness fallback. Instruction/region budgets, RAM mapping validation and helper boundaries are unchanged.

Fresh acceptance:

- All 189 compiler tests, including 16672 actual first-use, ROM/RAM, budget, rejection, remapping, eviction, table-slot reuse and worker-lifetime checks. Tests exceed the 4096 capacity, revisit evicted ARM/Thumb entries, preserve foreign registry replacements and verify reuse across resets and worker exit.
- 5632 native Thumb exchange cases; 5376 selected memory-fault cases; 20736 syscall cases; 17280 exclusive cases; 4032 ARM memory cases; 2880 Thumb memory cases; 1152 Thumb call cases. These instruction probes cover emitted semantics, not replacement policy; real-runner tests and game replays cover the latter.
- Three native package targets and seven exact image/frame-record/PCM replays, with actual policy readback and valid post-initialization change rejection.

Releasing a table slot makes its function and module eligible for collection; it does not force browser garbage collection. The runtime compiled_functions counter is cumulative installations, including recompilations after eviction, not live module count. The static WASM grows by 3754 bytes versus V25; a matching-binary comparison does not isolate that added binary cost.

Diagnostic coverage (not speed evidence):

| Route | Guest instructions | Interpreted | Share |
|---|---:|---:|---:|
| sky | 2,142,147,961 | 0 | 0.000000% |
| combat | 2,171,043,925 | 0 | 0.000000% |
| standard | 2,964,235,296 | 0 | 0.000000% |
| long | 3,031,637,220 | 0 | 0.000000% |

All interpreted counts match independent total-minus-compiled counters. Report zero only when the integer count is zero; a measured interval does not prove universal coverage. Correctness and census jobs may overlap, and their wall times are not benchmarks. A separate serial timing screen measures gameplay and warmup costs. No speed promotion or realtime claim; live is unchanged and nothing is pushed.
