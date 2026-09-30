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

The source survey also mentioned x86 v86 and SH4 Flycast: these cannot execute
identical ARM instructions. Equivalent-algorithm ports would form a separate
comparison. Voland's previously inspected no-op backend cannot get a CPU score.

## Workloads and checks

`kernels.S` contains arithmetic/xorshift, indexed RAM read/write, and conditional
integer workloads. Each runs a fixed number of iterations and sets a completion
register, followed by a terminal self-loop. The interpreters are polled every
200 execution steps (SkyEmu pipeline phases are not guest instructions).
Completed state can therefore include at most a bounded amount of terminal-loop
work; PC is intentionally excluded from cross-core equality. Registers R0-R14
and a checksum of all 256 data words are compared against an independent JS
reference. This is workload correctness, not a full ISA/callback conformance
suite. Final flags and PC are not claimed as cross-core comparisons.

`run.mjs` retains small-count checks, warmups and every timing observation. It
uses forward and reverse candidate order, one browser page at a time. Setup and
checksumming are outside the timed execution interval. Initialization is recorded
separately. Fresh pages share a browser process, so these are page-cold rather
than guaranteed disk/browser-cache-cold starts. Larger instruction-cache, random
memory, and realistic captured workloads remain necessary before broad claims.

## Reproduce

Place pinned upstream checkouts and `emsdk` in a scratch directory. Source
revisions and built artifact hashes are in the evidence manifest. Install the
Cloudpilot `src/uarm/tools/build-jump-table` npm dependencies, then:

```
python3 src/tests/benchmark/browser_cores/build.py SCRATCH
node src/tests/benchmark/browser_cores/run.mjs SCRATCH/browser-cores REPORT.json
```

`CORE_NAMES=skyemu7,skyemu9,rpcemu,cloudpilot` chooses the adapters;
`CORE_ITERATIONS` changes the fixed work. Puppeteer resolves from the existing
`src/tests/wasm` installation; Chromium is `/usr/bin/chromium`.
The EKA generator links the existing native `libcpu.a`; record its build/source
configuration before using it. Its JS adapter and generated `eka[0-2].wasm`
files must be placed in the scratch output directory.

No changes to the live emulator are made by this harness.
