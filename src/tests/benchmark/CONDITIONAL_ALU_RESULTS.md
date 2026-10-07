# Conditional ALU selection: rejected

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The controlled rerun uses four observations per variant, in ABBA then BAAB
order, at the original shared-audio Snakes window of guest seconds 78-96.
CPU throughput changes by -0.17% and wall throughput by -0.16%; native
instructions increase by 0.20%. One of four CPU pairs is faster by just 0.005%;
the full range is -0.50% to +0.005%. All eight observations pass the host checks.
The result is near flat with no demonstrated gain. The original 2.3% loss is
much smaller under these controls; neither magnitude is a claim about this
change applied to today's compiler and direct-memory defaults.

**Fault-check provenance corrected:** see [explicit rebuild audit](FAULT_PROBE_REBUILD_AUDIT.md). The original post-reboot probe runs were stale; rebuilt candidate probes now pass all 672 cases.

The broader conditional-MOV experiment uses WASM select for cheap non-flag-setting
ARM arithmetic and logical instructions with immediate or unshifted operands.
PC destinations, shifted operands, memory and helpers retain their original paths.
It builds on the recovered MOV/MVN and flag-store commits.

Correctness passes: 135 WASM tests (including 229,376 independent condition/budget
comparisons), 672 native/WASM fault cases, three native targets, seven frontend
checks, and a fresh checked native replay of 1,600 images, guest records and
4,919,249 stereo PCM frames. The known native-identical motion heuristic failure
remains separate from exact equality.

The serial served/candidate/candidate/served batch measures 13.6317, 13.5919,
14.2571 and 13.5741 host seconds. Mean served time is 13.6029 seconds; candidate
13.9245 seconds, about 2.3% less throughput. All runs execute 3,975,618,624 guest
instructions and 676 presentations over guest seconds 78–96, physical GPU and
shared audio, no capture/sampling. Environment: POST_REBOOT_RESULTS.md.

No demonstrated gain: runtime changes are rejected and archived with the binary
under `/home/claude/.scratch/eka-benchmark/conditional-alu-candidate*`.
Expanded independent regression coverage is retained. No deployment change.
