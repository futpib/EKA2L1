# Direct-memory entry-span extension

Not adopted. The extension passes correctness checks, but Snakes shows no
repeatable CPU improvement and Sky Force uses 2.85% more worker CPU, slower in
both pairs. The production compiler is restored. The complete implementation
and tests remain in the evidence file's `source_patch`; no runtime option or
LAN deployment is added.

The [three-access follow-up](DIRECT_SPAN_THREE_RESULTS.md) raises only the
selection threshold and repeats the comparison against production.

## Implementation

The candidate extends the existing ARM entry-span proof in direct mode:

- Two qualifying word accesses can select a proof; TLB keeps its four-access
  threshold and existing analysis.
- Straight-line code tracks entry-relative pointers through immediate ADD/SUB,
  unshifted register MOV and scalar pre/post-indexed writeback. It supports
  bounded, register-cached short blocks as well as connected regions.
- The direct arena can prove a span larger than 4 KiB, up to its 64 MiB size.
  Outside the arena, a proof must still fit in one fallback page-table entry.
- Grouped ordinary words use the direct backend's existing unaligned semantics.
  This does not change block-transfer alignment or ARM exception semantics.

For example, two post-indexed loads become conceptually:

```text
host = translate_complete_span(entry_r1, 8, read_permission)
if host == 0 or exit_pending:
    return original_compiled_block(cpu)

r0 = wasm_load32(host)
r1 += 4
r2 = wasm_load32(host + 4)
r1 += 4
```

The actual emitter retains instruction-budget, condition, interrupt and state
publication behavior. Span validity is checked at runtime. The compiler knows
the relationship between addresses, not the guest register's runtime value.
Backend selection is fixed when compiling; the arena mask belongs to the
current address space and is cached at entry. These accesses remain inside WASM.

The new analysis rejects unknown pointers, conditional changes that lose an
address relation, internal branches, moving loop bases, and possible helper
calls. Every memory operation in an affine-selected short block must be proved,
so a callback cannot invalidate a saved host pointer halfway through the block.
Existing invariant-base region proofs continue to handle eligible loops and
conditionals. Read and write permissions are grouped separately. No game, DLL,
guest address or particular loop is special-cased. Thumb's proof analysis is
unchanged. Direct mode retains its existing unsafe executable-write policy.

## Correctness

All checks passed on the candidate built against merged parent
`e7fd4ab289f623fac38e64d3366c8b332302bd77`:

- 1,470 new state and guard cases cover short blocks, regions, small budgets,
  conditional accesses, affine updates, page and arena boundaries, unaligned
  accesses, aliases, joins and loops. Ordinary executions compare registers,
  flags, memory and instruction progress with the interpreter and original
  compiler; wrapping-address fixtures check rejection before memory effects.
- Instrumented fallback bodies distinguish taking the proved body from merely
  getting the right answer through fallback. Valid multi-page arena cases must
  take the proof; multi-page fallback-table and wrapping-address cases must
  reject it. A separate high-PC case rejects a moving-base backedge when the
  code's exclusive end is 2^32.
- 816 existing ARM/Thumb memory comparisons, direct mapping publication/lifetime
  checks, 21,146 ARM short-block cases, cached callback state and 24,192
  instruction-budget cases pass.
- Snakes and Sky Force each match 60 native reference frames exactly, including
  pixels, instruction counts, guest timestamps, PCM and audio events. Native
  references are from the completed upstream merge. Replay rendering uses
  SwiftShader; CPU measurements use the hardware GPU.

The focused checks and real replays are the validation for this change; a new
full compiler-suite run is not claimed.
After the experiment, rebuilding the restored production source reproduces
all six frozen baseline browser artifacts byte-for-byte. The preserved patch
also passes `git apply --check` against the restored source.

## CPU measurements

Positive change means slower. Observations are worker CPU seconds; paired
changes compare the forward and reversed runs separately.

| # | Game | Baseline CPU observations | Candidate CPU observations | Mean CPU change | Pair changes |
|---|---|---|---|---:|---|
| 1 | Snakes | 1.790288, 1.648307 | 1.611574, 1.820212 | -0.20% | -9.98%, +10.43% |
| 2 | Sky Force combat | 8.418972, 8.634173 | 8.669751, 8.869245 | +2.85% | +2.98%, +2.72% |

Snakes' opposing pair results do not establish a speedup. Sky Force's small
regression repeats within this panel. This is enough to reject adoption of
the combined extension, not to establish that each individual change is slow.
The extension removes repeated translations but adds entry guards and outlined
fallback bodies to more functions. These are plausible costs; this campaign
does not isolate their contribution or establish a regression cause.

Two observations per build/game, baseline-candidate-candidate-baseline order.
Each uses a fresh browser, shared audio, normal tiering and the direct backend.
Sampling, tracing and custom diagnostics are off. No owned build, test or
correctness replay overlaps timing. All completed observations are retained.

Snakes executes 644,728,231 guest instructions in guest time 21-25 seconds;
Sky Force executes 2,171,043,925 in guest time 42.000001-48 seconds. The driver
checks equal instruction endpoints and identical presentation journals between
builds. The primary metric is DedicatedWorker scheduler CPU seconds. This
excludes descheduling but not frequency, cache, GC or JIT variation on the
shared i7-10875H host. Two observations per build are a directional screen.

## Reproduction

[Evidence](DIRECT_SPAN_RESULTS.json) retains the source patch against the parent
above, all observations, exact commands, artifact hashes, native-reference
hashes and compiler-test output. Freeze the parent's browser artifacts, apply
the evidence file's `source_patch`, rebuild `eka2l1_wasm` and `test_aot_wasm`, and
freeze the candidate separately. Run the four focused selectors recorded in
the evidence and the native replays before timing.

The evidence's `timing_driver` contains the full paired campaign. Each invocation
uses `memory_implementations.py timings --modes 2 --rounds 1` with an explicit
4,000,000-us Snakes or 6,000,000-us Sky Force window, then verifies equal guest
work and presentation journals across the frozen builds. Raw local artifacts
are under `/home/claude/.scratch/eka-direct-span-extension/merged/`.
