# Upstream master integration, 2026-09-30

Merged upstream `2594edf4d6bf55d7bd3f0b46250fe2318d4dc2e8` into local
`wasm-port`, bringing 33 previously absent commits. Parent before merge:
`25792310e1421aff3e29e7c0a1582555abba4045`. Existing optimization experiments,
immutable archives and raw measurements are retained. Nothing pushed or deployed.

## Integration decisions

Three conflicting files are resolved. The Qt installer retains the shared
installer wrapper and forwards upstream's isolated-drive checkbox through it.
Browser and CLI callers retain shared drives by default. The screen header
retains the deterministic redraw setter alongside upstream's regional redraw
helpers. Screen composition uses upstream's partial-redraw handling and keeps
the existing opaque clear color.

The shared installer was adapted to upstream's new ROM/RPKG/firmware API.
WASM now stages its own patch target dependency and packages that build's
patch directory, instead of relying on a separate native build. DLL/map changes
also become link dependencies. All 31 packaged DLL/map entries match the native
patch set byte-for-byte, including the new qjpeg patch.

## Verification

Native and WASM builds pass. All three native CTest targets pass: 329 general
cases / 28,834 assertions, 32 CPU cases / 531 assertions, and 2 network cases /
35 assertions. The full WASM compiler suite passes 162 tests; its pre-existing
crash-reproducer harness XFAIL remains labelled in the raw log. Frontend startup,
configuration and shutdown checks pass.

Both normal and interpreter-checked browser execution match the new native
reference exactly for all 1,600 images, guest records, 4,656,051 stereo PCM frames
and audio events. Shared DSP, original-emitter policy 7, folded TLB indexing and
grouped exact-byte scanning are selected. Experimental write protection and code
versions/lifecycle are disabled in this merge verification build.

The older route now includes a level-restart transition near 83 guest seconds.
The full 1,600-image gameplay validator correctly fails: maximum image gap
377,764 microseconds and minimum viewport change 0.04%. That failure is retained;
no threshold was weakened, frame dropped or transition disguised as active play.
Native and both browser modes agree through the transition and resumed gameplay.

A fresh 360-image longer-snake route matches native exactly and passes the
unchanged gameplay validator. It covers 42.047294–59.015758 guest seconds,
with 21.157 changing images per guest second, a 63,546 microsecond maximum gap,
and at least 18.3% viewport change between adjacent images.

This is a new upstream baseline: guest instruction totals, presentation counts,
images and PCM differ from the pre-merge reference. Pre-merge optimization timings
must not be presented as measurements of this source or as identical-work gains
across the merge. Correctness jobs overlapped; their wall times are not performance
measurements. The live launcher still serves its separately verified old archive.

The archived candidate, raw logs and captures are under
`/home/claude/.scratch/eka-benchmark/merge-20260930-*`. The archive records the
pre-merge parent, upstream parent and exact staged source patch; the evidence
records the implementation tree and binary hashes. Source/tests ran while the
merge was staged, before the merge commit. See UPSTREAM_20260930_EVIDENCE.json.
