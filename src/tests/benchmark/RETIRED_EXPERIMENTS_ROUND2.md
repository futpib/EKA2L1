# Second compiler experiment cleanup

This follows [the first cleanup](RETIRED_EXPERIMENTS.md). It removes eight more
unused or rejected experiment groups, including instruction-count policies that
the first cleanup retained. Historical measurements and reports remain in Git.
This is a maintenance change, not a measured speedup.

## Removed

1. Unused code-version/epoch hit counters and empty registry configuration hooks.
2. Exact-byte scanner modes 1, 3, and 4, including stored comparator pointers in
   primary and dependency cache entries. Original mode 0 and grouped mode 2 remain.
3. Compiled SVC emission, pending-trap flags, and the corresponding runner and
   outer-loop checks. SVCs continue through the interpreter's existing handler.
4. Compiled memory-miss handling. Restartable misses keep the established
   interpreter fallback; normal memory helpers remain where required.
5. Eager ROM regions, special ROM leaf fusion, and bounded Thumb ROM calls.
   Ordinary ROM block compilation and sampled hot-code compilation remain.
6. Leaf feature bits 1 through 64: expanded instruction eligibility, internal
   branches, call/tail prefixes, and branch veneers. Ordinary returning leaves,
   conditional integer leaves, and literal-PC veneers remain.
7. Instruction-count policies 8 and 18 and their deferred-count machinery.
   Policy 17 retains memory proofs, straight-line budget chunks, and natural-loop
   budget checks, with the ordinary precise instruction counter.
8. The N80-only Snakes executable patch and its forced-interpreter browser
   override. The loader no longer modifies Snakes for that resolution experiment.

Experiment-only fixtures and command plumbing were removed with their paths.
Surviving fixtures still cover budget exhaustion, predicates, callbacks, memory
faults, remapping, code mutation, aliases, and exact interpreter equivalence.

## Current configuration

Compiler policies are `-1` (configured), `0`, `4`, `5`, `6`, `7`, and `17`.
The browser defaults remain policy `17`, hotpath `2`, Thumb direct memory `1`,
and unsafe executable-byte policy `3`. Strict mode `0` retains exact primary
and dependency byte checks. Both modes retain mapping and lifetime checks.
Leaf features accept `0` or `128`; exact-byte comparison accepts `0` or `2`.
Numeric IDs have not been reassigned.

The browser launcher and replay/profile runners reject these retired environment
variables, even when set to zero:

- `EKA2L1_COMPILED_SVC`
- `EKA2L1_COMPILED_MEMORY_MISSES`
- `EKA2L1_ROM_LEAVES`
- `EKA2L1_ROM_CALLS`
- `EKA2L1_AOT_EAGER_REGIONS`
- `EKA2L1_SNAKES_N80_NATIVE_RESOLUTION`

The native replay runner also rejects the removed N80 option. Removed browser
configuration/report exports are absent. Old experiment commands require their
original Git revision.

## Verification

Validation artifacts are in `/home/claude/.scratch/eka-retire-round2/`.
The diagnostics-free gameplay WASM is 10,796,420 bytes, with SHA-256
`4ac9062069ac530ca9dadb6de4a97d52302d250776f9dd353fef384a8f5cf5e1`.
It is 27,996 bytes smaller than the first cleanup build.

- Native CPU CTest passed; the native fault and compiler probes built.
- The diagnostics-enabled WASM suite reported 167 passed and zero failed.
  The existing `crash-repro0x8046506E` expected failure remains a harness
  limitation. The focused verifier lookup-protection check also passed.
- All 15,232 native/WASM fault comparisons matched under policy 17, scanner 2,
  and unsafe modes 0 and 3. Cases cover entry budgets, deferred memory exits,
  remapping, write/remap interactions, wide snapshots, region block spans, and
  literal-PC veneers; registers, counts, callback state, and memory matched.
- Snakes and Sky Force each matched 60 unique reference frames, guest records,
  PCM, and audio events. These replay checks used SwiftShader.
- Browser API and launcher policy tests passed, including absent exports and
  rejection of retired policies, scanner modes, leaf bits, and environment
  options. Chrome profiler mapping tests passed.
- A real Chrome profile with custom diagnostics disabled captured 33 isolates,
  including 4,293 samples in the selected guest worker and guest-entry labels.
  The profiling run used hardware Vulkan.
- Both games passed on `https://claude-laptop.lan:8188/` with hardware Vulkan,
  launcher selection, keyboard input, advancing guest time and frames, and no
  fatal page errors. Screenshots show active gameplay. The served WASM hash and
  default policy 17 were checked against the frozen gameplay build.

The first Snakes replay stalled during early boot with guest time at 82,328 us
and no presented frames. The previous build completed the same harness, and an
isolated candidate rerun matched all 60 reference frames, guest records, PCM,
and audio events. The initial stall did not produce a fatal browser error; its
cause has not been established. Logs and the failed-run screenshot are retained
under `stalled-replay-standard/` and `stalled-*` in the artifact directory.
