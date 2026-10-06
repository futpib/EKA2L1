# Bounded state retention and total-change timing

The timing verdicts below are being reassessed with measured fixed frequency
and an isolated CPU core. See [controlled results](CONTROLLED_RESULTS.md) and
[scope](CONTROLLED_REASSESSMENT.md); pending comparisons are explicitly marked.
Original observations and correctness evidence remain below.

All 24 preplanned serial observations retained. Each route reverses its three-build order in the second batch. Normal Chromium, NVIDIA hardware, no CPU sampling or detailed counters; no owned builds, correctness or diagnostic jobs overlap. Original execution limits and guest scheduling retained. The old live archive lacks the experimental APIs, so its absent readbacks are retained as null, not fabricated. Its prior exact Sky Force gates are reused from the byte-identical archive; the served WASM was freshly hash-verified before this panel.

The new binary runs the normal speed policy: Thumb memory enabled; first-use compilation, compiled SVC/exclusive/memory-miss policies, ARM memory and ROM leaf/call experiments disabled. Only ROM dispatch 0 versus 2 differs between matching control and cohort candidate. This panel includes accumulated binary growth and other changes in the comparisons against untouched live.

| Route | Batch | Order | Live s | New control s | Cohorts s | Cohorts vs control | Cohorts vs live | Control vs live |
|---|---:|---|---:|---:|---:|---:|---:|---:|
| sky | 0 | live / control / cohorts | 15.15040 | 10.81990 | 12.98890 | -16.70% | +16.64% | +40.02% |
| sky | 1 | cohorts / control / live | 15.40710 | 10.60840 | 12.28930 | -13.68% | +25.37% | +45.23% |
| combat | 0 | cohorts / live / control | 14.82580 | 10.44540 | 12.11450 | -13.78% | +22.38% | +41.94% |
| combat | 1 | control / live / cohorts | 14.64450 | 10.16720 | 11.60610 | -12.40% | +26.18% | +44.04% |
| standard | 0 | control / cohorts / live | 11.30330 | 10.48680 | 10.31630 | +1.65% | +9.57% | +7.79% |
| standard | 1 | live / cohorts / control | 10.21770 | 10.65790 | 10.47860 | +1.71% | -2.49% | -4.13% |
| long | 0 | live / cohorts / control | 10.01140 | 9.98485 | 9.98561 | -0.01% | +0.26% | +0.27% |
| long | 1 | control / cohorts / live | 9.69251 | 10.33470 | 12.07640 | -14.42% | -19.74% | -6.21% |

Unpaced gameplay capacity (guest seconds divided by measured wall seconds):

| Route | Live realtime | New control realtime | Cohort realtime |
|---|---:|---:|---:|
| sky | 0.389-0.396x | 0.555-0.566x | 0.462-0.488x |
| combat | 0.405-0.410x | 0.574-0.590x | 0.495-0.517x |
| standard | 1.592-1.762x | 1.689-1.716x | 1.718-1.745x |
| long | 1.798-1.857x | 1.742-1.803x | 1.491-1.803x |

Startup and whole-command costs are separate from the gameplay interval. Warmup starts after the emulator run call returns; whole-command time includes browser initialization, assets, warmup, gameplay and export. WASM allocator footprint excludes browser/JIT memory. Cumulative installations include replaced functions; they are not a live-slot count.

| Observation | Warmup s | Whole command s | Cumulative installations | WASM allocated bytes |
|---|---:|---:|---:|---:|
| total-cohorts-v28c-00-sky-live | 68.313 | 89.313 | 10134 | 584514056 |
| total-cohorts-v28c-01-sky-control | 49.219 | 65.972 | 9500 | 585050560 |
| total-cohorts-v28c-02-sky-cohorts | 55.749 | 74.685 | 9500 | 585842160 |
| total-cohorts-v28c-03-combat-cohorts | 84.622 | 102.745 | 10070 | 594794368 |
| total-cohorts-v28c-04-combat-live | 106.476 | 127.633 | 10705 | 593472080 |
| total-cohorts-v28c-05-combat-control | 75.587 | 91.818 | 10070 | 594001808 |
| total-cohorts-v28c-06-standard-control | 12.553 | 28.798 | 12943 | 582282160 |
| total-cohorts-v28c-07-standard-cohorts | 14.066 | 30.501 | 12943 | 583073816 |
| total-cohorts-v28c-08-standard-live | 12.559 | 29.748 | 13822 | 581881336 |
| total-cohorts-v28c-09-long-live | 24.864 | 40.920 | 14235 | 590495352 |
| total-cohorts-v28c-10-long-cohorts | 27.131 | 43.073 | 13418 | 591952968 |
| total-cohorts-v28c-11-long-control | 25.871 | 41.575 | 13418 | 591160608 |
| total-cohorts-v28c-12-sky-cohorts | 58.267 | 76.437 | 9500 | 585843488 |
| total-cohorts-v28c-13-sky-control | 51.989 | 68.379 | 9500 | 585050056 |
| total-cohorts-v28c-14-sky-live | 74.090 | 95.223 | 10134 | 584514040 |
| total-cohorts-v28c-15-combat-control | 76.085 | 92.918 | 10070 | 594001056 |
| total-cohorts-v28c-16-combat-live | 110.504 | 131.245 | 10705 | 593471776 |
| total-cohorts-v28c-17-combat-cohorts | 87.396 | 105.199 | 10070 | 594794352 |
| total-cohorts-v28c-18-standard-live | 15.320 | 31.301 | 13822 | 581881456 |
| total-cohorts-v28c-19-standard-cohorts | 15.824 | 32.404 | 12943 | 583074136 |
| total-cohorts-v28c-20-standard-control | 14.067 | 30.552 | 12943 | 582283112 |
| total-cohorts-v28c-21-long-control | 32.411 | 49.287 | 13418 | 591160152 |
| total-cohorts-v28c-22-long-cohorts | 27.379 | 46.186 | 13418 | 591951688 |
| total-cohorts-v28c-23-long-live | 27.631 | 43.074 | 14235 | 590498888 |

All guest instruction, guest-time and presentation totals agree within each comparison. These finite routes and samples do not guarantee a universal gain or sustained live/audio realtime. Review all observations before selection. No outlier is removed. No deployment or push is implied.

## Disposition

Do not promote mode 2. Both Sky Force routes regress in both orders versus the matching control: stationary -16.70%/-13.68%, moving/firing -13.78%/-12.40%. Standard Snakes favors groups by 1.65%/1.71%, but longer Snakes is flat then -14.42%; its 12.0764-second closing candidate remains included. Retaining some guest registers and reducing outer boundaries did not establish a shared speed benefit.

The normal-policy control remains the next experiment baseline: Sky Force 0.555-0.566x stationary and 0.574-0.590x moving/firing; Snakes 1.689-1.716x standard and 1.742-1.803x longer. Against untouched live it improves Sky Force 40.0-45.2% / 41.9-44.0%, but Snakes total-change comparisons reverse or lose in the second batch. This panel does not establish zero Snakes loss, and Sky Force remains below realtime. No promotion or deployment.

Per the new priority, dynamic ROM groups are parked. Next measure frozen verification lookup, trusted-byte/original-layout cache lookup, diagnostics-free outer execution and guard-interval omission independently before selecting a useful combination. All prior negative results and coverage-only options remain preserved and disabled.
