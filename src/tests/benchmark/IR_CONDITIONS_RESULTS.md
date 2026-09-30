# Conditional integer IR: no promotion

> Correction (2026-09-30): standalone per-policy fault coverage is superseded by
> [the explicit-policy audit](FAULT_POLICY_AUDIT_RESULTS.md). Original raw results remain below.

Policy 13 extends the opt-in mixed IR across pure conditional integer operations.
Predicates use preceding flags; false conditions still consume an instruction.
Conditional memory and control transfers remain original-emitter boundaries.
See IR_CONDITIONS_DESIGN.md for the value and precise-exit contract.

All 157 compiler tests pass, including 57,344 new condition/flag/budget/fault
comparisons. Explicitly rebuilt probes match native in all 27,520 cases across
policies 12 and 13 (13,760 each), including 5,376 new conditional cases per policy.
Both interpreter-checked gameplay replays match native for all 1,600 images,
guest records and 4,919,249 stereo PCM frames. Three native targets and all nine
frontend checks pass. These matrices support the tested cases, not a proof of
universal correctness.

The two previously captured busy-loop fixtures are byte-identical under policies
12 and 13, with zero conditional IR instructions. Any measured whole-game change
must arise elsewhere; this extension does not improve those two fixture bodies.

Archive: /home/claude/.scratch/eka-benchmark/ir-conditions-candidate.
WASM SHA-256: 0c8222476afe98f92e00d6ac49551757857f478f810421fe4f2ef83c4cbeeab2.
Source is e3953b69a plus the archived patch. The initial build failed because the
select opcode constant was missing; the successful rebuilt archive includes it.
The rejected build logs remain in scratch. IR_CONDITIONS_EVIDENCE.json records
source/probe hashes, exact comparisons and fixture provenance.

The completed serial batch does not support replacing the served archive. The batch compares policies 13, 12 and 7 in this same application
binary plus the exact served policy-7 archive. All observations are retained. Nothing
is deployed or pushed; the current HTTPS application remains unchanged.

## Completed serial batch

| Run | Seconds |
| --- | ---: |
| conditions-1 | 13.2275 |
| writes-1 | 15.1736 |
| combined-1 | 14.3168 |
| served-1 | 12.6534 |
| served-2 | 13.2084 |
| combined-2 | 14.0044 |
| writes-2 | 13.4586 |
| conditions-2 | 14.9605 |

Mean elapsed seconds: conditions 14.0940, writes 14.3161, combined 14.1606, served 12.9309.
Candidate throughput relative to controls: writes +1.58%, combined +0.47%, served -8.25%.

All eight samples are retained. This single batch does not establish a
repeatable marginal conditional IR benefit or support replacing the exact served
archive. The selected policy 7 archive remains live. No owned heavy job
overlapped warmup or measurement. The IR policies stay opt-in.

The two adjacent candidate/preceding-IR pairs disagree: 13.2275s versus15.1736s
at opening, 14.9605s versus13.4586s at closing. The pooled1.58% marginal lead is
not repeated pairwise. Candidate loses8.25% throughput against the served archive.
Fresh diagnostic profiles follow separately; they are not promotion timings.
