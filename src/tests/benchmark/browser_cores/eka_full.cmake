option(EKA_BROWSER_CPU_COMPARISON "Build standalone full CPU browser comparison" OFF)
if(EMSCRIPTEN AND EKA_BROWSER_CPU_COMPARISON)
    message(FATAL_ERROR "The instruction-bounded browser CPU comparison is retired. Use test_aot_wasm and paced-gameplay.ts with watchdog execution.")
endif()
