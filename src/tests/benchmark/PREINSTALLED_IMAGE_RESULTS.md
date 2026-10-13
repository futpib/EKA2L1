# Hooked On preinstalled filesystem pilot

Hooked On reaches actual Costa Rica gameplay in **102.31 -> 66.80 seconds**
through the normal paced browser launcher: **35.51 seconds saved, 34.71% less
wall time**. This is one accepted fresh-browser comparison per path plus a
deployed image repeat on the same laptop, not a statistical campaign or a
prediction for every device.

The launcher restores installed files before C++ creates its device and package
managers. It skips ROM/RPKG extraction, N-Gage runtime SIS installation and the
guest N-Gage importer. Game launch, logos, language/profile setup and runtime
ARM-to-WASM compilation remain. Each session restores fresh writable files.
There is no CPU/kernel/browser checkpoint and no saved generated WASM module.

## Measurements

Chrome/153.0.8010.52, Intel i7-10875H, hardware ANGLE/Vulkan. Runs are serialized,
unprofiled and use fresh browser HTTP/CacheStorage and guest filesystems. Server
assets are already downloaded; timings include LAN downloads, not retrieval
from IPFS. The endpoint is a reviewed boat scene after dismissing the welcome
message, followed by movement and a ten-second paced presentation window.
OCR-guided input/screenshot polling remains in the startup wall measurement.

| # | Path | Wall seconds to boat gameplay | Paced FPS | Guest seconds / host second |
| ---: | --- | ---: | ---: | ---: |
| 1 | Normal installation | 102.314 | 31.975 | 1.000010 |
| 2 | Preinstalled files | 66.799 | 31.880 | 1.000007 |
| 3 | Deployed image repeat on `.lan` | 65.677 | 31.546 | 1.000061 |

The normal installer exits successfully at 51.63 seconds. Its completion
button launches the game directly; the image instead starts from the client
home screen. Those earlier endpoints are not presented as interchangeable
gameplay measurements. The final boat scene is the shared endpoint.

Unpaced checks use the existing route's stationary Costa Rica boat with the
anchor introduction completed and help dismissed. Both windows run
continuously with the same 1,800-second guest deadline; there are no route
pauses inside the measurement window.

| # | Path | Unpaced FPS | Window seconds | New compiled functions |
| ---: | --- | ---: | ---: | ---: |
| 1 | Normal installation | 236.134 | 10.126 | 6 |
| 2 | Preinstalled files | 235.384 | 10.115 | 0 |

The image observation is 0.32% lower. These single short windows establish no
runtime gain or repeatable regression. Both paths retain zero instruction
totals and report no guest/page errors. The runtime WASM bytes are identical:
`176bef9bbb8f0899f8538b60978a0674a30e95b9cec32ddb015dc1cd10cd39ca`.
The candidate freezes that measured binary and uses the updated shell/loader;
no native/emulated execution policy or compiler code changes in this pilot.

## Artifact and correctness

The clean image contains **9,828 files**, **230,406,209 unpacked bytes**, and
**133,970,683 compressed bytes**. The original ROM/RPKG/runtime/game-container
downloads total **231,575,582 bytes**, excluding the common frontend. The image
therefore saves about 97.60 MB of those downloads too. The comparison does not
exercise a bandwidth-limited link.

Capture runs the stock Nokia 5320 device and the existing Hooked On 0.74 BiNPDA
package through installation. The CPU is parked at a scheduler boundary, the
successful installer exit and completion screen are retained, and shutdown
closes file handles and saves registries before `/data` is read. No game is
launched or player save created during capture. Source SHA-256 checks bind the
image to the exact ROM, RPKG, runtime SIS and `.n-gage` container in the launcher.

The format is gzip around a four-byte little-endian JSON-header length, the
UTF-8 header, and file contents in manifest order. Directories and empty files
are preserved. The browser streams decompression and explicitly truncates each
file before using the SDK's bulk writes. The server verifies the compressed
artifact hash at startup. Schema/source/version mismatches, duplicate paths,
path traversal, missing parent directories, excessive sizes, truncation and
trailing bytes are rejected. Failure prevents emulator initialization.

`installed-image.test.ts` checks source identity independent of property order,
exact fixture bytes, empty files, replacement of modified writable files,
invalid manifests/paths/payload lengths and server artifact integrity. Existing
compiler-policy and asset-cache suites pass. Actual browser runs reach
gameplay in both paths; all accepted screenshots are retained for review.

Two rejected paced attempts are retained and excluded: full-screen OCR missed
the language chooser in the first image run; the first installer run incorrectly
waited for the client home screen after the completion button had already
launched the game. The corrected harness thresholds blue menu text separately
and handles that direct launch. Neither failure was an image-restoration or
guest fault.

## Deployment and reproduction

The generated image and metadata stay outside Git:
`/home/claude/.local/share/eka2l1/preinstalled/hooked-on.img.gz` and
`hooked-on.json`. Image SHA-256:
`f8120773f69d7337c2f56a7fbcb947b33b4b60db0b20348444a514f41c75f719`.
No firmware, SIS, game container or image bytes are committed.

The `.lan` service sets `EKA2L1_PREINSTALLED_DIR` to that directory and serves
the frozen measured runtime with the updated frontend. Only Hooked On has an
image; other games retain their installation paths. Source/metadata/hash
mismatches fail server startup rather than silently serving stale images.

A fresh browser against that public endpoint reaches the same boat scene in
65.68 seconds and runs at 31.55 paced FPS with guest time at 1x. Downloaded
runtime bytes match the measured WASM hash. The updated `startup.mjs` also
recognizes image restoration instead of awaiting an installer that will not
run; its earlier nontrivial-screen endpoint can still be the N-Gage client.
An unprepared Snakes launch shows its Start Game menu, 191 presentations,
zero instruction totals and no guest/page errors. The private preview is
stopped and its listener removed. CMake configuration copies the loader into
the normal build output; that file matches the tested source exactly.

```sh
node src/tests/wasm/capture-installed-image.mjs https://claude-laptop.lan:8188/ hooked-on /absolute/path/to/new-capture
# The capture tool uses custom loading, so it always installs fresh source files.
EKA2L1_PREINSTALLED_DIR=/absolute/path/to/new-capture node src/tests/wasm/serve.ts 8188
node src/tests/wasm/installed-image.test.ts
node src/tests/wasm/compiler-policy.test.ts
node src/tests/wasm/asset-cache.test.ts
```

Raw evidence, source capture, timing/OCR and unpaced scripts, screenshots,
logs, reports and exact build artifacts:
`/home/claude/.scratch/eka-preinstalled-hooked-20261013`.
`summary.json` records accepted timings and rejected attempts. The normal
installation control is `paced-baseline-b/report.json`, the private image
comparison is `paced-candidate-b/report.json`; continuous unpaced reports are
under `runtime-baseline-a` and `runtime-candidate-a`.
