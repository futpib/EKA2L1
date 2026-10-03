# Shared V8 interpreter fallback comparison

All four routes use frozen V8 WASM `b463ca09030f7532f40f095783ae7eba427dd4c580501419fbd126277f3a7f8c`. Sky counters are reused from af2cc71e1; Snakes counters are fresh. Each exact interpreter count equals total guest instructions minus compiled instructions. These diagnostic wall times are excluded from speed claims.

| Route | Interpreted instructions | Share | Per guest second |
| --- | ---: | ---: | ---: |
| sky | 41,886,443 | 1.955% | 6,981,074 |
| combat | 38,457,510 | 1.771% | 6,409,586 |
| standard | 8,467,865 | 0.286% | 470,437 |
| long | 4,494,314 | 0.148% | 249,684 |

Sky Force has more fallback both by share and absolute rate. Its main handlers are ARM SWI, BX, LDREX/STREX, TEQ, branch, and stack return. Address samples identify shared euser.dll syscall stubs and an exclusive-operation loop; selection must be by instruction semantics, never game/address. The conditional SWI count includes instructions whose condition fails, so it is not a syscall-invocation count.

Per user steering, address fallback before continuing the unverified V19 dispatcher. V19 source is preserved with hashes under `.scratch/eka-sky-performance/parked-rom-dispatch-v19`; no V19 test or performance result is claimed. First evaluate generic ARM exclusive-operation compiler support, retaining the existing monitor and callback semantics. Realtime remains unmet. Live unchanged; nothing pushed.
