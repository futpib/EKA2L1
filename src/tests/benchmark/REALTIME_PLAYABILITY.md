# Realtime Snakes gameplay in the browser

Verified on 2026-09-27 with Chrome 150.0.7871.186, Intel Core i7-10875H and physical NVIDIA Quadro T1000 Max-Q. This establishes playable realtime for the tested Snakes route on this desktop. It does not establish uniform frame timing, all-game coverage or mobile-device performance. Sound remains off; audio-quality work is deferred.

## Sustained live checks

These are separate, isolated runs through the real browser UI, with periodic visible-scene screenshots included in elapsed time. No profiling counters or CPU sampling were enabled. Automatic startup uses the same Start function as manual upload/start, including live CPU configuration, worker-owned WebGL initialization, keyboard/touch activation and host pacing around the unchanged guest clock.

| Route | Guest seconds | Host seconds | Mean realtime ratio | Worst ten-sample interval | Peak temporary lag |
| --- | ---: | ---: | ---: | ---: | ---: |
| Preloaded automatic startup | 120.426731 | 120.452241 | 0.999788x | 0.974727x | 0.255s |
| Manual upload and Start | 120.531047 | 120.516908 | 1.000117x | 0.966325x | 0.332s |

Both endpoint differences are below one changing game frame. Temporary slowdowns recover; perfectly locked timing is not claimed. Lag is relative to measurement-start alignment. The slowest individual one-second samples are 0.9355x and 0.9335x. The separate fixed 78–96 guest-second replay runs at 1.0336x mean throughput without pacing, capture or diagnostics; see `EXIT_GUARD_RESULTS.md` for paired controls.

Both runs pass keyboard/touch delivery, narrow layout, blur release, changing visible gameplay and shutdown checks. Retained screenshots show active Level 1 gameplay throughout the route. Keydown-to-guest-queue delivery ranges from 0.68 to 7.25ms across these tests; this is **not** input-to-display latency. Touch was automated in desktop Chromium, not tested on a physical phone.

## Correctness and native compatibility

Two final browser runs match native across all **1,600 distinct gameplay images**, every guest timestamp/instruction record, and PCM/audio events. The endpoint is **102.484363 guest seconds / 16,261,337,499 guest instructions**. One replay also checks every 1,024th compiled invocation against the interpreter. All **131 WASM tests**, all **three native CTest targets**, and all **seven frontend smoke checks** pass. These results apply to the tested binary hash recorded in the evidence, not every possible game or device.

The final CPU steps inline distant small ARM leaf helpers into validated regions, then avoid redundant exit checks after pure instructions. Every instruction-budget check remains. Mapping generations and exact code-byte checks cover all inlined ranges; memory/helper paths and code-write aliases preserve exits. SIMD grouping was measured and rejected because it regressed speed.

Normal Qt/Dynarmic startup also passes after removing its unused WASM AOT initialization. It reaches actual gameplay and exits cleanly after the native capture workflow. That Xvfb/software-GL run is a compatibility check, not a same-GPU Qt/browser speed comparison.

## Launch

From the single checkout on `wasm-port`:

```sh
cd ~/code/EKA2L1/src/tests/wasm
npm run serve -- 8188
```

The actual `serve.ts 8188` entry point was also exercised through startup, gameplay, keyboard input and shutdown with no page/HTTP/request errors.

Open `http://127.0.0.1:8188/`. The existing launcher uses its cached ROM/RPKG/SIS assets, fetching its configured CIDs when absent, and starts Snakes. Use Enter to select and arrows/WASD to move; touch controls appear below the canvas. No benchmark environment flags are required for live play. A plain hosted build also supports manual file upload and Start.

Reproduce the live tests (assets directory contains SYM.ROM, SYM.RPKG and Snakes.sis):

```sh
node live.ts ASSETS NEW_OUTPUT 120
EKA2L1_LIVE_AUTOSTART=1 node live.ts ASSETS OTHER_NEW_OUTPUT 120
```

Raw per-second samples, binary hashes, replay comparisons and the Qt workflow record are in `REALTIME_PLAYABILITY_EVIDENCE.json`. Local screenshots/logs are under `/home/claude/.scratch/eka-benchmark/exit-live-*`, `exit-extended1600*`, and `qt-jit-fixed`. All changes are local; nothing was pushed.

### LAN HTTPS launcher

The launcher accepts `EKA2L1_SERVE_HOST` (bind address), `EKA2L1_SERVE_NAME`
(displayed hostname), and paired `EKA2L1_TLS_CERT` / `EKA2L1_TLS_KEY` PEM paths.
Defaults remain HTTP on loopback for local use and automated tests. LAN access
requires HTTPS with a certificate trusted by the client: plain HTTP `.lan`
is not a secure context and cannot run the SharedArrayBuffer worker build.

Current machine configuration:

```sh
cd ~/code/EKA2L1/src/tests/wasm
EKA2L1_SERVE_HOST=:: EKA2L1_SERVE_NAME=claude-laptop.lan \
EKA2L1_TLS_CERT="$HOME/.config/eka2l1/tls/server.crt" \
EKA2L1_TLS_KEY="$HOME/.config/eka2l1/tls/server.key" \
npm run serve -- 8188
```

Open `https://claude-laptop.lan:8188/`. The wildcard listener accepts IPv4 and
IPv6 on this Linux host. Its private keys are outside the repository, mode 0600.
The public CA certificate is `~/.config/eka2l1/tls/lan-ca.crt`; install/trust it
on each client device before opening the page. It is installed in this user's
local NSS browser trust stores as `EKA2L1-LAN-2026`. Other devices do not inherit
that trust. The certificate expires in September 2027. No emulator rebuild is
needed to change the listener or renew certificates.

LAN validation on 2026-09-27: Chromium opened the actual `.lan` HTTPS URL
without certificate-bypass flags, reported secure context / cross-origin
isolation / SharedArrayBuffer availability, and reached visible Snakes gameplay.
Keyboard input, 390px layout and shutdown passed without page/request/HTTP
errors. IPv4 and hostname-resolved IPv6 returned HTTP 200 with COOP/COEP;
traversal returned 403. Loopback defaults, occupied-port rejection and all seven
frontend smoke checks passed. Artifacts: `.scratch/eka-benchmark/lan-check.json`,
`lan-ready.png`, `lan-mobile.png` under `/home/claude`. This verifies the LAN
interface from this host; a separate physical client was not available for testing.
