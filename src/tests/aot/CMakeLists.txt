add_executable(test_wasm_cost test_wasm_cost.cpp)
target_include_directories(test_wasm_cost PRIVATE ../../emu/cpu/include)

add_executable(test_state_liveness test_state_liveness.cpp ../../emu/cpu/src/aot/state_liveness.cpp)
target_include_directories(test_state_liveness PRIVATE ../../emu/cpu/include)

add_executable(test_source_maps test_source_maps.cpp
    ../../emu/cpu/src/aot/wasm_emitter.cpp ../../emu/cpu/src/aot/state_liveness.cpp)
target_include_directories(test_source_maps PRIVATE ../../emu/cpu/include ../../emu/common/include)
target_compile_definitions(test_source_maps PRIVATE EKA2L1_AOT_SOURCE_MAPS=1)
target_link_libraries(test_source_maps PRIVATE zlibstatic)

if(EMSCRIPTEN)
    add_executable(test_device_disk ../wasm/persistent-disk-fixture.cpp ../../emu/wasm/persistent_disk.cpp)
    target_include_directories(test_device_disk PRIVATE ${PROJECT_SOURCE_DIR}/src/external/rapidjson/include)
    target_compile_options(test_device_disk PRIVATE -pthread)
    target_link_options(test_device_disk PRIVATE -pthread
        "SHELL:-Wl,--wrap=__wasi_fd_write,--wrap=__wasi_fd_pwrite"
        "SHELL:--profiling-funcs"
        "SHELL:-s STACK_SIZE=1048576" "SHELL:-s STACK_OVERFLOW_CHECK=1"
        "SHELL:-s WASMFS=1" "SHELL:-s FORCE_FILESYSTEM=1"
        "SHELL:-s PTHREAD_POOL_SIZE=3" "SHELL:-s PROXY_TO_PTHREAD=1"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall']")
    set_target_properties(test_device_disk PROPERTIES SUFFIX ".js")
    configure_file(../wasm/persistent-disk-fixture.html test_device_disk.html COPYONLY)
    configure_file(../../emu/wasm/persistent-disk.js persistent-disk.js COPYONLY)

    add_executable(test_aot_wasm test_aot_wasm.cpp test_interpreter_reference.cpp
        ../../emu/cpu/src/dyncom/arm_dyncom_dec.cpp
        ../../emu/cpu/src/dyncom/arm_dyncom_interpreter.cpp
        ../../emu/cpu/src/dyncom/arm_dyncom_thumb.cpp
        ../../emu/cpu/src/dyncom/arm_dyncom_trans.cpp)
    target_compile_definitions(test_aot_wasm PRIVATE $<TARGET_PROPERTY:cpu,COMPILE_DEFINITIONS>)
    target_link_libraries(test_aot_wasm PRIVATE cpu epocmem config)
    target_link_options(test_aot_wasm PRIVATE
        "SHELL:-s PTHREAD_POOL_SIZE=1"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s INITIAL_MEMORY=134217728"
        "SHELL:-s EXPORTED_FUNCTIONS=['_main','_malloc','_free','_test_tlb_read32','_test_tlb_write32','_test_tlb_read8']"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['addFunction','removeFunction']"
        "SHELL:-s ALLOW_TABLE_GROWTH=1"
        "SHELL:-s EXIT_RUNTIME=1"
        "SHELL:-s USE_PTHREADS=1")
    set_target_properties(test_aot_wasm PROPERTIES
        OUTPUT_NAME "test_aot_wasm"
        SUFFIX ".js")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../benchmark/audio_probe.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/../benchmark/browser_cores/eka_full.cmake)

add_executable(test_svc_dispatch test_svc_dispatch.cpp)
target_include_directories(test_svc_dispatch PRIVATE
    ../../emu/kernel/include ../../emu/mem/include
    ../../emu/common/include ../../emu/utils/include)
if(EMSCRIPTEN)
    target_link_options(test_svc_dispatch PRIVATE "SHELL:-s EXIT_RUNTIME=1")
    set_target_properties(test_svc_dispatch PROPERTIES SUFFIX ".js")
endif()

add_executable(test_exclusive_monitor test_exclusive_monitor.cpp
    ../../emu/cpu/src/12l1r/exclusive_monitor.cpp)
target_include_directories(test_exclusive_monitor PRIVATE
    ../../emu/cpu/include ../../emu/common/include)
if(EMSCRIPTEN)
    target_compile_options(test_exclusive_monitor PRIVATE -pthread)
    target_link_options(test_exclusive_monitor PRIVATE -pthread)
    target_link_options(test_exclusive_monitor PRIVATE
        "SHELL:-s PTHREAD_POOL_SIZE=1" "SHELL:-s EXIT_RUNTIME=1")
    set_target_properties(test_exclusive_monitor PROPERTIES SUFFIX ".js")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(test_exclusive_monitor PRIVATE Threads::Threads)
endif()

if(EMSCRIPTEN)
    add_executable(test_speex_simd test_speex_simd.cpp)
    target_include_directories(test_speex_simd PRIVATE ../../emu/drivers/include)
    target_compile_options(test_speex_simd PRIVATE -msimd128)
    target_link_options(test_speex_simd PRIVATE -msimd128 "SHELL:-s EXIT_RUNTIME=1")
    set_target_properties(test_speex_simd PROPERTIES SUFFIX ".js")
endif()
