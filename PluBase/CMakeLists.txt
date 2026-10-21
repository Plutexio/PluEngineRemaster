cmake_minimum_required(VERSION 3.21)

project(PluBase
        VERSION 0.1.0
        DESCRIPTION "Common types, logging, asserts, UUIDs and thread affinity shared by Plu projects"
        LANGUAGES CXX)

# SHARED, not static: the loggers in Log and the main-thread id in ThreadAffinity are process-wide
# state. A static archive linked into several shared libraries would give each of them a copy of
# its own, and only one copy would ever be initialised.
add_library(PluBase SHARED
        src/Log.cpp
        src/PluUUID.cpp
        src/ThreadAffinity.cpp
)
add_library(PluBase::PluBase ALIAS PluBase)

# A parent project that already provides these targets (PluEngine vendors spdlog and glm and adds
# PluSTL as a subdirectory) wins; standalone, they come from find_package.
if(NOT TARGET PluSTL::PluSTL)
    find_package(PluSTL CONFIG REQUIRED)
endif()
if(NOT TARGET spdlog::spdlog)
    find_package(spdlog CONFIG REQUIRED)
endif()
if(NOT TARGET glm::glm)
    find_package(glm CONFIG REQUIRED)
endif()

include(GNUInstallDirs)
target_include_directories(PluBase PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(PluBase PUBLIC cxx_std_20)
target_link_libraries(PluBase PUBLIC PluSTL::PluSTL spdlog::spdlog glm::glm)

target_compile_definitions(PluBase
        PRIVATE PLU_BUILD_PLUBASE
        # PUBLIC because the asserts are macros: they expand in the consumer's translation units.
        PUBLIC $<$<CONFIG:Debug>:PLU_DEBUG>
)

set_target_properties(PluBase PROPERTIES
        # Meant to sit next to the executable that loads it, together with spdlog.
        INSTALL_RPATH "$ORIGIN"
        BUILD_RPATH "$ORIGIN"
)

option(PLUBASE_INSTALL "Generate the PluBase install and package-config rules" ${PROJECT_IS_TOP_LEVEL})

if(PLUBASE_INSTALL)
    include(CMakePackageConfigHelpers)

    set(PLUBASE_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/PluBase")

    install(TARGETS PluBase EXPORT PluBaseTargets
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT PluBaseTargets
            NAMESPACE PluBase::
            DESTINATION "${PLUBASE_CMAKE_DIR}")

    configure_package_config_file(cmake/PluBaseConfig.cmake.in
            "${CMAKE_CURRENT_BINARY_DIR}/PluBaseConfig.cmake"
            INSTALL_DESTINATION "${PLUBASE_CMAKE_DIR}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/PluBaseConfigVersion.cmake"
            COMPATIBILITY SameMajorVersion)
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/PluBaseConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/PluBaseConfigVersion.cmake"
            DESTINATION "${PLUBASE_CMAKE_DIR}")
endif()
