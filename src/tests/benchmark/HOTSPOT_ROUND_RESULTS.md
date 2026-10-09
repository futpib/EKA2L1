# Profile-driven hotspot round

The round is active. The baseline is the adopted runtime in `536ea5af3`
(documentation-only HEAD at the start was `c69ef4533`). The preceding two
Thumb helper experiments established a stopping point for that small helper,
not for the remaining ten hotspots.

## Progress

| # | Target | Status | Runtime evidence |
|---:|---|---|---|
| 1 | Static built-in syscall bindings | Adopted and served on LAN; [full result](STATIC_SVC_BINDINGS_RESULTS.md) | Snakes +0.19% (flat), Sky Force +2.53% CPU throughput over both batches, 7/8 faster pairs |
| 2 | Redundant exclusive-monitor clearing | Adopted and served on LAN; [full result](EMPTY_MONITOR_CLEAR_RESULTS.md) | Sky Force +3.38% CPU throughput, 3/4 faster pairs; Snakes flat (-0.09%) |
| 3 | EUser list scanning | Adopted and served on LAN; [full result](LIST_SCAN_RESULTS.md) | Sky Force +5.61% CPU throughput, 4/4 faster pairs; Snakes -0.16% (flat) |
| 4 | Audio interpolation SIMD | Adopted and served on LAN; [full result](AUDIO_SIMD_RESULTS.md) | Sky Force +3.03% retained mean, 4/4 faster pairs (three +0.80% to +1.33%); Snakes -0.47% |
| 5 | ROM lookup, RAM code, Cone/Ws32 and remaining runner work | Native code inspected; independent candidates remain under investigation | No speed claim |
| 6 | Private runner counters and registry acquisition | Adopted and served on LAN; [full result](RUNNER_LOCALS_RESULTS.md) | Snakes +3.01%, Sky Force +5.84% CPU throughput; 4/4 faster pairs in each |
| 7 | Combined ROM range checks | Not adopted; [full result](ROM_RANGE_RESULTS.md) and [patch](ROM_RANGE_EXPERIMENT.patch) | Snakes +0.02%, Sky Force +0.26%; 2/4 faster pairs in each |
| 8 | Registry-only ROM range check | Adopted and served on LAN; [full result](ROM_REGISTRY_RANGE_RESULTS.md) | Sky Force +1.47%, 3/4 faster pairs; Snakes +0.01% (flat) |
| 9 | Packed exclusive-monitor summary | Adopted and served on LAN; [full result](PACKED_MONITOR_RESULTS.md) | Sky Force +3.83%, 4/4 faster pairs; Snakes -0.10% (flat) |
| 10 | Published-view exclusive reads | Not adopted; [full result](PUBLISHED_EXCLUSIVE_READ_RESULTS.md) and [patch](PUBLISHED_EXCLUSIVE_READ_EXPERIMENT.patch) | Sky Force +0.14%, 2/4 faster pairs despite -0.97% native instructions; Snakes +0.39% |
| 11 | CPU-owned reservation monitor | Not adopted; [full result](CONFINED_MONITOR_RESULTS.md) and [patch](CONFINED_MONITOR_EXPERIMENT.patch) | Sky Force +0.93%, 2/4 faster pairs; Snakes -0.09% (flat) |

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

## Hot RAM attribution

The live loader places `skyforce.exe` (UID `0xa020d913`) at `0x70000000`
in a `0x70000` code allocation. The hot `0x70008184–0x700082ff` cluster is
therefore game code. Its import table points the call at `0x70008194` to EUser
ordinal 674, ROM wrapper `0x801a6603`, ARM stub `0x8019dc50`, and SVC 5,
`tick_count`. The caller subtracts a saved tick, counts small/large differences,
and often returns before further update work. This is evidence of polling and
frame-control work, not a recovered source-level function name. Existing
compiler defaults already fold its simple ARM import veneer. Loader lines,
image hashes, import and ROM checks are retained in `ram-ownership.json` and
`skyforce-hot-ram.asm` under the campaign root.
