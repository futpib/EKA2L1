# Post-reboot performance revalidation

Recovered 2026-09-29 with the original uncommitted ARM register-store patch
intact. That experiment was subsequently rejected and archived; see
[REGISTER_STORE_RESULTS.md](REGISTER_STORE_RESULTS.md). The original thread was
retrieved to `.scratch/eka-benchmark/recovery-thread.json` under `/home/claude`.

Two committed but previously unreported changes were present: `40ea34499`
(MOV/MVN unused source loads) and `7cbb39daa` (direct flag stores). Their saved
reports describe positive pre-reboot batches. New serial comparisons test these
two commits together against the last delivered build, `8b5439a5e`.

| Post-reboot batch | Delivered build (s) | Recovered commits (s) | Mean throughput change |
|---|---|---|---|
| A, delivered first/last | 13.3992 / 13.5369 | 15.2711 / 15.3430 | -12.0% |
| B, recovered first/last | 14.5924 / 13.8116 | 13.4319 / 13.5550 | +5.3% |

The directions disagree. Across all eight runs the delivered-build mean is
13.835025 seconds and the recovered-commit mean is 14.40025 seconds (-3.9%
throughput). No current combined gain is confirmed, and the LAN service was
restored using the last delivered archive rather than promoting these commits.
These controls do not establish that either individual simplification is an
intrinsic regression; the prior single-change measurements remain historical
evidence. Do not add their reported percentages together.

All runs execute 3,975,618,624 guest instructions and 676 presentations over
guest seconds 78–96, with shared audio processing and physical NVIDIA graphics,
without capture, sampling or detailed counters. Warmup and timing are serial,
one owned browser at a time. Other workloads were active on the host. Chromium
is 153.0.8010.52; the post-reboot driver is NVIDIA 610.57.04, with no pinned
library override. Do not pool these timings with the older driver environment.

The report's old instruction-count typo was corrected from the raw artifacts
in `adec8319c`. Runtime source was untouched by that correction. Exact binary
hashes, per-run work and environment are in POST_REBOOT_EVIDENCE.json.
