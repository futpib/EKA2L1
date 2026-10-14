# Contiguous process arena graduation

Graduated on 2026-10-14 at the user's request, against `8f2c0df15` on
`wasm-port`. Contiguous prefix growth is now the sole direct-memory arena
implementation. `EKA2L1_ARENA_GROWTH`, its initialization/readback APIs, the
old full-arena allocator and compiler selection branches are removed. The
launcher rejects the retired variable even when set to zero. TLB/native
reference execution and canonical page-table fallbacks remain.

Every arena reserves a stable 64 MiB WASM range and initializes a prefix
starting at 4 MiB, doubling on guest chunk allocation. It never moves native
objects or retained pointers. Generated ordinary accesses use one cached
start bound and bias. The controller now publishes one range instead of
separate base and growing-range arrays; the unused arena mask is removed.
Destruction always frees the same reservation and subtracts its initialized
prefix from the counters.

This graduation follows an explicit user choice, not a newly established
throughput win. The [original measurements](ARENA_GROWTH_RESULTS.md) retain
their clock/workload limitations and failed Asphalt attempt. Full reservation
still consumes 64 MiB per process; the benefit targets initialization and
physical residency. Growth is synchronous and can hitch if a future gameplay
allocation needs a larger prefix.

## Verification

- WASM compiler tests: **190 passed, zero failed**. The previously documented
  crash-reproduction XFAIL remains separately labeled.
- Native CPU tests: **915 assertions in 37 cases passed**.
- Launcher tests pass for retired flag rejection, compatibility selection,
  startup failure propagation, and configuration-dependent cache identity.
- All eight games reach gameplay and the N-Gage client reaches Home/My Games
  on the frozen graduated browser bundle, without the arena flag or APIs.
  Successful captures have no recorded guest/page errors and zero instruction
  accounting. Guest sound is enabled and browser output muted.
- These runs check compatibility, not controlled throughput or exact
  full-state/image/PCM replay equivalence.

Two old fixtures assumed direct access at the end of the entire reserved
64 MiB range with no canonical page. They now use the real initialized prefix
and mapped final page. Their independent load values, helper/fault behavior
and syscall-return state/progress expectations are preserved. Initial failed
logs remain in the evidence directory; the corrected full suite passes.

The private HTTPS server initially had not bound its socket: the first Snakes,
Sky Force and K-Rally attempts record `ERR_CONNECTION_REFUSED` before emulator
startup. All three succeed after readiness. Asphalt's first scripted route
times out on the English-selection screen without a guest error; an unchanged
runtime retry reaches the racing HUD. The route also recognizes a late English
screen now. No failed attempt is treated as gameplay or a valid timing window.
All attempts remain in [the compact record](ARENA_GROWTH_ADOPTION.json).

The public `https://claude-laptop.lan:8188/` service serves that frozen graduated
artifact. Real game-picker launches of Snakes and Sky Force pass presentation
and keyboard-input checks with no recorded runtime errors; both retired arena
APIs are absent and initialized-prefix/growth counters are active. LAN screenshots
and `lan-check.json` remain in the evidence directory. A final service drop-in
selects the new bundle and unsets retired options before restart.

The frozen WASM SHA-256 is
`a196638dd3770b4dbebee36b1139f616589a24fd4bdd41fac60b97f597302cf8`.
Its C++ source map SHA-256 is
`8a87843d702fac58a8df4335d1953d35af1adf8abaff0278c6b24a3906f3f8a2`.
Builds, full test logs, rejected attempts and actual browser screenshots are in
`/home/claude/.scratch/eka-arena-graduation-20261014`.

## Reproduction

```sh
cmake --build build-wasm --target eka2l1_wasm test_aot_wasm -j4
node build-wasm/src/tests/aot/test_aot_wasm.js
cmake --build build --target eka_cpu_tests -j4
build/src/tests/eka_cpu_tests
node src/tests/wasm/compiler-policy.test.ts
node src/tests/wasm/arena-growth-benchmark.mjs snakes /tmp/arena-run HTTPS_URL
```

Use no `EKA2L1_ARENA_GROWTH` setting. The benchmark now measures the single
graduated implementation; historical 0/1 comparisons require their recorded
source checkout and frozen bundles.
