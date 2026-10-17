# Next Asphalt 3 startup memory burst

After removing the download-response tee, the next large rise on a fresh
stock-5320 Asphalt 3 launch occurs during **device installation**, specifically
between the launcher statuses `Installing device...` and `Uploading SIS...`.
Two separate unprofiled Chromium 153 runs on the same private server measured
whole-browser PSS rising by 415.1 and 423.4 MiB over that phase (about
0.75–0.83 seconds). In the second run, the game renderer accounted for almost
all of the rise: 285.3 to 710.2 MiB PSS. The route subsequently reached
`_gameRunning` without a page error. This is a cold-install effect, not a
measured gameplay cost or a warm-start cost.

In that same run, the first cumulative 200-MiB trigger occurred about
0.64 seconds after installation began. Browser PSS had risen 239.6 MiB since
the first eligible sample; a separate run triggered at 208.8 MiB. The trigger
snapshot identifies the growing backing as WASM linear memory: a mapping with
422.31 MiB virtual capacity and about 358 MiB PSS. At installation end it was
still about 358 MiB resident. A source-profiled capture at a similar point
found 98.2 MiB of **native allocation estimates**, mostly 92.2 MiB of Blink
`ArrayBufferContents`; these samples are not an additive breakdown of PSS.
They include buffers allocated earlier during asset staging. The native sampler
cannot see allocations within WASM linear memory.

The launch path streams the 121.09 MB RPKG input into OPFS, then calls
`eka2l1_install_device`. That calls `install_rpkg`, which reads entries and
writes each extracted file to `/data/drives/z/temp` through WasmFS. A direct
read of the exact RPKG found 8,988 entries carrying 114.65 MiB of file bytes.
The installer then copies the 69.95 MB ROM to `/data/roms`. The first likely
source-level allocation path is `rpkg.cpp:70-129`, especially `wf.write` at
line 121; the ROM copy follows in `install_device.cpp:34`. The measured WASM
growth is larger than these payload totals, so this source inspection does
**not** assign all 415–423 MiB to file contents. WasmFS capacity, transient
copies, memory growth, and allocator reuse remain unseparated inside the WASM
bucket. A diagnostic C++ allocator capture *during this phase* would be needed
for exact suballocation attribution.

After SIS installation, the launcher moves the installed ROM (at least 4 MiB)
and firmware files of at least 64 KiB to OPFS. That happens **after** extraction,
so it cannot prevent this install peak. The whole-browser PSS does not fall by
the size of the peak by the first running frame: in the second run it was
1,197.4 MiB at device-install end and 1,257.3 MiB one second after
`_gameRunning`. Other startup allocations also occur, so this is not evidence
that all extracted file objects remain live. At the running snapshot the
largest V8 sandbox mapping had 365.7 MiB PSS, but it merged beyond the
reported 506.8 MiB WASM capacity; its exact WASM-only residency cannot be
derived from that mapping alone.

The next optimization experiment should route extracted read-only firmware
files directly to OPFS (or at least their large subset) during `install_rpkg`,
and avoid copying the ROM into WasmFS before offload. It needs an install-time
PSS comparison, a warm-launch check, and the same game route before promotion.
The source-level attribution above is a hypothesis to test, not a claim that
this change will save the entire observed peak.

The first smaller remaining asset-phase allocation is separate: the derived
ROM-code cache is read into a roughly 34.5 MB `ArrayBuffer` for SHA-256
verification and staging. During OPFS RPKG upload, Chrome also held roughly
60–90 MiB of sampled `ArrayBuffer` chunks; these are transient and should
not be confused with the larger install-phase WASM rise.

Follow-up: [direct ROM installation results](DIRECT_ROM_INSTALL_RESULTS.md)
measure the ROM leg of this proposed change. The firmware-extraction leg was
not part of that implementation.

Raw captures are under `/home/claude/.scratch/eka-next-burst-20261017`,
`/home/claude/.scratch/eka-device-install-cumulative-20261017`,
`/home/claude/.scratch/eka-device-install-residency-20261017`, and
`/home/claude/.scratch/eka-device-install-phases-20261017`.
The [burst probe](../wasm/memory-burst-probe.mjs) now records launcher status,
the device-install start/end mappings, and the first running snapshot.
