# Native instruction samples of compiled-region handoffs

The largest sampled handoff costs are V8's indirect-call sequence and successor lookup. Progress, stop, interrupt and watchdog checks are smaller. Two local rewrites were tested; neither earns adoption. Both prototypes are archived and removed, and the existing LAN artifact remains served. There is no new runtime speedup claim.

This investigation uses source `0e354cd12`, after browser instruction accounting was removed. `NumInstrsToExecute` in this runner is now a stop latch, not a per-instruction countdown. Earlier reports with instruction budgets describe different runtime code.

## What actually executes

Samples come from normally warmed gameplay, with Chrome 153.0.8010.52 / V8 15.3.76.13 and natural tiering. Linux `task-clock:u` samples at a 1 ms CPU period were matched to the particular JIT code version and its native bytes. Both source-map options were enabled in the Release build. Debugger/module capture happened after native sampling. No guest instruction counters, CDP CPU sampler or forced optimizing tier were used.

Snakes has 5,362 guest-worker samples, of which 1,563 (29.15%) fall in `execute_chain`. Sky Force has 3,394, of which 506 (14.91%) fall there. These are new windows and a different sampling method from the earlier 33%/15% CDP figures; the difference is not a speedup. Both accepted captures show gameplay at their start and end. An initial Sky Force language-menu capture is retained but excluded.

The following disjoint ranges were audited in the native disassembly and anchored to WASM/C++ where metadata exists. Percentages use **all sampled guest-worker CPU time**, not just runner time. Offsets are half-open ranges within the corresponding 4,480-byte TurboFan runner.

| # | Native sequence | Offsets | Snakes samples / share | Sky Force samples / share |
|---:|---|---|---:|---:|
| 1 | V8 loop poll and reloads | `d00–d16` | 71 / 1.32% | 13 / 0.38% |
| 2 | Clear exit flag | `d16–d22` | 0 / 0.00% | 1 / 0.03% |
| 3 | V8 indirect call | `d22–d88` | 608 / **11.34%** | 218 / **6.42%** |
| 4 | Progress, stop and return reloads | `d88–daf` | 104 / 1.94% | 20 / 0.59% |
| 5 | IRQ check | `daf–dde` | 37 / 0.69% | 9 / 0.27% |
| 6 | Watchdog check | `dde–e05` | 51 / 0.95% | 24 / 0.71% |
| 7 | Align PC and attach ARM/Thumb tag | `e05–e32` | 96 / 1.79% | 26 / 0.77% |
| 8 | Classify ROM/RAM address | `e32–e5f` | 46 / 0.86% | 13 / 0.38% |
| 9 | ROM lookup, including cold registry paths | `e5f–f24` | 62 / 1.16% | 84 / 2.47% |
| 10 | RAM lookup hit path | `f24–fd2` | 454 / **8.47%** | 86 / **2.53%** |
| 11 | RAM miss call site | `fd2–ff5` | 2 / 0.04% | 1 / 0.03% |
| 12 | RAM backedge | `ff5–ffd` | 30 / 0.56% | 2 / 0.06% |

Entry, exit and other cold instructions account for the small remaining runner share. The out-of-line RAM lookup has another 83 Snakes samples (1.55%) and 25 Sky Force samples (0.74%), outside the runner totals.

The indirect call is a **WASM-to-WASM** handoff. It checks the V8 dispatch table and type, loads the target and callee instance, decodes a code-pointer handle, checks its signature and calls the native target. There is no JavaScript call in this sequence. It is substantially more machinery than a native C++ function-pointer call.

The inlined RAM hit does this on every lookup:

```text
core = cpu.parent
source = core.mapping_generation_source
generation = source ? atomic_load_acquire(*source) : 0
key = (core.address_space_id << 32) | tagged_pc
slot = dispatch[hash(key)]
if generation != 0 and slot.key == key
   and slot.generation == generation and slot.source == source
   and slot.function != null:
    return slot.function
return lookup_uncached(core, tagged_pc)
```

That is several dependent loads before a function can be called. ASID, generation and source identity protect against remapping and address-space changes; they are not instruction-byte scans. Hoisting them across arbitrary compiled regions needs a contract that excludes relevant helper/syscall/mapping changes.

V8 also leaves duplicate native memory reads where the emitted WASM has one load. For example, the stop test contains:

```asm
d98: mov r9, [rbx+r8+0x348]
da0: cmp qword ptr [rbx+r8+0x348], 0
```

The first result is overwritten later. Similar load-plus-memory-compare pairs appear in the RAM identity checks. This is an observed code-generation opportunity, not a measured removable-time estimate.

Samples often land after dependent loads: the branch at `d81` gets 242/93 samples, and a constant move at `d43` gets 166/70. Neither establishes that the branch mispredicts or the constant move is intrinsically expensive. Sampling skid and dependency stalls prevent assigning those counts directly to isolated instruction latency. Zero samples on the exit-flag store likewise do not prove that store is free or removable.

## Experiments and disposition

| # | Candidate | Actual warmed native result | Disposition |
|---:|---|---|---|
| 1 | Combine RAM key/generation/source comparisons using XOR/OR | Successful identity sequence remains 14 instructions; four conditional branches become two, but two ORs are added. Duplicate reads remain. Runner shrinks 4,480 → 4,416 bytes. | Removed at native instruction-count screen; no timing claim. [Patch](HANDOFF_COMBINED_IDENTITY_EXPERIMENT.patch). |
| 2 | Form PC alignment mask as `~3u \| (TFlag << 1)` | Alignment plus mode tag falls from nine to seven instructions. Runner shrinks 4,480 → 4,416 bytes. | Short timing screen is inconclusive; removed. [Patch and focused tests](HANDOFF_PC_MASK_EXPERIMENT.patch). |

The second candidate relies on architectural `TFlag` being 0/1, as the current CPU writers establish. Sixteen focused ARM/Thumb low-bit successor tests through the actual runner pass. Existing 56 runner stop/IRQ/policy/mutation cases and seven inline comparisons pass. The first candidate additionally passes 12,288 frozen-cache identity/remap/invalidation comparisons and the existing dispatch attachment/collision/reset checks.

### Short timing screen, not adoption evidence

Both source-mapped artifacts ran without native sampling. Each game used fresh-process ABBA order on CPU 7, with sibling 15 reserved and other work confined to support CPUs. The requested frequency was 3.6 GHz; accepted hardware-counter intervals measured about 3.591 GHz and passed the predeclared clock, throttle, affinity and counter checks. No temperature gates or charging changes were used. Both fixed-frequency sessions restored their host settings without errors.

| # | Game | Mean worker CPU seconds, control → candidate | Raw CPU throughput delta | Pair deltas | Work-equivalence limitation |
|---:|---|---:|---:|---|---|
| 1 | Snakes | 0.807959 → 0.818505 | -1.29% | -1.44%, -1.14% | 65 frames per control versus 65/66 per candidate; matching-index images differ substantially. PNG compression was included. |
| 2 | Sky Force | 0.741033 → 0.739120 | +0.26% | -0.75%, +1.28% | All 128 frames, but the final control reaches 4% stage progress versus 2% in the other runs. Mode 1 excludes PNG compression and retains no pixel hashes. |

These deltas are descriptive, **not established gains or regressions**. Equal guest-clock duration and presentation counts do not establish equal gameplay work under the watchdog-only scheduler. Snakes' same-index images differ at roughly 42–48% of pixels on average (channel difference greater than eight); no compared frame pair is exact. Sky Force's endpoint HUD also exposes different work. A repeatable gameplay-progress window is needed before making subpercent adoption claims on this runtime.

The first Sky Force attempt used the old input route, reached the title menu and failed the worker-dominance check. It is retained as rejected; this was not a clock failure or evidence against the optimization. The corrected [watchdog-only route](watchdog-sky-force-countfree.input), sampled at 58–62 guest seconds, reaches combat in all four screen observations. Its screenshots still require review; the route does not guarantee identical progress. The browser launcher dialog overlays the harness endpoint screenshot because the harness starts the emulator through its exports; visible gameplay/HUD and separate raw Snakes frames were inspected.

## What the evidence supports next

Progress/stop/reload, IRQ and watchdog ranges together account for 3.58% of Snakes and 1.56% of Sky Force samples. The V8 call and inlined RAM hit alone account for 19.81% and 8.96%. This prioritizes eliminating complete handoffs or shortening the dependent lookup/call sequence over blindly deleting individual safety checks. It does not promise recovery of those percentages: guards, code layout, memory stalls and the resulting V8 code still need measurement. Further fusion must be assessed against the fusion already adopted in this baseline.

## Reproduction and retained evidence

The [machine-readable report](HANDOFF_NATIVE_PROFILE_RESULTS.json) includes code identities, native hashes, hot instructions, exact source anchors where available, range counts, timing observations, scene audits and host restoration. Raw captures, disassembly, source maps, build snapshots, drivers and logs are under `/home/claude/.scratch/eka-handoff-native-20261010`.

The accepted runner versions are Snakes PID 2362354 / code 336581 and Sky Force PID 2425224 / code 335156. All 160 selected versions in each capture were recovered, with no sampler errors or losses. Source positions remain sparse: Snakes has 316 samples mapped to C++, 700 to ARM, 3,540 without instruction positions, 504 in JIT code without a selected snapshot and 302 outside JIT; Sky Force has 123/287/2,713/124/147 respectively. Manual native-range classification covers the runner arithmetic between exact source anchors without pretending those instructions have individual source labels.

The diagnostic captures were not isolated timing runs: other host activity and some capture/postprocessing overlap occurred. The live driver is retained as `paced-native.ts` with hashes in the JSON. Its post-sampling module selection uses worker 13 for these verified captures and must be rechecked for another build; it is not a generic worker-discovery implementation. Use the existing [native profiling workflow](CHROME_PROFILING.md#native-samples-with-arm-and-c-source-attribution) and `native_attribution.py` to reproduce the mapping. The native browser fixture, two native-profile JavaScript tests and four Python attribution tests passed before collection.

Both runtime prototypes and their test hooks were removed after archiving. The baseline WASM and focused test target were rebuilt successfully with source metadata retained for inspection. The production LAN service stayed active on its preceding artifact throughout.
