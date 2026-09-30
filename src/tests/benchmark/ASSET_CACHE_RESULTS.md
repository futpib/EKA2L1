# Persistent caching for the LAN launcher

Delivered at https://claude-laptop.lan:8188/ on2026-09-30. Previously the server
retained its IPFS source downloads, but responses had no Cache-Control, ETag or
Last-Modified and the launcher fetched every preload on every launch. There was
no explicit persistent browser asset cache.

ROM, RPKG and SIS downloads now use browser Cache Storage under content-hashed
URLs. A successful replacement is stored before obsolete versions are removed.
Storage denial or quota failure falls back to downloading, without preventing
play. Large files use this explicit cache because HTTP caches can limit entry
size. JS, WASM and data use SHA-256 version URLs and one-year immutable HTTP
caching. Unversioned resources and HTML require revalidation; ETags support304.
The HTML manifest and injected compiler policy participate in its ETag, so new
builds receive new asset URLs. A request for an obsolete version cannot silently
receive different bytes. HEAD returns correct lengths without a body.

## Measured actual workflow

Both a local server and the actual trusted HTTPS origin passed the same sequence:

| Launch | ROM/RPKG/SIS network requests | Runtime bytes transferred |
| --- | ---: | ---: |
| Empty browser profile |3|11,262,418|
| Reload |0|0|
| Close and restart browser with same profile |0|0|

All three game assets, totaling192,004,131 bytes, remain in browser Cache Storage.
Every launch reached the running game with policy7/eager0/TLB1 and a secure,
cross-origin-isolated context. There were no page, HTTP or request errors. These
are transfer measurements, not a claim that initialization or guest installation
is eliminated, nor a general startup-speed benchmark. The browser can evict
storage or the user can clear it, in which case the next launch downloads again.
This change does not promise that the entire launcher works offline.

Focused browser/HTTP tests pass: immutable version headers, correct HEAD,
conditional304, changed-asset invalidation, stale-version rejection, persistent
asset reuse after browser restart while offline, obsolete-version pruning,
quota/blocked-storage fallback, and failed-download handling. Full local and
actual HTTPS launcher checks pass sound, measured mute/unmute, keyboard/touch,
visible softkey pause/resume, mobile layout, rendering and shutdown.

The tested emulator JS/WASM archive is unchanged. Downloaded versioned files
match the previously accepted archive hashes:

- JS:5ce9172048cc8fe85cb6ba6451f0cfaa963b87ac67edd19b602904e992fa74ec
- WASM:2010a74b50b54d529b4175eb62a1374961b14086c9c81fa655c0eddfe961dcaf

No new full CPU/replay acceptance is claimed for this server-only change. Its
implementation is334f40f33. All changes are committed locally; nothing pushed.

## Reproduction

```sh
node --experimental-strip-types src/tests/wasm/asset-cache.test.ts
EKA2L1_AOT_IR_MODE=7 EKA2L1_AOT_EAGER_REGIONS=0 EKA2L1_TLB_HASH=1 node --experimental-strip-types src/tests/wasm/cache-live.ts ASSETS NEW_OUTPUT https://claude-laptop.lan:8188/
```

The cache-live harness uses a fresh, output-local persistent browser profile and
records CDP response/transfer data for cold, reload and restarted sessions. It
asserts cached preload reuse and absence of runtime body transfers on repeats.
Run softkeys.ts with the same policy for live sound/input acceptance. Evidence
and headers are retained in ASSET_CACHE_EVIDENCE.json; raw browser profiles and
screenshots remain under /home/claude/.scratch/eka-benchmark/asset-cache-*.

Compiler optimization continues separately; the prepared exact-validation span
census is next. The direct-switch discriminator is committed asfa1d948af.
