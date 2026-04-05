add_executable(eka2l1_wasm main.cpp)

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
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s INITIAL_MEMORY=268435456"
    "SHELL:-s USE_PTHREADS=1"
    "SHELL:-s PTHREAD_POOL_SIZE=4"
    "SHELL:-s FULL_ES3=1"
    "SHELL:-s MIN_WEBGL_VERSION=2"
    "SHELL:-s MAX_WEBGL_VERSION=2"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "SHELL:-s EXPORTED_FUNCTIONS=['_main','_eka2l1_init','_eka2l1_install_device','_eka2l1_install_sis','_eka2l1_run','_eka2l1_shutdown']"
    "SHELL:-s FORCE_FILESYSTEM=1"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")

set_target_properties(eka2l1_wasm PROPERTIES
    OUTPUT_NAME "eka2l1"
    SUFFIX ".html")
