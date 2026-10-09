# Profile-driven hotspot round

The round is active. The baseline is the adopted runtime in `536ea5af3`
(documentation-only HEAD at the start was `c69ef4533`). The preceding two
Thumb helper experiments established a stopping point for that small helper,
not for the remaining ten hotspots.

## Progress

| # | Target | Status | Runtime evidence |
|---:|---|---|---|
| 1 | Static built-in syscall bindings | Adopted and served on LAN; [full result](STATIC_SVC_BINDINGS_RESULTS.md) | Snakes +0.19% (flat), Sky Force +2.53% CPU throughput over both batches, 7/8 faster pairs |
| 2 | Redundant exclusive-monitor clearing | Native/WASM model tests and both exact game replays pass; timing pending | No speed claim |
| 3 | EUser list scanning | Prototype passes 54,720 independent interpreter state comparisons; timing pending | No speed claim |
| 4 | Audio interpolation SIMD | Prototype passes 7,680 exact scalar/SIMD comparisons; game replays and timing pending | No speed claim |
| 5 | ROM lookup, RAM code, Cone/Ws32 and remaining runner work | Native code inspected; independent candidates remain under investigation | No speed claim |

The syscall candidate keeps owning snapshots for stateful callbacks that can
change their own registration. Built-in bindings use a static bridge with a
compile-time handler target. Actual warmed V8 code bypasses the owning callable
copy/invoke/destruction sequence. That proves removal of work; the controlled
runtime comparison decides adoption.

The first comparison retains all 16 valid observations, including the slow
Sky Force candidate. It passes all 88 host-restoration checks. Confirmation
adds eight new Sky Force observations without replacing any earlier result.
It measures +2.82% with all four pairs faster; the combined +2.53% earns adoption.
There are no temperature gates or cooldowns. The worker is pinned to CPU 7,
sibling 15 is reserved, and measured frequency, affinity and counter rules
validate the fixed 3.6 GHz request.

Each clear winner will be committed and enabled before the next timing
experiment. Runtime-negative or unresolved prototypes will be archived and
removed from active source. Exact image, guest-progress and PCM replays are
required for both games. LAN remains available on the frozen adopted artifact
while source and isolated benchmark builds change.

Raw plans, hashes, patches, observations, native captures, tests and progress:
`/home/claude/.scratch/eka-hotspot-round`. This report will be updated with
final dispositions and a direct combined comparison; gains are not additive.
