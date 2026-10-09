# Snakes capture and native deterministic benchmark

The [experiment index](EXPERIMENT_INDEX.md) is the central catalogue of measured
gains and losses, pending comparisons, evidence and current adoption decisions.
Controlled results refresh it automatically. After updating a historical report
or `EXPERIMENT_STATUS.json`, run `python3 src/tests/benchmark/experiment_index.py`;
use `--check` to verify it is current. Historical percentages retain their
original metrics and are not added together or treated as current-runtime gains.

The [verified audio replay results](RESULTS.md) record the four-run comparison and test limitations.

The [four compiler-option experiments](COMPILER_OPTIONS_RESULTS.md) compare V8 tiering, connected regions, a validation-cost ceiling, and restricted Dynarmic-IR-to-WASM kernels.

The native deterministic benchmark is opt-in (`EKA2L1_BENCHMARK=1`). Browser
execution now always uses a watchdog with no instruction accounting. Paced play
samples host time; unpaced capture advances virtual slices at yields and skips
idle time. Browser instruction totals remain zero, and instruction-clock replay
identity with native captures no longer applies. For current playability checks,
use [paced gameplay](../wasm/README.md). The [count-free adoption report](COUNT_FREE_DEFAULT_RESULTS.md)
records the transition. Earlier [AOT results](AOT_RESULTS.md) describe the older
counted implementation.

- The native benchmark clock advances from executed instructions (one synthetic cycle per instruction, 484 MHz at the default clock), with at most 4,840 instructions per dispatch. This is a reproducible clock model, not a hardware cycle-accuracy claim.
- When no guest thread is runnable, execution jumps to the next scheduled event. There is no real-time timer thread or host vsync pacing.
- Guest UTC starts at 2024-01-01 00:00:00, timezone UTC. Guest `Math::Random` and host-generated kernel object names have fixed seeds.
- Input comes from `snakes.input`: virtual microseconds, raw Symbian scan code, press/release. Host keyboard, mouse and controller input are excluded.
- Native retains the original device input-method DLL, matching WASM instead of applying Qt-only host-input replacements. Patch libraries and guest directory entries use sorted order so guest addresses and file-search work do not depend on the host filesystem.
- Audio uses the same guest-clock PCM DSP on both targets. A virtual timer requests buffers every 10 ms, with a 40 ms low-water threshold. Signed PCM8/PCM16 mono/stereo is mixed into 48 kHz stereo PCM16 using integer zero-order-hold resampling and saturating volume/mixing. Unsupported compressed formats are rejected. Host audio callbacks never advance the guest.
- By default, capture starts at 21 virtual seconds, after the scripted replay has entered gameplay. Adjacent identical RGBA images are suppressed, so duplicate presentations do not consume the 1,000-image budget. `frames.jsonl` retains the presentation ordinal, guest time and instruction count for every accepted image. GPU completion is synchronized before guest execution continues.
- Each native repeat begins with a copy of the freshly installed device. Pixel hashes and guest timing must match exactly.

## Native build

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DEKA2L1_ENABLE_SCRIPTING_ABILITY=OFF -DEKA2L1_BUILD_TOOLS=OFF \
  -DMBEDTLS_FATAL_WARNINGS=OFF
cmake --build build -j8
ctest --test-dir build --output-on-failure
python3 src/tests/benchmark/run_native.py \
  --assets /absolute/path/to/assets --output /absolute/path/to/new-results \
  --frames 1000 --repeat 2
```

The runner requires Xvfb, Mesa software OpenGL and Python Pillow. Output must be a new directory. It records wall elapsed time separately; wall time never feeds back into the guest. A host timeout detects stalls, it does not pace execution.

## Assets

Use the original assets (not committed to this repository). The runner validates their SHA-256 hashes before installation. Public gateway errors must never be accepted as asset data.

| Filename | IPFS CID |
| --- | --- |
| SYM.ROM | bafybeicj2jkrjfirzdz5jezz6hjbx2ylyv343kaecnhytl3g6yjy3mwmqm |
| SYM.RPKG | bafybeihjy4vjxb5cy7zxca4kedg5ncxf5xrbj5basirfefemqwru73aipu |
| Snakes.sis | bafybeicuomcc2zhzi3vwfb5xihnlkikz3biaa43g4d22z2wptcmhvmp3di |

Retrieve using `ipfs cat CID > filename` with a running IPFS node. The old `@futpib/fetch-cid` downloader does not check HTTP status and can cache gateway notices as files.

The [Snakes release collection](SNAKES_RELEASES.md) preserves four original
Nokia installers and two firmware builds, with hashes and separate IPFS CIDs.
It identifies a newer game build, 0.6.0.20, without changing this benchmark's
reference asset.

The experimental [Nokia N80 assets](N80_ASSETS.md) use the same ROM/RPKG convention
with separate CIDs and a reproducible firmware converter. Select their manifest
using `--asset-manifest src/tests/benchmark/n80-assets.json`. Device installation
and deterministic Snakes gameplay/audio pass in the native build. Set
`EKA2L1_SNAKES_N80_NATIVE_RESOLUTION=1` for true 352×416 rendering; without it the
game doubles a 176×208 image. See the N80 notes for reproduction and scope.
These are not the default reference assets. The runner launches SIS game
UID `0x2000730F` explicitly because N80 also bundles another game named Snakes.
Use `--app-uid` to select another release, and `--rom-app` to skip SIS
installation for a bundled game. The [stock-resolution survey](STOCK_RESOLUTION_RESULTS.md)
compares three game executables and explains why larger stock displays do not
automatically provide more rendered detail.

## Browser build and comparison

For performance work, use the [Chrome profiling workflow](CHROME_PROFILING.md).
Normal WASM builds compile out custom timers and guest census; Chrome profiles
and traces are the default source of time attribution.

Use Emscripten 4.0.10 (the version used for validation), Node.js with TypeScript stripping, Python 3 for audio validation, and Chromium. Activate the SDK environment first.

```sh
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DEKA2L1_ENABLE_SCRIPTING_ABILITY=OFF -DEKA2L1_BUILD_TOOLS=OFF \
  -DMBEDTLS_FATAL_WARNINGS=OFF
cmake --build build-wasm -j8
node build-wasm/src/tests/aot/test_aot_wasm.js
cd src/tests/wasm
npm ci --ignore-scripts
EKA2L1_BENCHMARK_AOT=5 node benchmark.ts /absolute/path/to/assets /absolute/path/to/wasm-run-1 1000
```

The browser harness defaults to `watchdog-snakes-countfree.input` and starts
capture at 42 guest seconds. Native replay retains `snakes.input` and 21 seconds.
The profile harness defaults to the same browser route, sampling 42–46 seconds.
Inspect captured scenes when changing clock, input or game settings.

Set `CHROMIUM_PATH` if Chromium is not at `/usr/bin/chromium`. The local server supplies the isolation headers required by WASM pthreads. Each browser run gets a fresh memory filesystem. The same replay file and asset hashes are used on both targets. Browser polling only observes completion; it does not deliver input or pace guest execution.

For native deterministic repeats and historical counted captures, `compare.py` requires non-silent gameplay PCM with buffer callbacks and compares the entire audio sample stream and timestamped audio events exactly, as well as every RGBA pixel, frame number, presentation ordinal, size, virtual timestamp and instruction count. It exits nonzero and prints the first differing record on divergence. There is no pixel tolerance or frame realignment. Startup screens are warmup and do not count toward the default 1,000-image window. The replay enters Level 1 automatically, then supplies direction changes. Native `--start-us` selects a different warmup boundary; `--all-presentations` retains duplicates for diagnostics. The optional last positional browser argument selects the warmup boundary. Count-free browser runs use a different clock and should not be expected to pass this exact timestamp/instruction comparison.

`metrics.json` measures wall time from the first contentful presentation after the warmup boundary through the last captured image, including rendering, readback and PNG/manifest writes. Use that common capture window for performance comparisons. `report.json` also records runner elapsed time, which includes different amounts of setup/export work on each target and is not directly comparable. Host time is diagnostic only; it never advances the guest clock. The harness uses software rendering and capture overhead; its FPS is not a paced-play speed measurement.

The benchmark is scoped to this fixed Snakes/device replay in fresh processes. It does not promise deterministic network, compressed audio, microphone input, arbitrary host services, save-state resumes, or hardware-accurate instruction timing.

## Memory implementations

The browser launcher and harnesses support two implementations, selected before guest
initialization with `EKA2L1_MEMORY_IMPL` or `eka2l1_memory_impl_configure(mode)`:

| # | Value | Implementation |
|---|---:|---|
| 1 | 0 | Optional 512-entry TLB |
| 2 | 2 | Default direct memory with compact ARM/Thumb lowering and mapping-change notifications |

Direct mode retains the [all-cuts implementation](DIRECT_MEMORY_CUTS_RESULTS.md).
Its process-local arena shares C++'s backing in the primary WASM memory. The
fallback page directory handles aliases and addresses outside that arena; it
is part of direct mode, not a separate selectable implementation. Direct mode
requires compiled regions and the unsafe code-write policy. The driver below supplies these settings. The WASM runtime and normal
launcher default to direct memory; native cores retain their TLB backend.
Replay/profile harnesses also default to direct with AOT mode 5 and unsafe code
mode 3. Interpreter and mutation-compatible runs select TLB unless explicitly
overridden. Instruction-count verification has been removed. Set `EKA2L1_MEMORY_IMPL=0` to
select TLB in the launcher or harnesses.

[Default-selection validation](DIRECT_MEMORY_DEFAULT_RESULTS.json) records
mode 2 in both games through the browser picker and the LAN launcher, controls
and browser audio checks, explicit TLB selection, and the default Snakes replay's
exact match to 60 native reference frames and audio.

Eligible bounded, register-cached ARM short blocks always use inline memory
access. `EKA2L1_ARM_MEMORY` and its configuration/readback API are retired;
remove the variable from launch and benchmark environments. TLB/direct selection
still uses `EKA2L1_MEMORY_IMPL`. Uncached and unbounded standalone translations
retain their helper path, and connected ARM regions retain their existing inline
path. See the [adoption checks and eligibility](ARM_MEMORY_ADOPTION.md).

The [ARM short-block reassessment](ARM_MEMORY_REASSESSMENT.md) records the
on/off comparison that preceded adoption. The [slow32 census](SLOW32_CENSUS.md)
records the earlier load-guard failures, helper calls and interpreter deferrals
with short ARM memory inlining disabled.

The [scalar alignment experiment](UNALIGNED_SCALAR_RESULTS.md) compares removing
the direct arena's scalar alignment check and replacing page-based scalar
alignment with a page-end check. The direct half is now fixed behavior: ordinary
scalar accesses need no natural alignment inside the arena, while the fallback
table checks that the entire access fits in one page. TLB retains its alignment
checks. The original combined patch is preserved separately.
`compare_memory_builds.py` runs paired comparisons of
two frozen browser builds through the existing memory benchmark driver.

The [direct entry-span extension](DIRECT_SPAN_RESULTS.md) tried two-access
proofs, predictable pointer updates and spans larger than a page inside the
arena. It passed correctness checks but was not adopted: Snakes had no
repeatable CPU gain and Sky Force used 2.85% more worker CPU. Its implementation,
tests and complete measurements are preserved in the evidence file.
The [three-access follow-up](DIRECT_SPAN_THREE_RESULTS.md) also found no
repeatable Snakes gain and 2.44% more Sky Force worker CPU; it remains unadopted.

The [cheaper lowering of existing proofs](SHARED_SPAN_LOWERING_RESULTS.md) is
adopted for direct memory in both ARM and Thumb. It removes redundant address
work and repeated tests of a shared transfer proof without broadening proof
eligibility. The executed-WASM regression gate allows no instruction-count
increase; paired timings found 7.63% less Sky Force worker CPU and flat Snakes
CPU. The TLB trial also removed WASM instructions but regressed CPU time, so
TLB retains its original lowering.

Allocation-range mode, standalone flat-page mode, earlier direct variants and
delayed activation have been removed. Values 1 and 3 are rejected. Remove
`EKA2L1_DIRECT_POLICY` and `EKA2L1_MEMORY_ACTIVATE_US` from harness environments;
there is no direct-policy or activation API. Direct mode always uses the best
retained implementation from boot. Historical benchmark reports retain their
original results and reproduction commands for the source revisions they name.

Run correctness comparisons before collecting CPU timings. Use a frozen browser
build and fresh output directories:

```sh
node build-wasm/src/tests/aot/test_aot_wasm.js --memory-implementations-only
python3 src/tests/benchmark/memory_implementations.py "$OUT/replays" replays \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 0 2 --frames 60
python3 src/tests/benchmark/memory_implementations.py "$OUT/timings" timings \
  --build "$BUILD" --snakes-assets "$SNAKES_ASSETS" --sky-assets "$SKY_ASSETS" \
  --reference-root "$REFERENCES" --modes 0 2 --rounds 2
```

## Gameplay and clock validation

```sh
python3 src/tests/benchmark/validate_gameplay.py /absolute/path/to/new-results/run-0/frames
python3 src/tests/benchmark/validate_gameplay.py /absolute/path/to/wasm-run-1
```

This fixed-replay gate requires at least 95% distinct 3D viewports, at least 1% changed viewport pixels between every pair of adjacent images, strictly increasing guest timestamps, no gap above 100 ms, and 18–25 changing images per virtual second. It excludes the HUD from its motion checks. Inspect the captured images/video as well: image variation alone cannot identify gameplay. The capture filter only removes adjacent identical full-frame images; it never skips a changed image after warmup.

Guest timers, tick counters, UTC and display refresh use the same virtual clock. The emulated display refresh is 60 Hz; this Snakes replay produces about 21.3 changed images per virtual second, consistent with three 64 Hz guest ticks between game updates. Output images therefore need their recorded timestamps for playback, rather than being forced to 60 different game images per second. This verifies emulated time progression, not physical-handset FPS or cycle accuracy.

Interactive browser play paces this clock against host time. If the guest falls
more than 100 ms behind, the pacer rebases its host origin instead of accumulating
a catch-up debt. Guest time, timers and instruction accounting are unchanged;
unpaced replay does not use this branch. Delays above this tolerance delay play
instead of creating seconds of catch-up.

The Linux browser regression suspends only its own renderer processes for
250 ms and 6 seconds, then checks every one-second recovery window as well as
whole-window pacing, input and settled audio. It requires a host capable of
sustaining real-time gameplay (within 2%). A whole-session average alone
cannot detect a stall followed by compensating overspeed.

```sh
cd src/tests/wasm
node pacing.ts /absolute/path/to/assets /absolute/path/to/new-pacing-results
# Optionally test an already served build:
node pacing.ts /absolute/path/to/assets /absolute/path/to/new-lan-results https://claude-laptop.lan:8188/
```

Use the same compiler-policy environment as the launcher being checked; the
test verifies its readback. `EKA2L1_WASM_BUILD_DIR` selects a frozen build when
the test starts its own server. See [PACING_RESULTS.md](PACING_RESULTS.md) for
before/after browser measurements and native replay validation.

## Audio capture

Each run exports `audio.wav` and `audio.jsonl` next to `frames.jsonl`. The WAV
starts at guest time zero, including startup silence, and ends at the final
captured image (rounded down to a 48 kHz sample boundary). The event log records
stream creation, format/rate/channel/volume changes, writes, buffer requests,
and stop/destruction with guest timestamps. The native runner checks audio on
every repeat; `compare.py` requires both audio files, so the older silent baseline
cannot accidentally pass the audio gate.

```sh
python3 src/tests/benchmark/validate_audio.py /absolute/path/to/wasm-run-1
```

Play `audio.wav` in a media player. When combining it with the gameplay frames,
trim the audio at the first frame's `virtual_us` and retain the frame timestamps.
The benchmark runs without host pacing and exports audio for playback afterwards;
it does not turn on real-time speaker playback in either frontend. The existing
interactive audio paths are unchanged. This is a deterministic PCM reference,
not a hardware DSP/filter or codec-fidelity claim.

## Rebuilding the fault probes

Both fault-probe executables are excluded from the default CMake build. Explicitly
build `eka_cpu_fault_native` in `build` and `eka_cpu_fault_wasm` in `build-wasm`
after compiler/runtime changes. Record the source state and executable hashes
before claiming candidate-specific results. See
[the rebuild commands and provenance audit](FAULT_PROBE_REBUILD_AUDIT.md).

## WASM executable-byte policy

WASM and the ordinary browser launcher default to `EKA2L1_UNSAFE_CODE=3`:
loaded executable bytes are trusted until their mapping/image is retired or
replaced. Primary and inlined-dependency byte scans, generated code-write overlap
guards, code-overlap entry-proof checks and mutation tracking are omitted.
This deliberately does not support runtime self-modifying executable code.
Mapping/lifetime handling, address translation, access permissions, faults,
instruction budgets, interrupts and guest scheduling remain unchanged.

Set `EKA2L1_UNSAFE_CODE=0` before startup for mutation-compatible execution and
reference comparisons. Modes 1 (scan removal only) and 2 (guard removal only)
remain available for attribution. The launcher and browser harnesses report the
actual selected mode and reject unsupported or mismatched configuration. Native
execution retains its existing default. Mutation tests explicitly select mode 0;
separate counterexamples document the stale-code behavior of modes 1/2/3.
See `UNSAFE_CODE_RESULTS.md` and `UNSAFE_CODE_ATTRIBUTION_RESULTS.md` for the
historical measurements; their diagnostic-only disposition is superseded by the
user-authorized mode-3 default rollout.
