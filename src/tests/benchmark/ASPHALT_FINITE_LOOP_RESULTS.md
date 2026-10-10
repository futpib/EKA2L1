# Asphalt framebuffer loop: remove repeated region dispatch

The default browser build now compiles proved finite Thumb countdown loops as
WASM loops. A four-run ABBA screen of Asphalt: Urban GT 2 improves mean unpaced
race-rendering throughput from **39.9 to 49.3 presentations/s (+23.5%)**.
The original game SIS and stock Nokia 5320 firmware are unchanged.

## Profile and guest-code attribution

The initial Chromium CPU profile during a race attributes 47.27% of the CPU
worker's sampled span to `execute_chain` (including inlined runtime work),
42.75% to generated guest code, and another 3.38% to uncached trusted lookup.
The graphics worker spends 95.32% of its sampled span waiting. These are
per-worker sample percentages, not additive CPU usage across workers.

Five adjacent hot game regions start at `0x700019c4`, `0x700019cc`,
`0x700019d6`, `0x700019dc` and `0x700019e2`. Disassembling the original E32
executable identifies this loop:

```text
r3 = 0x4b << 10                     # 76,800 pixels: 320 x 240
loop:
    pixel = load_u16(source); source += 2
    store_u8(dest++, pixel << 3)
    store_u8(dest++, (pixel >> 3) & 0xfc)
    store_u8(dest++, (pixel >> 11) << 3)
    store_u8(dest++, 0)
    r3 -= 1
    if r3 != 0: goto loop
```

The RAM Thumb translator previously ended a region after each store and
returned again at the loop backedge: approximately **384,000 region dispatches
per converted framebuffer**. These are same-worker WASM-to-WASM handoffs,
including lookup and state publication/reload. They do not cross JavaScript.

The optimization recognizes register effects and control flow, without game,
address, framebuffer-size or pixel-format checks. A straight-line scalar body
ending in either encoding of `SUBS Rn,#1` followed by `BNE entry` is eligible
when no earlier instruction writes the counter. The decrement terminates even
through unsigned wraparound. Calls and other control flow are excluded.
Registers and flags remain in WASM locals across iterations. The existing
trusted-code policy and direct-memory configuration are required; mutation-
compatible execution retains its previous behavior.

Memory checks remain at every access. A failed check returns zero progress at
the exact pre-instruction PC, with architectural state published, so the
interpreter can service that access. No memory callback executes inside the
proved loop: it could change the counter or mappings and invalidate the proof.
The proved finite body needs no watchdog poll; existing outer dispatch handles
yielding afterward. Guest clocks and instruction accounting are unchanged.

The follow-up profile no longer contains the four intermediate hot store
regions. Sampled dispatcher time per presentation falls from 17.30 to 11.64 ms;
the five original pixel-region entries together fall from 3.36 to 0.96 ms per
presentation. These are sampling-derived diagnostics, not isolated timings of
individual instructions. The dispatcher remains the largest overall bucket
(44.16%); this change removes the identified framebuffer component of that cost.

## Unprofiled comparison

Measured 2026-10-10 with Chromium 153.0.8010.52, Intel i7-10875H and NVIDIA
hardware graphics. Both builds use the same compiler defaults, count-free
execution and watchdog interval. One emulator runs at a time on a shared
workstation. Browser audio is muted; guest audio processing remains active.

Each fresh browser selects Arcade / Single Race / Volkswagen Golf GTI Mk V /
San Francisco. There is no steering; after a 20-second warm-up the car is
against the first outside barrier while the race keeps rendering. Start/end
screenshots for every sample were inspected. This is a repeatable warmed race
scene, not a whole-track benchmark or a claim about every car and camera view.

| # | Run order | Build | Presentations | Wall seconds | Presentations/s | Emulator compilation ms |
| ---: | --- | --- | ---: | ---: | ---: | ---: |
| 1 | A1 | Baseline | 817 | 20.0915 | 40.664 | 21.309 |
| 2 | B1 | Candidate | 972 | 20.0870 | 48.390 | 16.641 |
| 3 | B2 | Candidate | 1,009 | 20.0907 | 50.222 | 16.921 |
| 4 | A2 | Baseline | 787 | 20.0899 | 39.174 | 21.691 |

Mean throughput rises 23.5%; the two adjacent pair comparisons favour the
candidate by 19.0% and 28.2%. This small shared-host screen supports adoption;
it does not establish an isolated peak or a narrow confidence interval.

FPS is the UI's presentation counter, not display refresh or deduplicated
images. No instruction counts or guest-clock multipliers enter the speedup
calculation. All measured samples have zero instruction totals, no route
pauses and no fatal/page errors. Loading and navigation are excluded. The
compilation column sums translation, emission and installation during the
window; V8 compilation is not separately measured. Profilers are disabled for
all four adoption timings.

## Validation and artifacts

The full WASM compiler suite passes: **183 passed, 0 failed**. New coverage
includes 224 interpreter comparisons of registers, flags and complete memory,
TLB and direct-page backends, memory permissions, page boundaries, unaligned
addresses, endian fallback, expired watchdogs, both decrement encodings, all
eight counter registers at recognition, and rejection of counter rewrites,
other branches, calls, syscalls and incomplete loops. Mutation-compatible
mode still returns at its original first-store boundary.

Commands:

```sh
cmake --build build-wasm --target test_aot_wasm eka2l1_wasm -j4
node build-wasm/src/tests/aot/test_aot_wasm.js --thumb-finite-loops-only
node build-wasm/src/tests/aot/test_aot_wasm.js
```

[Machine-readable measurements](asphalt-finite-loop-results.json) preserve all
four timing samples and the profile summaries. Raw CPU profiles, screenshots,
logs, frozen builds, the interactive `run.mjs` harness and serial `abba.py`
runner are under `/home/claude/.scratch/eka-asphalt-perf-20261010/`.
The test server uses the production `server.ts` and compiler defaults; uploads
are the cached stock ROM/RPKG and original Asphalt SIS. The unpaced test API
sets a distant 1,800-second guest deadline before initialization, and every
measured sample is checked to remain before that deadline.

Source baseline: `610a7bb8e`.
Baseline WASM SHA-256:
`3d0bb2ee382bcef42c641ba1d609e019e65233b75310eabfd265fcd65b4b60ff`.
Candidate WASM SHA-256:
`0897120710c85c17e04762902fa34d471d98cf5160b79ad4bdeff27497bf0010`.

The raw archive also retains an intermediate profile whose recognizer covered
only the eight-bit-immediate subtraction encoding. It did not select Asphalt's
three-bit-immediate form and left performance unchanged. The final fixture uses
the actual decoded instruction sequence; the final profile verifies selection.
That intermediate capture is excluded from the adoption timings.

The candidate is served on `https://claude-laptop.lan:8188/`. A fresh normal
launcher session selected Asphalt, loaded the matching candidate WASM digest,
reached a paced race and accepted steering input. The driving screenshot was
inspected; no fatal/page errors were recorded. The browser reported the NVIDIA
Quadro T1000 Vulkan renderer. This paced launch is an integration check, not an
additional performance comparison.
