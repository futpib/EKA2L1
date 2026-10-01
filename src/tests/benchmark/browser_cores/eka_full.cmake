option(EKA_BROWSER_CPU_COMPARISON "Build standalone full CPU browser comparison" OFF)
if(EMSCRIPTEN AND EKA_BROWSER_CPU_COMPARISON)
    add_executable(eka_browser_cpu EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/eka_full.cpp)
    target_link_libraries(eka_browser_cpu PRIVATE cpu)
    target_compile_options(eka_browser_cpu PRIVATE -O3 -msimd128)
    target_link_options(eka_browser_cpu PRIVATE --no-entry
        "SHELL:-s ALLOW_MEMORY_GROWTH=1" "SHELL:-s INITIAL_MEMORY=134217728"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['addFunction']"
        "SHELL:-s EXPORTED_FUNCTIONS=['_malloc','_free']"
        "SHELL:-s ALLOW_TABLE_GROWTH=1" "SHELL:-s USE_PTHREADS=1"
        "SHELL:-s MODULARIZE=1" "SHELL:-s EXPORT_NAME=createCore")
    set_target_properties(eka_browser_cpu PROPERTIES SUFFIX ".js")
endif()
