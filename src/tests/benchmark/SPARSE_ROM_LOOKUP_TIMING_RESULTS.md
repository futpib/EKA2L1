# Sparse ROM lookup: rejected speed screen

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

The controlled rerun completes ABBA then BAAB for both games, with four fresh
observations per variant in the same frozen binary. Snakes loses 0.66% CPU and
0.32% wall throughput, with all four CPU pairs slightly slower and only 0.04%
fewer retired native instructions. Sky Force improves by 2.64% CPU and 2.57%
wall throughput, with all four CPU pairs faster (+0.80% to +4.74%) and 1.70%
fewer native instructions. The original Sky Force losses do not repeat. This
recovers a historical Sky Force gain alongside a small Snakes cost; it does
not establish the effect of porting the lookup to current defaults. No
production setting changes as part of this reassessment.

## Original screen

Same frozen V31 binary, hotpath0/8, ROM grouping and unrelated experiments off. All eight observations retained in the predeclared two-game reversed-order screen. Guest instruction and presentation totals agree per route. No owned builds or diagnostic jobs overlap.

| # | Route/batch | Control realtime | Candidate realtime | Throughput change | Warmup control/candidate | Index bytes |
|---:|---|---:|---:|---:|---:|---:|
| 1 | combat / 0 | 0.555x | 0.518x | -6.67% | 77.09/86.15s | 3066880 |
| 2 | combat / 1 | 0.536x | 0.472x | -11.94% | 86.14/82.88s | 3066880 |
| 3 | standard / 0 | 1.756x | 1.784x | +1.59% | 13.06/12.55s | 3574784 |
| 4 | standard / 1 | 1.745x | 1.434x | -17.82% | 15.83/12.55s | 3574784 |

Sky Force loses in both orders, while Snakes reverses from a small gain to a substantial loss. The option remains off; no full acceptance or promotion follows. The emitted hash removal is real, but is not a demonstrated performance improvement. Directory/page memory is 3.1–3.6 MB in this screen; static runtime grows 1,230 bytes. The slow closing Snakes candidate is retained. No deployment or push.
