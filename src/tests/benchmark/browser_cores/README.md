# Browser CPU comparison (in progress)

This harness runs original, redistributable ARM32 programs without Symbian,
Palm OS, RISC OS, console firmware, or game assets. It is a CPU experiment, not a
whole-emulator ranking or a prediction of Snakes speed.

## Current adapters

- SkyEmu ARM7 and ARM9: upstream interpreter including pipeline phases, with
  bounded flat-memory callbacks. No display, audio or peripheral scheduler.
- RPCEmu SA110 interpreter: upstream instruction loop and memory access inlines,
  prepopulated direct RAM mappings; unsupported SWI/device/slow-memory paths abort.
  It does not measure MMU page-table walks or the full RISC PC machine.
- Cloudpilot uARM: upstream CPU, MMU (disabled guest translation), RAM dirty-page
  tracking and decoded instruction cache, with the upstream production Binaryen
  direct-dispatch transformation and optimization options. An unused framebuffer
  range is reserved. Palm PACE and display callbacks abort if reached.
- EKA2L1 **generated regions only**: original emitter, policy 7, bounded loops,
  instruction accounting and memory guards. Generated offline by the native
  translator. Browser module compilation is included in initialization, but ARM
  translation and the outer exact code-cache validator/dispatcher are excluded.
  This is a lower-level measurement and must remain labelled separately.
- QEMU and Pebble repository TCI: original prebuilt binaries, bare virt board
  with Cortex-A15, identical ARM32 workload bodies and integer-only C wrapper.
  Results use semihosting host timestamps, separately labelled. Failed starts are
  not timing samples. The downloaded QEMU demo binary's source/build provenance
  must not be inferred from the current source checkout alone.

The x86 v86 and SH4 Flycast adapters below execute equivalent algorithms, not
identical ARM instructions, and form separate comparisons. Voland has no working
guest-code backend at the recorded revision and cannot get a CPU score.

## Workloads and checks

`kernels.S` contains arithmetic/xorshift, indexed RAM read/write, conditional
integer workloads, and a dependent read chain across 64 KiB. Each runs a fixed number of iterations and sets a completion
register, followed by a terminal self-loop. The interpreters are polled every
200 execution steps (SkyEmu pipeline phases are not guest instructions).
Completed state can therefore include at most a bounded amount of terminal-loop
work; PC is intentionally excluded from cross-core equality. Registers R0-R14
and a checksum of all data words (256, or 16,384 for the dependent chain) are compared against an independent JS
reference. This is workload correctness, not a full ISA/callback conformance
suite. Final flags and PC are not claimed as cross-core comparisons.

`run.mjs` retains small-count checks, warmups and every timing observation. It
uses forward and reverse candidate order, one browser page at a time. Setup and
checksumming are outside the timed execution interval. Initialization is recorded
separately. Fresh pages share a browser process, so these are page-cold rather
than guaranteed disk/browser-cache-cold starts. Larger instruction-cache, other memory access patterns, and realistic captured workloads remain necessary before broad claims.

## Reproduce

Place pinned upstream checkouts and `emsdk` in a scratch directory. Source
revisions and built artifact hashes are in the evidence manifest. Install the
Cloudpilot `src/uarm/tools/build-jump-table` npm dependencies, then:

```
python3 src/tests/benchmark/browser_cores/build.py SCRATCH
node src/tests/benchmark/browser_cores/run.mjs SCRATCH/browser-cores REPORT.json
```

`CORE_NAMES=skyemu7,skyemu9,rpcemu,cloudpilot` chooses the adapters;
`CORE_KINDS=4` includes the dependent chain; `CORE_ITERATIONS` changes the fixed work. Puppeteer resolves from the existing
`src/tests/wasm` installation; Chromium is `/usr/bin/chromium`.
The EKA generator links the existing native `libcpu.a`; record its build/source
configuration before using it. Its JS adapter and generated `eka[0-3].wasm`
files must be placed in the scratch output directory.

No changes to the live emulator are made by this harness.

## Separate x86 algorithm port

`nonarm_guest.c` implements the same algorithms in C, compiled for x86 and booted
by v86's multiboot loader, without a BIOS or guest OS. This executes different
instructions and uses v86's full CPU/memory machinery. It must not be pooled with
the ARM instruction comparison. UART marker receipt supplies the timestamps;
the timestamp is captured when the marker prefix arrives, before formatting the
result or calculating the memory hash. First three repetitions remain warmups.

```
clang --target=i386-none-elf -m32 -mno-sse -mno-mmx -fno-vectorize \
  -fno-slp-vectorize -O2 -ffreestanding -fno-builtin -fno-pic -nostdlib \
  -fuse-ld=lld -Wl,--no-pie -Wl,-T,src/tests/benchmark/browser_cores/v86_link.ld \
  src/tests/benchmark/browser_cores/v86_start.S \
  src/tests/benchmark/browser_cores/nonarm_guest.c -o OUTPUT/v86_guest.elf
node src/tests/benchmark/browser_cores/qemu_run.mjs OUTPUT v86 REPORT.json
python3 src/tests/benchmark/browser_cores/check_qemu.py REPORT.json ARM_REFERENCE.json CHECKED.json
```

Place `v86.html`, upstream `libv86.js`, and `v86.wasm` in OUTPUT. The reference
file must contain the same iteration count and all four workloads. The checker
compares algorithm outputs and memory, not x86 architectural registers.

## Separate Flycast SH4 adapter

Upstream Flycast `2c48c018` plus nasomers/flycast-wasm `16e9c7b4` supplies the
actual decoder/SSA, WASM block generator and `c_dispatch_loop`. The original
SH4 loops implement the same algorithms, not the ARM instruction streams.
RAM and context are initialized without a BIOS. All reachable blocks are
compiled during setup. Execution retains the production dispatch checks,
but omits the console frame scheduler, devices, interrupts and adaptive
hot-block/chain promotion. It is a CPU adapter, not the release console app.

The published patch does not apply to `shell/libretro/audiostream.cpp` at the
pinned upstream revision. That frontend-only file was excluded; no audio
frontend is linked into this adapter. Initialize the libchdr, tinygettext and
other configured dependency submodules recursively before CMake configuration.

To reproduce from those pinned scratch checkouts:

1. Apply `wasm-jit-phase1-modified.patch` with
   `git apply --exclude=shell/libretro/audiostream.cpp`.
2. Copy `rec_wasm.cpp`, `wasm_emit.h`, `wasm_module_builder.h` and
   `fly_instrument.h` into `flycast-source/core/rec-wasm/`.
3. Append an absolute `#include` for this folder's `flycast_adapter.inc` to
   `rec_wasm.cpp`. The checked-in `sh4_kernels.h` contains the original assembled
   programs; `build_sh4_kernels.py TOOLCHAIN OUTPUT` regenerates them using a
   privately extracted Debian GNU binutils 2.35.2 SH4 package.
4. Configure `build-cpu-bench` with emcmake, Release, LIBRETRO=ON, USE_GLES=ON,
   C and C++ flags `-DJIT_PROD_BUILD=1 -DFLY_RELEASE_BUILD=1`; build the static
   core archive. The recorded build also sets USE_MODEM/USE_UPNP/
   USE_RACHIEVEMENTS OFF (verify CMake's actual option support before interpreting
   those as enabled/disabled features).
5. `python3 src/tests/benchmark/browser_cores/link_flycast.py SCRATCH`
6. `node src/tests/benchmark/browser_cores/flycast_run.mjs SCRATCH/browser-cores-nonarm ARM_REFERENCE.json REPORT.json`

The adapter checks the five output variables and complete memory hash against
the independent algorithm reference; scratch SH4 registers and PC differ by ISA.
It polls completion between 10,000-cycle dispatch slices, so bounded terminal-loop
work is included. Setup (decode, SSA, module compilation, initialization) is
recorded separately from execution. Initial and warmup observations are retained.

## Full EKA CPU control

`eka_full.cpp` adds the normal `dyncom_core::run` path: demand compilation,
interpreter fallback, validated code-cache lookup, exact byte comparisons and
compiled dispatch. It uses the same flat bounded RAM callbacks as the other
CPU adapters, without Symbian/device scheduling. Runtime choices match the
served policy: original emitter policy 7, folded TLB index, grouped exact scanner.
The build rejects code versions/lifecycle/write protection at compile time.
The first full run includes demand translation/module compilation. Measured runs
reuse compiled code; setup resets registers/data and refills the TLB. Completion
is polled every 10,000 guest instructions, including bounded terminal-loop work.

Configure a **separate** Emscripten build with `EKA_BROWSER_CPU_COMPARISON=ON`,
`EKA2L1_WASM_CODE_VERSIONS=OFF`, `EKA2L1_WASM_CODE_LIFECYCLE=OFF` and
`EKA2L1_WASM_CODE_WRITE_PROTECTION=OFF`; build target `eka_browser_cpu`.
Copy its JS/WASM to a scratch output as `eka_full.js` / `eka_browser_cpu.wasm`
(the loader requests the original WASM basename), then run:

```
CORE_NAMES=eka_full CORE_KINDS=4 node src/tests/benchmark/browser_cores/run.mjs OUTPUT REPORT.json
```

The runner requires actual guest compilation and separately checks an untimed
census of compiled instructions, dispatches and interpreted instructions. An
untimed code-mutation check clears only the interpreter decoded cache, leaves
the AOT cache and mapping generation intact, and verifies changed results and
restoration. This tests rejection of stale compiled code by the exact validator.
These diagnostics are excluded from measured medians. This is still a CPU-kernel comparison,
not a whole-Snakes performance result.
