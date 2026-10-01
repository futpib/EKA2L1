# Nokia N80 firmware assets

The N80 ROM and RPKG use the existing individual-file IPFS convention. Their
CIDs, SHA-256 hashes, sizes and source archive are recorded in
[n80-assets.json](n80-assets.json). The default benchmark still uses the Nokia
5320 assets.

The pair installs successfully as **Nokia N80 (05), RM-92, Symbian 9.1** with
machine UID `0x200005f9`. Its original `wsini.ini` declares 352×416 portrait and
416×352 landscape modes. The tested Snakes SIS **does not reach gameplay in the
tested emulator build**: it encounters unimplemented system calls and panics.
These assets are available for compatibility work, not a passing N80 benchmark.

## Pin and retrieve

```sh
ipfs pin add bafybeich2rmx24qbgshmdf3pufazf2wujd5233sqgxkheh7c5ms4xm6edi
ipfs pin add bafybeihwio3n2bekh73lco4mo4mxt55iyryx6jzzn3fkpfiusdhih4s3em
mkdir n80-assets
ipfs cat bafybeich2rmx24qbgshmdf3pufazf2wujd5233sqgxkheh7c5ms4xm6edi > n80-assets/SYM.ROM
ipfs cat bafybeihwio3n2bekh73lco4mo4mxt55iyryx6jzzn3fkpfiusdhih4s3em > n80-assets/SYM.RPKG
ipfs cat bafybeicuomcc2zhzi3vwfb5xihnlkikz3biaa43g4d22z2wptcmhvmp3di > n80-assets/Snakes.sis
```

Both new blobs were recursively pinned locally and read back through `ipfs cat`
with matching SHA-256 hashes. This verifies the local IPFS objects; a remote
node's successful retrieval is a separate check.

## Reproduce the blobs

```sh
ipfs cat bafybeic6uqroqcxbqqrez5f7jjpowrrw7pp2lsylbzxr4mncvdker5i3tu > n80-firmware.zip
python3 src/tests/benchmark/extract_n80.py n80-firmware.zip reproduced-n80
```

The converter requires the exact archive hash, checks every ZIP member's CRC,
and checks both final output hashes. It only handles this firmware revision.
It needs Python's standard library; no emulator build or external unpacker is
required. The archive was downloaded from
[Android Data Host](https://androiddatahost.com/tryu5), linked by
[FirmwareFile](https://firmwarefile.com/nokia-n80-rm-92).

The source ZIP contains several product variants. Its included VPL files all
reference the same core, V05 language image and U01 user area. The selected
`RM92_0526969_5.0719.0.2_001.vpl` describes "RUSSIA Smooth Stainless"; its
referenced files passed the VPL CRC checks. The converter uses the core and V05
image, leaving the factory C: user area out of the fresh emulator profile.

The extraction procedure follows EKA2L1's ROM/ROFS layouts and the
[official RPKG packer](https://github.com/EKA2L1/rpkgmaker) format:

1. Read the old FPSX blocks using their exact lengths. The stock importer in the
   tested build assumes 512-byte padding, loses alignment and misclassifies the
   ROFx image as a FAT user area.
2. Select application-processor data. The `SOS*CORE` certificate at flash address
   `0x420000` contains the beginning of the raw DEFLATE stream at offset `0x3d0`.
   Append its contiguous data blocks starting at `0x420400`. Decompression reaches
   its end marker with 445 trailing `0xff` padding bytes. Remove the 48-byte zero
   prefix to obtain the ROM, whose stored uncompressed size is 21,217,280 bytes.
3. Extract 969 ROM entries and 2,333 base ROFS entries. Apply 3,518 V05 entries
   and resolve its 2,333 references to unchanged base ROFS files. After overlays,
   the Z: filesystem contains 6,819 files. All file and directory reads are
   checked against their source image bounds.
4. Write an RPK2 header with the N80 machine UID and sorted lowercase UTF-16LE
   paths. Use read-only/archive attributes and the base ROFS build timestamp for
   deterministic package metadata. ROM and filesystem content bytes are preserved.

The repository converter independently reproduced the same ROM and RPKG hashes
as the initial extraction and packaging.

## Emulator validation, 2026-10-01

The native executable was frozen from an existing WASM-development checkout:
embedded version `wasm-port-25792310e`, SHA-256
`268ca713af03daf982a8f2583393d588461e4f2e83cfff0127d340b2d198ab56`.
It was not built from the documentation commit on master. Runs used fresh device
profiles, Xvfb and Mesa software OpenGL, with the existing deterministic input
replay and unchanged Snakes SIS.

```sh
python3 src/tests/benchmark/run_native.py \
  --assets /absolute/path/to/n80-assets \
  --asset-manifest src/tests/benchmark/n80-assets.json \
  --binary /absolute/path/to/eka2l1_qt \
  --output /absolute/path/to/new-n80-run \
  --frames 60 --repeat 1 --timeout 180
```

Device installation succeeds. The ordinary replay captures zero gameplay frames
before failure. A diagnostic run with `--start-us 0 --all-presentations` captured
one 352×416 startup image at virtual time 149,994 µs, then encountered the same
failure. The image contains a partial background, not gameplay.

The relevant log sequence is preserved in
[n80-evidence/failure.txt](n80-evidence/failure.txt). It reports a bad screen index,
unimplemented system calls `0x82` and `0xA`, and guest `USER-EXEC 3`, followed by a
host segmentation fault. These observations do not establish which emulator
defect causes the guest panic. The result neither proves nor disproves the
game's native 352×416 support on an actual N80.

The diagnostic [startup image](n80-evidence/startup-352x416.png) and
[validation metadata](n80-evidence/validation.json) distinguish successful
installation/display initialization from the failed gameplay test.

After adding manifest selection, the unchanged default 5320 runner passed a
fresh 60-frame gameplay capture and the gameplay-motion and audio validators.
The N80 command above reproduced the installation success and guest panic.
