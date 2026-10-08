# Cleanup after the completed controlled sweep

Audited the 56 rejected or inconclusive entries against `wasm-port` at
`ac38ea361`. Four compiler paths and adjustable limits covering six additional
entries still existed. They are now removed. The other 46 entries had already
been removed, restored out after experiments, or superseded by adopted code.
The 56 rows comprise 53 experiment families: connected callees had four variants.

This is source/configuration cleanup, not a measured runtime speedup. Historical
reports, observations, candidate patches and archive comparison tools remain.
Commands targeting removed options require their original revision and harness.
In particular, archive-log comparison can still read old numeric policy markers;
it cannot enable those policies in the current emulator.

| # | Listed experiment | Source disposition | Retained behavior / removal |
| ---: | --- | --- | --- |
| 1 | Division-digit lowering | Removed now | Deleted the recognizer, division emission, selection counters and API/env options. |
| 2 | Trusted lookup inline variant | Already absent | The separate experiment is gone; adopted compact trusted dispatch remains. |
| 3 | Entry-only state pruning | Removed now | ARM and Thumb always run full state pruning; deleted the selector/API. |
| 4 | Store-only state pruning | Already absent | Full load/store pruning remains. |
| 5 | Blanket direct span, two accesses | Already absent | Only the retained cost-gated span lowering remains. |
| 6 | Blanket direct span, three accesses | Already absent | The rejected threshold-only policy is gone. |
| 7 | Thumb static connected regions | Already absent | Ordinary Thumb blocks and retained memory lowering remain. |
| 8 | Thumb transfer gate variant | Already absent | Retained transfer lowering remains; no experiment selector. |
| 9 | Batched instruction counts | Removed now | Deleted IR 18 and deferred count offsets/commits; retained IR 17 precise counters. |
| 10 | Span page reuse | Already absent | The experimental page-reuse path is gone. |
| 11 | Dynamic ROM cohorts | Already absent | No cohort dispatcher or grouping policy. |
| 12 | Compiled memory misses | Already absent | Existing helper/interpreter fault handling remains. |
| 13 | RAM first-use compilation | Already absent | Sampled hot-code compilation remains. |
| 14 | ROM first-use compilation/recycling | Already absent | No first-use/recycling experiment. |
| 15 | ROM module dispatch | Already absent | Ordinary region dispatch remains. |
| 16 | ROM state cohorts | Already absent | No shared cohort state ABI. |
| 17 | Mixed IR backend | Already absent | Alternate IR backend/policies were deleted. |
| 18 | Longer IR segments | Already absent | No alternate segment compiler. |
| 19 | IR stack values | Already absent | No alternate stack-value policy. |
| 20 | Conditional-loop budget variant | Already absent | Adopted natural-loop budget proofs in IR 17 remain. |
| 21 | Lazy-flags IR variant | Already absent | The alternate IR flag policy is gone; ordinary flag emission remains. |
| 22 | Incoming-register definitions | Already absent | No alternate incoming-definition policy. |
| 23 | Adjacent wide-result reuse | Already absent | Distinct later lazy i64 values with exact exit snapshots remain; see distinction below. |
| 24 | Direct register stores | Already absent | No separate store-through policy; adopted state pruning remains. |
| 25 | Conditional ALU select | Already absent | No rejected select-lowering policy. |
| 26 | Compare-operand reuse | Already absent | No alternate compare-reuse policy. |
| 27 | Whole-entry budget with inline recovery | Removed now | Removed mode 1. Adopted outlined recovery 2 and precise reference 0 remain. |
| 28 | Cached address displacement | Already absent | Alternate addressing policy is gone. |
| 29 | Deferred read exit variant | Already absent | Alternate IR variant is gone; retained precise deferred-memory recovery remains. |
| 30 | Owner-core reuse | Already absent | Rejected adaptation was restored out after timing. |
| 31 | Aligned cache hash | Already absent | Original TLB index remains. |
| 32 | Connected callees: narrow bound 16 | Already absent | Rejected source restored; forward-only returning leaves remain. |
| 33 | Connected callees: full bound 64 | Already absent | Rejected source restored. |
| 34 | Connected callees: budget proofs, bound 64 | Already absent | Rejected source restored. |
| 35 | Connected callees: budget proofs, bound 16 | Already absent | Rejected source restored. |
| 36 | Guarded successor lookup | Already absent | No separate successor cache/path. |
| 37 | Call-prefix fusion | Already absent | Register-only tail prefixes are a different adopted implementation. |
| 38 | Preserve inner leaves | Already absent | No rejected outer/inner-call selection path. |
| 39 | Conditional-leaf bound tuning | Selector removed now | Fixed adopted bound 32, including graduated expanded forward-only leaves. |
| 40 | Larger hot-source window | Selector removed now | Fixed 512 bytes. |
| 41 | Four inline sites | Selector removed now | Fixed eight sites. |
| 42 | Sixteen inline sites | Selector removed now | Fixed eight sites. |
| 43 | Shorter region chain | Selector removed now | Fixed 512-region chain cap. |
| 44 | Uncapped region chain | Selector removed now | Removed unlimited-chain condition; fixed 512-region cap. |
| 45 | Eager ROM regions | Already absent | No eager-regions selector. |
| 46 | Special ROM leaf fusion | Already absent | Ordinary compiled ROM blocks remain. |
| 47 | Bounded ROM calls | Already absent | Rejected recovery patch was restored out. |
| 48 | Allocation-range backend | Already absent | Memory implementations are TLB 0 and direct 2 only. |
| 49 | Standalone full-page-table backend | Already absent | The table required by direct memory outside the arena remains. |
| 50 | Last-page-only cache | Already absent | No cache mode selector. |
| 51 | Folded TLB | Already absent | Original TLB index remains. |
| 52 | TLB plus last-page cache | Already absent | No combined cache path. |
| 53 | Folded TLB plus last-page cache | Already absent | Neither alternate cache path remains. |
| 54 | Older page-cache guards | Already absent | Current retained direct/TLB guards remain. |
| 55 | Page-cache span reuse | Already absent | No old page-cache span implementation. |
| 56 | Unaligned scalar TLB | Already absent | Rejected patch restored; TLB alignment guards remain. Direct scalar access is independently unaligned. |

The adjacent-only multiply reuse in [WIDE_REUSE_RESULTS.md](WIDE_REUSE_RESULTS.md)
is different from the retained [lazy i64 snapshot representation](LAZY_WIDE_RESULTS.md).
The latter delays splitting values and reconstructs exact register halves at
exits. Likewise, adopted forward-only returning leaves, branch/literal veneers,
register-only tail prefixes, direct-memory page-table fallback, and full state
pruning must not be deleted merely because a rejected experiment shares their
names or helper machinery.

## Current interface

- IR policies: `-1` (configured), `0`, `4`, `5`, `6`, `7`, `17`; default `17`.
- Entry budget: `0` (precise per-span reference) or `2` (outlined recovery);
  default `2`. Mode `1` is rejected.
- Execution limits are compile-time constants `512,32,8,512`: source bytes,
  leaf instructions, inline sites, regions per chain. The WASM readback remains;
  the configuration export is absent. CPU run budgets and scheduling are unchanged.
- `EKA2L1_DIVISION_DIGITS`, `EKA2L1_ENTRY_ONLY_PRUNING`, and
  `EKA2L1_EXECUTION_LIMITS` are rejected, including zero/default values.
  Removed configuration/report exports are absent. Programmatic launcher
  policies may assert the fixed limit tuple but cannot change it.
- TLB `0` and direct `2`, unsafe-code `0` and `3`, and all graduated defaults
  remain. Strict mode retains code-byte checks; both modes retain mapping checks.

Semantic fixtures remain for division instruction sequences, exact instruction
counts, callbacks, faults, aliases, interrupts and short budgets. Limit fixtures
now vary guest code lengths/site counts around the fixed constants instead of
changing the retired settings. No runtime counters were added.

## Verification

Raw artifacts are in `/home/claude/.scratch/eka-retire-sweep/`.
The final diagnostics-free WASM is 10,916,917 bytes, 3,122 bytes smaller than the
previously served artifact, with SHA-256
`c8d3a132468bf4b308a859f630084fbbc0b349d19e6e3fb27f0f33c7cdc23ca1`.
No timing comparison was run for this cleanup.

- Native CPU CTest passed; native and WASM compiler/fault targets built.
- The full WASM compiler suite passed 174 checks and exposed one incorrect
  assumption in the converted count fixture: IR 7 and IR 17 can return at
  different valid boundaries. The fixture now checks each differing count's
  registers, flags and memory against the independent interpreter; equal-count
  cases still compare the complete state, memory and helper counts. Its focused
  rerun passed all 34,560 cases. No runtime code changed after the other 174
  checks passed. Two existing diagnostic-only skips and one documented XFAIL
  remain; this is not a claim that the original full-suite log has zero failures.
- All 30,464 native/WASM fault comparisons matched in unsafe modes 0/3 and
  entry-budget modes 0/2: counts, registers, callback state and exact memory.
  All 41,472 independent native/WASM syscall comparisons also matched.
- Both games matched 60 unique reference images, guest frame records, PCM and
  audio events on the final frozen artifact. These deterministic replays use
  SwiftShader; hardware gameplay is checked separately below.
- Browser API, launcher-policy and experiment-index tests passed. Removed
  exports/options are rejected; retained defaults and fixed limits read back
  correctly. A real compiled profile-runner smoke check also passed with AOT 5,
  direct memory 2, entry budget 2 and IR 17 on the exact artifact.
- The diagnostics build was isolated for fault coverage counters, then the
  production build configuration was restored to diagnostics OFF. All six
  final runtime files match the frozen candidate manifest.

Initial failed attempts remain in the artifact directory: the diagnostics-free
fault probe could not satisfy its compiled-instruction coverage assertion;
rerunning the diagnostic probe resolved that harness requirement. The first
profile smoke command omitted AOT 5 and therefore selected interpreter/TLB 0;
the compiled smoke explicitly selects AOT 5 and checks the observed settings.
Both rounds of exact game replays are preserved; the second verifies the final
relinked production artifact. These are validation corrections, not discarded
timing observations.

The final artifact is served at `https://claude-laptop.lan:8188/`. The downloaded
WASM hash and both games' live compiler readbacks match the validated build.
The service removes retired variables, including the old execution-limit setting.
Real hardware-Vulkan game-picker checks passed game selection, keyboard/touch
input, advancing guest clocks and frames, and layout/manual-file controls.
Screenshots were inspected and show gameplay in both games; no fatal page errors
occurred. The previously reproduced host audio-device failure persists in both
non-silent-browser-audio checks, so the complete audio E2E result remains failed.
Exact PCM and audio-event replay equality passed independently.
