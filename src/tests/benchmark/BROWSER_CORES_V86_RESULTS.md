# v86 equivalent-algorithm comparison, 2026-09-30

Bare multiboot x86 ELF, no guest OS or BIOS. Same four algorithms implemented in
C, compiled by clang 22 at -O2 without vectorization/SSE/MMX. This is a different
ISA and instruction stream; do not combine it with the ARM instruction table.

All 64 completed outputs across two fresh-browser runs match the independent
algorithm reference and complete data-memory checksum. Values named R0 etc. in
the reused checker are algorithm variables here, not x86 register assertions.
One million iterations; median ms over repetitions 3–7 in each run. Repetitions
0–2 remain in the evidence. Host timestamps are taken at receipt of the UART
marker prefix, so serial delivery overhead remains in these measurements.

| Arithmetic | Indexed 1 KiB RAM | Conditions | Dependent 64 KiB reads |
|---:|---:|---:|---:|
| 26.84 | 20.41 | 25.31 | 17.22 |

The two initial diagnostic runs used complete-line timestamps, which included
result printing and checksumming. They are retained separately and excluded for
that measurement defect, not because of their speed. The corrected pair is the
entire timing comparison. See BROWSER_CORES_V86_EVIDENCE.json and browser_cores/
for exact source, command, artifact hashes and every sample.

No Flycast score is claimed here. Its release frontend lacks a bare CPU entry
point; a source harness is being investigated. The existing live emulator stays
unchanged.
