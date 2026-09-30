# Invariant read spans: acceptance complete, timing pending

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

Performance is not yet measured. No promotion, push or deployment.

Reproduction uses EKA2L1_SHARED_AUDIO=1 and serial_variants.py ASSETS NEW_OUTPUT
proof=ARCHIVE plain=ARCHIVE served=SERVED --ir-mode proof=4 --ir-mode plain=0
--eager-regions proof=0 --eager-regions plain=0. This runs one fresh browser at
a time in proof/plain/served/served/plain/proof order, guest seconds 78–96,
with hardware GPU, shared audio, sampling and detailed counters disabled.
