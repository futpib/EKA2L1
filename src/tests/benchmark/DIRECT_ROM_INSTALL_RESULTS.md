# Direct ROM installation

The browser launcher stages the ROM as a disposable input. With OPFS mounted,
`eka2l1_install_device` now moves that input directly to the device's durable
OPFS ROM path and creates the installed symlink. This removes the full-size
WasmFS ROM copy and the later OPFS copy. An input staged in WasmFS can still
take this path: the move streams across filesystems. Without OPFS, a browser
input moves into the installed WasmFS path instead of being copied. Native
installers retain their source-copy behavior because those paths name users'
original files.

The matched Chrome 153 comparison used the final WASM binary (SHA-256
`f3990f476d96cea55635c204beb97578298df961367aaa04301a9416c59f8c35`)
and a test-only override that sent the ROM through WasmFS before the existing
offload. Each run used a fresh persistent profile and cold-installed
stock-5320 Asphalt 3 from the same local server. PSS is the sum of Chromium
processes at the named phase; WASM capacity is
`HEAPU8.buffer.byteLength` at the running snapshot. Numbers are MiB except
install time. Runs 3–4 reverse the order of the first pair. All reached
`_gameRunning`. The running snapshot followed five seconds of gameplay and a
requested renderer GC. These are phase snapshots, not sampled peak-memory
claims.

| # | Run | Direct ROM | Install time (s) | Install-end browser PSS | Running WASM capacity | Running browser PSS |
|---:|---|---|---:|---:|---:|---:|
| 1 | Pair 1 | On | 0.878 | 1278.6 | 531.0 | 1635.6 |
| 2 | Pair 1 | Off | 0.963 | 1360.5 | 603.1 | 1713.8 |
| 3 | Pair 2 | Off | 1.306 | 1355.7 | 603.1 | 1681.5 |
| 4 | Pair 2 | On | 0.970 | 1277.0 | 531.0 | 1632.2 |

Direct installation reduced install-end browser PSS by **81.9 and 78.7
MiB** in the two pairs and running WASM capacity by **72.1 MiB** in both.
The install phase was faster in both pairs, but only two pairs were measured.
Running whole-browser PSS was 49–78 MiB lower in these pairs; earlier
pre-graduation captures varied in both directions, so the firm running-memory
result is the WASM capacity reduction. The launcher can leave a large RPKG
browser Blob resident, and renderer GC and browser mappings vary independently
of WASM capacity.

Chrome E2E checks reached Asphalt 3 gameplay with a 69,947,392-byte installed
ROM symlink to the namespaced OPFS target. A Snakes cold install and reload
both reached gameplay; the restored manifest kept the ROM offloaded and its
link resolved to the same-size file. Separate Snakes checks forced the ROM
source into `/tmp` WasmFS. With an OPFS target, the game ran with an installed
OPFS symlink; with the target disabled, it ran with a regular WasmFS ROM.
In both cases the disposable `/tmp` source was gone and the installed file was
69,947,392 bytes. These forced checks exercise the two source/target branches;
they are not a Firefox graphics test. The persistent-disk fixture passed in
Firefox with `OPFS=false`; the full headless Firefox launcher did not reach
gameplay because WebGL initialization aborted before game execution. The
test-only override is
`EKA2L1_BURST_DISABLE_DIRECT_ROM=1` in
[`memory-burst-probe.mjs`](../wasm/memory-burst-probe.mjs); it does not expose a
normal-launch mode. Raw captures are under
`/home/claude/.scratch/eka-rom-direct-final-{enabled1,disabled1,disabled2,enabled2}`.
