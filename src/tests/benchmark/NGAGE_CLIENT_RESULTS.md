# Original N-Gage client on the stock browser device

Checked 2026-10-13 on `wasm-port`, Chromium 153.0.8010.52 with hardware
ANGLE/Vulkan, through the production `.lan` launcher. The normal picker now
includes **N-Gage** at `?game=n-gage`, UID `0x20003b78`. It uses the same stock
Nokia 5320 RM-409 04.13 firmware and saved virtual phone as the eight games.
There is no client or firmware patch.

## Package identity

**1.40.1557** is the latest original S60 3rd Edition client recovered in this
search. This is a recovery result, not proof that no later build ever existed.
It was already the runtime dependency for Hooked On, Asphalt 3 and Tetris.
Later archive filenames marked 2011 also contain 1.40.1557; modified BodyZ
packages do not establish a newer official release.

The original [Nokia installer wrapper](https://archive.org/download/Dance.Fabulous.nGage/N-Gage%202.0/Software/N-Gage%20Installer%20v1.40.1557/Other/N-Gage%20Aplicativo%201.40%202011/1%C2%BA%20N-gage_1.40_1557.sisx)
was downloaded again. Its SHA-256 is
`2396b65d14abbacd371142954b65d20eb2c30dd05f3f9e548287f474cfdac6b5`.
Decompressing its client payload produces exactly the existing served SISX:
12,346,300 bytes, SHA-256
`e31bb10c5bf013dfd3a51ef09cc6e313cc6eb2275a2b3363f44f4dd712330ba4`.
The wrapper and client identify version 1.40.1557 and creation date 2009-08-05.

The [earlier signature checks](PRE_TOUCH_GAME_INSTALLERS.md#recovery-and-authenticity)
verified both embedded Nokia controller signatures and the client's 449
described file records. This does not independently validate the certificate
chain. Fresh byte comparison establishes that the served component is unchanged,
including its signatures. Its individual IPFS CID remains
`bafybeigkftb66qpv2lq256qfr3jmlmwkj72v3bdsd4hpy63a6eutk27sxm`.
The manifest records provenance and identity; SIS and firmware bytes stay
outside Git.

## Browser checks

A fresh ordinary `?game=n-gage` launch installs the signed client, reaches the
welcome and Home screens, and responds to tab navigation and the Options menu.
My Games is empty until a game is imported. Restarting the browser and reloading
the page reuse the saved device and runtime installation: no `/preload/` package
or firmware downloads, and no captured page errors or guest panics.

Importing the current Hooked On container through the normal launcher populates
My Games. The card remains visible when the browser is closed and reopened at
the standalone N-Gage entry. This checks the shared persistent catalogue, not
only the frontend's installed-package marker. Screenshots were reviewed because
OCR misses the stylized game title.

The earlier prepared image could show Hooked On on Home while My Games was
empty. The capture tool parked execution immediately after the importer thread
exited. Importer exit precedes remaining client registration work. The corrected
capture uses normal paced execution, waits five seconds after successful exit,
dismisses the completed dialogue, waits two more seconds, then shuts down and
reads the filesystem. These are builder waits, not gameplay delays. Review the
capture screenshot and a restored client library before deploying a new image.

Running the corrected capture tool from fresh source assets produced an image
whose restored My Games contains the card. A complete browser restart followed
by standalone-client launch preserves it, without package downloads or captured
guest/page errors. The same checks are repeated on the public `.lan` endpoint.

The new image has the same four source hashes, 9,828 files and 230,406,209
unpacked bytes as the earlier pilot. Compressed size is 133,970,694 bytes;
SHA-256 is
`74c01397458621c38c564ddc76965cea4ffb5509061140e5e95a3be9acc795c5`.
It is deployed under `/home/claude/.local/share/eka2l1/preinstalled` with the old
image retained in the scratch evidence directory. Existing saved phones are
never overwritten, so deploying this image does not repair an older saved
catalogue automatically. There is no filesystem-format or native-runtime change.

Online N-Gage services, account login and game activation were not validated.
The client displays a TRIAL badge for the imported preservation container;
these checks establish client/library operation, not licensing status.
No performance gain is claimed from this catalogue correction. Runtime WASM
remains `9670e79c417c64f51f129b2b99a907f8a8c112571b4bd98af811d4bab06dc022`.

## Evidence and rejected checks

Raw packages, extraction comparison, browser scripts, screenshots and reports:
`/home/claude/.scratch/eka-ngage-client-20261013`.
`client-restore.json` covers direct-client restart/reload;
`fresh-restored-library.json` covers normal import persistence;
`factory-image/` comes from the corrected repository capture tool;
`factory-restored-library.json` covers image-derived persistence;
`public-check/` covers deployment.

Rejected attempts remain separate: concurrent capture/reload caused a harness
race; a blind right-softkey press exited the client on Home; one fresh-image
assertion expected the wrong HTTP endpoint. Its screenshot was valid, but its
failed assertion is not reported as a passing run. The corrected public check
uses `/preload/games/hooked-on/image`.

`installed-image.test.ts`, JavaScript syntax checks and `git diff --check`
pass. No CPU/compiler change was made, and no new FPS comparison was conducted.
