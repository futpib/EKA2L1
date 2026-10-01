# Same application binary compiler-mode control

Outlining was not promoted. Test generated-code choices while holding
the main Emscripten binary fixed. Separate build-time flags currently change
the surrounding application binary as well as emitted guest functions. This
is a possible confound, not an established cause of the observed variation.

Add an explicit ARM IR policy argument with configured, disabled, inline and
outlined choices. Defaults preserve compile-time options; disabled must select
the original emitter and inline must retain the original segment fallback.
Private helpers always disable IR. Do not introduce per-guest-instruction
policy checks: policy is consumed during translation only.

A WASM pre-init configuration function validates requested support and passes
the policy to hot ROM/RAM compilation. Experimental build options stay OFF by
default. Profile/replay harnesses accept an optional mode and record it. A
serial same-archive comparison rotates disabled/inline/outlined modes in fresh
browsers. This holds main app JS/WASM hashes constant, not browser JIT state,
native addresses or host noise. Generated modules deliberately differ.

Require full compiler suite plus a mode-selection/state/budget matrix, fresh
fault probes, native/frontend checks and exact checked replay for each selected
mode. No promotion without repeated real gameplay gains and live/audio gates.
Record original separate-binary results intact before changing the experiment.
