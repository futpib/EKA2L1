# Ungated counters capture a slower execution rate

Four serial fresh browsers run unchanged archive deferred-counts-candidate with
policy 8, shared audio, hardware GPU, and the same 18 guest seconds. Every run
executes 3,975,618,624 guest instructions and 676 presentations. No owned heavy
work overlaps warmup or measurement. These are diagnostics, not promotion runs.

| Run | Wall s | Busiest worker CPU s | User instructions B | User cycles B | IPC | Cycles / CPU time, B/s | Local attachment ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 12.3509 | 11.3374 | 113.29519 | 47.13330 | 2.404 | 4.157 | 96.6 |
| 2 | 12.5605 | 11.5106 | 113.45282 | 47.93725 | 2.367 | 4.165 | 63.9 |
| 3 | 14.2037 | 13.0088 | 113.36703 | 47.69753 | 2.377 | 3.667 | 107.1 |
| 4 | 12.4400 | 11.4015 | 113.46437 | 47.34239 | 2.397 | 4.152 | 79.5 |

Run 3 reproduces a slow result. The busiest surviving thread is DedicatedWorker
in every run. Its retired instruction/cycle totals remain close and IPC stays
within the fast-run range, while accounted CPU time grows. The ratio of user
cycles to scheduler CPU time falls from roughly 4.16 to 3.67 billion/s.
This is consistent with a lower effective CPU clock rate, rather than a large
increase in retired host instructions in that worker. It is an accounting proxy,
not a direct frequency sensor or proof of a particular thermal, power, turbo or
background-load cause. It does not explain every earlier slow run or exclude
other causes elsewhere in the process.

All attached busiest-worker counters report full enabled/running coverage and
no counter errors. However, attachment occurs after the ordinary warmup stdout
message, without pausing the browser. It misses the opening interval listed in
the table, plus unknown stdout delivery delay. Snapshot and enable/disable
boundaries also differ; CPU runtime includes small kernel time excluded by the
counters. Do not treat the raw totals as exact full-window counts or compare them
directly with the earlier gated policy-6 series. New/exited threads are excluded.

Fast runs also occur without the added start pause. This does not isolate every
possible effect of that pause. No timings are normalized by cycle rate, no
outliers are removed, and these diagnostic observations are not pooled with
ordinary deployment timings. The already delivered policy-7 archive was accepted
using its separate raw timing and correctness/live records.

Reproduce serially with EKA2L1_AOT_IR_MODE=8 and EKA2L1_AOT_EAGER_REGIONS=0:

```sh
python3 src/tests/benchmark/scheduler_probe.py ASSETS ARCHIVED_BUILD NEW_OUTPUT \
  --hardware-counters --no-start-gate
```

Exact source/archive hashes, snapshots, counter errors/coverage and all four raw
results are in UNGATED_COUNTER_EVIDENCE.json. No host settings were changed.
