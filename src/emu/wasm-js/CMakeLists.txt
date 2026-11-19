add_executable(eka2l1_js main.cpp platform.cpp)
if(EKA2L1_COUNTED_JS)
    target_compile_definitions(eka2l1_js PRIVATE EKA2L1_COUNTED_JS=1)
endif()
if(EKA2L1_COUNTED_LOOP_JS)
    target_compile_definitions(eka2l1_js PRIVATE EKA2L1_COUNTED_LOOP_JS=1)
endif()
if(EKA2L1_COUNTED_REGION_JS)
    target_compile_definitions(eka2l1_js PRIVATE EKA2L1_COUNTED_REGION_JS=1)
endif()
if(EKA2L1_WORD_INCREMENT_JS)
    target_compile_definitions(eka2l1_js PRIVATE EKA2L1_WORD_INCREMENT_JS=1)
endif()
get_property(EKA2L1_JS_PATCH_TARGET GLOBAL PROPERTY LAST_SYMBIAN_PROJECT_TARGET)
add_dependencies(eka2l1_js ${EKA2L1_JS_PATCH_TARGET})
target_include_directories(eka2l1_js PRIVATE ${PROJECT_SOURCE_DIR}/src/external/glm)
target_link_libraries(eka2l1_js PRIVATE common config cpu drivers epoc epockern
    epocloader epocmem epocpkg epocservs epocutils epocio epocdispatch gdbstub j2me)
target_link_options(eka2l1_js PRIVATE
    "SHELL:-fexceptions"
    "SHELL:-Wl,--error-limit=0"
    "SHELL:--emit-symbol-map"
    "SHELL:--no-entry"
    "SHELL:-s WASM=0"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_ES6=1"
    "SHELL:-s ENVIRONMENT=web,node"
    "SHELL:-s ALLOW_MEMORY_GROWTH=0"
    "SHELL:-s INITIAL_MEMORY=536870912"
    "SHELL:-s STACK_SIZE=4194304"
    "SHELL:-s ASSERTIONS=1"
    "SHELL:-s ABORTING_MALLOC=0"
    "SHELL:-s DISABLE_EXCEPTION_CATCHING=0"
    "SHELL:-s FORCE_FILESYSTEM=1"
    "SHELL:-s FULL_ES3=1"
    "SHELL:-s MIN_WEBGL_VERSION=2"
    "SHELL:-s MAX_WEBGL_VERSION=2"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','HEAPU8','HEAP16','UTF8ToString']"
    "SHELL:-s EXPORTED_FUNCTIONS=['_malloc','_free']"
    "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/gles/@resources/"
    "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/upscale/@resources/upscale/"
    "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/defaultbank.sf2@resources/defaultbank.sf2"
    "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/src/emu/drivers/resources/defaultbank.hsb@resources/defaultbank.hsb"
    "SHELL:--preload-file ${CMAKE_BINARY_DIR}/bin/patch/@patch/")
set_target_properties(eka2l1_js PROPERTIES OUTPUT_NAME "eka2l1-js" SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
