if (NOT IOS)
    add_executable(eka_audio_probe EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/audio_probe.cpp)
    target_link_libraries(eka_audio_probe PRIVATE drivers common)
    if(EMSCRIPTEN)
        target_link_options(eka_audio_probe PRIVATE
            "SHELL:-s ALLOW_MEMORY_GROWTH=1" "SHELL:-s INITIAL_MEMORY=134217728"
            "SHELL:-s EXIT_RUNTIME=1" "SHELL:-s USE_PTHREADS=1"
            "SHELL:-s ERROR_ON_UNDEFINED_SYMBOLS=0")
        set_target_properties(eka_audio_probe PROPERTIES SUFFIX ".js")
    endif()
endif()
