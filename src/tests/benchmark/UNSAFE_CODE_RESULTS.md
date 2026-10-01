# Unsafe executable-byte assumption experiment

Explicitly user-authorized diagnostic, off by default. No deployment or push.
The assumption is that executable bytes never change after loading/relocation,
until the mapping or image is retired/replaced. This is intentionally incompatible
with self-modifying code; passing the game replay cannot prove absence of mutations.

## Recovered earlier experiment

COMPILER_OPTIONS_RESULTS.md and build_probe_variant.py document September 27's
`ceiling` archive: only equal_code_bytes() returned true. Mapping, backing,
extent and dispatch stayed; generated store guards were not removed. Two serial
repetitions on source d50abf943 and Chromium 150 averaged 16.1295 s versus
18.0860 s (10.8% lower elapsed; 1.121x throughput). Different code, browser and
guest window make that historical evidence, not a current prediction.
VALIDITY_AND_COST_RESULTS.md's later version/barrier tests were separate safe
experiments, not this fully unsafe mode.

## Implementation and limits

`eka2l1_unsafe_code_configure` accepts 0 (control), 1 (scan removal), 2
(code-write guard removal), or 3 (both and no mutation tracking), before CPU
initialization only. `eka2l1_unsafe_code_report` exposes actual selected mode.
Only research replay/profile harnesses select it; the live launcher does not.

Mode 3 bypasses the entire primary/dependency byte-validation path, including
optional versions/epochs. The compiler omits scalar and block-store overlap
checks and the code-overlap portion of both entry-span/IR proofs. It retains
ordinary translation, read/write permission, alignment, page/extent/wrap, fault,
callback, interrupt, stop, budget and scheduling behavior. There is no generated
per-store diagnostic-mode branch, periodic scan, or substitute tracking.

Mutation tracking allocation/watch/version/epoch/host-barrier helpers become
no-ops in mode 3, and watched-code write protection cannot be enabled. This
matching binary is built with CODE_VERSIONS, CODE_LIFECYCLE and
CODE_WRITE_PROTECTION OFF already; the ordinary control has no such barriers.
Mapping generation, address-space identity, backing and extent checks remain.
Explicit invalidate/IMB operations remain because the memory managers also use
these to retire chunk mappings; ordinary guest stores do not invoke them.
Cached snapshots and dependency extent metadata remain for translation/mapping
identity. This does not remove unrelated lifetime bookkeeping or scheduling.

Focused tests deliberately require acceptance of changed primary/dependency
bytes and execution of stale translated instructions after an overlapping store.
These are intentional semantic limitations, not bugs to mask with fallback scans.
The same tests verify remap, missing mapping, short extent, address-space change,
retirement and partial-budget handling. Native instruction-fault comparisons
cover unrelated access/fault semantics. Exact native image/audio replays, outside
timings, check the assumption only for these executions.

## Preplanned measurements

First compare mode 3 against mode 0 in the same frozen binary and the untouched
conditional-only live archive. Both current routes, two serial reordered batches,
two observations per mode per batch (24 total), original compiler policy 7,
512-byte source / 16-instruction leaves / 8 sites / 512 runner cap, conditional
leaves enabled, all other eligibility bits off. Guest work/audio/rendering fixed.
No owned builds or profilers during timing; retain all observations and passive
host telemetry. Initial orders: control, unsafe, baseline; then unsafe, baseline,
control; each order followed by its reversal. Diagnostic code inspection and
correctness are outside timing. Attribution modes 1/2 can follow if useful.

The completed runner-cap timings are preserved as 0f2672f98 (32 observations;
no repeatable advantage). Its remaining census and frozen literal-PC candidate
b37918c9c are held behind the unsafe-experiment completion marker. No new
veneer work runs concurrently. All prior work remains preserved.

Initial status (superseded by acceptance and timing sections below): implementation building.

Checkpoint: all 175 normal-mode compiler tests and 40,640 explicitly selected native fault comparisons in mode 3 pass. Matching control faults, exact native replays and all timings remain pending. Frozen archive and executable queue sources are retained in the evidence file.

### Retained initial replay configuration failure

The first standard replay incorrectly passed start_us=0, whereas the existing
native reference and normal harness default start at 21,000,000 us. All 1,600
frames were captured, but the visible-canvas threshold failed on the wrong
window. Comparing those captures to native differs from frame zero (723,785 vs
21,032,065 us). The entire run, screenshot, log, comparison and original harness
are retained. This is a configuration failure, not a measured unsafe-mode mismatch.
The replay is repeated with the correct timestamp; completed 175-test and both
40,640-case fault results are reused, not claimed as fresh runs.

## Correctness and intentional incompatibility

All 175 normal-mode compiler tests pass, including 42 focused diagnostic checks.
Both mode 0 and mode 3 match 40,640 explicitly selected native instruction-fault
comparisons each. Both modes exactly match the native standard 1,600 images / 
4,656,051 PCM frames and longer 360 images / 2,832,756 PCM frames, with guest
records, compiler policy, actual unsafe-mode readback and artifact hashes checked.
Replay used verify_aot=0: no interpreter check or fallback concealed unsafe execution.
The API rejects mode changes after initialization.

The focused test intentionally demonstrates stale primary/dependency acceptance
and stale execution after a store overwrites the next instruction; the control
rejects or exits. Remap/lifetime/budget checks still pass. This is the requested
semantic limitation, not evidence that Snakes never mutates executable bytes.

Disassembly outside timing confirms zero AOT_CODE_BEGIN/END loads in unsafe
scalar-store and four-store entry-proof modules. Corresponding controls contain
those loads. Module sizes: scalar 627 -> 582 bytes; proof 2,822 -> 2,575 bytes.
The proof fixture asserts all four writes actually use the entry proof. Mode 1
retains store guards; mode 2 emits the same guard-free paths as mode 3. The binary
has mutation-version/lifecycle/protection build options OFF; source also bypasses
them in full unsafe mode when built in, but those optional builds were not tested.

The completed full-mode measurements follow.

## Fully unsafe serial timings

All 24 planned observations are retained, including slow candidates and controls. Elapsed seconds for identical guest work within each route, lower is faster. Mode 3 is compared first; attribution modes have not been timed here.

| Route/batch | Matching control | Fully unsafe | Untouched live |
| --- | ---: | ---: | ---: |
| long a | 11.1025 | 9.6421 | 11.6264 |
| long b | 11.9815 | 11.8449 | 16.2727 |
| standard a | 12.8835 | 10.4977 | 16.0891 |
| standard b | 11.7991 | 9.9641 | 12.0057 |

long a: versus control +15.15% throughput, corresponding-half pairs +19.88% / +10.81%; versus baseline +20.58% throughput, corresponding-half pairs +23.07% / +18.29%; 

long b: versus control +1.15% throughput, corresponding-half pairs -4.90% / +9.07%; versus baseline +37.38% throughput, corresponding-half pairs +58.64% / +9.60%; 

standard a: versus control +22.73% throughput, corresponding-half pairs +7.63% / +37.68%; versus baseline +53.26% throughput, corresponding-half pairs +89.86% / +17.03%; 

standard b: versus control +18.42% throughput, corresponding-half pairs +23.42% / +14.10%; versus baseline +20.49% throughput, corresponding-half pairs +40.19% / +3.49%; 

These corresponding-half pairs are not all adjacent. Passive host readings and process observations accompany every batch; no observation is normalized or removed. Different older live compiler layout is a separate control. This deliberately unsafe semantic assumption is not suitable for promotion. No deployment or push.

## Interpretation and attribution decision

All four matching-control batch means favor full unsafe mode; seven of eight corresponding-half comparisons do. Standard-route means improve by 22.73% / 18.42% throughput. Longer-route means improve by 15.15% / 1.15%, with a reversed pair in confirmation. Thus removed code-mutation work has a material measured cost on these executions, but this small noisy sample does not establish a stable percentage for both routes.

The 19.83-second standard-A and 21.292-second long-B untouched-control observations inflate those live-archive comparisons. They remain in the table and raw evidence; no observations are excluded or normalized. Passive observers recorded no watched competing benchmark/build/profiler process, which does not prove the host was contention-free or explain the slow samples. Performance counters were disabled during timings, so their zero fields cannot establish absence of mutation, invalidation or IMB activity.

Attribution is useful given the standard-route result and uncertain longer-route effect. The next fixed panel measures scan-only (1), guard-only (2), both (3) and exact control (0) in this same archive, after independent component fault/replay acceptance. It adds 32 serial reordered observations, with every mode moved between inner and outer positions. No new compiler or scheduling change is mixed in; the earlier untouched archive remains a separate panel. Detailed plan and evidence are in UNSAFE_CODE_ATTRIBUTION_RESULTS.md / EVIDENCE.json once acceptance completes.
