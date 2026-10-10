# Direct region tail chaining experiment

The prototype makes a small, fully linked ARM loop much faster, but does **not
establish a game-speed gain**. It is archived in
[the reproducible patch](DIRECT_CHAIN_EXPERIMENT.patch), with no active runtime
selector and no default or LAN deployment change.

## Game comparison

Stock is `ebb614bca` (runtime `0e354cd12`). The candidate links known successors
inside each generated WASM module with `return_call`. Both use ordinary Chrome
153.0.8010.52 / V8 15.3.76.13 tiering, direct memory, unsafe-code mode 3, a 2 ms
watchdog, shared audio, hardware graphics, and disabled custom diagnostics.

ABBA order per game; two fresh browsers per variant per game. CPU 7 is reserved
with sibling 15, support threads use the other cores, and the requested clock
is 3.6 GHz. All eight observations pass the previously declared frequency,
isolation, counter and throttling rules. Measured clock is approximately
3591.5–3591.6 MHz. There are no temperature gates or cooldowns. Startup,
translation and warmup are outside the measured windows.

| # | Measurement | Stock | Direct chains | Observed difference |
|---|---|---:|---:|---:|
| 1 | Snakes worker CPU, 42–46 guest seconds | 0.815033 s | 0.813490 s | +0.19% CPU throughput |
| 2 | Sky Force worker CPU, 58–62 guest seconds | 0.759246 s | 0.758847 s | +0.05% CPU throughput |
| 3 | Snakes retired native instructions | 6.6337 billion | 6.6254 billion | −0.13% |
| 4 | Sky Force retired native instructions | 7.5042 billion | 7.3666 billion | −1.83% |

These ratios are **not established speedups**. Snakes presents 67/66 frames in
the controls and 67/69 in the candidates; its endpoints have visibly different
floor positions. Sky Force presents 128/128 versus 128/127, reaches score 650
and stage progress 2% in every run, but enemy/projectile positions differ. Guest
time alone does not establish identical work under watchdog-only scheduling.
Both CPU comparisons have mixed pair directions. More repetitions of these
short, unequal-work windows would not establish a small causal effect.

All runs reach gameplay without an observed abort or WASM instantiation error.
This is a gameplay smoke check, not an exact replay correctness claim. The
first stock Snakes screenshot was partly covered by the launch dialog. The
harness now closes that dialog **after measurement**, so later screenshots are
reviewable; this change is retained independently of the experiment.

## Exact-work diagnostic

A separate Chrome test executes the **real C++ `execute_chain` runner**, using
the real translator and registry. It performs 10,000,000 iterations of a
four-ARM-instruction loop split across two generated regions, followed by a
two-instruction exit. Both variants run exactly 40,000,002 guest instructions
and check their result. This deliberately stresses tiny-region handoffs and is
not a game-speed estimate.

```text
A: r0 += 1; branch B
B: r2 -= 1; if r2 != 0: branch A; else: branch C
C: r3 += 7; return through lr
```

Stock returns through the runner between A and B. The candidate uses direct
WASM tail calls and polls the watchdog on the B→A cycle edge. Both retain the
original generated state loads/stores; this experiment does not fuse register
locals across regions.

After normal warmup, ABBA then BAAB supplies four measurements per variant.
The same reserved core and fixed-clock request apply, with actual frequency
verified from non-multiplexed hardware counters for each measurement. JIT
logging is enabled for this diagnostic; sampling and forced TurboFan are not.
Each timed invocation includes the same small register reset/registry setup.

| # | Measurement | Stock | Direct chains | Change |
|---|---|---:|---:|---:|
| 1 | Mean CPU time | 247.710 ms | 53.098 ms | **4.665× faster** |
| 2 | Mean native instructions | 2.13029 billion | 0.81021 billion | **−61.97%** |
| 3 | Native instructions per loop iteration, approximately | 213 | 81 | −132 |

All four candidate/control pairs are faster. This establishes the local cost
saving for a fully linked tiny-region workload, not its frequency in a game.

## Warmed Sky Force attribution

A separate ordinary-tiering combat capture from 58–78 guest seconds records
3,755 active guest-worker samples. The V8 metadata adapter matches 160 selected
code versions; ARM and C++ source maps both resolve. Instruction attribution
remains sparse: 321 samples have an ARM anchor and 184 a C++ anchor; 3,000 have
no exact instruction position, 139 have no retained code snapshot, and 111 are
outside the JIT. Those unknowns are not silently assigned to nearby source.

The six hottest generated functions have **no linked outgoing edge** in their
actual captured WASM bodies. Among unambiguously matched snapshots, functions
containing direct links contribute 211 samples: 5.62% of all worker samples,
or 9.44% of the 2,236 matched generated-code samples. This counts their entire
bodies, not executions of the links. Unsnapshotted functions remain unknown.
`execute_chain` still accounts for 547/3,755 samples (14.57%, including inlined
lookup work). This is a coverage limitation of this prototype, not proof that
eliminating a hot handoff is unhelpful.

Actual warmed TurboFan code at guest region `0x70020df8` confirms a linked edge
to `0x70020ff8`: after its exit checks and PC comparison, the generated code
stores the successor PC, restores its frame/argument registers, and performs a
fixed-target native jump at native offset `0x6413`. It avoids returning through
the runner's lookup and WASM-table call. There is no per-edge JS call. Source
and native offsets are recorded in the raw annotated assembly; the complete
native hash is in the JSON/capture.

## Scope and correctness

The prototype operates after existing ARM/Thumb translation and local-state
optimization. It links only targets available in the same module. ROM can
link to ROM; RAM can link within its address space or into immutable ROM. It
does not discover dynamic return targets or connect separate generated modules.
Hot compilation batches grow from 32 to 256 functions to expose more targets;
the game comparison tests this bundle, not an isolated batch-size change.

For linked RAM entries, cache dependencies include every transitively reachable
RAM body. The ordinary entry lookup validates their mappings; there is no
mapping-generation check at each direct edge. An actually executed helper ends
the chain. Successful inline memory accesses can continue. Zero-progress and
pending syscall exits return to the runtime. DFS backedges cover all linked
cycles and poll the watchdog; acyclic paths acquire no new watchdog poll.
Required generated-body exits remain. Thus this tests moving checks to proof
boundaries, not disabling all checks on arbitrary code.

The prototype passes 146 focused ARM/Thumb full-state, memory-helper, finite and
cyclic chain, bounded-host-stack, asynchronous-watchdog, ASID and transitive RAM
mapping checks. The complete suite reports 182 passed, zero failed; diagnostics-
only cases are skipped in this diagnostics-free build. Both games were exercised
in the browser. All frequency/isolation helper invocations, including the failed
micro startup, report successful host restoration.

One micro-driver startup failed because standalone logging had not been
initialized; it was corrected before measurement. One copied profiling harness
omitted `watchdog.js`; resource resolution was corrected before gameplay. Both
failed attempts remain in the raw evidence. No timed observation was discarded.

After archiving and removing the prototype, the restored runtime and test binary
rebuild successfully; the restored suite reports 181 passed, zero failed. The
unchanged production LAN service returns HTTP 200.

## Evidence and reproduction

[Machine-readable results](DIRECT_CHAIN_RESULTS.json) include all eight game
observations, all eight micro observations, the clock rules, hashes, link
coverage and restoration status. Raw builds, source snapshot, inputs, logs,
screenshots, JIT dumps and annotated code are retained under
`/home/claude/.scratch/eka-direct-chain-20261010`.

The stock WASM SHA-256 is
`e8dabc480c34780348ee6c3c3f731cde1eff36822d5cbcc224834dfecf7d840f`;
the candidate is
`632051d72e9964d12265d9955e69c3c268dfb80d9edd976327bc8e2564e2143d`.
The archive applies to this report's source state (`git apply --check` passes).
To restore it locally, apply the patch and build `eka2l1_wasm` and
`test_aot_wasm`. The test binary exposes `--direct-chain-only` and the micro
fixture export. `profile.ts` accepts `EKA2L1_DIRECT_CHAIN=0|1` only while the
patch is applied; mode 1 requires unsafe-code mode 3 and is frozen before init.

Use the existing [frequency-control wrapper](CONTROLLED_BENCHMARKS.md), with a
new host-state file, around this micro command:

```sh
node src/tests/benchmark/direct_chain/micro.mjs \
  build-wasm/src/tests/aot /ABS/NEW_OUTPUT 7 3600 2304
```

The last three arguments are the reserved CPU, requested MHz and calibrated
reference MHz. The driver uses ordinary headless Chromium, the real compiled
runner, hardware instruction/cycle counters, and `/proc` thread CPU time.
The saved `screen.py` and `screen-plan.json` reproduce the game screen against
the frozen stock/candidate artifacts. A follow-up needs better hot-edge
coverage and a work-matched game measurement before graduation.
