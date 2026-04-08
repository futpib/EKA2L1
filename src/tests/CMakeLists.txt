include(FileUtils)

add_subdirectory(epoc)
add_subdirectory(common)
add_subdirectory(cpu)

add_executable(ekatests
	tests.cpp
    ${COMMON_TEST_FILES}
    ${CORE_TEST_FILES}
    ${CPU_TEST_FILES})


target_link_libraries(ekatests PRIVATE
    Catch2
    common
    cpu
    epocio
    epockern
    epocloader
    epocservs)

add_test(
  NAME ekatests
  COMMAND ekatests
)

add_executable(eka_cpu_tests
    tests.cpp
    ${CPU_TEST_FILES})

target_link_libraries(eka_cpu_tests PRIVATE
    Catch2
    cpu)

add_test(
  NAME eka_cpu_tests
  COMMAND eka_cpu_tests
)

set(EPOC_LOADER_ASSETS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/epoc/loader/assets/")
add_assets(ekatests ${EPOC_LOADER_ASSETS_PATH} "loaderassets")

set(COMMON_ASSETS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/common/assets/")
add_assets(ekatests ${COMMON_ASSETS_PATH} "commonassets")

set(SERVICES_CENTRALREPO_ASSETS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/epoc/services/centralrepo/assets/")
add_assets(ekatests ${SERVICES_CENTRALREPO_ASSETS_PATH} "centralrepoassets")

set(SERVICES_APPLIST_ASSETS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/epoc/services/applist/assets/")
add_assets(ekatests ${SERVICES_APPLIST_ASSETS_PATH} "applistassets")
