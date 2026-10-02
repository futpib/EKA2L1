# Direct memory accesses in compiled Thumb code

Sky Force Stage 1 exposed a shared compiler gap: compiled Thumb loads and stores
always used imported memory callbacks, flushing and reloading the register cache
around each access. ARM regions already have direct TLB paths. This experiment
adds guarded direct accesses to bounded Thumb code with register caching, in both
eager ROM and hot application translations. It contains no game names, addresses,
ROM fingerprints, timing shortcuts or changes to guest instruction accounting.

`EKA2L1_THUMB_MEMORY=1` selects the experiment in the browser test harnesses and
launcher. The runtime APIs configure/report the setting before initialization;
configuration after initialization is rejected. The default is currently off.

Each access independently checks the configured TLB index, appropriate read/write
tag, a nonzero host page, nonzero guest page, natural alignment and little-endian
state. Aligned scalar accesses cannot cross a page. A failed check uses the
existing callback and register-cache barriers. No page proof is retained across
callbacks or remappings. Signed loads keep their original sign-extension logic.

Direct stores currently require executable-byte mode 3, which already omits
mutation tracking and overlap guards. Modes 0/1/2 keep their original store
callbacks; direct reads remain eligible. This introduces no new executable-byte
assumption. Translation bounds, RAM stop-after-store behavior, budgets and guest
scheduling remain unchanged.

A new production-runner/native probe found an existing Thumb PUSH ordering bug:
the compiled path wrote SP before its stores, so exception callbacks saw the new
SP. Both the old and experimental paths failed the same 56 of 720 callback-state
comparisons, although all final registers, instruction counts and memory matched.
The correction publishes SP after the stores, matching the native core. The
original failed results are retained separately from the corrected build.

Validation includes a full compiler suite, permission/alignment/endian/budget
comparisons against the original callback path, callbacks which change registers
and TLB mappings, and native comparisons of actual read/write fault callbacks.
The latter cover scalar accesses, LDM/STM, PUSH/POP, repaired/failed/stopped/retry
fault policies, page crossings, read-only/read-write mappings and both TLB layouts.

The Sky Force harness uses `EKA2L1_APP_UID=0xa020d913`,
`EKA2L1_ASSET_MANIFEST=../benchmark/sky-force-assets.json` and
`../benchmark/sky-force.input`. For compatibility with the existing asset runner,
the unchanged Sky Force SIS occupies the fixture filename `Snakes.sis`; its
manifest checks the actual Sky Force digest. The normal launcher uses
`SkyForce.sis`. Use shared audio on both native and browser replays. The initial
reference is 240 unique images from guest second 28, with 1,703,464 stereo PCM
frames through guest microsecond 35,488,839.

Promotion requires exact replay, repeatable unsampled Sky Force improvement,
and preserved Snakes realtime on both established routes. Reaching realtime
Sky Force remains the task target; a modest intermediate gain is insufficient.
No per-game policy is planned. These games are regression workloads, not evidence
of universal compatibility or speed on every Symbian title.

## Verified opt-in checkpoint

The frozen v2 runtime (`548fe0ca861a1a2898aa51d1c676961301406752f3ec68225b9bb4404758394d`)
passes all 177 compiler tests, including 9,218 new direct/callback comparisons.
All 2,880 native/WASM Thumb fault cases match every recorded field. Sky Force
control, candidate and interpreter-checked candidate each match all 240 native
images, guest records and 1,703,464 PCM frames. The candidate also matches the
standard Snakes 1,600 images / 4,656,051 PCM frames and longer route 360 images /
2,832,756 PCM frames. The existing known crash-repro XFAIL is unchanged.

`THUMB_MEMORY_EVIDENCE.json` records the frozen source patch and binary hashes,
full suite output, fault summaries, replay reports, frontend checks, failure
history and fixed timing schedule. These acceptance jobs overlapped, so their
wall times are not performance evidence. The subsequent 24-observation serial
panel uses shared audio, the physical GPU, rendering without capture, no CPU
sampling or detailed counters, and both orders on all three routes. Neither
the initial no-shared-audio screens nor the instrumented diagnosis is pooled
with those timings. Performance and normal live acceptance are still pending;
the experiment remains off and the served archive remains untouched.
