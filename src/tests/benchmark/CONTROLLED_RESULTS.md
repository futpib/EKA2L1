# Controlled optimization comparisons

Status: in progress. 101/109 game comparisons complete; 808/872 valid observations and 20 retained invalid observations.

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
| 52 | guard-publication-omission | Sky Force | -2.54% | -2.47% | -0.22% | -8.63% to +5.26% | 1/4 | 9 |
| 53 | conditional-loop-budgets | Snakes | -0.05% | +0.46% | -0.00% | -0.71% to +0.42% | 2/4 | 0 |
| 54 | conditional-loop-budgets | Sky Force | -1.44% | -1.11% | -2.67% | -4.95% to +3.39% | 1/4 | 0 |
| 55 | broad-loop-budgets | Snakes | -0.27% | -0.18% | -0.04% | -1.36% to +0.95% | 2/4 | 0 |
| 56 | broad-loop-budgets | Sky Force | +4.30% | +4.03% | -2.70% | +2.46% to +6.33% | 4/4 | 0 |
| 57 | lazy-flags | Snakes | -1.09% | -1.07% | -0.03% | -2.84% to -0.37% | 0/4 | 0 |
| 58 | incoming-register-definitions | Snakes | -0.14% | -0.08% | +0.06% | -1.09% to +0.81% | 2/4 | 0 |
| 59 | wide-result-reuse | Snakes | -0.47% | -0.30% | -0.07% | -2.95% to +0.83% | 3/4 | 3 |
| 60 | direct-register-stores | Snakes | -0.43% | -0.13% | -0.00% | -1.48% to +0.11% | 1/4 | 0 |
| 61 | conditional-alu-select | Snakes | -0.17% | -0.16% | +0.20% | -0.50% to +0.00% | 1/4 | 0 |
| 62 | compare-operand-reuse | Snakes | +0.40% | +0.39% | -0.03% | -3.68% to +3.96% | 2/4 | 1 |
| 63 | whole-entry-budget | Snakes | +2.00% | +1.90% | -3.23% | +0.65% to +2.76% | 4/4 | 0 |
| 64 | outlined-entry-budget | Snakes | +1.91% | +1.75% | -3.04% | +0.79% to +2.89% | 4/4 | 0 |
| 65 | cached-address-displacement | Snakes | -0.27% | -0.36% | -0.41% | -1.79% to +1.36% | 2/4 | 2 |
| 66 | deferred-read-exit | Snakes | +0.46% | +0.43% | -1.52% | -1.06% to +2.59% | 2/4 | 0 |
| 67 | owner-core-reuse | Snakes | +0.81% | +0.75% | -0.24% | +0.01% to +1.68% | 4/4 | 0 |
| 68 | aligned-cache-hash | Snakes | +0.65% | +0.47% | +0.20% | -0.09% to +1.43% | 3/4 | 0 |
| 69 | compact-generated-memory | Snakes | +8.05% | +7.45% | -6.81% | +7.48% to +8.80% | 4/4 | 0 |
| 70 | connected-callee-loops | Snakes | +6.87% | +6.52% | -7.07% | +5.65% to +7.57% | 4/4 | 0 |
| 71 | guarded-successor-lookup | Snakes | -0.47% | -0.51% | +3.22% | -1.03% to +0.46% | 1/4 | 0 |
| 72 | expanded-leaf-eligibility | Snakes | +1.96% | +1.63% | -1.54% | +0.88% to +3.21% | 4/4 | 0 |
| 73 | call-prefix-fusion | Snakes | -4.22% | -3.89% | +1.58% | -6.52% to -1.42% | 0/4 | 0 |
| 74 | preserve-inner-leaves | Snakes | +3.94% | +3.46% | -2.83% | +3.02% to +4.98% | 4/4 | 0 |
| 75 | branch-veneer-fusion | Snakes | +0.76% | +0.75% | -0.20% | +0.12% to +1.12% | 4/4 | 0 |
| 76 | tail-prefix-fusion | Snakes | +0.99% | +0.93% | -1.18% | -0.16% to +2.61% | 3/4 | 0 |
| 77 | expanded-leaf-bound | Snakes | +1.00% | +0.87% | -0.85% | +0.06% to +1.94% | 4/4 | 0 |
| 78 | conditional-leaf-bound | Snakes | +0.12% | +0.11% | -0.17% | -1.03% to +1.25% | 3/4 | 0 |
| 79 | hot-source-window | Snakes | -3.06% | -3.06% | +0.64% | -3.73% to -2.27% | 0/4 | 0 |
| 80 | four-inline-sites | Snakes | -2.03% | -1.86% | +3.28% | -3.82% to -0.76% | 0/4 | 0 |
| 81 | sixteen-inline-sites | Snakes | +0.26% | +0.30% | -0.04% | -0.66% to +1.56% | 2/4 | 0 |
| 82 | shorter-region-chain | Snakes | -1.26% | -1.13% | +0.68% | -3.10% to +0.46% | 1/4 | 0 |
| 83 | uncapped-region-chain | Snakes | +0.51% | +0.48% | -0.00% | -0.40% to +1.50% | 2/4 | 0 |
| 84 | eager-rom-regions | Snakes | +0.04% | +0.06% | -0.65% | -0.67% to +1.80% | 1/4 | 0 |
| 85 | eager-rom-regions | Sky Force | -0.73% | -0.81% | -0.04% | -2.34% to +0.77% | 2/4 | 0 |
| 86 | rom-leaf-fusion | Snakes | -0.52% | -0.47% | -0.03% | -2.38% to +0.83% | 2/4 | 0 |
| 87 | rom-leaf-fusion | Sky Force | -1.71% | -1.60% | -0.03% | -5.24% to +0.68% | 1/4 | 0 |
| 88 | bounded-rom-calls | Snakes | +0.17% | +0.09% | +0.01% | -0.68% to +1.59% | 2/4 | 0 |
| 89 | bounded-rom-calls | Sky Force | +0.61% | +0.52% | -0.32% | -0.92% to +2.08% | 3/4 | 0 |
| 90 | allocation-ranges | Snakes | -7.85% | -6.67% | +5.31% | -8.54% to -7.35% | 0/4 | 0 |
| 91 | allocation-ranges | Sky Force | -18.78% | -17.74% | +17.11% | -19.79% to -17.47% | 0/4 | 0 |
| 92 | full-page-table | Snakes | +16.00% | +13.32% | -17.29% | +15.19% to +16.77% | 4/4 | 0 |
| 93 | full-page-table | Sky Force | -5.01% | -4.69% | -2.57% | -7.19% to -3.53% | 0/4 | 0 |
| 94 | last-page-only | Snakes | -5.67% | -5.44% | -2.10% | -7.90% to -3.27% | 0/4 | 0 |
| 95 | last-page-only | Sky Force | -19.00% | -18.11% | +10.37% | -24.62% to -15.85% | 0/4 | 0 |
| 96 | folded-tlb | Snakes | +6.51% | +5.68% | -6.92% | +5.10% to +7.96% | 4/4 | 0 |
| 97 | folded-tlb | Sky Force | -2.79% | -2.60% | +0.73% | -5.78% to -0.30% | 0/4 | 0 |
| 98 | tlb-plus-last-page | Snakes | +0.51% | +0.78% | -2.17% | -0.76% to +1.44% | 3/4 | 0 |
| 99 | tlb-plus-last-page | Sky Force | -0.22% | -0.27% | +0.39% | -2.68% to +0.79% | 3/4 | 0 |
| 100 | folded-tlb-plus-last-page | Snakes | +6.76% | +5.99% | -9.80% | +5.96% to +7.73% | 4/4 | 0 |
| 101 | folded-tlb-plus-last-page | Sky Force | -2.17% | -2.08% | +0.88% | -3.81% to -0.77% | 0/4 | 0 |

All observations, clock checks, errors, exact plans, settings, hashes and
absolute evidence paths are retained in the companion JSON. Raw scheduler
and browser reports remain in the campaign directory.

wide-result-reuse / Snakes: The valid second candidate of the first panel overlapped an unrelated test process. It remains included. The small pooled effect is inconclusive; see [the recorded limitation](CONTROLLED_REASSESSMENT.md).

compare-operand-reuse / Snakes: Repeated build activity surrounded early observations, including one clock-invalid control. All valid observations remain included across the strengthened launch-wait boundary; see [the host limitations](CONTROLLED_REASSESSMENT.md).

Battery charging is temporarily inhibited from the recorded extended-phase
resume onward, with restoration at every phase exit. The guard-publication
Sky Force comparison spans this host-setting boundary; its earlier valid
observations, including the slow candidate, remain included. See the
[power investigation and limitations](CONTROLLED_REASSESSMENT.md).

## Remaining comparisons

- memory: older-page-cache-guards/standard (0/8), older-page-cache-guards/combat (0/8), page-cache-span-reuse/standard (0/8), page-cache-span-reuse/combat (0/8), current-tlb-vs-direct/standard (0/8), current-tlb-vs-direct/combat (0/8), tlb-unaligned-scalar/standard (0/8), tlb-unaligned-scalar/combat (0/8).
