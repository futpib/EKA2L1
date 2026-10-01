# Fault-probe rebuild correction, 2026-09-29

The fault-probe executables are `EXCLUDE_FROM_ALL` CMake targets. The initial
post-reboot experiment runs invoked them after a default build, which did not
relink them. Their successful output therefore did not establish fault parity
for those candidate sources. The previous WASM probe timestamp was 14:11 UTC;
the wide-reuse gameplay build was from 17:06 UTC. The full WASM suite and checked
native replays were freshly built/executed independently and remain valid.

This audit explicitly rebuilt the native probe and then the WASM probe separately
for each exact compiler variant. All five pass all 672 cases, including callback
state/order and exact memory changes: committed compiler, rejected register-store,
conditional MOV, conditional ALU, and wide-result reuse. Thus the corrected fresh
checks support the same parity conclusion; the earlier claimed provenance was
wrong. No deployment or performance decision is changed by this correction.

`FAULT_PROBE_REBUILD_AUDIT.json` records HEAD, compiler/header hashes, executable
hashes and comparisons. Original stale-probe logs remain preserved; replacement
logs and per-variant JS/WASM probe artifacts are under
`/home/claude/.scratch/eka-benchmark/fault-rebuild-audit/`. The sequential rebuild
script is `../rebuild_fault_audit.py` relative to that directory. It restores the
original source/header in a finally block and excludes tests from runtime patches.

Build these targets explicitly before each candidate's fault comparison:

```sh
cmake --build build --target eka_cpu_fault_native -j6
cmake --build build-wasm --target eka_cpu_fault_wasm -j6
build/src/tests/eka_cpu_fault_native --extended > native.log
node build-wasm/src/tests/aot/eka_cpu_fault_wasm.js --extended > wasm.log
python3 src/tests/benchmark/compare_cpu_faults.py native.log wasm.log result.json --cases 672 --require-equal
```

Record the current commit, dirty source patch and probe binary hashes with the
results. A successful run alone does not establish that an excluded target was
rebuilt from the candidate.
