# Bounded ROM-call timing panel

All sixteen planned serial observations are retained. Both policies use the same
frozen V17 binary with Thumb memory enabled, ARM memory/eager regions/ROM leaves
disabled, mode 3, feature 128 and original limits. Sampling and detailed counters
are off. Each route has two pairs in opposite orders. Guest instruction and
presentation totals match within every pair. This is not a live-archive comparison.

| Batch | Route | Control seconds | Candidate seconds | Throughput change | Candidate realtime |
| --- | --- | ---: | ---: | ---: | ---: |
| 0 | sky | 10.83030 | 11.34900 | -4.57% | 0.529x |
| 0 | combat | 12.63730 | 10.53350 | +19.97% | 0.570x |
| 0 | standard | 10.20810 | 10.38500 | -1.70% | 1.733x |
| 0 | long | 11.97010 | 9.94671 | +20.34% | 1.810x |
| 1 | sky | 11.18810 | 10.83620 | +3.25% | 0.554x |
| 1 | combat | 10.54100 | 11.45820 | -8.00% | 0.524x |
| 1 | standard | 10.18830 | 10.22720 | -0.38% | 1.760x |
| 1 | long | 9.85740 | 9.62612 | +2.40% | 1.870x |

These measurements do not establish sustained realtime Sky Force or zero Snakes
regression. Review both orders and slow observations; do not select only positive
pairs. The separate invocation census is diagnostic and excluded from timings.
No deployment or push.
