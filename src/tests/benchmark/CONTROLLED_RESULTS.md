# Controlled optimization comparisons

Status: in progress. 51/109 game comparisons complete; 413/872 valid observations and 11 retained invalid observations.

[Earlier partial-isolation measurements](CONTROLLED_PRELIMINARY_RESULTS.md)
remain separate: their measurement process could use the reserved core.
The corrected campaign excludes the monitor and controller too, and checks
the monitor affinity in every clock sample.

CPU and wall columns show throughput change: positive is faster. Native
instruction change is candidate/control minus one: negative is less work.
Each completed comparison has four fresh observations per variant, in
ABBA then BAAB order. Every valid observation is included. The paired range
shows all four adjacent CPU comparisons; it is not a confidence interval.
Four favorable pairs alone do not prove a small gain generalizes.

See [method and controls](CONTROLLED_BENCHMARKS.md) and
[scope and interpretation](CONTROLLED_REASSESSMENT.md). Frozen historical
comparisons measure their original configurations; gains are not additive
and do not establish the same effect on the current production branch.

| # | Experiment | Game | CPU throughput | Wall throughput | Native instructions | Paired CPU range | Faster pairs | Invalid |
| ---: | --- | --- | ---: | ---: | ---: | --- | ---: | ---: |
| 1 | compact-dispatch | Snakes | +5.91% | +4.92% | -6.65% | +4.79% to +7.10% | 4/4 | 0 |
| 2 | compact-dispatch | Sky Force | +1.17% | +1.03% | -0.98% | +0.32% to +2.84% | 4/4 | 0 |
| 3 | division-digits | Snakes | +0.94% | -0.06% | -0.38% | +0.40% to +1.26% | 4/4 | 1 |
| 4 | division-digits | Sky Force | +0.67% | +0.67% | -0.00% | -2.08% to +3.65% | 2/4 | 4 |
| 5 | state-pruning | Snakes | +0.08% | +0.06% | -0.08% | -0.84% to +1.15% | 2/4 | 0 |
| 6 | state-pruning | Sky Force | +0.66% | +0.63% | -1.36% | -0.12% to +1.37% | 3/4 | 0 |
| 7 | trusted-lookup-inline | Snakes | -0.39% | -0.10% | -0.00% | -1.23% to +0.51% | 1/4 | 0 |
| 8 | trusted-lookup-inline | Sky Force | +2.56% | +2.39% | +0.00% | -0.56% to +8.25% | 3/4 | 0 |
| 9 | entry-only-pruning | Snakes | -0.39% | -0.12% | +0.42% | -1.15% to +0.46% | 2/4 | 0 |
| 10 | entry-only-pruning | Sky Force | +3.14% | +2.87% | -1.38% | +1.45% to +5.13% | 4/4 | 0 |
| 11 | store-only-pruning | Snakes | +0.40% | +0.34% | +0.50% | +0.22% to +0.58% | 4/4 | 0 |
| 12 | store-only-pruning | Sky Force | -0.41% | -0.38% | -0.67% | -1.04% to +1.15% | 1/4 | 0 |
| 13 | direct-span-two | Snakes | -0.50% | -0.28% | -0.30% | -1.12% to -0.14% | 0/4 | 0 |
| 14 | direct-span-two | Sky Force | +0.28% | +0.26% | +0.03% | -0.53% to +0.80% | 3/4 | 0 |
| 15 | direct-span-three | Snakes | -0.02% | -0.33% | -0.31% | -1.27% to +0.69% | 3/4 | 0 |
| 16 | direct-span-three | Sky Force | -0.82% | -0.89% | -0.01% | -2.40% to +0.58% | 2/4 | 0 |
| 17 | thumb-static | Snakes | +0.91% | +0.59% | -0.17% | +0.16% to +1.52% | 4/4 | 0 |
| 18 | thumb-static | Sky Force | -1.84% | -1.78% | -0.52% | -5.37% to -0.22% | 0/4 | 0 |
| 19 | thumb-transfer-gate | Snakes | -0.65% | -0.42% | +0.18% | -1.28% to +0.18% | 1/4 | 0 |
| 20 | thumb-transfer-gate | Sky Force | +0.04% | -0.22% | +0.03% | -1.89% to +2.70% | 2/4 | 0 |
| 21 | batched-counts | Snakes | -0.13% | +0.31% | -0.41% | -1.80% to +1.18% | 2/4 | 0 |
| 22 | batched-counts | Sky Force | +1.06% | +1.01% | -0.15% | +0.16% to +2.25% | 4/4 | 0 |
| 23 | span-page-reuse | Snakes | -1.76% | -1.54% | +1.12% | -2.74% to -0.91% | 0/4 | 0 |
| 24 | span-page-reuse | Sky Force | +0.96% | +0.88% | +0.82% | -1.87% to +3.47% | 2/4 | 0 |
| 25 | dynamic-rom-cohorts | Snakes | -11.17% | -9.48% | +10.03% | -11.74% to -9.80% | 0/4 | 0 |
| 26 | dynamic-rom-cohorts | Sky Force | -69.50% | -68.11% | +207.72% | -69.91% to -69.01% | 0/4 | 0 |
| 27 | sparse-rom-lookup | Snakes | -0.66% | -0.32% | -0.04% | -0.98% to -0.28% | 0/4 | 0 |
| 28 | sparse-rom-lookup | Sky Force | +2.64% | +2.57% | -1.70% | +0.80% to +4.74% | 4/4 | 0 |
| 29 | compiled-syscalls | Snakes | -0.61% | -0.50% | +0.58% | -1.35% to +0.94% | 1/4 | 0 |
| 30 | compiled-syscalls | Sky Force | +8.11% | +7.58% | -2.35% | +3.90% to +16.87% | 4/4 | 0 |
| 31 | compiled-memory-misses | Snakes | -9.00% | -7.98% | +7.82% | -10.24% to -7.97% | 0/4 | 0 |
| 32 | compiled-memory-misses | Sky Force | -3.48% | -3.33% | +4.29% | -5.31% to -2.40% | 0/4 | 0 |
| 33 | ram-first-use | Snakes | -5.14% | -4.15% | +0.01% | -5.50% to -4.83% | 0/4 | 0 |
| 34 | ram-first-use | Sky Force | -1.66% | -1.14% | +1.24% | -3.11% to -0.36% | 0/4 | 0 |
| 35 | rom-first-use-recycling | Snakes | -3.84% | +0.65% | +1.71% | -5.10% to -1.48% | 0/4 | 0 |
| 36 | rom-first-use-recycling | Sky Force | -8.14% | -4.48% | +1.18% | -9.78% to -6.63% | 0/4 | 0 |
| 37 | rom-module-dispatch | Snakes | +0.77% | -2.27% | +1.31% | -3.96% to +5.30% | 2/4 | 0 |
| 38 | rom-module-dispatch | Sky Force | -3.02% | -5.14% | +6.20% | -6.09% to +0.02% | 1/4 | 0 |
| 39 | rom-state-cohorts | Snakes | -2.89% | -2.60% | +3.90% | -4.68% to -2.17% | 0/4 | 0 |
| 40 | rom-state-cohorts | Sky Force | -11.44% | -10.84% | +11.30% | -13.35% to -9.81% | 0/4 | 0 |
| 41 | mixed-ir | Snakes | -4.50% | -4.43% | +2.68% | -5.74% to -2.98% | 0/4 | 0 |
| 42 | longer-ir-segments | Snakes | -5.34% | -4.90% | +3.45% | -5.88% to -4.83% | 0/4 | 0 |
| 43 | ir-stack-values | Snakes | -0.08% | -0.10% | -0.01% | -0.46% to +0.17% | 2/4 | 0 |
| 44 | compiled-syscalls-long-snakes | Snakes | -0.58% | -0.33% | +0.54% | -1.01% to -0.34% | 0/4 | 0 |
| 45 | quiet-runtime-cuts | Snakes | +1.86% | +1.84% | -1.85% | +0.67% to +2.70% | 4/4 | 0 |
| 46 | quiet-runtime-cuts | Sky Force | +0.80% | +0.64% | -1.45% | -0.80% to +2.02% | 3/4 | 0 |
| 47 | ram-hit-inline | Snakes | +3.66% | +2.77% | -4.54% | +1.93% to +5.83% | 4/4 | 0 |
| 48 | ram-hit-inline | Sky Force | +2.14% | +2.05% | -0.60% | +1.00% to +4.84% | 4/4 | 0 |
| 49 | verifier-specialization | Snakes | -0.10% | +0.37% | -0.34% | -0.34% to +0.46% | 1/4 | 0 |
| 50 | verifier-specialization | Sky Force | +0.34% | +0.39% | -0.32% | -1.30% to +2.81% | 1/4 | 0 |
| 51 | guard-publication-omission | Snakes | +0.21% | +0.12% | -0.74% | -0.18% to +0.54% | 3/4 | 0 |

All observations, clock checks, errors, exact plans, settings, hashes and
absolute evidence paths are retained in the companion JSON. Raw scheduler
and browser reports remain in the campaign directory.

## Remaining comparisons

- extended: guard-publication-omission/combat (5/8), conditional-loop-budgets/standard (0/8), conditional-loop-budgets/combat (0/8), broad-loop-budgets/standard (0/8), broad-loop-budgets/combat (0/8), lazy-flags/standard (0/8), incoming-register-definitions/standard (0/8), wide-result-reuse/standard (0/8), direct-register-stores/standard (0/8), conditional-alu-select/standard (0/8), compare-operand-reuse/standard (0/8), whole-entry-budget/standard (0/8), outlined-entry-budget/standard (0/8), cached-address-displacement/standard (0/8), deferred-read-exit/standard (0/8), owner-core-reuse/standard (0/8), aligned-cache-hash/standard (0/8), compact-generated-memory/standard (0/8), connected-callee-loops/standard (0/8), guarded-successor-lookup/standard (0/8).
- inlining: expanded-leaf-eligibility/standard (0/8), call-prefix-fusion/standard (0/8), preserve-inner-leaves/standard (0/8), branch-veneer-fusion/standard (0/8), tail-prefix-fusion/standard (0/8), expanded-leaf-bound/standard (0/8), conditional-leaf-bound/standard (0/8), hot-source-window/standard (0/8), four-inline-sites/standard (0/8), sixteen-inline-sites/standard (0/8), shorter-region-chain/standard (0/8), uncapped-region-chain/standard (0/8), eager-rom-regions/standard (0/8), eager-rom-regions/combat (0/8), rom-leaf-fusion/standard (0/8), rom-leaf-fusion/combat (0/8), bounded-rom-calls/standard (0/8), bounded-rom-calls/combat (0/8).
- memory: allocation-ranges/standard (0/8), allocation-ranges/combat (0/8), full-page-table/standard (0/8), full-page-table/combat (0/8), last-page-only/standard (0/8), last-page-only/combat (0/8), folded-tlb/standard (0/8), folded-tlb/combat (0/8), tlb-plus-last-page/standard (0/8), tlb-plus-last-page/combat (0/8), folded-tlb-plus-last-page/standard (0/8), folded-tlb-plus-last-page/combat (0/8), older-page-cache-guards/standard (0/8), older-page-cache-guards/combat (0/8), page-cache-span-reuse/standard (0/8), page-cache-span-reuse/combat (0/8), current-tlb-vs-direct/standard (0/8), current-tlb-vs-direct/combat (0/8), tlb-unaligned-scalar/standard (0/8), tlb-unaligned-scalar/combat (0/8).
