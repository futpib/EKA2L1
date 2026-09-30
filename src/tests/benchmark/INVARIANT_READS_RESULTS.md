# Invariant read spans: first timing batch favorable, confirmation pending

Policy 4 proves selected read spans through registers that cannot change in a
region, including its loops and inlined leaves. It uses original emission,
not integer IR. Policy 0 is the original compiler control in the same binary.
All other policies and normal defaults are unchanged. See INVARIANT_READS_DESIGN.md.

All 145 compiler tests pass, including 15,360 new comparisons covering exact
progress, state, memory, short budgets and pending interrupts. The explicitly
rebuilt native/WASM probes pass 7,584 cases per policy 0 and 4 (15,168 total).
Native CTest passes all 3 targets; frontend passes 9 checks. Both policies,
with eager regions off, pass checked replay against 1,600 native images,
guest records and 4,919,249 stereo PCM frames. The known native-identical
movement-heuristic limitation is separate from exact equality.

Nine and eleven reads select the proof in the two captured busy loop regions.
Hot bodies shrink from 19,852/12,083 to 18,780/10,862 bytes; complete modules
grow from 20,070/12,299 to 38,890/23,201 bytes because they retain original
private fallbacks. Policy 0's two fixture modules match their prior original
compiler bytes. Size and static selection do not establish gameplay benefit.

Archive: /home/claude/.scratch/eka-benchmark/invariant-reads-candidate.
Source base 1e9c112b8db021b022f941f9b5699376142689c1 plus archived patch.
Application WASM SHA256:
426038540c288ed57e721169080b643252629a4891596f0fbd70dc3b30479a52.
Source/binary hashes and exact results: INVARIANT_READS_EVIDENCE.json.

First serial timing batch:

| Variant | Trial seconds | Mean seconds |
| --- | --- | ---: |
| Invariant reads, policy 4 | 13.1891 / 13.0209 | 13.10500 |
| Original, policy 0, same binary | 13.4126 / 13.4748 | 13.44370 |
| Served build | 13.5122 / 14.5246 | 14.01840 |

The candidate mean is 2.58% higher throughput than same-binary original,
and 6.97% higher than served. Both adjacent candidate/control pairs favor
proofs in this batch. All observations remain, including the slow served
sample. This is preliminary; confirmation with a changed order is pending.
Every run executes 3,975,618,624 instructions and 676 presentations. No owned
build/test/profiler overlaps the warmups or timing. No promotion, push or deployment.

Reproduction uses EKA2L1_SHARED_AUDIO=1 and serial_variants.py ASSETS NEW_OUTPUT
proof=ARCHIVE plain=ARCHIVE served=SERVED --ir-mode proof=4 --ir-mode plain=0
--eager-regions proof=0 --eager-regions plain=0. This runs one fresh browser at
a time in proof/plain/served/served/plain/proof order, guest seconds 78–96,
with hardware GPU, shared audio, sampling and detailed counters disabled.

Additional callback-remapping acceptance: the probe now has 64 cases in which
an unproved byte or multi-register load faults and its callback replaces the
proved root page. It independently asserts the first read used the old mapping
and later reads used the new backing, as well as comparing callback state to
native. Both original and proof policies pass. The entire expanded fault suite
was freshly rerun: 7,648 cases per policy, 15,296 total, all exact. Native CTest
also passes all three targets again. The compiler test binary and application
JS/WASM are byte-identical to the earlier accepted archive; 145 compiler tests,
frontend checks and exact replays are reused for those unchanged bytes, not
claimed as additional fresh runs. New probe archive: invariant-reads-remap-candidate.
The comparison tool omits the old unmapped summary for this new 64-case mode,
whose permission dimension means read-only versus read/write root mapping.
