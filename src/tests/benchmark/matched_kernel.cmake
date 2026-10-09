# Local captured bytes are intentionally not checked into the repository.
set(EKA_MATCHED_KERNEL_FIXTURES "" CACHE PATH "Offline matched-kernel fixture directory")
if(EKA_MATCHED_KERNEL_FIXTURES AND NOT EMSCRIPTEN)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/matched_kernels.inc
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/generate_matched_kernels.py
            ${EKA_MATCHED_KERNEL_FIXTURES} ${CMAKE_CURRENT_BINARY_DIR}/matched_kernels.inc
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/generate_matched_kernels.py
            ${EKA_MATCHED_KERNEL_FIXTURES}/1879455500.arm ${EKA_MATCHED_KERNEL_FIXTURES}/1879129820.arm)
    add_executable(eka_matched_kernel EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/matched_kernel.cpp ${CMAKE_CURRENT_BINARY_DIR}/matched_kernels.inc)
    target_include_directories(eka_matched_kernel PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(eka_matched_kernel PRIVATE cpu)
    target_compile_options(eka_matched_kernel PRIVATE -O3)
endif()

if(EKA_MATCHED_KERNEL_FIXTURES AND NOT EMSCRIPTEN)
    add_executable(eka_matched_fault EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/cpu_fault_probe.cpp)
    target_compile_definitions(eka_matched_fault PRIVATE EKA_MATCHED_REFERENCE=1)
    target_link_libraries(eka_matched_fault PRIVATE cpu)
endif()
