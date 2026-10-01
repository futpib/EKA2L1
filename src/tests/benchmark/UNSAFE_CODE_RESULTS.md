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

Status: implementation building; no new performance result yet.
