# Repaired and extended browser AOT

2026-09-26. Tested runtime: `fd74f24a01db5932fd33b5d314b95a66577522a0`.
Reports, binary hashes and comparison summaries: [AOT_EVIDENCE.json](AOT_EVIDENCE.json).

AOT now completes the deterministic Snakes replay correctly, and the extension
compiles hot ROM blocks beyond the original DLL exports. **It has not yet
produced a substantial performance improvement:** the repeated hot-ROM mean is
only 1.3% below the interpreter mean, comparable to the observed run variation.

## Correctness

Two fresh AOT browser runs match both fresh native interpreter runs and the
saved interpreter reference exactly:

- 1,000 byte-identical PNGs, with all RGBA pixels and frame records equal.
- Equal presentation ordinals, guest timestamps and instruction counts.
- Endpoint: frame 999 at **68.040042 guest seconds**, **10,342,580,529 instructions**.
- Capture spans **47.013860 guest seconds** from 21.026182; all 1,000 images and
  cropped 3D viewports are distinct. Minimum adjacent viewport change is 22.05%;
  cadence is 21.249 changing images per guest second.
- Complete PCM and timestamped audio-event logs also match. This is regression
  equality; the requested audio-quality work remains deferred.

Both full browser runs use clean `fd74f24a`. Native run 0 used the preceding
runtime build; native run 1 launched the reviewed rebuild. The native runner's
shared report hash applies to run 0 only. Evidence includes a separate hash of
run 1's actual running executable, equal to the reviewed binary on disk. This
avoids attributing the older run to a later build.

Before the final review, the hot-ROM path at clean `53d78d00` also completed
startup and 20 gameplay images with **every nonempty compiled block replayed in
an independent interpreter checker**. All 20 PNGs and records matched native.
That check compared registers, NZCV/T and memory using a private overlay. It is
scoped to ordinary memory, not duplicated MMIO side effects. The later review
added interpreter fallback for unsupported PC and banked-register forms; the
full replay and final test suite above validate the reviewed version.

## Performance

Each measured window runs from 21 to 25 guest seconds: **655,867,149 guest
instructions**, 170 presentations and 85 changing images. All five trials
match the native reference's first 85 PNGs and records exactly. Counter values
also repeat exactly within each mode.

| Mode | Host seconds for 4 guest seconds | Compiled instruction coverage | Decoded instructions |
| --- | ---: | ---: | ---: |
| Interpreter, first control | 55.5895 | 0% | 90,055,999 |
| Repaired ROM exports | 58.1590 | 0.468% | 89,621,016 |
| Hot ROM, first trial | 54.0323 | 18.835% | 81,038,003 |
| Hot ROM, second trial | 54.7929 | 18.835% | 81,038,003 |
| Interpreter, second control | 54.7060 | 0% | 90,055,999 |

Interpreter mean: **55.14775 s**. Hot-ROM mean: **54.41260 s**, or **1.0135x**
the throughput (1.333% less elapsed time). Their ranges overlap. With only two
repeats per main mode, no fixed CPU frequency and normal desktop activity,
this does not establish a robust speed win. Export-only AOT is slower in this
single trial. Both main modes remain about **0.073x real time** on this host.

The extension lowers decoding by 10.0% and cache misses from 14,069,057 to
12,379,963 (12.0%), but still leaves 81.2% of instructions interpreted. The
137,740 context loads and complete decoded-cache clears persist in every mode.
Compiled execution itself makes 10,924,720 dispatches in the measured window.
These are instruction/work counts, not CPU-time percentages.

Five fresh fixtures warmed concurrently and paused at the window boundary.
An external gate delayed the serial measurements until all correctness jobs
exited. Sampling and per-block checking were disabled; full framebuffer/PNG
capture was enabled for every mode. Paused browser groups used approximately
0.5–1% of one CPU in a two-second OS accounting check. Warmup times were contended
and must not be used as startup speed measurements. Verification/hashing ran
after the timing batch.

Environment: Release build, Emscripten 4.0.10, Chromium 150.0.7871.186,
Intel i7-10875H, Linux, **SwiftShader**. No physical-GPU timing was measured.
The reproduction harness is [AOT.md](AOT.md); `profile_batch.py --compare-aot`
uses the same mode order and serial measurement barrier.

## Implementation and remaining work

Bounded compiled blocks honor the remaining instruction budget, count actual
executed instructions, and return to the dispatcher at branches. Keys include
ARM/Thumb mode. Repairs cover interworking, carry/overflow flags, register shifts,
byte and halfword stores, and unsupported instruction encoding aliases.
Unsupported forms fall back to DynCom instead of being approximated.

Mode 1 compiles selected ROM DLL exports and reachable local blocks. Mode 2
adds deterministic sampling of frequently executed ROM entries, capped at
4,096 additional blocks, 128 code bytes per block and 32 functions per module.
The extension generates WASM during execution; the existing subsystem calls
this AOT. Sampling and compilation decisions use guest counters, never host time.
Hot-ROM mode remains opt-in; the default benchmark remains the interpreter
reference. Existing interactive export compilation receives the translator
repairs, but no new UI switch was added.

The next AOT targets are dispatch overhead and broader game-code coverage.
The current dispatcher still copies pre/post registers for crash history and
performs module accounting; their individual costs have not been measured in
this follow-up. RAM/game-code compilation needs address-space-aware keys and
code-change invalidation before it can be enabled safely. This implementation
intentionally limits hot compilation to immutable ROM. It does not establish
compatibility for other games, arbitrary instruction forms or hardware timing.

## Tests and artifacts

At the reviewed runtime:

- WASM CPU/AOT executable: **122 passed, 0 failed**, including **23,268** exact
  budget/register/flag/PC/memory differential comparisons. The existing test
  harness still reports its pre-existing expected-failure case separately.
- Full native CTest: CPU target passes. `ekatests` has **78 passing cases and
  seven previously observed failures** (410/417 assertions) in allocator,
  number parsing and app registration. No clean upstream base run establishes
  their origin; the full native suite is not green.
- Full browser captures: both complete without runner errors; all comparisons
  and active-gameplay validation pass. Self-review and `git diff --check` pass.

Local raw evidence is under `/home/claude/.scratch/eka-benchmark/`:
`aot-reviewed-1`, `aot-reviewed-2`, `aot-native-reference`, `aot-performance`,
and `aot-hot-verify-5`. Final suite logs are
`/home/claude/.scratch/eka-aot-final-{native,wasm}-tests.log`.
Source and compact evidence are committed locally; nothing was pushed.
