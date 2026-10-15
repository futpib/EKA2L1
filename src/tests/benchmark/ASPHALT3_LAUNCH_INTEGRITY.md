# Asphalt 3 launch validation failures

2026-10-15. Two inconsistent artifacts in the known BodyZ 1.2.7 preservation
installation cause deterministic guest validation failures: modified executable
code, and a plain archive carrying an encrypted-file suffix. The compatibility
repair restores the official code in memory and corrects that archive name
before the game executes. Exact complete-content fingerprints restrict both
changes. Dependency CIDs are unchanged; no game binaries are tracked in Git.

## Failure chain

The visible N-Gage `-1000002` is a generic startup failure. Captures identify an
earlier validation rejection. Subsequent SDK operations return `-1000007` even
when a read has copied the requested bytes and set the correct output count.
The game treats that read as failed and does not advance its resource cursor.

One path ignores the failed font-header read, interprets stale stack data as a
signed glyph count, requests `0xFFFFB0C0` bytes and throws an integer exception.
Another repeatedly reads the start of `radarmph.rle` instead of its palette and
compressed pixel stream. The decoder then overwrites heap cell metadata and
panics with `USER 44`. The original RLE asset has exactly 102 decoded pixels for
its 17-by-6 image. The overflow is downstream of the invalid read result.

A whole launch does not provide a deterministic replay: execution uses an
external watchdog, and the guest has its own stateful validation machinery.
The exact source of the earlier run-to-run path variation was not established.
It is not used to justify the repair. Each captured rejection is reproducible
from fixed guest inputs in an independent ARM engine; a successful retry alone
was never sufficient evidence of a complete fix.

## Modified executable

The installed BodyZ executable has 23 changed code bytes relative to the
recovered Nokia-signed official 1.2.7 executable. These are seven ARM words:

| # | Code offset | Preserved word | Official word |
| --- | --- | --- | --- |
| 1 | `0x3F24` | `E3A03D05` | `E593F02C` |
| 2 | `0x3F28` | `E2833001` | `E3A03021` |
| 3 | `0x5654` | `EA000005` | `0A000005` |
| 4 | `0xDF0B4` | `E3A03020` | `E51B3040` |
| 5 | `0xDF30C` | `E51B2018` | `E2433352` |
| 6 | `0xDF310` | `E3A03000` | `E24338F9` |
| 7 | `0xDF314` | `E0623003` | `E2433B02` |

The first three instructions occur in the captured integrity input: 28 ranges,
16,220 bytes. The SDK's SHA-1 matches Python's SHA-1 for these bytes. Independent
ARM replay reproduces the failing SDK result `B1E2A993`. Restoring those three
instructions in the same captured state returns SDK success `B1E2A95F`.
An additional 47 comparisons of surrounding guest arithmetic found no register,
flag or written-memory differences between the browser translation and Unicorn.

The partial repair did not fix the complete launch. Restoring all seven words
makes the complete code section byte-identical to the official code, but the
mislabeled archive still triggers a second rejection. Restoring only executable
bytes in the installed file did not fix that second problem either.

The code repair requires UID `0x2000AFE3`, code length `0x160C30`, uncompressed
input, valid bounds and whole-code XXH64 seed-0 `7A9ED46E51197171`. Official,
changed, truncated, differently identified and compressed images are excluded.
The source code SHA-256 is
`6572f68013caaed3877fc9a9bfe335d89aaf1818b6ff5a15e59c157f8e7e23f9`.
The repaired/official code SHA-256 is
`897101abf15158524415f8c157bd136d7b5f2db707ae24ca882a6e216eb28b68`.
The installed executable and its package metadata are not rewritten.

## Mislabeled plain archive

`C:\Private\2000AFE3\A3\trackobj.bar.ngdat` contains a plain BAR archive,
196,585 bytes long. Its first eight bytes are `62 0B 00 00 7F F4 02 00`.
Its SHA-256 is
`47f1eb4253d2c7ce1578b4ec3e438ee007bb50f5f74f69b129591edd645448e4`.

The first failing game-side flag write occurs at `0x74EDEED4`, immediately after
the SDK file-metadata callback selected by operation 5. Callback arguments include
the UTF-16 filename and an output pointer; this was initially mistaken for a
seek operation before the arguments were inspected. The callback returns `0x72`
instead of normal success `0x20`, and the game changes its validation flag from
`84E7786A` to `84E77872`. The SDK code-hash flag stays unchanged in this capture.

Independent ARM replay, with modeled OS calls and the installed file bytes,
reproduces that callback result with identical output registers. Using the same
archive bytes under the name `trackobj.bar` changes only R0 to success `0x20`.
The following game-side arithmetic also matches independent ARM execution
exactly. This is a second input-level counterexample, rather than evidence that
the caller's translation is incorrect.

The loader corrects the name only after recognizing the exact preserved
executable, and only when the destination does not exist and the complete
archive length and XXH64 seed-0 `591C3C6B792964E2` match. It closes its read handle
before renaming. Bytes are preserved, existing files are never overwritten, and
subsequent launches leave the repaired name alone. There is no guest hot-path
work and no change to general SDK validation.

## Validation

The native tests passed 42 assertions across three cases, including complete
code equality with the official executable, refusal of other images, byte
preservation on rename, refusal of damaged resources, idempotence and protection
of an existing destination. Native CPU tests passed 915 assertions in 37 cases.
The WASM suite passed 196 checks, and the compiled-only follow-up passed its
24 CPU checks plus cache, dispatch and callback cases.

Production candidate 106 passed three fresh unpaced browser installations and
launches: `repair106-asphalt3-cold-1`, `-cold-2` and `-cold-3`. Each reached the
actual racing HUD after choosing sound YES, ran the standard five-second route
and reported no guest errors. Both automatic repair messages appeared in the
browser logs. No injected file edits or diagnostic exports were used.

A fresh normal paced launcher run, `repair106-asphalt3-paced-navigation-1`,
reached racing and rendered 305 frames over ten seconds (30.49 FPS), with no
reported errors. The first paced helper timed out on a client view because its
visual navigation omitted a continue input. The revised helper logs those
inputs and reaches the racing HUD; the failed helper run remains retained as
`repair106-asphalt3-paced-1`. These concurrently run startup checks establish
launch behavior, not an isolated comparison of emulator throughput or memory.
They do not establish campaign completion or full-game activation.

The local fixture tests are explicitly opt-in and do not distribute game bytes:

```sh
EKA2L1_ASPHALT3_BODYZ_EXE=/path/to/preserved/asphalt3.exe \
EKA2L1_ASPHALT3_OFFICIAL_EXE=/path/to/official/asphalt3.exe \
EKA2L1_ASPHALT3_TRACKOBJ=/path/to/trackobj.bar.ngdat \
  build/src/tests/ekatests '[.preserved-asphalt-image],[.preserved-asphalt-resources]'
```

Raw evidence is retained under
`/home/claude/.scratch/eka-compiled-only-20261014/`:

- `negative93-asphalt3-1/`: original captured SDK code-hash rejection and hash inputs.
- `replay93-official-complete.log`: fixed-input replay after restoring the covered instructions.
- `negative95-asphalt3-0/unicorn-segments.json`: 47 matching caller segments.
- `negative100-asphalt3-heap-1/`: failing heap and mapped RAM capture.
- `negative101-asphalt3-image-1/`: incorrect image input immediately before decoding.
- `negative102-asphalt3-sdk-read-1/`: SDK read errors despite copied bytes.
- `negative103-asphalt3-seek-1/`: file-metadata callback and first bad flag write.
- `replay103-seek32-correct-tls.log`: independent replay reproducing `0x72`.
- `replay103-plain-file-name-correct-tls.log`: same bytes with the correct name return `0x20`.
- `repair98-asphalt3-plaintext-name-1/`: browser race with sound after the manual filename intervention.
- `build-106-native-preserved-tests.log`, `build-106-cpu-tests.log`, `build-106-wasm-tests.log`: regression checks.

Candidate 106 removes all temporary tracing. Its WASM SHA-256 is
`d31435fa2b15ab989776f661ce7afc6bfb416b953474193adf992bc1f7a2f7dd`.
Concurrent diagnostic runs are stability evidence, not throughput benchmarks.
The public `.lan` control has not been replaced by the compiled-only candidate.
