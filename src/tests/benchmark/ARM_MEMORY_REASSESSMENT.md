# Reassessing the ARM short-block memory default

`EKA2L1_ARM_MEMORY` remains disabled by default. This investigation changes no
emulator source, configuration default or deployment. It runs the existing switch
against both currently retained memory implementations using one frozen normal
binary. [Raw results and exact commands](ARM_MEMORY_REASSESSMENT.json) include all
observations and their provenance.

For the measured workloads, enabling it now looks like a useful tradeoff:
with the default TLB backend, Sky Force combat uses 5.95% less worker CPU across
the two-run means, while Snakes uses 1.31% more. Both paired comparisons have
the same direction within each game. The recommendation is to enable it for this
workload mix, accepting the small Snakes cost. This recommendation is narrower than a
claim of a universal win: the earlier stationary Sky Force and long Snakes
regressions were not retested here, and direct has only one pair per game.

## Why it was not enabled originally

The implementation passed the reported correctness checks, but its first
[four-route performance screen](ARM_SHORT_MEMORY_RESULTS.md) was mixed: throughput
rose 9.06% in Sky Force combat and 1.88% in standard Snakes, while falling 7.57%
in stationary Sky Force and 3.98% in the longer Snakes route. There was only one
pair per route. That was insufficient evidence to promote it.

The later [span optimization](ARM_TRANSFER_SPAN_RESULTS.md) and
[runtime-state caching](ARM_STATE_CACHE_RESULTS.md) screens also gave mixed
results. The latter compared two versions with the option already enabled; it
did not isolate enabling the option. These reports document why it remained
opt-in, without establishing that disabled is optimal on today's compiler.

## Current CPU-time screen

Positive change means more CPU time with the option enabled. Each cell is one
observation, not an average or a confidence interval. All runs are retained.

| # | Game | Memory | ARM_MEMORY=0 worker CPU s | ARM_MEMORY=1 worker CPU s | CPU change | Renderer CPU off/on s |
|---|---|---|---:|---:|---:|---:|
| 1 | Snakes | TLB | 1.983340 | 2.014631 | +1.58% | 2.370 / 2.400 |
| 2 | Snakes | Direct | 1.760052 | 1.717220 | -2.43% | 2.400 / 2.350 |
| 3 | Sky Force combat | TLB | 9.535655 | 8.727033 | -8.48% | 10.180 / 9.380 |
| 4 | Sky Force combat | Direct | 8.632490 | 8.509127 | -1.43% | 9.230 / 9.100 |

The larger first-pair TLB/Sky Force result prompted a focused reversed-order
confirmation of the default TLB backend, with ARM_MEMORY=1 before 0 in each game:

| # | Game | Reverse-pair off CPU s | Reverse-pair on CPU s | First-pair change | Reverse-pair change | Change of two-run means |
|---|---|---:|---:|---:|---:|---:|
| 1 | Snakes | 1.958559 | 1.978990 | +1.58% | +1.04% | +1.31% |
| 2 | Sky Force combat | 9.021428 | 8.725016 | -8.48% | -3.29% | -5.95% |

The primary value is Linux scheduler runtime of the matched emulation worker,
reported as `DedicatedWorker`. Renderer CPU additionally includes rendering,
audio and compilation threads. CPU time avoids counting descheduling as execution
but still varies with CPU frequency, cache state and browser compilation.

Snakes runs guest time 21-25 seconds, executing 644,728,231 instructions and 84
presentations. Sky Force combat runs guest time 42.000001-48 seconds, executing
2,171,043,925 instructions and 192 presentations. All four configurations within
each game have identical endpoints and byte-identical presentation journals.

The initial order is TLB off/on, then direct off/on, first Snakes then Sky Force.
The second phase reverses TLB to on/off. Direct has one pair only. Sampling,
tracing, verification and detailed profiling are off. This is the normal binary,
with no slow32 census counters. The only on/off change within each pair is
`EKA2L1_ARM_MEMORY`.

## Correctness and scope

Before timing, enabling the option passed four current 60-frame native-reference
replays: Snakes and Sky Force combat under TLB and direct. The comparisons check
exact images, guest instruction counts, timestamps, PCM and audio events. The
normal binary is unchanged from the earlier default-off acceptance checks.

The flag enables inline memory access in eligible short ARM blocks; connected
ARM regions already have inline access. It also selects PC/runtime-state caching
and callback-exit handling for those blocks. This is not a switch that solely
deletes a call instruction. Successful inline accesses avoid callback barriers;
the generated code and register requirements also change.

The [slow32 census](SLOW32_CENSUS.md) measured only 2,290 unconditional read32
helper calls in the Snakes window and 2,040 in Sky Force combat with this option
off. Those counts show little remaining opportunity specifically from those
read32 calls; they do not quantify byte/halfword accesses, stores, startup, or
the state-caching changes selected by the flag.

This screen does not repeat the historically regressing stationary Sky Force or
long Snakes routes, and it is not sufficient to establish a universal speedup.
No default is changed during this assessment.

## Reproduction

Every row in the JSON contains the exact command, environment and build hashes.
Run its recorded command with a new output directory, using the normal build.
For example, from the repository root:

```python
import json, os, subprocess
from pathlib import Path
evidence = json.loads(Path('src/tests/benchmark/ARM_MEMORY_REASSESSMENT.json').read_text())
command = evidence['timings'][0]['command']
env = {k: v for k, v in os.environ.items() if not k.startswith('EKA2L1_')}
env.update(command['env'])
args = list(command['args'])
args[3] = '/tmp/eka-arm-memory-fresh-run'
subprocess.run(args, cwd=command['cwd'], env=env, check=True)
```

Set `EKA2L1_ARM_MEMORY` to `0` or `1` and keep other settings fixed for a pair.
`EKA2L1_MEMORY_IMPL=0` selects TLB and `2` selects the best retained direct path.
The runtime-reported values are checked in every recorded run.
