# Corrected default: direct confirmation does not support delivery

Every warmup and timing run was serial, with no owned build/test/profiler
overlap. Hardware GPU, shared audio, guest seconds 78–96; each run executes
3,975,618,624 instructions and 676 presentations. All samples are retained.

| Order | Served mean | Corrected default mean | Default throughput change |
| --- | ---: | ---: | ---: |
| served/fixed/fixed/served | 14.83700s | 16.71175s | -11.2% |
| fixed/served/served/fixed | 13.50610s | 14.71345s | -8.2% |

Both orders favor the served build. Earlier favorable default controls do not
establish a repeated improvement. This small noisy sample does not estimate
the causal cost of each retained change. No live acceptance or deployment
follows; the served build remains unchanged.

The fixed archive is `region-ir-default-candidate`, with experimental IR OFF.
Its recorded acceptance covers 140 full compiler tests and 4,272 fault cases.
The corrected IR-ON replay is a different artifact and must not be credited
to this OFF archive. Any future delivery requires fresh exact replay and
frontend/live acceptance on the exact selected binary.

Raw observations, hashes and provenance: `DEFAULT_CONFIRMATION_EVIDENCE.json`.
