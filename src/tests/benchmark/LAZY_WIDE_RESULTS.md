# Wide values and precise exit snapshots

Retained locally as a compiler step; **not deployed and not a demonstrated new
LAN speedup**. All three batch means improve over the immediately preceding
compiler with the same callback-state fix. Comparisons against the served build
have mixed signs and nearly tie when pooled. Outliers and overlapping ranges
remain; these small samples do not establish statistical significance.

| Batch | Immediate baseline | Wide snapshots | Served | Gain vs immediate | Gain vs served |
| --- | ---: | ---: | ---: | ---: | ---: |
| A | 14.22935s | 13.77845s | 14.58120s | +3.27% | +5.83% |
| B | 14.69345s | 14.33540s | 14.00920s | +2.50% | -2.28% |
| C | 14.24060s | 13.79790s | 13.49110s | +3.21% | -2.22% |

Pooled across six runs per build: immediate 14.38780s,
wide 13.97058s, served 14.02717s. The implied throughput
differences are 2.99% and
0.41% respectively. The latter does not
support deployment. This is not a promise of a fixed percentage improvement.

A order: direct/wide/served/served/wide/direct.
B order: wide/served/direct/direct/served/wide.
C order: served/direct/wide/wide/direct/served.
Together these rotate every build through every position. They reduce order
confounding without eliminating host variation. Warmup and measurement are
serial, with no owned build/test/profile job overlapping performance. Shared
audio and physical GPU; no capture, sampling or detailed counters. Each run
executes 3,975,618,624 instructions and 676 presentations in guest seconds 78–96.
Every timing, including the slow controls and candidate, is retained in evidence.

## Representation and exact state

Unconditional nonflag long multiply results can remain one i64 value representing
two architectural registers in a straight-line region. Compatible accumulates
reuse it without rebuilding or eagerly splitting the pair. A 32-bit consumer
extracts its half; every exit emits the precise reconstruction before the shared
state writeback. Emitting a guarded exit does not forget the representation on
its surviving path. Branches, joins, helpers, unknown encodings, destination
clobbers and conditional/flag producers end it conservatively. A narrow ordinary
ALU subset and restartable deferred word reads may preserve it; a failed read
leaves before side effects and cannot mutate callback-visible state in-region.

Fallthrough materializes before closing a target label; taken branches have
already materialized before jumping. No alternate entry skips the producer.
Budgets, interrupts, memory/code guards and callback contracts remain intact.
This is a small value-representation prototype with exact exit snapshots, not a
complete SSA compiler. Unlike WIDE_REUSE_RESULTS.md, it does not split every
result immediately. There is no guest-address whitelist.

The extracted long math kernel grows from 12,699 to 12,794 WASM bytes because cold
exits contain reconstruction; the short prefix remains 3,432 bytes. These are
static WASM sizes, not V8 native sizes, coverage percentages or speed evidence.

## Verification and provenance

All 138 WASM tests pass, including 92,160 long multiply comparisons and 27,648
expanded memory/dataflow/budget cases. Three native CTest targets and seven
frontend checks pass. All four original explicitly rebuilt 672-case fault modes,
48 read-span runner cases and 672 new wide-snapshot runner cases match native.
The new mode executes MOVS/SMULL/access through real runners and compares
callback-visible registers/flags, event order, exact instruction count and memory.

Checked native replay matches all 1,600 images, guest records and 4,919,249 stereo
PCM frames. The known native-identical movement-heuristic failure remains and is
not called a pass. Regression additions are separately committed as d725441ce.
Archive lazy-wide-candidate was built from 3c3cefc8b plus its saved patch; source,
loader, WASM, test and explicitly built probe hashes are recorded in evidence.
WASM SHA-256: a13eaca4292c360fbecf6785018924dfc18eedec0a077b54673ea0fb1e8cdc23.
Every timing report's hash was checked against its archive when assembling the
report. No new live/audio acceptance, HTTPS deployment or push is claimed.

Reproduce with serial_variants.py, EKA2L1_SHARED_AUDIO=1, archived paths and the
three orders above. See LAZY_WIDE_EVIDENCE.json for complete observations,
configuration, browser/renderer, hashes, checks and raw artifact paths.
