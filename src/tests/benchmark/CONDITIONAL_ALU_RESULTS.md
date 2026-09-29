# Conditional ALU selection: rejected

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
