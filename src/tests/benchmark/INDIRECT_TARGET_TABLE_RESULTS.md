# Constant-time indirect-call target selection

The eight-target cap in `a290e3a68` is an implementation bound, not a measured
optimum. A compiler-built perfect hash and WASM `br_table` can select arbitrarily
many discovered candidates in constant runtime work. The prototype removes
the indirect-call target and callsite caps, reaches all twelve literal-hinted
Sky Force tile functions, and passes tests with 96 targets. The existing source
window and callee eligibility rules still apply; this does not discover every
function in a game or remove browser/code-size limits.

**Decision: archive, with no runtime/default/LAN change.** Constant-time
selection works, but the tested layouts do not establish a net runtime win.
The retained changes extend the captured-renderer fixtures to additive and
subtractive blending and fix the browser microbenchmark's tiering warmup.

## Lookup and actual native code

The primary prototype preserves one initial direct comparison. All remaining
known targets have unique slots in a multiply/shift hash selected at compile
time. Unknown addresses need one exact-address check after the jump:

```text
if target == first_candidate:
    execute inlined body with shared caller locals
else:
    slot = uint32(target * constant_multiplier) >> constant_shift
    jump_table[slot]:
        if target != this_case_address: ordinary_BLX_exit()
        execute inlined body with shared caller locals
continue caller after a successful matching return
```

There is no runtime search, collision probing, WASM function call, or JS
crossing on a successful selection. Table construction and emitted code size
grow with the candidate set. Callee memory helpers and exceptional exits retain
their original behavior.

A JIT dump from ordinary Chrome tiering confirms the actual optimized root
contains `imul`, `shr`, an indexed native jump through `[r10+r15*8]`, and the
exact target comparison. This is not inferred from unexecuted forced-TurboFan
code. Source metadata classifies selection as dispatch work.

## Corrected fixed-work measurements

Each row renders 100,000 captured tile rows with sixteen tiles per row through
the real compiler, direct-memory implementation and compiled runner. The main
caller is `0x70020df8`; the three callee bodies are `0x700032bc`, `0x70003bc4`
and `0x70003cb4`. Pixel outputs are checked. The blending fixtures repeatedly
accumulate/clamp the same destination; these are controlled renderer workloads,
not complete gameplay frames. Their registry uses the ROM lookup path while
the live game uses RAM lookup.

The control is the existing eight-target fusion, not fusion disabled. Positive
CPU throughput is faster; negative retired instructions means less native work.

| # | Candidate layout / workload | CPU throughput | Native instructions | Faster adjacent pairs |
| ---: | --- | ---: | ---: | ---: |
| 1 | One fast case + table / main tile | -4.32% | +0.002% | 0/4 |
| 2 | One fast case + table / additive | +3.57% | -1.06% | 4/4 |
| 3 | One fast case + table / subtractive | +3.84% | +1.11% | 4/4 |
| 4 | Eight fast cases + table / main tile | +0.51% | -0.06% | 3/4 |
| 5 | Eight fast cases + table / additive | -1.12% | -0.97% | 1/4 |
| 6 | Eight fast cases + table / subtractive | +1.74% | +0.70% | 4/4 |

The second layout still accepts all twelve targets: eight is a fixed comparison
prefix, not a target limit, and the remaining candidates use the table. Its
worst-case selection work remains bounded independently of target count.

Chrome 153 / V8 15.3, ABBA/BAAB, requested 2.4 GHz, isolated CPU 7 and sibling
15. Counters ran for 100% of each measured interval and passed the clock checks.
All rows are retained, including a +10.03% first subtractive pair and a -4.01%
last main-tile pair; the means should not imply false precision. The native
function grows from 236,352 bytes in the control to about 330 KB. Register
allocation and code layout change, but their individual contributions to the
regressions have not been established.

Earlier pure-table, in-table fast-case, and twelve-target linear experiments
had insufficient warmup: JIT timestamps show root optimization overlapping
their first timed rows. Those runs are preserved but excluded from the final
comparison. The driver now runs two full untimed workloads per variant and
checks that root TurboFan installations precede the first measurement.
Debugger attachment remains after timing. Both roots pass that check in every
final comparison. One postprocessing run initially rejected an incomplete
unrelated JIT tail; offline verification recovered the complete root records
without rerunning or removing timing rows. The parser now records such tails.

The retained driver's original single-build off/on mode also passes an actual
browser run. With verified warm tiering, existing eight-target fusion improves
the main renderer by 36.57% CPU throughput and removes 15.99% of retired
instructions versus fusion disabled. That comparison confirms the earlier
retained optimization; it is not a gain from the target-table candidate.

No whole-game speedup is claimed, and no new gameplay timing/deployment was
performed after the fixed-work results failed to establish a worthwhile default.

## Verification and reproduction

The primary candidate passes all 182 compiler tests, including 2,880
state/memory/callback comparisons, 240 independent interpreter comparisons,
and 438 exact-target/fallback cases across dense, sparse and wrapping address
sets of 2, 12 and 96 targets. Both lookup layouts pass the focused suite.
All benchmark host-control invocations restored their settings.

Apply `INDIRECT_TARGET_TABLE_EXPERIMENT.patch` to the retained source for the
primary candidate. Apply `INDIRECT_TARGET_PREFIX_EXPERIMENT.patch` afterward
for the fixed eight-case prefix variant. Build `test_aot_wasm` and
`eka2l1_wasm`; run the full test JS and `--stack-returns-only` checks. Freeze
separate control/candidate test directories before comparing them:

```sh
sudo -n systemd-run --quiet --scope --slice=ekabench.slice \
  python3 src/tests/benchmark/fixed_frequency.py --khz 2400000 \
  --state "$task_root/host.json" --platform-profile performance \
  --isolate-cpus 7,15 -- \
  node src/tests/benchmark/stack_returns/micro.mjs \
  "$candidate_test_build" "$task_root/tile" 7 2400 2304 \
  tile_row_benchmark 100000 "$control_test_build"
```

Repeat with `tile_add_row_benchmark` and `tile_sub_row_benchmark`, fresh output
paths, and the appropriate CPU topology/reference frequency for the host.
The [JSON report](INDIRECT_TARGET_TABLE_RESULTS.json) retains every final row,
artifact hashes, JIT timestamps, and host-restoration outcomes. Raw artifacts,
all attempted layouts, and disassembly are under
`/home/claude/.scratch/eka-indirect-table-20261010`.
