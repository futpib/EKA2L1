# Headroom optimization trials

Measured 2026-09-27 on the i7-10875H / Quadro T1000 Max-Q desktop, Chrome
150.0.7871.186, physical NVIDIA Vulkan rendering. **The CPU experiments did not
establish meaningful additional headroom. The 1.25x heavy-window target is not
met.** Memory cleanup and a smaller worker pool are retained; this is not a
claim of a large gameplay speedup.

## Retained changes

- `5fd870327`: recover a collided recent-cache entry before checking its mapping
  generation, avoiding unnecessary mapping resolution. Exact code and dependency
  bytes are still checked. Combine cached-page alignment/page guards; cached
  endian/permission state remains valid until a helper exits the region.
- `9ca2be4d5`: default to a configurable 32-worker pool instead of 64. Delete
  temporary ROM/RPKG/SIS uploads after their synchronous installation call and
  release file selections. This workload uploads 192,004,131 bytes (183.1 MiB).
  Installed files remain in emulator storage. Benchmark installation mirrors
  the same cleanup. Live tests now record resources and check upload release.
- Retain repeated-load differential tests and permission/endian/alignment/page
  fallback tests developed during the rejected span-reuse experiment.

The CPU changes are general mechanisms, not game/address recognition. Workload
selection and tuning remain Snakes-specific; other games and devices have not
been tested. The worker-pool size is configurable with
`-DEKA2L1_WASM_WORKER_POOL_SIZE=64` if another workload needs more capacity.

## Controlled CPU trials

Each batch warms four independent browser fixtures, pauses at exactly 78 guest
seconds, then releases **old/new/new/old serially** through 96 guest seconds.
Capture, CPU sampling, detailed counters and diagnostic checking are disabled.
No builds or correctness tests run during measurement. Each trial executes
3,975,200,506 guest instructions and 676 presentations. The archived control is
`8b0388f4c`; all candidates include the retained CPU changes above. Later
candidates also include the worker/upload changes. Trial times cannot be added
or multiplied across batches. Two repetitions and a shared host give limited
precision.

| Candidate | Old host seconds | Candidate host seconds | Decision |
| --- | --- | --- | --- |
| Retained mapping/page simplification | 17.5421, 17.4613 | 17.4339, 17.3158 | Small simplification retained; observed mean gain 0.73%, not robust new headroom |
| Cached page-level write overlap guard | 17.5643, 17.4928 | 17.2883, 17.4075 | Removed; no clear additional gain |
| Generated exact-byte validator functions | 17.5813, 18.0871 | 18.2259, 18.1703 | Removed; regression |
| Reuse long-multiply accumulator | 17.4634, 17.2979 | 17.1564, 17.3151 | Removed; no clear additional gain |
| Grouped budget checks with a small-budget compiled variant | 17.2994, 17.1381 | 17.4123, 17.3667 | Removed; regression |
| Cache validated spans for repeated reads from unchanged bases | 17.4901, 17.3448 | 17.5858, 16.9643 | Removed; overlapping ranges |

The final kept-build confirmation measured **17.4159 / 18.6549 seconds** for
the original and **17.6534 / 17.2201 seconds** for the kept build. The ranges
overlap and the controls vary substantially; no reliable CPU speedup is
established. The kept mean is **17.43675 seconds for 18 guest seconds**, or
**1.0323x realtime**, far short of 1.25x. The slower control is retained in the
report rather than excluded after seeing the result.

All six candidates passed the 1,600-image native comparison, including guest
records and audio output. Passing correctness did not justify retaining extra
compiler/cache state without a useful measured gain. The first write-guard
replay exported all images but hit a harness working-directory error during
post-validation; running the comparator directly passed. The harness path is
now relative to its module, not the caller's working directory.

Rejected patches and raw logs are retained locally under
`/home/claude/.scratch/eka-benchmark/HEADROOM_*_REJECTED.patch` and
`headroom-*-timing/`. A failed initial timing setup omitted the archived `.data`
file; it is excluded. The repaired first batch is `headroom-cpu-timing2`.

## Final live and memory verification

Serial two-minute trials use the same manual upload/Start and keyboard/touch
route; the small timing difference changes the exact live input schedule, so
these are matched workflows, not identical deterministic guest records.

| Measurement | Original | Kept build |
| --- | ---: | ---: |
| Guest / host time | 0.99993x | 1.00011x |
| Largest sampled temporary lag | 0.307 s | 0.291 s |
| End-of-run browser PSS | 1921.2 MiB | 1634.8 MiB |
| Active / unused pool, startup and end | 19 / 45 | 19 / 13 |
| Temporary installation files | 183.1 MiB | 0 |
| Final emulator allocation | 545.62 MiB | 545.60 MiB |
| Final WASM linear-memory capacity | 569.06 MiB | 664.25 MiB |

The final browser PSS is **286.4 MiB (14.9%) lower in this pair**. Startup PSS
was 1556.9 versus 1572.5 MiB, so the reduction is not uniformly present at every
instant. This is a resident-memory observation, not an all-client bound or proof
that a leak was fixed. WASM capacity can differ with growth timing and is not the
same as allocated or resident memory. Retained emulator allocation is almost
identical. Both runs reach about 144.15 guest seconds and 15,302 compiled
functions. Sound remains off.

The kept run passes changing gameplay images, held keyboard input, touch,
narrow layout, blur release and shutdown. Queue-delivery observations range
0.64–3.00 ms; they do not measure display-response latency. Intermediate and final
images show active gameplay. An additional automatic-start smoke run checks
that preloaded uploads are also released. The 32-worker pool has spare capacity
in these tested routes; other applications and long-running thread-creation
patterns remain untested. No physical remote LAN client was used.

Raw samples, resources, hardware/binary identities and reports are in
`HEADROOM_EVIDENCE.json`.

## Correctness and limitations

The retained build passes **132 WASM tests**, including **402,976** bounded
state/memory comparisons, all **three native CTest targets**, and all **seven
frontend checks**. It matches the same native 1,600-image reference
through 102.484363 guest seconds. Exact instruction budgets, code-byte checks,
address-space/mapping guards and code-write alias exits remain in place. No
clock scaling, skipped guest work or changed gameplay was used to obtain speed.
The sampled interpreter checker checks one in 1,024 compiled calls; the inline
memory path is additionally covered by differential tests and full replay.

The repeated-read guard fixture uses raw helper imports. Its endian and
unaligned cases assert fallback routing, not full architectural endian semantics;
the initial test incorrectly compared those raw imports with the interpreter
and was corrected. Eligible fast-path state/memory comparisons remain exact.
Audio output comparison verifies repeatability, not sound quality; sound remains
off in live play.

## Next CPU investigation

These trials do not show that more headroom is impossible. They show that the
source-level reductions tried here do not provide it. Before another compiler
rewrite, inspect the browser compiler's lowering of the measured hot regions
and isolate their execution cost: register spills, branches, and generated
function entry/exit overhead. This is a proposed next investigation, not a
measured new bottleneck or promised speedup. Larger control-flow fusion remains
a possibility; increasing thresholds or adding per-entry state without a
measured benefit is not justified by these results.

## Reproduction

From the repository root, after building and finishing correctness work:

```sh
EKA2L1_GPU=hardware EKA2L1_PROFILE_DETAIL=0 \
python3 src/tests/benchmark/profile_batch.py \
  --assets ASSETS --output NEW_BATCH --compare-build ARCHIVED_BUILD \
  --before-aot 5 --after-aot 5 --capture-mode 2 \
  --start-us 78000000 --end-us 96000000

cd src/tests/wasm
EKA2L1_EXPECT_UPLOAD_RELEASE=1 node live.ts ASSETS NEW_LIVE_OUTPUT 120
```

An archived frontend needs `eka2l1.data`, `.html`, `.js` and `.wasm`. The live
trial uses actual upload/Start, held keyboard and touch input, narrow layout,
blur release, changing scene images, and shutdown. Input-delivery latency
measures queue consumption, not display response.
