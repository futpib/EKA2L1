# Browser RPKG installation: eliminate the whole-file read

The browser installer stages the uploaded RPKG through `File.stream()` into
OPFS. The C++ extractor uses a 1 MiB input buffer and writes extracted entries
in 64 KiB chunks. After extraction, however, the cleanup called
`FS.analyzePath(rpkgPath).exists`. This SDK's WasmFS implementation resolves
`analyzePath` by calling `FS.readFile` for an existing file. That requested the
entire 121,092,775-byte RPKG from OPFS and copied it into browser and WASM
buffers just before deleting it.

Cleanup now calls `FS.unlink` directly and tolerates `ENOENT` (44), because
successful device installation may move the ROM source. This keeps the OPFS
input path bounded without changing extraction or game runtime code. The
non-OPFS fallback still uses `FileReader.readAsArrayBuffer`, and extracted
drive-Z files are still held in WasmFS until the later offload step; those are
separate sources of memory use.

Four cold Asphalt 3 launches used fresh Chromium profiles, identical WASM and
assets, and private local servers. The candidate differed from control only in
`eka2l1.html`. PSS is the sum of the browser process group at the start and
end of the device-install phase. All four runs reached `_gameRunning`.

| # | Run | Install PSS start | Install PSS end | Install rise | Running PSS | Running WASM capacity |
|---:|---|---:|---:|---:|---:|---:|
| 1 | Control | 786.7 MiB | 1268.9 MiB | 482.2 MiB | 1473.7 MiB | 531.0 MiB |
| 2 | Candidate | 798.8 MiB | 942.5 MiB | 143.8 MiB | 1209.5 MiB | 368.8 MiB |
| 3 | Candidate | 788.5 MiB | 939.4 MiB | 150.9 MiB | 1207.5 MiB | 368.8 MiB |
| 4 | Control | 795.0 MiB | 1262.8 MiB | 467.8 MiB | 1471.1 MiB | 531.0 MiB |

An instrumented candidate run saw no large RPKG `Blob.slice` request. The
control trace requested a single slice covering the full RPKG. A separate
fresh Snakes install reached the title screen and produced 110 frames in 15
seconds. These measurements establish a large installation-memory reduction;
they do not establish a startup-speed improvement.
