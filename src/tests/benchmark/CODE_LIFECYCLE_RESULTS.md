# Mutation-driven code validity: implementation and measurements

The lifecycle experiment moves repeated proof checks out of unchanged compiled
RAM entry. Correctness passes, but the measured runs do **not establish a
reliable net speedup**. The new option remains off by default; the LAN launcher
retains the verified baseline. This is not a claim that all guest memory checks
can be done ahead of execution.

Implementation: `8c1be4595`, based on `c2c3ef970`. Mechanism and invariants are in
[CODE_LIFECYCLE.md](CODE_LIFECYCLE.md). It retains the original
`EKA2L1_WASM_CODE_VERSIONS` experiment as a separate control.

## What moved out of the steady path

Code allocations begin unwatched. An exact comparison establishes a snapshot;
only its physical backing pages and inlined dependencies become watched. Stores
to watched pages update versions and publish a mutation notification. Host
pointer exposure, retirement and reuse publish notifications too. Multiple
notifications coalesce into one CPU validation epoch. A cache entry already
validated in that epoch skips both byte scans and its stamp walk. Mapping,
address-space and ARM/Thumb guards remain independent and mandatory.

After a relevant version changes, an exact comparison decides whether the entry
can be retained. Untracked or escaped host pointers continue to require exact
checking. Page-version overflow cannot rearm a page; epoch exhaustion permanently
disables the epoch shortcut. Existing within-region code-write exits, permissions,
endian/alignment handling and exact instruction budgets remain intact.

This removes repeated validation work, but every generated store still probes
the physical-page tracking table. Watched stores also publish notifications.
Those costs do not disappear merely because entry checks are cheaper. These
trials measure the net result, not an isolated causal attribution to barriers.

## Diagnostic mechanism check

Separate, unsampled detailed-counter run over guest seconds 21–25:

- 35,395,161 version hits, all also epoch hits: neither code bytes nor page stamps
  were walked on these hits.
- 8,499 exact byte comparisons remain, including mapping refresh and fallback.
- Guest instructions: 617,185,821; presentations: 160.

Do not use this diagnostic run's wall time as an isolated optimization result.
Its execution overlapped correctness work; its purpose is checking the counters.

## Controlled timing

18 guest seconds, heavy window 78–96, physical NVIDIA GPU, rendering enabled,
no capture/readback, counters and sampling disabled. Each batch warms four
fixtures, pauses them, then measures baseline/candidate/candidate/baseline
serially. All other owned test/build/replay jobs are stopped or finished before
measurement. This is still a shared host, not a controlled dedicated machine.

| Batch | Baseline host seconds | Lifecycle host seconds |
| --- | --- | --- |
| First | 18.7418 / 18.6904 | 18.9040 / 18.2562 |
| Confirmation | 32.8247 / 18.2064 | 18.4681 / 21.2598 |

First means: 18.7161 versus 18.5801 seconds, only 0.7% more throughput with
overlapping ranges. The confirmation has a large **baseline** outlier, which is
retained. Its favorable mean ratio must not be used as a reliable speedup claim;
no cause for that variability was established. No samples were removed.
Candidate heavy-window throughput averages 0.969× and 0.906× realtime in the two
batches; the prior 1.25× target is not met.

All eight runs perform exactly 3,975,200,506 guest instructions and 676
presentations. GPU metadata, browser version, binary hashes and raw measurement
objects are in [CODE_LIFECYCLE_EVIDENCE.json](CODE_LIFECYCLE_EVIDENCE.json).

## Sustained UI runs

After the timing batches, two isolated live runs followed the same upload/Start,
keyboard/touch and 120-second route, candidate first and baseline second:

| Build | Guest seconds | Host seconds | Realtime ratio | Max sampled lag |
| --- | --- | --- | --- | --- |
| Lifecycle | 103.477084 | 120.661844 | 0.85758× | 20.136s |
| Baseline | 120.585851 | 120.466777 | 1.00099× | 0.434s |

Both pass actual startup, held keyboard/touch, blur release, narrow layout,
changing visible gameplay, upload cleanup and shutdown. Intermediate screenshots
were inspected and remain active gameplay. Candidate input-queue delivery is
1.03–4.23ms; this is **not** input-to-display latency and does not negate guest
lag. The candidate did not sustain realtime in this run.

One live pair cannot isolate a universal regression factor, especially after
the timing variability. It does provide another reason not to enable the option.
The served baseline remains unchanged; no headroom or smoothness gain is claimed.

## Correctness

- Final lifecycle build: 135 WASM tests, including lazy watching, coalesced writes,
  aliases, dependency mutation, mapping refresh, retained host pointers,
  retirement/reuse and both generated/C++ page-version overflow. Epoch exhaustion
  is tested separately.
- Native/WASM fault probe: 480/480 exact matches, including exception callback
  state/order and changed memory bytes.
- Initial and final checked 1,600-image runs match native-interpreter pixels,
  guest timing/instruction records and audio exactly. Endpoint: 102.484363 guest
  seconds, 16,261,337,499 instructions. Both use a one-in-1,024 interpreter check.
  The initial artifact predates inlining the epoch helper and expanded tests;
  the evidence identifies both hashes separately.
- All three native test targets and all seven frontend checks pass.
- Restored default: 132 WASM tests, 480/480 fault cases and a fresh checked
  1,600-image native comparison pass. Both build options are restored to OFF.

Final candidate WASM SHA-256:
`4eb22f5f9c557f3f53db3051cc441250aa3acf9c47ac3cd7ae74d36a3f0052a6`.
The baseline archive is the prior verified `c2c3ef970` build. Native behavior and
sound-quality scope are unchanged. No second game is claimed tested.

## Reproduction

Configure `build-wasm` with `-DEKA2L1_WASM_CODE_LIFECYCLE=ON`, then build
`eka2l1_wasm`, `test_aot_wasm` and `eka_cpu_fault_wasm`. The lifecycle setting
implies versions for that configuration. Use the commands and acceptance gates
in [MEMORY_AND_CONNECTED_RESULTS.md](MEMORY_AND_CONNECTED_RESULTS.md), selecting
AOT mode 5. Archives and raw logs are under
`/home/claude/.scratch/eka-benchmark/lifecycle-*`.

The timing command is:

```sh
source /home/claude/.scratch/eka-benchmark/validity-gpu.env
EKA2L1_GPU=hardware EKA2L1_PROFILE_DETAIL=0 python3 src/tests/benchmark/profile_batch.py \
  --assets /home/claude/.scratch/eka-benchmark/assets --output NEW_OUTPUT \
  --compare-build /home/claude/.scratch/eka-benchmark/lifecycle-base \
  --before-aot 5 --after-aot 5 --capture-mode 2 \
  --start-us 78000000 --end-us 96000000
```

For live comparison, run `src/tests/wasm/live.ts` for 120 seconds serially with
`EKA2L1_EXPECT_UPLOAD_RELEASE=1`. Select the archived baseline using
`EKA2L1_WASM_BUILD_DIR`. Pin the same working GPU libraries in both processes.
To restore normal settings, configure both `EKA2L1_WASM_CODE_LIFECYCLE` and
`EKA2L1_WASM_CODE_VERSIONS` to `OFF`, then rebuild.
