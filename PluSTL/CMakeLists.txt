cmake_minimum_required(VERSION 3.21)

project(PluSTL
        VERSION 0.1.0
        DESCRIPTION "Header-only C++20 containers, strings, hashers and smart pointers"
        LANGUAGES CXX)

# Header-only. Consumers either add_subdirectory() this repo (or pull it with FetchContent)
# and link PluSTL::PluSTL, or install it and find_package(PluSTL).
add_library(PluSTL INTERFACE)
add_library(PluSTL::PluSTL ALIAS PluSTL)

include(GNUInstallDirs)
target_include_directories(PluSTL INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(PluSTL INTERFACE cxx_std_20)

# PluSTL/Concurrent/ and the pointers' thread-affinity check use std::thread / std::mutex /
# std::atomic. On glibc >= 2.34 pthread is folded into libc so this is a no-op there, but it
# is what makes the containers link on every other toolchain.
find_package(Threads REQUIRED)
target_link_libraries(PluSTL INTERFACE Threads::Threads)

option(PLUSTL_BUILD_TESTS "Build the PluSTL tests" ${PROJECT_IS_TOP_LEVEL})
option(PLUSTL_INSTALL "Generate the PluSTL install and package-config rules" ${PROJECT_IS_TOP_LEVEL})

if(PLUSTL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PLUSTL_INSTALL)
    include(CMakePackageConfigHelpers)

    # Header-only, so the package files are architecture independent and live under datadir.
    set(PLUSTL_CMAKE_DIR "${CMAKE_INSTALL_DATADIR}/cmake/PluSTL")

    install(TARGETS PluSTL EXPORT PluSTLTargets)
    install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT PluSTLTargets
            NAMESPACE PluSTL::
            DESTINATION "${PLUSTL_CMAKE_DIR}")

    configure_package_config_file(cmake/PluSTLConfig.cmake.in
            "${CMAKE_CURRENT_BINARY_DIR}/PluSTLConfig.cmake"
            INSTALL_DESTINATION "${PLUSTL_CMAKE_DIR}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/PluSTLConfigVersion.cmake"
            COMPATIBILITY SameMajorVersion
            ARCH_INDEPENDENT)
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/PluSTLConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/PluSTLConfigVersion.cmake"
            DESTINATION "${PLUSTL_CMAKE_DIR}")
endif()
