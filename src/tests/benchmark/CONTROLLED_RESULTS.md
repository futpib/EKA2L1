# Controlled optimization comparisons

Status: in progress. 8/108 game comparisons complete; 64/864 valid observations and 5 retained invalid observations.

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

All observations, clock checks, errors, exact plans, settings, hashes and
absolute evidence paths are retained in the companion JSON. Raw scheduler
and browser reports remain in the campaign directory.

## Remaining comparisons

- reserved: entry-only-pruning/standard (0/8), entry-only-pruning/combat (0/8), store-only-pruning/standard (0/8), store-only-pruning/combat (0/8), direct-span-two/standard (0/8), direct-span-two/combat (0/8), direct-span-three/standard (0/8), direct-span-three/combat (0/8), thumb-static/standard (0/8), thumb-static/combat (0/8), thumb-transfer-gate/standard (0/8), thumb-transfer-gate/combat (0/8), batched-counts/standard (0/8), batched-counts/combat (0/8), span-page-reuse/standard (0/8), span-page-reuse/combat (0/8).
- architecture: dynamic-rom-cohorts/standard (0/8), dynamic-rom-cohorts/combat (0/8), sparse-rom-lookup/standard (0/8), sparse-rom-lookup/combat (0/8), compiled-syscalls/standard (0/8), compiled-syscalls/combat (0/8), compiled-memory-misses/standard (0/8), compiled-memory-misses/combat (0/8), ram-first-use/standard (0/8), ram-first-use/combat (0/8), rom-first-use-recycling/standard (0/8), rom-first-use-recycling/combat (0/8), rom-module-dispatch/standard (0/8), rom-module-dispatch/combat (0/8), rom-state-cohorts/standard (0/8), rom-state-cohorts/combat (0/8), mixed-ir/standard (0/8), longer-ir-segments/standard (0/8), ir-stack-values/standard (0/8).
- extended: quiet-runtime-cuts/standard (0/8), quiet-runtime-cuts/combat (0/8), ram-hit-inline/standard (0/8), ram-hit-inline/combat (0/8), verifier-specialization/standard (0/8), verifier-specialization/combat (0/8), guard-publication-omission/standard (0/8), guard-publication-omission/combat (0/8), conditional-loop-budgets/standard (0/8), conditional-loop-budgets/combat (0/8), broad-loop-budgets/standard (0/8), broad-loop-budgets/combat (0/8), lazy-flags/standard (0/8), incoming-register-definitions/standard (0/8), wide-result-reuse/standard (0/8), direct-register-stores/standard (0/8), conditional-alu-select/standard (0/8), compare-operand-reuse/standard (0/8), whole-entry-budget/standard (0/8), outlined-entry-budget/standard (0/8), cached-address-displacement/standard (0/8), deferred-read-exit/standard (0/8), owner-core-reuse/standard (0/8), aligned-cache-hash/standard (0/8), compact-generated-memory/standard (0/8), connected-callee-loops/standard (0/8), guarded-successor-lookup/standard (0/8).
- inlining: expanded-leaf-eligibility/standard (0/8), call-prefix-fusion/standard (0/8), preserve-inner-leaves/standard (0/8), branch-veneer-fusion/standard (0/8), tail-prefix-fusion/standard (0/8), expanded-leaf-bound/standard (0/8), conditional-leaf-bound/standard (0/8), hot-source-window/standard (0/8), four-inline-sites/standard (0/8), sixteen-inline-sites/standard (0/8), shorter-region-chain/standard (0/8), uncapped-region-chain/standard (0/8), eager-rom-regions/standard (0/8), eager-rom-regions/combat (0/8), rom-leaf-fusion/standard (0/8), rom-leaf-fusion/combat (0/8), bounded-rom-calls/standard (0/8), bounded-rom-calls/combat (0/8).
- memory: allocation-ranges/standard (0/8), allocation-ranges/combat (0/8), full-page-table/standard (0/8), full-page-table/combat (0/8), last-page-only/standard (0/8), last-page-only/combat (0/8), folded-tlb/standard (0/8), folded-tlb/combat (0/8), tlb-plus-last-page/standard (0/8), tlb-plus-last-page/combat (0/8), folded-tlb-plus-last-page/standard (0/8), folded-tlb-plus-last-page/combat (0/8), older-page-cache-guards/standard (0/8), older-page-cache-guards/combat (0/8), page-cache-span-reuse/standard (0/8), page-cache-span-reuse/combat (0/8), current-tlb-vs-direct/standard (0/8), current-tlb-vs-direct/combat (0/8), tlb-unaligned-scalar/standard (0/8), tlb-unaligned-scalar/combat (0/8).
