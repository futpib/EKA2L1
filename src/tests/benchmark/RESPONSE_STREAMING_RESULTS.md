# Streaming built-in installer responses

The game picker currently reads each downloaded ROM/RPKG/SIS response as a
`Blob`, wraps it in a `File`, and later streams large files to temporary OPFS
storage. Two browser-only changes tested whether removing that detour lowers
startup memory:

1. Stream the reopened Cache Storage response directly to OPFS. Keep small SIS
   files in WasmFS through `response.arrayBuffer()`.
2. On a persistent device's cold cache miss, stream the network response
   directly to OPFS without first saving it in Cache Storage. Existing cache
   hits still work. This also gives up offline reuse of a successfully
   downloaded asset if installation fails before the device is saved.

Neither change was retained. Both reached `_gameRunning` in stock-5320
Asphalt 3, but neither showed a startup-time or memory benefit. The second
change made the pre-install memory rise substantially larger. A `Blob` is a
logical whole-body object; these results do not establish that Chrome copies
all of its bytes into resident RAM.

Chrome 153 used fresh browser profiles, the same local assets and the same
WASM binary (`f3990f476d96cea55635c204beb97578298df961367aaa04301a9416c59f8c35`).
The control and candidate ran on separate private servers, with the candidate
HTML overlaid on the release build. The probe followed the real `?game=asphalt-3`
launcher path and sampled all Chromium processes' proportional set size (PSS)
every 100 ms. Pre-install growth is the highest PSS sample between `Loading`
and `Installing device` minus the first sample in that interval. Install-end
PSS is the snapshot at `Uploading SIS`; neither value is an exact allocation
total. Time runs from `Loading` to `_gameRunning`, not to the racing HUD.

| # | Download and staging path | Pre-install peak growth, MiB | Install-end browser PSS, MiB | Loading to running, s | Running WASM capacity, MiB |
|---:|---|---:|---:|---:|---:|
| 1 | Current Blob/File A | 28.6 | 1258.8 | 7.20 | 531.0 |
| 2 | Cached response stream A | 40.5 | 1294.1 | 7.24 | 531.0 |
| 3 | Current Blob/File B | 13.1 | 1259.6 | 7.75 | 531.0 |
| 4 | Cached response stream B | 70.8 | 1288.2 | 7.43 | 531.0 |
| 5 | Current Blob/File C | 13.9 | 1254.5 | 7.32 | 531.0 |
| 6 | Direct network stream A | 105.9 | 1340.1 | 7.38 | 531.0 |
| 7 | Direct network stream B | 109.0 | 1257.5 | 7.37 | 531.0 |

The cached-response stream increased install-end PSS by 35.3 and 28.6 MiB
in its two paired runs. The direct-network stream increased pre-install peak
growth by at least 77.3 MiB relative to the measured control range. Its
install-end PSS varied by 82.6 MiB between two runs, so that snapshot does
not support a stable later-memory conclusion. All seven runs had the same
running WASM capacity. The control itself varied in total startup time, and
no measured candidate speedup stands out from that variation.

Raw captures are under `/home/claude/.scratch/eka-response-stream-{control-a,control-b,candidate-a,candidate-b}`
and `/home/claude/.scratch/eka-response-network-{control-c,candidate-a,candidate-b}`.
The rejected source patch is
`/home/claude/.scratch/eka-response-stream-20261017.patch`. The running `.lan`
service and production launcher were left on the original path.
