if(EMSCRIPTEN)
    add_executable(test_aot_wasm test_aot_wasm.cpp)
    target_link_libraries(test_aot_wasm PRIVATE cpu)
    target_link_options(test_aot_wasm PRIVATE
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s INITIAL_MEMORY=134217728"
        "SHELL:-s EXPORTED_FUNCTIONS=['_main','_test_tlb_read32','_test_tlb_write32','_test_tlb_read8']"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['addFunction']"
        "SHELL:-s ALLOW_TABLE_GROWTH=1"
        "SHELL:-s EXIT_RUNTIME=1"
        "SHELL:-s USE_PTHREADS=1")
    set_target_properties(test_aot_wasm PROPERTIES
        OUTPUT_NAME "test_aot_wasm"
        SUFFIX ".js")
endif()
