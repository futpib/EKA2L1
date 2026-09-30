# Hardware counter diagnostic: slow mode not reproduced

Four serial fresh browsers used the unchanged budget-chunks archive and policy 6,
shared audio, physical NVIDIA GPU, guest seconds 78–96, 3,975,618,624 guest
instructions and 676 presentations. No owned build/test/browser jobs overlapped.
No affinity, priority, frequency or system-wide setting changed.

| Run | Elapsed seconds | Busiest worker CPU seconds | User instructions (billions) | User cycles (billions) | Instructions/cycle |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 12.8053 | 11.8167 | 115.145039 | 49.235548 | 2.3387 |
| 2 | 12.7911 | 11.7848 | 115.134276 | 49.186723 | 2.3408 |
| 3 | 12.7291 | 11.7377 | 115.137570 | 48.916111 | 2.3538 |
| 4 | 12.6425 | 11.6513 | 115.130262 | 48.599108 | 2.3690 |

The busiest surviving thread is named DedicatedWorker. All attached counters
report full running/enabled coverage, with no attachment/enable/disable/read
errors. Its instruction range is only about 0.013%; cycles vary about 1.31%.
Cycles divided by scheduler runtime range 4.167–4.174 billion/s. This is an
approximate accounting ratio, not a direct physical clock measurement.

Every diagnostic run is fast. Therefore this series does not establish why the
ordinary candidate runs alternated between roughly 12.6 and 14.1 seconds. It
does not prove that tiering, CPU frequency, cache behavior or interference is
responsible or absent in the earlier slow runs. No slow sample was discarded.

The optional hardware-counter tool passed a local busy-loop self-check with
positive instructions/cycles and full counting coverage, then these four actual
browser workflows. Source is c911bc967. It counts only user execution in threads
present before release, excludes kernel/hypervisor and does not inherit new
threads. Independent enable/disable calls and protocol delivery approximately
bracket the window. These diagnostics perturb execution and are not pooled
with ordinary timing controls or used to justify deployment. The snapshots and
raw counter metadata, hashes and measurements are in HARDWARE_COUNTER_EVIDENCE.json.

Reproduce serially:

```sh
EKA2L1_AOT_IR_MODE=6 EKA2L1_AOT_EAGER_REGIONS=0 \
PUPPETEER_EXECUTABLE_PATH=/usr/bin/chromium \
python3 src/tests/benchmark/scheduler_probe.py ASSETS ARCHIVED_BUILD NEW_OUTPUT \
  --hardware-counters
```

The next optimization experiment combines the separately tested write-span
proofs and budget chunks, keeping original instruction lowering and precise
fallbacks. This is an unmeasured hypothesis; served policy 4 is unchanged.
