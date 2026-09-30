# Eager ROM regions: acceptance complete, performance pending

The opt-in eager ARM path now uses the existing region compiler. Both control
and candidate select IR policy0 in the same application binary. The default
retains basic blocks. EAGER_REGIONS_DESIGN.md describes scope and controls.

All 144 compiler tests pass freshly, native CTest passes 3 targets, and frontend
passes 9 checks. Explicit fault targets are up to date and byte-identical to
IR_RECIPES_EVIDENCE.json's verified policy0 probe (7,584 cases); those fault
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

Serial timing is pending. No promotion, push or deployment.
