# Compiler configuration cleanup

All five requested cleanup groups are implemented against `334245f58` on
`wasm-port`. This is maintenance, with no new speedup claim.

| # | Area | Current behavior |
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

## Verification

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
