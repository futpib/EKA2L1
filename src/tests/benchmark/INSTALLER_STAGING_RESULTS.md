# Temporary installer staging and WASM residency

The [arena residency capture](DIRECT_ARENA_RESIDENCY.md) found that pages
formerly used by temporary WasmFS ROM/RPKG/SIS buffers remained resident after
those files were unlinked. The allocator can reuse their addresses, but the
browser does not reclaim dirty WASM linear-memory pages. This experiment
streams large installer inputs to the existing OPFS mount, then removes them
after installation. The ROM and RPKG use OPFS when available; SIS/SISX uses it
at 8 MiB or larger. Smaller packages retain the in-memory upload path. The
threshold separates the sub-3-MiB packages in the current set from the
11–27-MiB packages; it is not a measured optimum.

The browser build also uses a 1 MiB read buffer while extracting an OPFS
RPKG or copying an OPFS ROM into the installed storage. Without those buffers,
OPFS's many small calls made device installation about 20 seconds in one
Asphalt 3 run. A separate 1 MiB buffer in the SIS parser's generic file stream
was rejected: paired first-launch runs made SIS installation slower, about
2.6–2.7 seconds versus 1.8–1.9 seconds without it.

## Gameplay memory

Chromium 153 ran transient installs and five-second unpaced gameplay captures
using `arena-growth-benchmark.mjs`. The same control build was served at
`127.0.0.1:18888` (WASM SHA-256 `490322b0528ec2645253785e53147ae98b5029a4d4363a3b2744fbbf77766b7d`);
the final candidate was served at `127.0.0.1:18894` (WASM SHA-256
`8812ae4b5646ff32ca5d50d683c02aae3388dfbfbec3d0572f2ae0801534d592`).
Resident-memory figures are MiB of proportional set size from the browser
processes' `/proc/*/smaps` at the gameplay capture. WASM PSS is the backing
mapping within that total. All listed routes reached gameplay and reported no
guest errors.

| # | Game and capture | WASM PSS | Whole-browser PSS | Five-second FPS |
|---:|---|---:|---:|---:|
| 1 | Snakes, control | 578.0 | 1722.3 | 19.2 |
| 2 | Snakes, final staging | 455.7 | 1595.8 | 51.8 |
| 3 | Asphalt 3, control A | 820.6 | 2706.3 | 46.7 |
| 4 | Asphalt 3, final staging A | 786.3 | 2749.1 | 58.6 |
| 5 | Asphalt 3, control B | 904.0 | 2800.3 | 73.4 |
| 6 | Asphalt 3, final staging B | 823.7 | 2726.7 | 84.4 |

Snakes saved 122.3 MiB of resident WASM and 126.5 MiB across the browser at
the measured scene. The paired Asphalt captures saved 34.3 and 80.3 MiB of
resident WASM. Its whole-browser delta changed sign between pairs (+42.8 and
-73.6 MiB), so the data do not establish a reliable whole-browser saving for
that game. The control itself varied by 83.4 MiB of WASM PSS across two runs.
The FPS values also vary with host load and compilation; they establish that
the measured candidate reached active gameplay, not a throughput speedup.

Screening the staging choices showed why the final route is selective. RPKG
alone saved about 101 MiB of WASM PSS in Snakes but only 14 MiB in one
Asphalt capture. ROM+RPKG saved about 122 MiB in Snakes and 22 MiB in one
Asphalt capture. Staging all three saved about 123 MiB in Snakes and 99 MiB
in one Asphalt capture, but staging Snakes's 0.92-MiB SIS coincided with a
large cold-start delay. The final route stages the 11.77-MiB N-Gage runtime
SISX used by Asphalt 3 while keeping Snakes's small SIS in memory. These
single screening captures are not sufficient to attribute the variable
Asphalt PSS to one installer file.

## Startup and cleanup

Fresh transient first launches show a startup cost. Short, serial Chromium
launches measured the interval from `Initializing...` to `Running` and the
two installation phases for the Asphalt 3 N-Gage runtime:

| # | Build | Device install, s | SIS install, s | Initializing to Running, s |
|---:|---|---:|---:|---:|
| 1 | Control A | 0.30 | 0.61 | 1.36 |
| 2 | Control B | 0.30 | 0.61 | 1.36 |
| 3 | Final staging A | 0.73 | 1.90 | 3.43 |
| 4 | Final staging B | 0.71 | 1.84 | 3.36 |

The OPFS first-install path costs roughly two seconds here. It does not run
on a launch that reuses an already installed device/package. A direct browser
check created a 1 MiB staged ROM, confirmed its exact size, unlinked it, and
confirmed no `eka2l1-stage-` file remained. It also confirmed the small-SIS
fallback uses `/tmp`. Files left by a browser crash during installation are
not yet scavenged.

Full captures and the frozen browser bundles are under
`/home/claude/.scratch/eka-staging-experiment/`. The public `.lan` build was
left unchanged while evaluating this memory/startup tradeoff. The Firefox
path falls back to WasmFS because this repository does not mount OPFS there;
the memory result was measured only on Chromium.
