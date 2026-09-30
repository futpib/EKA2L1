# Eager ROM regions: no promotion

The opt-in eager ARM path now uses the existing region compiler. Both control
and candidate select IR policy 0 in the same application binary. The default
retains basic blocks. EAGER_REGIONS_DESIGN.md describes scope and controls.

All 144 compiler tests pass freshly, native CTest passes 3 targets, and frontend
passes 9 checks. Explicit fault targets are up to date and byte-identical to
IR_RECIPES_EVIDENCE.json's verified policy 0 probe (7,584 cases); those fault
results are reused, not reported as a new run. The changed eager boot route
has fresh off/on checked replays: both exactly match 1,600 native images,
guest records and 4,919,249 stereo PCM frames. The existing native-identical
movement-heuristic limitation remains separate from exact equality.

Diagnostic counts in guest seconds 78–96 change from 179,465,047 to 178,691,134
compiled dispatches (0.43% fewer), and 3,874,401 to 3,872,812 compiled runner calls.
Both runs execute 3,975,618,624 guest instructions and 676 presentations. These
instrumented runs overlap compiler tests; their wall times are not production
measurements. The small count reduction does not predict a useful speedup.

The eager module grows from 8,899,473 to 9,935,367 bytes, while public functions
fall from 6,108 to 6,078. Scan/translation plus emission take 303.990 + 7.740 ms off
and 290.185 + 8.630 ms on in concurrent replay diagnostics; these are observations,
not an isolated startup performance comparison. Browser compilation and
instantiation are additional costs excluded from these two timings.

Archive: /home/claude/.scratch/eka-benchmark/eager-regions-candidate.
Source base 8ef7de08e4fea21af2db2c81463df82eecf2fb6b plus archived patch.
Application WASM SHA256:
ec29735a9a9b6e051c973ebda88b1e341d04f402540f43775bc2b12b199a6201.
Source/binary hashes, exact comparisons and counters: EAGER_REGIONS_EVIDENCE.json.

| Variant | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Eager regions on, IR off | 13.4120 / 14.6904 | 14.05120 |
| Eager regions off, IR off | 13.5226 / 14.6728 | 14.09770 |
| Served build | 15.2441 / 13.4660 | 14.35505 |

Order: on/off/served/served/off/on. The mean throughput difference against
same-binary off is only +0.33%; the first adjacent pair favors on and the
closing pair slightly favors off. The +2.2% pooled comparison with served
is dominated by overlapping, variable samples. No useful repeated speedup
has been established, so the larger eager module is not promoted. All six
observations remain. Nothing pushed or deployed.

Timing is warmed, serial, unsampled, hardware GPU and shared audio enabled,
guest seconds 78–96, with detailed counters disabled. All runs execute the
same 3,975,618,624 guest instructions and 676 presentations. No owned compiler,
test or diagnostic overlaps warmup or timing. Source hashes match the archived
candidate. On/off use identical app JS/WASM; served uses its separate archive.

Reproduction: EKA2L1_SHARED_AUDIO=1 with serial_variants.py ASSETS NEW_OUTPUT
on=ARCHIVE off=ARCHIVE served=SERVED --ir-mode on=0 --ir-mode off=0
--eager-regions on=1 --eager-regions off=0. Use a new output directory. The
browser control is pre-init only; normal startup retains the old path.

Next candidate: prove fixed read spans through unchanged entry registers
across existing loops, while keeping all other memory accesses, budgets and
exits precise. Unlike the old whole-region proof, this would accept a subset
of accesses in mixed/control-flow regions. This is a design hypothesis only.
