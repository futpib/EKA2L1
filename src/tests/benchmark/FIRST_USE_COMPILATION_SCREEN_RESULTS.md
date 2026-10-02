# First-use compilation coverage screen (rejected)

V24 installs both ROM and RAM entries on first missing lookup, then retries the normal validated lookup once. Default off. Original code policy, rejection identity, capacity and execution limits remain. It was not timed because native replay failed.

All 188 compiler tests passed after adding malloc/free exports to the test target; the missing-export failed run is retained. Production/probe artifacts were unchanged by that link-only correction. Instruction probes and native package targets passed, but normal and checked Sky Force replays have a +1 cumulative instruction offset in all 240 frames. Pixels, presentation IDs and guest timestamps match; this does not pass the exact oracle. The matching control passes.

| Route | Interpreted | Guest instructions | Share |
|---|---:|---:|---:|
| sky | 161,274,959 | 2,142,147,961 | 7.528656% |
| combat | 148,893,607 | 2,171,043,925 | 6.858157% |
| standard | 36,965,768 | 2,964,235,296 | 1.247059% |
| long | 35,253,266 | 3,031,637,220 | 1.162846% |

The separate diagnostic build confirms that all-first-use compilation reaches the 4,096-entry immutable-ROM capacity during startup. Missing ROM entries dominate subsequent fallback. Those diagnostic wall times are not benchmarks. Original raw artifacts, source patches and logs are retained under the scratch paths in the evidence.

The startup instruction offset appears between two syscalls in the second CPU slice. The instruction trace identifies Thumb BX PC using the current instruction address instead of architectural PC+4. Its wrong ARM destination executes one extra instruction before reaching the intended continuation. A new native register-exchange probe also exposes halfword-aligned ARM destinations at exact budget exits: 432 mismatches in 5,632 cases before the correction. V24d fixes the PC read bias and aligns outgoing ARM targets before returning; its probe matches all 5,632 cases. Full corrected acceptance is pending. BLX PC retains the existing DynCom behavior for the unpredictable encoding.

Next coverage candidate will retain the ROM hotness filter while installing missing RAM entries on first use. The existing fallback remains; no zero-interpretation or speed claim.

Latest completed V23c unpaced screen (c9c6f4395): Sky Force 0.587x stationary / 0.606x moving-and-firing; Snakes 1.651x standard / 1.722x longer. These are experimental-build measurements, not LAN deployment. Live remains unchanged and nothing is pushed.
