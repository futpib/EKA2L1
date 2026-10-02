# Preserved official Snakes releases

Recovered and checked on 2026-10-02. This collection covers the original
Nokia/IOMO **Snakes**, with four distinct official downloadable installers and
two additional builds extracted from preserved firmware. It excludes Snakes
Subsonic, Snake III, CrazySoft Snakes Deluxe, leaked prototypes and modified
installers.

The newest recovered standalone build is **0.6.0.20**, packaged on
2007-03-13. The current benchmark and browser game contains **0.6.0.19 a3**,
packaged on 2007-01-18. Their executables differ. This preservation work does
not change the launcher, benchmark reference or stock Nokia 5320 firmware,
and does not establish gameplay or resolution support for the newer build.

[Download the collection ZIP](https://claude-laptop.lan:8188/downloads/snakes/Snakes-preserved-releases.zip),
or use the [individual downloads and manifest](https://claude-laptop.lan:8188/downloads/snakes/index.html).
The [tracked manifest](snakes-releases.json) records full hashes, IPFS CIDs,
original archive URLs, signature checks and duplicate relationships. Game
binaries remain outside Git.

## Original downloadable installers

These files were retrieved unchanged from Wayback captures of Nokia's own
`arena.n-gage.com` and `nds1.nokia.com` download servers. Every installer calls
its SIS package version **1.0.0**; the game build identifiers distinguish them.

| # | Release | Game build | Package date | Original installer |
|---|---|---|---|---|
| 1 | Original N-Gage / QD | 0.2.1.9 | Released January 2005 | [SNAKES.SIS](https://claude-laptop.lan:8188/downloads/snakes/official-installers/SNAKES.SIS) |
| 2 | Nokia 3250 / early S60v3 | 0.6.0.4 a3 | 2006-02-20 | [Nokia_3250_snakes.SIS](https://claude-laptop.lan:8188/downloads/snakes/official-installers/Nokia_3250_snakes.SIS) |
| 3 | Nokia N95 / N76 | 0.6.0.19 a3 | 2007-01-18 | [Nokia_N95_Snakes.sis](https://claude-laptop.lan:8188/downloads/snakes/official-installers/Nokia_N95_Snakes.sis) |
| 4 | General S60v3 release, offered on Nokia 5700 support page | 0.6.0.20 | 2007-03-13 | [snakes60all.sis](https://claude-laptop.lan:8188/downloads/snakes/official-installers/snakes60all.sis) |

The three S60v3 build identifiers were read from their decompressed executable
code. The N-Gage executable matches the preserved distribution identified as
0.2.1.9; its internal version text was not independently decoded. Package
dates come from SIS metadata, not Wayback capture dates.

Nokia's [archived 5700 support page](https://web.archive.org/web/20090209210758id_/http://europe.nokia.com:80/A4403831)
links directly to `snakes60all.sis`.
[Contemporary N-Gage reporting](https://allaboutsymbian.com/news/item/Snakes_Direct_Download_Link.php)
links the January 2005 release to Nokia's N-Gage download directory.

## Firmware builds

These ZIPs were created locally from unchanged game files in firmware dumps.
They are preservation extracts with their original `Z/` paths, **not official
SIS installers**, and have not been tested as standalone installations.

| # | Device | Build identification | Files |
|---|---|---|---|
| 1 | N70 / S60v2 | Distinct executable, UID `0x10208A24`; internal version string not decoded | [N70 game files](https://claude-laptop.lan:8188/downloads/snakes/firmware-extracts/N70-Snakes-ROM-files.zip) |
| 2 | N80 / S60v3 | Embedded version `0.6.0.7 a3`, UID `0x10208A45` | [N80 game files](https://claude-laptop.lan:8188/downloads/snakes/firmware-extracts/N80-Snakes-ROM-files.zip) |

The N70 files come from a [preserved firmware collection](https://archive.org/details/images-1_202601).
The N80 files come from the already documented [stock firmware conversion](N80_ASSETS.md).
The manifest records source archive and RPKG hashes, plus each extracted file's
hash. Firmware mirror provenance is weaker than a recovered Nokia-hosted
installer; these extracts do not establish an inventory of every regional ROM.

An unsigned S60v2 SIS found in a community bundle contains the exact N70
executable, five PAK data archives, AIF, game.id and English/German resources
from that firmware dump. Its other language resources are absent from the
regional dump and could not be checked against it. Its installer provenance
is unverified, so that SIS is excluded from the official installer set.

## Duplicates and the existing benchmark

- Nokia's `Nokia_N76_Snakes.SIS` is byte-identical to `Nokia_N95_Snakes.sis`.
- Nokia's N-Gage `snakes.zip` contains the exact same `SNAKES.SIS` as the direct download.
- The archive's “Snakes HD” SISX is byte-identical to Nokia's 3250 installer.
  Its name does not indicate a newer or higher-resolution build.
- The community bundle's S60v3 SIS is byte-identical to `snakes60all.sis`.
- All **104 installed payloads** in the current benchmark SIS match the original
  Nokia N95 installer. The benchmark SIS retains Nokia's signature and adds a
  third-party co-signature; it is a different installer wrapper, not a different
  game build.
- The two MyAbandonware N-Gage archives have identical game executables and
  data archives. They differ in a launcher resource and an installation marker;
  neither is needed when the original Nokia installer is available.

## Verification and limits

The S60v3 installers' RSA/SHA-1 controller signatures verify with their embedded
`Nokia Content` certificates. All 43, 104 and 104 payload hashes, respectively,
match the signed file descriptions. This is signature and payload validation,
not independent certificate-chain trust validation. The N-Gage old-format SIS
was parsed and its payloads decompressed; its certificate is present, but its
old-format signature was not verified. Its provenance is the Nokia-hosted
archive capture.

The six downloadable artifacts are locally pinned with CIDv1. Each was read
back with `ipfs cat` and compared byte-for-byte with the saved artifact. Use
the manifest's `cid` and `sha256` fields to retrieve and check any individual
file. The collection ZIP contains these same six artifacts, a README and the
manifest. The browser download URLs were checked against the saved hashes.

Wayback's indexed successful application downloads on the two Nokia hosts
yielded these four distinct installers; the relevant CDX rows are retained in
the manifest. This does **not** prove that no other official release, regional
firmware build or subsequently lost package existed. In particular, recovery
of 0.6.0.20 establishes a newer build than our current reference, not that it
was the final build ever produced.
