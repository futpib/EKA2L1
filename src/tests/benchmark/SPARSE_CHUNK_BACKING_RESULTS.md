# Demand-backed WASM chunks

The multiple-memory-model browser build previously called `common::map_memory(max_size_)` for every chunk outside its 64 MiB direct arena. In the Asphalt 3 racing scene, the 128 MiB `AknIconSrv` chunk had only 4 KiB committed and the 96 MiB `FbsLargeChunk` had 2.316 MiB committed, yet both complete host backings were resident. See [the allocation and guest-commit trace](ASPHALT3_POST_MERGE_MEMORY_PROFILE.md).

Non-direct WASM chunks now reserve a stable, 64 KiB-aligned linear-memory range without touching its pages. The existing guest page table still controls access. `commit()` zeroes each newly mapped guest page, including after decommit; chunk destruction frees the backing. The direct arena, generated ARM memory access code, and native chunk backing are unchanged. The separate backing keeps FBS's host pointers stable and remains one physical allocation shared by every process mapping the global chunk.

The stock-ROM, fresh-install Asphalt 3 route reached the racing HUD with guest sound enabled and no guest errors in all four runs. The route then sampled five seconds of unpaced play. Both builds used the same browser harness, device assets, input route, and no derived ROM cache. The old WASM hash was `08b851ee32134e2727548bca00bc84729d9731d4ae547cd425be7e7cbdc77748`; the demand-backed hash was `4262c20d79c4c94c0a6ff754986e59d8e1f4c61c310d1ca2f7e6d8f71440118e`.

| # | Run | Browser PSS, MiB | Resident WASM backing, MiB | Unpaced FPS | CPU clock at sample |
|---:|---|---:|---:|---:|---|
| 1 | Old, first pair | 3096.44 | 1166.0 | 116.71 | ~3.6 GHz |
| 2 | Demand-backed, first pair | 2854.03 | 944.0 | 87.66 | ~2.8–3.0 GHz |
| 3 | Old, matched-clock pair | 3044.60 | 1152.0 | 117.63 | ~3.6 GHz |
| 4 | Demand-backed, matched-clock pair | 2904.25 | 988.2 | 119.60 | ~3.6 GHz |

The matched-clock pair saved 140.35 MiB of whole-browser PSS and 163.8 MiB of resident WASM backing, with no measured gameplay slowdown. The first pair saved 242.41 MiB of browser PSS and 222 MiB of resident WASM backing; its FPS values are not comparable because the CPU clock differed. Browser PSS includes Chromium and varies between launches, while the WASM mapping more directly measures the backing change. The browser's WASM *capacity* remains near 1.9 GiB: stable host pointers still require reserving address space. Previously touched pages may remain resident after guest decommit; this change avoids touching uncommitted pages in the first place.

`node build-wasm/src/tests/aot/test_aot_wasm.js` passed 197 tests, including a new 128 MiB local and 96 MiB global chunk test for zero-fill, recommit, stable pointers, process isolation, and sharing. The demand-backed Sky Force browser route also reached its gameplay HUD with no errors. Raw captures are in `/home/claude/.scratch/eka-sparse-chunk-20261016-{baseline,candidate,baseline-fixed,candidate-fixed,sky-force}`.
