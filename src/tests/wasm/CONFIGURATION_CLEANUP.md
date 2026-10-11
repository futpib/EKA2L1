# Compiler configuration cleanup

## Current retirement (2026-10-11)

Implemented the nine requested retirements against `d53162c23` on `wasm-port`.
This removes selectable variants and stale plumbing; no speedup is claimed.

| # | Removed | Retained behavior |
| ---: | --- | --- |
| 1 | Browser verifier, AOT diagnostic, detailed-profile, guest-profile and exit-census configuration | Chrome sampling/native attribution, coarse progress, compilation/fault metadata and native correctness diagnostics. |
| 2 | Predicated-leaf toggle and feature mask | Predicated leaves and branch/tail/literal veneers are fixed on (former mask 224). |
| 3 | Compiler policies 4–6 and numeric policy selector | Former policy-7 lowering; private precise form remains for correctness fixtures. |
| 4 | Compiled-syscall, sparse-ROM, Thumb-memory and trusted-lookup off switches | Adopted implementations plus required interpreter, memory and guarded-lookup fallbacks. |
| 5 | Byte comparator variant 2 and its selection API | Original comparator with exhaustive unaligned/tail/mutation tests. |
| 6 | Compiled LDREX/STREX experiment and helper import | Interpreter exclusive accesses and the existing reservation monitor. |
| 7 | `EKA2L1_WASM_DEFER_MEMORY=OFF` build variant | Runtime ARM region compilation always defers eligible memory misses; precise test emission remains. |
| 8 | Internal `AOT_HOT`, `AOT_REGION`, `AOT_CHAIN`, `AOT_RAM` environment stages | One pre-init compiler/interpreter choice; compiled mode includes ROM, RAM, regions and chaining. |
| 9 | Fixed-value `executionLimits` policy plumbing and validation | Constants 512 source bytes, 32 leaf instructions, 8 inline sites, no chain cap; read-only report remains. |

The browser configuration API now takes one compiler-mode argument, 0 or 5.
The normal launcher configures only watchdog timing, executable-byte policy and
memory implementation. Retired environment names are rejected, including
explicit values equal to their former defaults.

Mutation-compatible executable-byte mode 0 and TLB memory mode 0 remain
supported. Default compiled play remains unsafe-code mode 3, direct memory mode
2 and a 2,000 microsecond watchdog. These were outside the requested removals.

The compiler suite passes 183 tests with zero failures (the pre-existing
crash-repro XFAIL and production-disabled diagnostic tests remain). Eager
precompilation and late-load/capacity regressions pass. Both memory modes pass
46 independent ARM/Thumb state and memory comparisons plus process-switch,
alias, permission, callback and observer-lifetime checks. The latter standalone
fixture needed its old equal-instruction-count shortcut corrected: generated
functions now return progress, so equality is compared only at the same guest
exit, and each backend is still checked independently against the interpreter.
The exclusive monitor passes 254,407 reservation/cross-thread-clear checks.

Native compiler/fault probes build, and the native Thumb exchange oracle runs
2,816 cases. Browser API, launcher policy, Chrome sampled profiling and frame
capture checks pass. A separate interpreter capture uses unsafe-code mode 0
and TLB mode 0 and passes frame/PCM validation. Its initial two-frame boot-only
attempt correctly failed the gameplay-audio gate; the later capture starts after
42 guest seconds. Historical campaign drivers require matching source/harness
revisions; passing an old binary to the current harness does not restore removed
APIs. The experiment index has been regenerated, also discovering four previously
unindexed reports already present in the repository.

Fresh hardware-accelerated Chrome sessions through the normal `.lan` picker
pass gameplay/input checks for all five games: Snakes, Sky Force, Asphalt 2,
K-Rally and full Hooked On. Reviewed screenshots show play; Hooked On completes
the N-Gage installation and boat movement. Snakes and Sky Force present about
16 and 32 frames/s at 1.00 guest second per host second during ten-second smoke
windows. These are functional checks, not controlled performance comparisons.
Sound is muted in the launcher checks.

The deployed WASM SHA-256 is
`7e8d4ef829fc1d3f84d266941f105e8dcf64be68b6f504298b1dc27c7da1fd26`;
the downloaded LAN artifact matches. The service override also removes its two
retired leaf environment settings. See [compact evidence and local capture
paths](../benchmark/CONFIGURATION_RETIREMENT_RESULTS.json).

## Earlier cleanup against 334245f58

All five requested cleanup groups are implemented against `334245f58` on
`wasm-port`. This is maintenance, with no new speedup claim.

| # | Area | Behavior at that revision |
|---|---|---|
| 1 | Duplicate compiler policies | Policy 7 is canonical. The enum, lowering alias and active parsers no longer accept 17. Tests exercise the same lowering through 7. Historical reports retain original policy IDs. |
| 2 | AOT stages | The frontend and active capture/profile launchers accept only 0 (interpreter reference) or 5 (full compiler). Intermediate stages 1–4 are rejected. Internal compiler/fallback paths remain available to focused tests. |
| 3 | Graduated optimizations | Normal launch fixes compiled syscalls, sparse ROM lookup, Thumb inline memory and trusted lookup to their adopted defaults. Their environment switches are rejected by the normal launcher; explicit test overrides and the benchmark/profile APIs retain controls. Required general lookup, memory and syscall fallbacks remain. |
| 4 | Count-based exit diagnostics | Removed the uncalled recorder, budget classifier, obsolete test and recorder-owned counters/report fields. Compilation sites, invalidations, memory/fault metadata and leaf probes remain. |
| 5 | Historical tooling | Archived 13 counted browser-kernel harnesses and three drivers for archived instruction-bounded fault probes. Current generic WASM-cost tools and native fault probes remain active. The generated experiment index now marks outlined entry budgets as removed. |

Normal launch environments must remove `EKA2L1_COMPILED_SVC`,
`EKA2L1_SPARSE_ROM_LOOKUP`, `EKA2L1_THUMB_MEMORY` and `EKA2L1_HOTPATH`, including
explicit settings equal to their defaults. Set IR policy 7 or omit the override.
Dedicated `benchmark.ts` and `profile.ts` controls remain explicit and do not
inherit a normal-launch configuration through their helper HTTP server.

The [browser archive](archive/counted-kernels/README.md) and
[fault-matrix archive](../benchmark/archive/counted-kernels/README.md) describe the historical
ABI and matching artifact requirements. Original measurements are preserved.

### Verification at that revision

The [compact evidence](../benchmark/CONFIGURATION_CLEANUP_RESULTS.json) records the exact
WASM hash, browser policies, measurements and local capture directories.

- WASM frontend and compiler-test builds pass. The compiler suite passes
  182 tests with zero failures; the removed test exercised the obsolete
  count-based classifier. Both reference and policy-7 state/fault tests remain.
- Native CPU tests pass 497 assertions in 33 cases. Native compiler and fault
  probes build successfully.
- Real browser frontend API checks reject 17 and AOT 1–4, accept 7 and AOT 0/5,
  and retain pre-init targeted controls. Launcher policy, asset-cache, Chrome
  profiler and generic offline WASM-cost tests pass.
- A native diagnostic serialization check preserves compilation-site, probe
  and invalidation records and confirms the removed exit/count fields are absent.
- Fresh Chrome sessions through the real game picker pass gameplay and keyboard
  input checks for both games. Reviewed start/end scenes show gameplay.
  Snakes presents 15.95 frames/s and Sky Force 31.99 frames/s over ten-second
  paced windows, both at approximately 1.00 guest second per host second and
  zero instruction totals. Sound is muted. These are smoke checks, not a
  performance comparison; other validation work overlapped.
- The interpreter reference with all four targeted optimization switches off
  captures 20 menu frames and passes PCM validation. This proves reference
  execution and control plumbing, not gameplay. An earlier one-frame boot
  capture failed the gameplay-audio validator because it stopped too early;
  that attempt is retained separately.
- Archived scripts pass syntax/import checks. Historical artifacts were not
  rerun. The generated experiment index passes its freshness check.

The browser checks use a temporary loopback server and the rebuilt
`build-wasm` bundle. The existing LAN artifact is unchanged.
