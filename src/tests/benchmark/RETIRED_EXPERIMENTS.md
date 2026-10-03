# Retired compiler experiments and instrumentation

[The second cleanup](RETIRED_EXPERIMENTS_ROUND2.md) supersedes the retained-policy
list below: policies 8 and 18 and seven other experiment groups were removed.

The cleanup removes implementations and configuration plumbing that were disabled
or rejected, while preserving the adopted browser execution path. It is not a
new speedup claim. Historical experiment reports and measurements remain in Git.

Removed:

1. ROM module dispatch, state-sharing cohorts, and dynamic Thumb cohorts.
2. The alternate region IR backend: policies 1–3 and 9–16, graph lowering,
   segment/fallback metadata, and its build gates.
3. First-use ROM/RAM compilation and bounded ROM cache recycling. The sampled
   hot-code compiler remains.
4. Write versions, mutation epochs, pointer-escape tracking, and watched-code
   page write protection, including their disabled write hooks.
5. Guard-publication omission, verifier-check omission, the quiet-loop experiment
   selector, alternate lookup layout, and partial unsafe modes 1 and 2.
6. The standalone, unused PC histogram.
7. The legacy `EKA2L1_DYNCOM_PROFILE` opcode/pair/PC collector.

The original ARM emitter retains memory-span and budget proofs, natural-loop
budget checks, register caching, ordinary leaf inlining, and policy 18's opt-in
instruction-count batching. Policies 0, 4–8, 17, and 18 keep their numeric IDs;
`-1` remains the configured baseline. The browser default remains policy 17,
Thumb direct memory 1, and cache specialization 2.

`EKA2L1_UNSAFE_CODE=3` remains the WASM default. It trusts executable bytes.
Mode 0 retains exact primary/dependency-byte comparisons and code-write guards.
Both modes retain address-space, mapping-generation, backing, extent, lifetime,
and explicit invalidation checks. The cached block version used to attach a
compiled module is separate from the removed memory-write version tracker.

`EKA2L1_HOTPATH` now accepts only 0 (general cache lookup) and 2 (trusted-cache
specialization). Normal execution selects the quiet outer loop automatically;
opted-in diagnostics select the instrumented loop. Verification still prevents
compiled dispatch during its interpreter reference execution.

The browser launcher and replay/profile runners reject retired environment
options, including zero-valued settings. Remove `EKA2L1_ROM_DISPATCH`,
`EKA2L1_SYNCHRONOUS_COMPILATION`, `EKA2L1_CODE_WRITE_PROTECT`,
`EKA2L1_CODE_LOOKUP`, and `EKA2L1_OMIT_GUARD_PUBLICATION` from old commands.
Retired compiler policy IDs are rejected rather than mapped to a different
implementation. Old experiment commands require their original Git revision.

Chrome/V8 profiling, process-aware guest attribution, the verifier, crash
history, exit census, and benchmark work totals/measurement boundaries remain.
Correctness fixtures for mapping changes, code mutation, faults, short budgets,
callbacks, and interpreter equivalence also remain. Alternate-backend-only
selection tests are removed; useful instruction sequences now exercise the
surviving emitter.

## Verification

Validated on 2026-10-03, after the policy 18 commits through `9bcf5f5a9`:

- Native CPU CTest passed. The full WASM suite reported 175 passed, zero failed;
  its existing `crash-repro0x8046506E` expected failure is a harness limitation.
  A diagnostics-enabled build additionally passed verifier protection, guard
  publication, boundary details, exit census, batched counts, batched boundaries,
  and batched inline branches.
- All 4,480 native/WASM fault cases matched under policies 17 and 18, including
  entry budgets, deferred snapshots, remapping, write/remap interactions, wide
  snapshots, and region block spans. Registers, counts, callback-visible state,
  and exact memory matched.
- Snakes and Sky Force each matched their native-matched reference replay for
  60 unique frames, guest records, PCM, and audio events.
- Browser API tests verified retired exports are absent, retired policy IDs are
  rejected, and surviving settings can be selected and read back. Launcher
  policy tests and Chrome profiler mapping tests passed.
- A real Chrome profile with custom diagnostics disabled captured 33 isolates;
  the guest worker had 2,662 samples and 357 guest-entry-labeled frames.
- Both games passed on `https://claude-laptop.lan:8188/` with hardware Vulkan,
  game selection, keyboard input, advancing guest time and frames, and no fatal
  page errors. The served runtime hash and default policy 17 were checked.

The diagnostics-free gameplay build is 10,824,416 bytes, with WASM SHA-256
`302a526343de23c3153db7c46e0e42849c6c55c3e98fc28a6654bb2be69c4991`.
Local raw evidence, replay comparisons, diagnostic results, profile, screenshots,
and frozen build manifests are under
`/home/claude/.scratch/eka-retire-experiments/`. These are correctness and
profiling-path checks; no before/after timing campaign was run for this cleanup.
