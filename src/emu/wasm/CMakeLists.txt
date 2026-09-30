add_executable(eka2l1_wasm main.cpp)

# Package the patches staged by this build, including newly added upstream
# patches. Depending on a separate native build can silently embed stale DLLs.
get_property(EKA2L1_WASM_PATCH_TARGET GLOBAL PROPERTY LAST_SYMBIAN_PROJECT_TARGET)
add_dependencies(eka2l1_wasm ${EKA2L1_WASM_PATCH_TARGET})
file(GLOB_RECURSE EKA2L1_WASM_PATCH_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/patch/*/group/*.dll"
    "${PROJECT_SOURCE_DIR}/src/patch/*/group/*.map")
set_property(TARGET eka2l1_wasm APPEND PROPERTY LINK_DEPENDS ${EKA2L1_WASM_PATCH_FILES})

set(EKA2L1_WASM_WORKER_POOL_SIZE 32 CACHE STRING "Number of prestarted browser workers")
if(NOT EKA2L1_WASM_WORKER_POOL_SIZE MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "EKA2L1_WASM_WORKER_POOL_SIZE must be a positive integer")
endif()

option(EKA2L1_WASM_CHECK_EVERY_STACK_UPDATE "Instrument every WASM stack-pointer update" OFF)
if (EKA2L1_WASM_CHECK_EVERY_STACK_UPDATE)
    set(EKA2L1_WASM_STACK_CHECK_LEVEL 2)
else()
    # Retain the stack cookie; per-assignment instrumentation is diagnostic.
    set(EKA2L1_WASM_STACK_CHECK_LEVEL 1)
endif()

# graphics_driver_shared.h needs glm (used for reading screen_texture FBO)
target_include_directories(eka2l1_wasm PRIVATE
    ${PROJECT_SOURCE_DIR}/src/external/glm)

target_link_libraries(eka2l1_wasm PRIVATE
    common
    config
    cpu
    drivers
    epoc
    epockern
    epocloader
    epocmem
    epocpkg
    epocservs
    epocutils
    epocio
    epocdispatch
    gdbstub
    j2me)

target_link_options(eka2l1_wasm PRIVATE
    "SHELL:-s ERROR_ON_UNDEFINED_SYMBOLS=0"
    "SHELL:-Wl,--unresolved-symbols=ignore-all"
    "SHELL:-s ASSERTIONS=1"
    "SHELL:-s STACK_OVERFLOW_CHECK=${EKA2L1_WASM_STACK_CHECK_LEVEL}"
    "SHELL:-s STACK_SIZE=4194304"
    "SHELL:-s DEFAULT_PTHREAD_STACK_SIZE=2097152"
    "SHELL:-s ABORT_ON_WASM_EXCEPTIONS=1"
    "SHELL:--profiling-funcs"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s INITIAL_MEMORY=268435456"
    "SHELL:-s USE_PTHREADS=1"
    "SHELL:-s PTHREAD_POOL_SIZE=${EKA2L1_WASM_WORKER_POOL_SIZE}"
    "SHELL:-s FULL_ES3=1"
    "SHELL:-s MIN_WEBGL_VERSION=2"
    "SHELL:-s MAX_WEBGL_VERSION=2"
    "SHELL:-s OFFSCREEN_FRAMEBUFFER=1"
    "SHELL:-s OFFSCREENCANVAS_SUPPORT=1"
    "SHELL:-s OFFSCREENCANVASES_TO_PTHREAD='\"\"'"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','addFunction']"
    "SHELL:-s EXPORTED_FUNCTIONS=['_main','_eka2l1_init','_eka2l1_install_device','_eka2l1_install_sis','_eka2l1_run','_eka2l1_shutdown','_eka2l1_press_key','_eka2l1_get_distinct_colors','_eka2l1_start_frame_dump','_eka2l1_frame_dump_done','_eka2l1_frame_dump_captured','_aot_tlb_read32','_aot_tlb_write32','_aot_tlb_read8','_malloc','_free']"
    "SHELL:-s ALLOW_TABLE_GROWTH=1"
    "SHELL:-s FORCE_FILESYSTEM=1"
    "SHELL:-s PROXY_TO_PTHREAD"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/gles/@resources/"
    "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/upscale/@resources/upscale/"
    "SHELL:--preload-file ${CMAKE_BINARY_DIR}/bin/patch/@patch/")

set_target_properties(eka2l1_wasm PROPERTIES
    OUTPUT_NAME "eka2l1"
    SUFFIX ".html")

configure_file(audio.js audio.js COPYONLY)
configure_file(audio-worklet.js audio-worklet.js COPYONLY)
